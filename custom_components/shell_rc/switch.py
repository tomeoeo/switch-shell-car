"""Switches: cat mode (on = connect and wander), lights (used during sessions)."""

from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.restore_state import RestoreEntity

from . import ShellRcConfigEntry
from .car import ShellRcCar
from .entity import ShellRcEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ShellRcConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    car = entry.runtime_data
    async_add_entities([CatModeSwitch(car), LightsSwitch(car)])


class CatModeSwitch(ShellRcEntity, SwitchEntity):
    """Turns itself off when the session ends (time up, error, stop)."""

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "cat_mode")

    @property
    def is_on(self) -> bool:
        return self.car.cat_active

    @property
    def extra_state_attributes(self) -> dict[str, int]:
        return {"seconds_left": self.car.cat_left_s}

    async def async_turn_on(self, **kwargs: Any) -> None:
        self.car.async_start_cat()

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.car.async_stop_cat()


class LightsSwitch(ShellRcEntity, SwitchEntity, RestoreEntity):
    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "lights")

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        if (last := await self.async_get_last_state()) is not None:
            self.car.lights = last.state == "on"

    @property
    def is_on(self) -> bool:
        return self.car.lights

    async def async_turn_on(self, **kwargs: Any) -> None:
        self.car.lights = True
        self.async_write_ha_state()

    async def async_turn_off(self, **kwargs: Any) -> None:
        self.car.lights = False
        self.async_write_ha_state()
