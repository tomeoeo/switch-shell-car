// Pad -> DriveCmd. Portable C99 (the Switch app fills PadInput from libnx).
//
// Controls:
//   Left stick up/down  proportional throttle / reverse (min_speed .. gear cap)
//   ZR / ZL             full throttle / full reverse at the gear cap (both = stop)
//   D-pad left/right,   steering (binary). D-pad wins; the stick uses
//   or either stick X   hysteresis, the one deflected more counts.
//   L / R               gear down / up (caps 40/60/80/100)
//   B (held)            boost: cap 100 regardless of gear
//   A                   lights on/off
//   - held + Up/Down    min_speed +5 / -5 (start threshold calibration)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "qcar.h"

enum {
	PAD_ZR = 1u << 0,
	PAD_ZL = 1u << 1,
	PAD_LEFT = 1u << 2,
	PAD_RIGHT = 1u << 3,
	PAD_UP = 1u << 4,
	PAD_DOWN = 1u << 5,
	PAD_A = 1u << 6,
	PAD_B = 1u << 7,
	PAD_X = 1u << 8,
	PAD_Y = 1u << 9,
	PAD_L = 1u << 10,
	PAD_R = 1u << 11,
	PAD_PLUS = 1u << 12,
	PAD_MINUS = 1u << 13,
};

typedef struct {
	bool connected;
	uint32_t buttons;
	float lx, ly; // left stick, -1..1, y up = positive
	float rx, ry; // right stick
} PadInput;

#define INPUT_GEARS 4
extern const uint8_t INPUT_GEAR_CAP[INPUT_GEARS];

#define INPUT_MIN_SPEED_LO 10
#define INPUT_MIN_SPEED_HI 60

typedef struct {
	// settings
	uint8_t min_speed; // start threshold: the slowest speed byte that still moves the car
	int gear;          // 0..INPUT_GEARS-1
	float throttle_on, throttle_off;
	float steer_on, steer_off;
	// state
	int8_t stick_dir;
	int8_t stick_steer;
	bool lights;
	uint32_t prev_buttons;
	bool settings_changed; // set when gear/min_speed changed; caller clears
} InputMapper;

void input_init(InputMapper *m, uint8_t min_speed, int gear);
// Forget transient state (after reconnect / focus regain). Buttons held now
// must be released before they count again.
void input_reset(InputMapper *m, uint32_t held_buttons);
DriveCmd input_update(InputMapper *m, const PadInput *pad);
// True if any input that would move the car is active.
bool input_any_drive(const InputMapper *m, const PadInput *pad);
uint8_t input_speed_cap(const InputMapper *m, const PadInput *pad);
