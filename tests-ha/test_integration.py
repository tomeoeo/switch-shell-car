"""Home Assistant integration tests with a fake BLE client (no radio)."""

from __future__ import annotations

import asyncio
from unittest.mock import MagicMock, patch

from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData
import pytest

from homeassistant.components.bluetooth import BluetoothServiceInfoBleak
from homeassistant.config_entries import SOURCE_BLUETOOTH, SOURCE_USER
from homeassistant.core import HomeAssistant
from homeassistant.data_entry_flow import FlowResultType
from homeassistant.helpers import entity_registry as er

from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.shell_rc import car as car_mod
from custom_components.shell_rc.const import CONF_ADDRESS, DOMAIN
from custom_components.shell_rc.protocol import BATTERY_UUID, CONTROL_UUID, Codec

ADDRESS = "7A:87:25:B5:FD:A2"
BATTERY_72 = bytes.fromhex("83230ab55f4318b94eaff4696bff1ab1")


def service_info(address: str = ADDRESS, mfr: dict | None = None) -> BluetoothServiceInfoBleak:
    mfr = {21076: b"\x00\x3c"} if mfr is None else mfr
    adv = AdvertisementData(
        local_name=None,
        manufacturer_data=mfr,
        service_data={},
        service_uuids=["0000fff0-0000-1000-8000-00805f9b34fb"],
        tx_power=None,
        rssi=-60,
        platform_data=(),
    )
    device = BLEDevice(address, None, {})
    return BluetoothServiceInfoBleak(
        name=address,
        address=address,
        rssi=-60,
        manufacturer_data=mfr,
        service_data={},
        service_uuids=["0000fff0-0000-1000-8000-00805f9b34fb"],
        source="local",
        device=device,
        advertisement=adv,
        connectable=True,
        time=0,
        tx_power=None,
    )


class FakeClient:
    """Records writes; sends a battery notify shortly after subscribing."""

    def __init__(self, battery: bytes | None = BATTERY_72) -> None:
        self.writes: list[bytes] = []
        self.is_connected = True
        self.disconnected = False
        self._battery = battery

    async def start_notify(self, uuid, cb):
        assert uuid == BATTERY_UUID
        if self._battery is not None:
            asyncio.get_running_loop().call_later(0.05, cb, None, bytearray(self._battery))

    async def write_gatt_char(self, uuid, data, response=None):
        assert uuid == CONTROL_UUID
        assert response is False
        self.writes.append(bytes(data))

    async def disconnect(self):
        self.is_connected = False
        self.disconnected = True


async def test_bluetooth_discovery(hass: HomeAssistant, mock_bluetooth) -> None:
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_BLUETOOTH}, data=service_info()
    )
    assert result["type"] is FlowResultType.FORM
    assert result["step_id"] == "bluetooth_confirm"
    with patch("custom_components.shell_rc.async_setup_entry", return_value=True):
        result = await hass.config_entries.flow.async_configure(result["flow_id"], {})
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["title"] == "QCAR-B5FDA2"
    assert result["data"] == {CONF_ADDRESS: ADDRESS}


async def test_discovery_rejects_other_devices(hass: HomeAssistant, mock_bluetooth) -> None:
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": SOURCE_BLUETOOTH}, data=service_info(mfr={76: b"\x10\x05"})
    )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "not_supported"


async def test_user_flow_no_devices(hass: HomeAssistant, mock_bluetooth) -> None:
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": SOURCE_USER})
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "no_devices_found"


async def _setup(hass: HomeAssistant, client: FakeClient | None) -> MockConfigEntry:
    entry = MockConfigEntry(domain=DOMAIN, title="QCAR-B5FDA2", data={CONF_ADDRESS: ADDRESS}, unique_id=ADDRESS)
    entry.add_to_hass(hass)

    async def fake_connect(*args, **kwargs):
        return client

    p1 = patch.object(car_mod, "establish_connection", side_effect=fake_connect)
    p2 = patch.object(
        car_mod.bluetooth,
        "async_ble_device_from_address",
        return_value=None if client is None else MagicMock(),
    )
    p1.start()
    p2.start()
    entry.async_on_unload(p1.stop)
    entry.async_on_unload(p2.stop)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    return entry


def _state(hass: HomeAssistant, domain: str, key: str):
    entity_id = er.async_get(hass).async_get_entity_id(domain, DOMAIN, f"{ADDRESS}_{key}")
    assert entity_id, f"no {domain} with key {key}"
    state = hass.states.get(entity_id)
    assert state is not None
    return state


