// sdmc:/switch/shellrc/config.json. Tiny hand-rolled reader/writer, no JSON
// library. Every key is optional; see docs/switch.md.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <switch.h>

#define CONFIG_PATH "sdmc:/switch/shellrc/config.json"

typedef struct {
	bool has_device;
	BtdrvAddress device;
	uint8_t min_speed; // start threshold (practical: 20 on the Marvel module)
	int gear;          // 0..3
	// steering assist (core/steer.h)
	int steer_kick_ms;
	uint8_t steer_kick_speed;
	uint8_t steer_floor;
	// cat mode (core/autopilot.h)
	int cat_seconds;
	uint8_t cat_lo, cat_hi;
	// Home Assistant / HTTP
	int http_port;  // 0 = off (default)
	char token[33]; // empty = no token required
	bool keep_awake;
	// standby (keep_awake on, on the car list): every N minutes connect
	// briefly, read the battery, disconnect. 0 = off.
	int standby_minutes;
	// UI language "pl" or "en". New installs: en (the Switch has no Polish
	// system language to detect); configs from before 1.1.0 stay pl.
	char lang[3];
} Config;

void config_load(Config *c);
void config_save(const Config *c);
void addr_to_str(const BtdrvAddress *a, char out[18]);
bool addr_from_str(const char *s, BtdrvAddress *a);
