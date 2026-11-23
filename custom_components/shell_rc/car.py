"""One Shell RC car: BLE sessions (cat mode, battery checks) and state.

The module accepts a single BLE connection, so we only connect for a cat
session or a short battery check and disconnect afterwards. While the
Nintendo Switch app drives the car, our connection attempts fail; that is
reported in the status sensor, nothing else happens.
"""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from datetime import datetime, timedelta
import logging
import time

from bleak import BleakClient
from bleak_retry_connector import BleakClientWithServiceCache, establish_connection

from homeassistant.components import bluetooth
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, callback
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.util import dt as dt_util

from .const import (
    BATTERY_WAIT_S,
    DEFAULT_CAT_MAX,
    DEFAULT_CAT_MIN,
    DEFAULT_CAT_SECONDS,
    FRAME_INTERVAL_S,
    VISIBLE_WINDOW_S,
)
from .protocol import BATTERY_UUID, CONTROL_UUID, Autopilot, Codec, DriveCmd, SteerAssist, neutral

_LOGGER = logging.getLogger(__name__)

STATUS_IDLE = "idle"
STATUS_CONNECTING = "connecting"
STATUS_CAT = "cat"
STATUS_BATTERY = "battery_check"
STATUS_UNREACHABLE = "unreachable"
STATUS_ERROR = "error"


class CarUnreachable(Exception):
    """No connectable advertisement / connection failed."""


