"""Binary sensors: visible (advertising nearby), connected."""

from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import ShellRcConfigEntry
from .car import ShellRcCar
from .entity import ShellRcEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ShellRcConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    car = entry.runtime_data
    async_add_entities([VisibleSensor(car), ConnectedSensor(car)])


class VisibleSensor(ShellRcEntity, BinarySensorEntity):
    """On while the car advertises (it is switched on and in range).

    No device class: "presence" would render as Home/Away.
    """

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "visible")

    @property
    def is_on(self) -> bool:
        return self.car.visible


class ConnectedSensor(ShellRcEntity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "connected")

    @property
    def is_on(self) -> bool:
        return self.car.connected
