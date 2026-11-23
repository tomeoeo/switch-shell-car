"""Constants for the Shell RC integration."""

DOMAIN = "shell_rc"

CONF_ADDRESS = "address"
CONF_BATTERY_INTERVAL = "battery_interval"  # minutes, 0 = off
DEFAULT_BATTERY_INTERVAL = 30

FRAME_INTERVAL_S = 0.05  # the module's watchdog is ~0.4 s; 50 ms is plenty
BATTERY_WAIT_S = 75  # the first notify can take ~40 s after subscribing
VISIBLE_WINDOW_S = 120  # "visible" = advert seen within this window

DEFAULT_CAT_SECONDS = 120
DEFAULT_CAT_MIN = 20
DEFAULT_CAT_MAX = 60
