// Console UI (80x45 cells, 16x16 px font, 24-bit color escapes, ASCII only).
// No libnx dependency: the host preview (tools/ui-preview) compiles this
// file as-is and renders the same escape sequences to PNG.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define UI_COLS 80
#define UI_ROWS 45

typedef enum { UI_TONE_OK, UI_TONE_BUSY, UI_TONE_BAD, UI_TONE_NEUTRAL } UiTone;

typedef struct {
	char name[24];
	char addr[18];
	int rssi;
	bool last_used;
} UiCar;

typedef struct {
	const char *version;
	const UiCar *cars;
	int ncars;
	int cursor;
	bool scanning;
	int others;           // other BLE devices nearby
	const char *note;     // last event, may be ""
	UiTone note_tone;
	const char *standby;  // "" when standby is off
	const char *ha;       // Home Assistant line, may be ""
	unsigned frame;       // animation counter
} UiList;

typedef struct {
	const char *version;
	const char *name;
	const char *addr;
	const char *state;     // "jazda", "laczenie", ...
	UiTone state_tone;
	const char *state_msg; // detail when not driving, may be ""
	int battery;           // -1 unknown
	bool battery_low;
	int throttle;          // -1, 0, 1
	int speed;             // 0..100, valid when throttle != 0
	int steer;             // -1, 0, 1
	int gear;              // 1..4
	int gear_cap;          // %
	int min_speed;         // %
	bool lights;
	bool driving;          // connected and sending
	bool holding, paused, pad_ok;
	bool cat_active, cat_pending;
	unsigned cat_left_s;
	int cat_seconds, cat_lo, cat_hi;
	unsigned writes, errors, reconnects;
	double connect_ms;
	float period_p50, period_p95;
	const char *ha; // may be ""
} UiDrive;

// Paint the whole screen with the base color (call when switching screens).
void ui_clear(void);
void ui_draw_list(const UiList *s);
void ui_draw_drive(const UiDrive *s);
void ui_draw_error(const char *version, const char *message);
