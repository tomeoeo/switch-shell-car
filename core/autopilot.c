#include "autopilot.h"

static uint32_t next_rand(Autopilot *a) {
	// xorshift32
	uint32_t x = a->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	a->rng = x;
	return x;
}

static uint32_t range(Autopilot *a, uint32_t lo, uint32_t hi) {
	return hi <= lo ? lo : lo + next_rand(a) % (hi - lo + 1);
}

void autopilot_start(Autopilot *a, uint32_t seed, uint32_t now_ms, uint32_t duration_ms, uint8_t lo,
                     uint8_t hi) {
	a->rng = seed ? seed : 0x9e3779b9u;
	if (hi > QCAR_SPEED_MAX)
		hi = QCAR_SPEED_MAX;
	if (lo > hi)
		lo = hi;
	a->lo = lo;
	a->hi = hi;
	a->end_ms = now_ms + duration_ms;
	a->seg_end_ms = now_ms; // pick a segment on the first update
	a->seg = drive_neutral(false);
	a->done = duration_ms == 0;
	a->segments = 0;
}

static void new_segment(Autopilot *a, uint32_t now_ms) {
	DriveCmd c = drive_neutral(false);
	uint32_t dur;
	uint32_t roll = range(a, 0, 99);
	int8_t turn = (next_rand(a) & 1) ? 1 : -1;
	if (roll < 35) { // cruise forward
		c.throttle = 1;
		c.speed = (uint8_t)range(a, a->lo, a->hi);
		dur = range(a, 500, 2000);
	} else if (roll < 60) { // forward turn
		c.throttle = 1;
		c.steer = turn;
		c.speed = (uint8_t)range(a, a->lo, a->hi);
		dur = range(a, 400, 1500);
	} else if (roll < 70) { // back off, maybe turning
		c.throttle = -1;
		c.steer = (next_rand(a) % 3 == 0) ? turn : 0;
		c.speed = (uint8_t)range(a, a->lo, (a->lo + a->hi) / 2);
		dur = range(a, 300, 1000);
	} else if (roll < 85) { // sudden dart
		c.throttle = (next_rand(a) % 4 == 0) ? -1 : 1;
		c.speed = a->hi;
		dur = range(a, 200, 500);
	} else { // pause (the cat's turn)
		dur = range(a, 500, 3000);
	}
	a->seg = c;
	a->seg_end_ms = now_ms + dur;
	a->segments++;
}

DriveCmd autopilot_update(Autopilot *a, uint32_t now_ms, bool lights) {
	if (!a->done && (int32_t)(now_ms - a->end_ms) >= 0)
		a->done = true;
	if (a->done)
		return drive_neutral(lights);
	if ((int32_t)(now_ms - a->seg_end_ms) >= 0)
		new_segment(a, now_ms);
	DriveCmd c = a->seg;
	c.lights = lights;
	return c;
}

uint32_t autopilot_remaining_s(const Autopilot *a, uint32_t now_ms) {
	if (a->done || (int32_t)(a->end_ms - now_ms) <= 0)
		return 0;
	return (a->end_ms - now_ms + 999) / 1000;
}
