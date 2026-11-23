// QCAR (Shell Motorsport / Marvel 2026) control protocol. Portable C99.
// Mirrors protocol/drivers/qcar.ts. Confirmed on hardware 2026-11-22:
// directions, lights (byte 8 inverted), byte 9 = proportional speed 0..100.
//
// Plaintext: [0]=0 [1..3]='CTL' [4]=fwd [5]=back [6]=left [7]=right
//            [8]=lights (0x00 on, 0x01 off) [9]=speed [10..15]=0
// Battery notify plaintext: [0]=counter [1..3]='VBT' [4]=percent
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "aes128.h"

#define QCAR_SPEED_NORMAL 0x50 // 80, the official "normal" speed
#define QCAR_SPEED_TURBO 0x64  // 100, the official "turbo"
#define QCAR_SPEED_MAX 100     // never send more (values above untested)

// What the car should do now. throttle/steer are -1, 0, 1 so conflicting
// directions are unrepresentable. speed is used only while throttle != 0.
typedef struct {
	int8_t throttle; // 1 forward, -1 reverse, 0 none
	int8_t steer;    // -1 left, 1 right, 0 straight
	uint8_t speed;   // 0..100 (clamped)
	bool lights;
} DriveCmd;

static inline DriveCmd drive_neutral(bool lights) {
	DriveCmd c = {0, 0, QCAR_SPEED_NORMAL, lights};
	return c;
}

typedef struct {
	Aes128Key key;
} QcarCodec;

extern const uint8_t QCAR_KEY[16];

void qcar_init(QcarCodec *c);
// Plaintext frame. Without throttle, byte 9 is always 0x50 so neutral and
// steering-only frames are byte-identical to the confirmed vectors.
void qcar_plaintext(const DriveCmd *cmd, uint8_t out[16]);
void qcar_encode(const QcarCodec *c, const DriveCmd *cmd, uint8_t out[16]);
// Returns battery percent (0..100) or -1 if not a battery frame.
int qcar_decode_battery(const QcarCodec *c, const uint8_t in[16], int *counter);
