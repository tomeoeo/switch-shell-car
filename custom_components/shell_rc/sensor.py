"""Sensors: battery (kept across restarts), last battery check, last seen, RSSI, status."""

from __future__ import annotations

from datetime import datetime

from homeassistant.components.sensor import (
    RestoreSensor,
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.const import PERCENTAGE, SIGNAL_STRENGTH_DECIBELS_MILLIWATT, EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import ShellRcConfigEntry
from .car import (
    STATUS_BATTERY,
    STATUS_CAT,
    STATUS_CONNECTING,
    STATUS_ERROR,
    STATUS_IDLE,
    STATUS_UNREACHABLE,
    ShellRcCar,
)
from .entity import ShellRcEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ShellRcConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    car = entry.runtime_data
    async_add_entities(
        [
            BatterySensor(car),
            BatteryCheckedSensor(car),
            LastSeenSensor(car),
            RssiSensor(car),
            StatusSensor(car),
        ]
    )


class BatterySensor(ShellRcEntity, RestoreSensor):
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "battery")

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        if self.car.battery is None and (last := await self.async_get_last_sensor_data()) is not None:
            if isinstance(last.native_value, (int, float)):
                self.car.battery = int(last.native_value)

    @property
    def native_value(self) -> int | None:
        return self.car.battery


class BatteryCheckedSensor(ShellRcEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "battery_checked")

    @property
    def native_value(self) -> datetime | None:
        return self.car.battery_checked


class LastSeenSensor(ShellRcEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "last_seen")

    @property
    def native_value(self) -> datetime | None:
        # Minute resolution: adverts arrive every second, don't spam the recorder.
        return self.car.last_seen.replace(second=0, microsecond=0) if self.car.last_seen else None


class RssiSensor(ShellRcEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.SIGNAL_STRENGTH
    _attr_native_unit_of_measurement = SIGNAL_STRENGTH_DECIBELS_MILLIWATT
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_entity_category = EntityCategory.DIAGNOSTIC
    _attr_entity_registry_enabled_default = False

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "rssi")

    @property
    def native_value(self) -> int | None:
        return self.car.rssi


class StatusSensor(ShellRcEntity, SensorEntity):
    _attr_device_class = SensorDeviceClass.ENUM
    _attr_options = [
        STATUS_IDLE,
        STATUS_CONNECTING,
        STATUS_CAT,
        STATUS_BATTERY,
        STATUS_UNREACHABLE,
        STATUS_ERROR,
    ]

    def __init__(self, car: ShellRcCar) -> None:
        super().__init__(car, "status")

    @property
    def native_value(self) -> str:
        return self.car.status

    @property
    def extra_state_attributes(self) -> dict[str, str | None]:
        return {"last_error": self.car.last_error}