async def test_cat_session(hass: HomeAssistant, mock_bluetooth) -> None:
    client = FakeClient()
    entry = await _setup(hass, client)
    car = entry.runtime_data

    await hass.services.async_call("switch", "turn_on", {"entity_id": _state(hass, "switch", "cat_mode").entity_id})
    assert car.cat_active
    await asyncio.sleep(0.4)
    await hass.async_block_till_done()
    # Stop early via the button.
    await hass.services.async_call("button", "press", {"entity_id": _state(hass, "button", "stop").entity_id})
    await hass.async_block_till_done()
    assert not car.cat_active
    assert client.disconnected

    codec = Codec()
    plain = [codec.decrypt(w) for w in client.writes]
    assert len(plain) >= 6
    for p in plain:
        assert p[1:4] == b"CTL"
        assert not (p[4] and p[5]) and not (p[6] and p[7])
        if p[4] or p[5]:
            assert 20 <= p[9] <= 60  # cat range; the steering kick (60) is inside it
    # Starts and ends with neutral frames.
    assert plain[0][4:8] == b"\x00\x00\x00\x00"
    assert all(p[4:8] == b"\x00\x00\x00\x00" for p in plain[-3:])
    assert _state(hass, "switch", "cat_mode").state == "off"
    assert _state(hass, "sensor", "battery").state == "72"


async def test_battery_check(hass: HomeAssistant, mock_bluetooth) -> None:
    client = FakeClient()
    entry = await _setup(hass, client)
    car = entry.runtime_data
    car.last_seen = car_mod.dt_util.utcnow()  # visible
    await hass.services.async_call(
        "button", "press", {"entity_id": _state(hass, "button", "check_battery").entity_id}
    )
    for _ in range(50):
        await asyncio.sleep(0.02)
        if client.disconnected:
            break
    await hass.async_block_till_done()
    assert client.disconnected
    assert _state(hass, "sensor", "battery").state == "72"
    assert _state(hass, "sensor", "status").state == "idle"
    assert all(Codec().decrypt(w)[4:8] == b"\x00\x00\x00\x00" for w in client.writes)


async def test_unreachable(hass: HomeAssistant, mock_bluetooth) -> None:
    entry = await _setup(hass, None)
    car = entry.runtime_data
    car.async_start_cat(5)
    await asyncio.sleep(0.05)
    await hass.async_block_till_done()
    assert not car.cat_active
    assert _state(hass, "sensor", "status").state == "unreachable"
    assert _state(hass, "switch", "cat_mode").state == "off"


async def test_numbers_keep_min_le_max(hass: HomeAssistant, mock_bluetooth) -> None:
    entry = await _setup(hass, FakeClient())
    car = entry.runtime_data
    await hass.services.async_call(
        "number", "set_value", {"entity_id": _state(hass, "number", "cat_min").entity_id, "value": 80}, blocking=True
    )
    assert car.cat_min == 80 and car.cat_max == 80
    assert _state(hass, "number", "cat_max").state == "80.0"


async def test_visibility_from_history(hass: HomeAssistant, mock_bluetooth) -> None:
    """Identical adverts never re-fire callbacks; 'last seen' must come from history."""
    import time
    from types import SimpleNamespace

    entry = await _setup(hass, FakeClient())
    car = entry.runtime_data
    car.last_seen = None
    with patch.object(
        car_mod.bluetooth,
        "async_last_service_info",
        return_value=SimpleNamespace(time=time.monotonic() - 5, rssi=-70),
    ):
        car._tick_visibility(None)
    assert car.visible and car.rssi == -70
    assert _state(hass, "binary_sensor", "visible").state == "on"
    car.last_seen = None
    with patch.object(
        car_mod.bluetooth,
        "async_last_service_info",
        return_value=SimpleNamespace(time=time.monotonic() - 600, rssi=-90),
    ):
        car._tick_visibility(None)
    assert not car.visible
    assert _state(hass, "binary_sensor", "visible").state == "off"


async def test_unload(hass: HomeAssistant, mock_bluetooth) -> None:
    entry = await _setup(hass, FakeClient())
    assert await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    yield


@pytest.fixture(autouse=True)
def fast_frames():
    with patch.object(car_mod, "FRAME_INTERVAL_S", 0.01):
        yield
