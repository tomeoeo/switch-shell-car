// Steering assist. The speed byte drives both motors, so at low throttle the
// steering motor is sometimes too weak to turn the wheels (2026-11-22). While
// steering with throttle:
//  - "kick": for kick_ms after the steering direction changes, speed is at
//    least kick_speed (snaps the wheels over),
//  - "floor": speed is at least floor for the whole turn.
// Steering without throttle already uses 0x50 (qcar.c). 0 disables a part.
#pragma once
#include <stdint.h>

#include "qcar.h"

typedef struct {
	uint16_t kick_ms;
	uint8_t kick_speed;
	uint8_t floor;
	int8_t prev_steer;
	uint32_t kick_until_ms;
} SteerAssist;

void steer_assist_init(SteerAssist *s, uint16_t kick_ms, uint8_t kick_speed, uint8_t floor);
DriveCmd steer_assist_apply(SteerAssist *s, DriveCmd c, uint32_t now_ms);
