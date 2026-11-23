"""Buttons: check battery now, stop."""

from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import ShellRcConfigEntry
from .car import ShellRcCar
from .entity import ShellRcEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ShellRcConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    car = entry.runtime_data
    async_add_entities([CheckBatteryButton(car), StopButton(car)])


class CheckBatteryButton(ShellRcEntity, ButtonEntity):
    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "check_battery")

    async def async_press(self) -> None:
        self.car.async_check_battery()


class StopButton(ShellRcEntity, ButtonEntity):
    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "stop")

    async def async_press(self) -> None:
        await self.car.async_stop_cat()
