"""Shell RC: Shell (Marvel 2026) BLE toy cars in Home Assistant.

Cat mode (random slow wandering), battery level, visibility. The car is
connected only during a session; the Nintendo Switch app can drive it the
rest of the time (the module accepts one connection at a time).
"""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant

from .car import ShellRcCar
from .const import CONF_ADDRESS, CONF_BATTERY_INTERVAL, DEFAULT_BATTERY_INTERVAL

PLATFORMS = [Platform.BINARY_SENSOR, Platform.BUTTON, Platform.NUMBER, Platform.SENSOR, Platform.SWITCH]

type ShellRcConfigEntry = ConfigEntry[ShellRcCar]


async def async_setup_entry(hass: HomeAssistant, entry: ShellRcConfigEntry) -> bool:
    car = ShellRcCar(
        hass,
        entry.data[CONF_ADDRESS],
        entry.title,
        entry.options.get(CONF_BATTERY_INTERVAL, DEFAULT_BATTERY_INTERVAL),
    )
    await car.async_start()
    entry.runtime_data = car
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ShellRcConfigEntry) -> bool:
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await entry.runtime_data.async_stop()
    return unloaded


async def _async_update_listener(hass: HomeAssistant, entry: ShellRcConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)