class ShellRcCar:
    """State + BLE logic for one car. Entities subscribe with add_listener."""

    def __init__(self, hass: HomeAssistant, address: str, name: str, battery_interval_min: int) -> None:
        self.hass = hass
        self.address = address
        self.name = name
        self._battery_interval = battery_interval_min
        self._codec = Codec()
        self._listeners: list[CALLBACK_TYPE] = []
        self._unsubs: list[CALLBACK_TYPE] = []
        self._session_lock = asyncio.Lock()
        self._task: asyncio.Task | None = None
        self._stop_cat = asyncio.Event()
        self._battery_event = asyncio.Event()

        # state for entities
        self.status = STATUS_IDLE
        self.last_error: str | None = None
        self.connected = False
        self.battery: int | None = None
        self.battery_checked: datetime | None = None
        self.last_seen: datetime | None = None
        self.rssi: int | None = None
        self.cat_active = False
        self.cat_left_s = 0
        self.lights = False
        self.cat_seconds = DEFAULT_CAT_SECONDS
        self.cat_min = DEFAULT_CAT_MIN
        self.cat_max = DEFAULT_CAT_MAX

    # ------------------------------------------------------------------
    # listeners

    @callback
    def add_listener(self, cb: CALLBACK_TYPE) -> CALLBACK_TYPE:
        self._listeners.append(cb)

        @callback
        def remove() -> None:
            self._listeners.remove(cb)

        return remove

    @callback
    def _changed(self) -> None:
        for cb in list(self._listeners):
            cb()

    @property
    def visible(self) -> bool:
        return self.last_seen is not None and dt_util.utcnow() - self.last_seen < timedelta(
            seconds=VISIBLE_WINDOW_S
        )

    # ------------------------------------------------------------------
    # lifecycle

    async def async_start(self) -> None:
        self._unsubs.append(
            bluetooth.async_register_callback(
                self.hass,
                self._on_advertisement,
                bluetooth.BluetoothCallbackMatcher(address=self.address, connectable=False),
                bluetooth.BluetoothScanningMode.ACTIVE,
            )
        )
        # Re-evaluate "visible" even when no adverts arrive any more.
        self._unsubs.append(
            async_track_time_interval(self.hass, self._tick_visibility, timedelta(seconds=30))
        )
        if self._battery_interval > 0:
            self._unsubs.append(
                async_track_time_interval(
                    self.hass, self._periodic_battery, timedelta(minutes=self._battery_interval)
                )
            )
        self._refresh_from_history()

    async def async_stop(self) -> None:
        for unsub in self._unsubs:
            unsub()
        self._unsubs.clear()
        self._stop_cat.set()
        if self._task is not None:
            self._task.cancel()
            try:
                await self._task
            except (asyncio.CancelledError, Exception):  # noqa: BLE001 - shutting down
                pass

    @callback
    def _on_advertisement(
        self, info: bluetooth.BluetoothServiceInfoBleak, change: bluetooth.BluetoothChange
    ) -> None:
        was_visible = self.visible
        self.last_seen = dt_util.utcnow()
        self.rssi = info.rssi
        if not was_visible:
            _LOGGER.debug("%s visible again (rssi %s)", self.name, info.rssi)
        self._changed()

    @callback
    def _refresh_from_history(self) -> None:
        """Take "last seen" from HA's advertisement history.

        HA calls advertisement callbacks only when the advert *content*
        changes, and this module sends an identical advert forever, so the
        callback fires once. The history entry, however, is refreshed on
        every advert (service_info.time is monotonic seconds).
        """
        info = bluetooth.async_last_service_info(self.hass, self.address, connectable=False)
        if info is None:
            return
        mono = time.monotonic()
        # service_info.time is monotonic; the HA frontend shows it converted to
        # wall-clock. Accept both, just in case.
        age = time.time() - info.time if info.time > mono + 1e6 else mono - info.time
        age = max(0.0, age)
        seen = dt_util.utcnow() - timedelta(seconds=age)
        if self.last_seen is None or seen > self.last_seen:
            self.last_seen = seen
            self.rssi = info.rssi

    @callback
    def _tick_visibility(self, _now: datetime) -> None:
        self._refresh_from_history()
        self._changed()

    @callback
    def _periodic_battery(self, _now: datetime) -> None:
        self._refresh_from_history()
        if self._task is None or self._task.done():
            if self.visible:
                self.async_check_battery()

    # ------------------------------------------------------------------
    # BLE helpers

    async def _connect(self) -> BleakClient:
        device = bluetooth.async_ble_device_from_address(self.hass, self.address, connectable=True)
        if device is None:
            raise CarUnreachable("auto nie jest widoczne (wylaczone albo poza zasiegiem)")
        self.status = STATUS_CONNECTING
        self._changed()
        client = await establish_connection(
            BleakClientWithServiceCache,
            device,
            self.name,
            disconnected_callback=self._on_disconnect,
            max_attempts=3,
        )
        self.connected = True
        self.last_seen = dt_util.utcnow()  # it answered, so it is here
        self._battery_event.clear()
        await client.start_notify(BATTERY_UUID, self._on_notify)
        # Always start from neutral.
        await self._write(client, neutral(self.lights))
        self._changed()
        return client

    @callback
    def _on_disconnect(self, _client: BleakClient) -> None:
        self.connected = False
        self._changed()

    def _on_notify(self, _sender, data: bytearray) -> None:
        decoded = self._codec.decode_battery(bytes(data))
        if decoded is None:
            return
        self.battery, counter = decoded
        self.battery_checked = dt_util.utcnow()
        _LOGGER.debug("%s battery %s%% (counter %s)", self.name, self.battery, counter)
        self._battery_event.set()
        self.hass.loop.call_soon_threadsafe(self._changed)

    async def _write(self, client: BleakClient, cmd: DriveCmd) -> None:
        await client.write_gatt_char(CONTROL_UUID, self._codec.encode(cmd), response=False)

    async def _disconnect(self, client: BleakClient) -> None:
        try:
            if client.is_connected:
                for _ in range(3):  # ~150 ms of neutral before letting go
                    await self._write(client, neutral(self.lights))
                    await asyncio.sleep(FRAME_INTERVAL_S)
        except Exception:  # noqa: BLE001 - best effort, the watchdog stops the car anyway
            pass
        try:
            await client.disconnect()
        except Exception:  # noqa: BLE001
            pass
        self.connected = False

    def _fail(self, err: Exception) -> None:
        if isinstance(err, CarUnreachable):
            self.status = STATUS_UNREACHABLE
        else:
            self.status = STATUS_ERROR
        self.last_error = str(err) or type(err).__name__
        _LOGGER.warning("%s: %s", self.name, self.last_error)

    # ------------------------------------------------------------------
    # public actions

    @callback
    def async_start_cat(self, seconds: int | None = None) -> None:
        if self.cat_active:
            return
        if self._task is not None and not self._task.done():
            # A battery check is running: cancel it, the cat session also reads the battery.
            self._task.cancel()
        self._stop_cat.clear()
        self.cat_active = True
        self._changed()
        self._task = self.hass.async_create_background_task(
            self._run_cat(seconds or self.cat_seconds), f"shell_rc cat {self.address}"
        )

    async def async_stop_cat(self) -> None:
        self._stop_cat.set()
        if self._task is not None and not self._task.done():
            try:
                await asyncio.wait_for(asyncio.shield(self._task), timeout=3)
            except (TimeoutError, asyncio.CancelledError, Exception):  # noqa: BLE001
                pass

    @callback
    def async_check_battery(self) -> None:
        if self._task is not None and not self._task.done():
            return
        self._task = self.hass.async_create_background_task(
            self._run_battery_check(), f"shell_rc battery {self.address}"
        )

    # ------------------------------------------------------------------
    # sessions

    async def _run_cat(self, seconds: int) -> None:
        client: BleakClient | None = None
        try:
            async with self._session_lock:
                client = await self._connect()
                self.status = STATUS_CAT
                start = time.monotonic()
                pilot = Autopilot(
                    int(time.time() * 1000) & 0xFFFFFFFF, 0, int(seconds * 1000), self.cat_min, self.cat_max
                )
                steer = SteerAssist()
                _LOGGER.info("%s: cat mode %s s, speed %s-%s%%", self.name, seconds, self.cat_min, self.cat_max)
                next_t = time.monotonic()
                last_left = -1
                while not self._stop_cat.is_set() and client.is_connected:
                    now_ms = int((time.monotonic() - start) * 1000)
                    cmd = steer.apply(pilot.update(now_ms, self.lights), now_ms)
                    await self._write(client, cmd)
                    if pilot.done:
                        break
                    left = pilot.remaining_s(now_ms)
                    if left != last_left:
                        self.cat_left_s = last_left = left
                        self._changed()
                    next_t += FRAME_INTERVAL_S
                    await asyncio.sleep(max(0.0, next_t - time.monotonic()))
                if not client.is_connected and not pilot.done:
                    raise CarUnreachable("polaczenie zerwane w trakcie trybu kot")
                self.status = STATUS_IDLE
        except asyncio.CancelledError:
            self.status = STATUS_IDLE
            raise
        except Exception as err:  # noqa: BLE001 - report any BLE failure in the status sensor
            self._fail(err)
        finally:
            if client is not None:
                await self._disconnect(client)
            self.cat_active = False
            self.cat_left_s = 0
            self._changed()

    async def _run_battery_check(self) -> None:
        client: BleakClient | None = None
        try:
            async with self._session_lock:
                client = await self._connect()
                self.status = STATUS_BATTERY
                self._changed()
                deadline = time.monotonic() + BATTERY_WAIT_S
                # Keep feeding neutral frames while waiting (like the official app).
                while not self._battery_event.is_set() and time.monotonic() < deadline:
                    if not client.is_connected:
                        raise CarUnreachable("polaczenie zerwane w trakcie pomiaru")
                    await self._write(client, neutral(self.lights))
                    try:
                        await asyncio.wait_for(self._battery_event.wait(), timeout=0.2)
                    except TimeoutError:
                        pass
                if not self._battery_event.is_set():
                    raise CarUnreachable("brak odczytu baterii")
                self.status = STATUS_IDLE
        except asyncio.CancelledError:
            self.status = STATUS_IDLE
            raise
        except Exception as err:  # noqa: BLE001
            self._fail(err)
        finally:
            if client is not None:
                await self._disconnect(client)
            self._changed()
