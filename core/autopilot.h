// "Cat mode": the car wanders on its own in short random segments, slowly
// (speed_lo..speed_hi, default 20-60 %), with pauses and sudden darts.
// Deterministic for a given seed (tests). Never conflicting directions.
// After duration it returns neutral and sets done.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "qcar.h"

typedef struct {
	uint32_t rng;
	uint8_t lo, hi;
	uint32_t end_ms;     // whole session
	uint32_t seg_end_ms; // current segment
	DriveCmd seg;
	bool done;
	uint32_t segments;
} Autopilot;

void autopilot_start(Autopilot *a, uint32_t seed, uint32_t now_ms, uint32_t duration_ms, uint8_t lo,
                     uint8_t hi);
DriveCmd autopilot_update(Autopilot *a, uint32_t now_ms, bool lights);
// Seconds left (0 when done).
uint32_t autopilot_remaining_s(const Autopilot *a, uint32_t now_ms);
