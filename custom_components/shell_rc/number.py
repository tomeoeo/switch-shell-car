"""Numbers: cat mode duration and speed range (kept across restarts)."""

from __future__ import annotations

from homeassistant.components.number import NumberMode, RestoreNumber
from homeassistant.const import PERCENTAGE, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import ShellRcConfigEntry
from .car import ShellRcCar
from .entity import ShellRcEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ShellRcConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    car = entry.runtime_data
    async_add_entities(
        [
            CarNumber(car, "cat_seconds", 10, 1800, 10, UnitOfTime.SECONDS),
            CarNumber(car, "cat_min", 10, 100, 5, PERCENTAGE),
            CarNumber(car, "cat_max", 10, 100, 5, PERCENTAGE),
        ]
    )


class CarNumber(ShellRcEntity, RestoreNumber):
    _attr_mode = NumberMode.SLIDER

    def __init__(self, car: ShellRcCar, key: str, lo: int, hi: int, step: int, unit: str) -> None:
        super().__init__(car, key)
        self._key = key
        self._attr_native_min_value = lo
        self._attr_native_max_value = hi
        self._attr_native_step = step
        self._attr_native_unit_of_measurement = unit

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        if (last := await self.async_get_last_number_data()) is not None and last.native_value is not None:
            self._set(int(last.native_value))

    def _set(self, value: int) -> None:
        setattr(self.car, self._key, value)
        # Keep min <= max.
        if self.car.cat_min > self.car.cat_max:
            if self._key == "cat_min":
                self.car.cat_max = self.car.cat_min
            else:
                self.car.cat_min = self.car.cat_max

    @property
    def native_value(self) -> float:
        return float(getattr(self.car, self._key))

    async def async_set_native_value(self, value: float) -> None:
        self._set(int(value))
        self.car._changed()  # noqa: SLF001 - refresh the sibling min/max entities too
