// The car link: one thread that connects, then every 10 ms reads the pad
// (or the cat-mode autopilot), maps it to a command and writes a frame.
// Reconnects with backoff, always starting from neutral. Safety rules
// (project plan, Phase 2):
//  - neutral when: focus lost, no pad (manual), write failing, loop stalled
//    > 200 ms; afterwards neutral until all driving inputs are released;
//  - any driving input cancels cat mode (the human takes over);
//  - ~100 ms of neutral frames before every intentional disconnect;
//  - the module's own watchdog (~0.4 s) stops the car if we go silent.
#pragma once
#include <stdbool.h>
#include <switch.h>

#include "../../core/qcar.h"
#include "../../core/stats.h"

typedef enum {
	CAR_IDLE,
	CAR_CONNECTING,
	CAR_DRIVING,
	CAR_WAIT_RETRY,
} CarPhase;

typedef struct {
	uint8_t min_speed;
	int gear;
	int steer_kick_ms;
	uint8_t steer_kick_speed, steer_floor;
	uint8_t cat_lo, cat_hi;
	bool probe; // standby battery check: never drive manually (cat mode still can)
} CarSettings;

typedef struct {
	CarPhase phase;
	char message[96];
	BtdrvAddress addr;
	int battery; // -1 unknown
	int battery_counter;
	double battery_age_s;
	DriveCmd cmd; // what was last sent
	bool holding; // neutral until inputs released
	bool paused;  // focus lost
	bool pad_ok;
	int gear;
	uint8_t min_speed;
	bool lights;
	bool cat_active;
	bool cat_pending; // requested, starts once connected
	u32 cat_left_s;
	u32 cat_sessions;          // finished cat sessions (any reason)
	bool cat_ended_by_human;   // last session cancelled by the pad
	u32 writes, write_errors, reconnects;
	double connect_ms;
	Summary period; // ms between frames, last ~10 s
	bool settings_changed;
} CarStatus;

void car_start(const BtdrvAddress *addr, const CarSettings *s);
// Neutral ~100 ms, disconnect, join the thread. Safe to call when idle.
void car_stop(void);
bool car_running(void);
void car_status(CarStatus *out);
void car_clear_settings_changed(void);
// Cat mode: start (runs once connected) or stop. seconds > 0.
void car_cat(bool on, int seconds);
// From the applet focus hook (main thread).
void car_set_focus(bool in_focus);
