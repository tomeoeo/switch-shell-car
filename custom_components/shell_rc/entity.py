"""Base entity: one device per car, state pushed by ShellRcCar listeners."""

from __future__ import annotations

from homeassistant.helpers.device_registry import CONNECTION_BLUETOOTH, DeviceInfo
from homeassistant.helpers.entity import Entity

from .car import ShellRcCar
from .const import DOMAIN


class ShellRcEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, car: ShellRcCar, key: str) -> None:
        self.car = car
        self._attr_translation_key = key
        self._attr_unique_id = f"{car.address}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, car.address)},
            connections={(CONNECTION_BLUETOOTH, car.address)},
            name=car.name,
            manufacturer="Shell / BrandBase",
            model="QCAR (Marvel 2026)",
        )

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(self.car.add_listener(self.async_write_ha_state))
