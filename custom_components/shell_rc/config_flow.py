"""Config flow: Bluetooth discovery (manufacturer data 0x5254 / 00 3c) or manual pick."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.components.bluetooth import BluetoothServiceInfoBleak, async_discovered_service_info
from homeassistant.config_entries import ConfigEntry, ConfigFlow, ConfigFlowResult, OptionsFlow
from homeassistant.core import callback

from .const import CONF_ADDRESS, CONF_BATTERY_INTERVAL, DEFAULT_BATTERY_INTERVAL, DOMAIN
from .protocol import MANUFACTURER_ID


def is_shell_car(info: BluetoothServiceInfoBleak) -> bool:
    data = info.manufacturer_data.get(MANUFACTURER_ID)
    return data is not None and data[:2] == b"\x00\x3c"


def car_title(info: BluetoothServiceInfoBleak) -> str:
    if info.name and info.name.startswith(("QCAR-", "SL-")):
        return info.name
    # The advert carries no name; the official name is QCAR- + last 3 address bytes.
    return "QCAR-" + info.address.replace(":", "")[-6:].upper()


class ShellRcConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._discovery: BluetoothServiceInfoBleak | None = None
        self._discovered: dict[str, str] = {}

    async def async_step_bluetooth(self, discovery_info: BluetoothServiceInfoBleak) -> ConfigFlowResult:
        await self.async_set_unique_id(discovery_info.address)
        self._abort_if_unique_id_configured()
        if not is_shell_car(discovery_info):
            return self.async_abort(reason="not_supported")
        self._discovery = discovery_info
        self.context["title_placeholders"] = {"name": car_title(discovery_info)}
        return await self.async_step_bluetooth_confirm()

    async def async_step_bluetooth_confirm(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        assert self._discovery is not None
        title = car_title(self._discovery)
        if user_input is not None:
            return self.async_create_entry(title=title, data={CONF_ADDRESS: self._discovery.address})
        self._set_confirm_only()
        return self.async_show_form(step_id="bluetooth_confirm", description_placeholders={"name": title})

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            address = user_input[CONF_ADDRESS]
            await self.async_set_unique_id(address, raise_on_progress=False)
            self._abort_if_unique_id_configured()
            return self.async_create_entry(title=self._discovered[address], data={CONF_ADDRESS: address})

        current = self._async_current_ids(include_ignore=False)
        for info in async_discovered_service_info(self.hass, connectable=True):
            if info.address in current or info.address in self._discovered or not is_shell_car(info):
                continue
            self._discovered[info.address] = f"{car_title(info)} ({info.address})"
        if not self._discovered:
            return self.async_abort(reason="no_devices_found")
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required(CONF_ADDRESS): vol.In(self._discovered)}),
        )

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        return ShellRcOptionsFlow()


class ShellRcOptionsFlow(OptionsFlow):
    async def async_step_init(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            return self.async_create_entry(data=user_input)
        current = self.config_entry.options.get(CONF_BATTERY_INTERVAL, DEFAULT_BATTERY_INTERVAL)
        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(
                {vol.Required(CONF_BATTERY_INTERVAL, default=current): vol.All(int, vol.Range(min=0, max=1440))}
            ),
        )
