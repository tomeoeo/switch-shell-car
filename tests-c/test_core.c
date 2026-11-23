// Host tests for core/ (C). Same vectors as tests/*.test.ts.
// Build & run: make test-c (inside the devkitPro image, which has host gcc).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/aes128.h"
#include "../core/input.h"
#include "../core/qcar.h"
#include "../core/stats.h"
#include "../core/autopilot.h"
#include "../core/steer.h"

static int g_fail, g_pass;

#define CHECK(cond, ...)                                                                          \
	do {                                                                                          \
		if (cond) {                                                                               \
			g_pass++;                                                                             \
		} else {                                                                                  \
			g_fail++;                                                                             \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                          \
			printf(__VA_ARGS__);                                                                  \
			printf("\n");                                                                         \
		}                                                                                         \
	} while (0)

static void hex(const uint8_t *b, int n, char *out) {
	for (int i = 0; i < n; i++)
		sprintf(out + 2 * i, "%02x", b[i]);
}

static void unhex(const char *s, uint8_t *out) {
	for (int i = 0; s[2 * i]; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
}

static void test_aes(void) {
	Aes128Key k;
	uint8_t key[16], pt[16], ct[16], back[16];
	char h[33];
	unhex("000102030405060708090a0b0c0d0e0f", key);
	unhex("00112233445566778899aabbccddeeff", pt);
	aes128_expand_key(&k, key);
	aes128_encrypt_block(&k, pt, ct);
	hex(ct, 16, h);
	CHECK(strcmp(h, "69c4e0d86a7b0430d8cdb78070b4c55a") == 0, "FIPS-197 C.1 got %s", h);
	aes128_decrypt_block(&k, ct, back);
	CHECK(memcmp(back, pt, 16) == 0, "FIPS-197 C.1 decrypt");
	unhex("2b7e151628aed2a6abf7158809cf4f3c", key);
	aes128_expand_key(&k, key);
	hex(k.w + 160, 16, h);
	CHECK(strcmp(h, "d014f9a8c9ee2589e13f0cc8b6630ca6") == 0, "FIPS-197 A.1 last round key %s", h);
}

typedef struct {
	const char *name;
	DriveCmd cmd;
	const char *cipher;
} Vec;

static void test_qcar(void) {
	QcarCodec c;
	qcar_init(&c);
	// jenkings/BLE-RC-Car-ESP32 frames (the published vectors plus the rest of the
	// file). speed field: 0x50 normal, 0x64 turbo.
	const Vec v[] = {
	    {"IDLE", {0, 0, 0x50, false}, "025e695a48ff2a438ca680f83e04e45d"},
	    {"FORWARD", {1, 0, 0x50, false}, "29609c664852cff1b0f0cbb98014bd2c"},
	    {"BACKWARD", {-1, 0, 0x50, false}, "03209909ba9da1c8b986163c6d484655"},
	    {"LEFT", {0, -1, 0x50, false}, "51382112135cccdb46cf8921b705499a"},
	    {"RIGHT", {0, 1, 0x50, false}, "1b5769cdf13e8ab627080ff3cefc3bc0"},
	    {"FORWARD_TURBO", {1, 0, 0x64, false}, "e65567da8e6c560d09d3733a7f47ff06"},
	    {"BACKWARD_TURBO", {-1, 0, 0x64, false}, "cec2ff1d7acc163cd13b7e6153ad5c45"},
	    {"FORWARD_LEFT", {1, -1, 0x50, false}, "9928e590dfe821485f414fbb633d5c4e"},
	    {"FORWARD_RIGHT", {1, 1, 0x50, false}, "0f2ce56662d4fd9d32a44f102bf20aa7"},
	    {"BACKWARD_LEFT", {-1, -1, 0x50, false}, "98ce981d58d115afe11960bf4613925c"},
	    {"BACKWARD_RIGHT", {-1, 1, 0x50, false}, "f2520fba3144fb11468fe080c6c2c23c"},
	    {"FORWARD_TURBO_LEFT", {1, -1, 0x64, false}, "592381c943a417ca1bc3b59400e0fc12"},
	    {"FORWARD_TURBO_RIGHT", {1, 1, 0x64, false}, "fb976fba04af87022226ec50ae82f8c4"},
	    {"BACKWARD_TURBO_LEFT", {-1, -1, 0x64, false}, "d54ad55857d327745f141dd00d671595"},
	    {"BACKWARD_TURBO_RIGHT", {-1, 1, 0x64, false}, "80dfb2165f3260f1d9837750f43a43da"},
	    // gist "keep steady" = LEFT with lights on (byte 8 = 0)
	    {"LEFT_LIGHTS", {0, -1, 0x50, true}, "e1055f54d880f49c2ce547267f930bf2"},
	};
	for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		uint8_t out[16];
		char h[33];
		qcar_encode(&c, &v[i].cmd, out);
		hex(out, 16, h);
		CHECK(strcmp(h, v[i].cipher) == 0, "%s: got %s want %s", v[i].name, h, v[i].cipher);
	}

	// Neutral/steering frames ignore the speed field (always 0x50).
	DriveCmd odd = {0, 0, 37, false};
	uint8_t p[16];
	qcar_plaintext(&odd, p);
	CHECK(p[9] == 0x50, "neutral speed byte %d", p[9]);
	DriveCmd fast = {1, 0, 250, false};
	qcar_plaintext(&fast, p);
	CHECK(p[9] == 100, "speed clamped to 100, got %d", p[9]);
	DriveCmd slow = {1, 0, 35, true};
	qcar_plaintext(&slow, p);
	CHECK(p[9] == 35 && p[8] == 0 && p[4] == 1 && p[5] == 0, "proportional speed frame");

	// Battery notify from the Marvel module, 2026-11-22: counter 2, 72 %.
	uint8_t bat[16];
	unhex("83230ab55f4318b94eaff4696bff1ab1", bat);
	int counter = -1;
	CHECK(qcar_decode_battery(&c, bat, &counter) == 72 && counter == 2, "battery 72%% / 2");
	uint8_t idle[16];
	DriveCmd n = drive_neutral(false);
	qcar_encode(&c, &n, idle);
	CHECK(qcar_decode_battery(&c, idle, NULL) == -1, "control frame is not battery");
}

#define pad(buttons, lx, ly, rx) (&(PadInput){true, (buttons), (lx), (ly), (rx), 0})

static void test_input(void) {
	InputMapper m;
	input_init(&m, 30, 2); // gear 2 = cap 80
	DriveCmd c;

	c = input_update(&m, &(PadInput){0});
	CHECK(c.throttle == 0 && c.steer == 0 && c.speed == 0x50, "idle");

	// Stick throttle: dead zone, hysteresis, proportional speed.
	c = input_update(&m, &(PadInput){true, 0, 0, 0.12f, 0, 0});
	CHECK(c.throttle == 0, "below throttle_on");
	c = input_update(&m, &(PadInput){true, 0, 0, 0.16f, 0, 0});
	CHECK(c.throttle == 1 && c.speed >= 30 && c.speed <= 34, "just engaged: speed %d", c.speed);
	c = input_update(&m, &(PadInput){true, 0, 0, 0.12f, 0, 0});
	CHECK(c.throttle == 1 && c.speed == 31, "held above throttle_off: speed %d", c.speed);
	c = input_update(&m, &(PadInput){true, 0, 0, 0.05f, 0, 0});
	CHECK(c.throttle == 0 && c.speed == 0x50, "released");
	c = input_update(&m, &(PadInput){true, 0, 0, 1.0f, 0, 0});
	CHECK(c.throttle == 1 && c.speed == 80, "full stick = gear cap 80, got %d", c.speed);
	c = input_update(&m, &(PadInput){true, 0, 0, -0.55f, 0, 0});
	CHECK(c.throttle == -1 && c.speed == 55, "half reverse = 55, got %d", c.speed);
	c = input_update(&m, &(PadInput){true, 0, 0, NAN, 0, 0});
	CHECK(c.throttle == 0, "NaN stick = no throttle");

	// Triggers override the stick, both = stop, boost = 100.
	c = input_update(&m, &(PadInput){true, PAD_ZR, 0, -1.0f, 0, 0});
	CHECK(c.throttle == 1 && c.speed == 80, "ZR overrides stick");
	c = input_update(&m, &(PadInput){true, PAD_ZR | PAD_ZL, 0, 0, 0, 0});
	CHECK(c.throttle == 0, "ZR+ZL = stop");
	c = input_update(&m, &(PadInput){true, PAD_ZL | PAD_B, 0, 0, 0, 0});
	CHECK(c.throttle == -1 && c.speed == 100, "boost");

	// Gears.
	input_update(&m, &(PadInput){0});
	input_update(&m, &(PadInput){true, PAD_L, 0, 0, 0, 0});
	c = input_update(&m, &(PadInput){true, PAD_L | PAD_ZR, 0, 0, 0, 0}); // held, no 2nd step
	CHECK(m.gear == 1 && c.speed == 60, "gear down once: gear %d speed %d", m.gear, c.speed);
	input_update(&m, &(PadInput){0});
	for (int i = 0; i < 5; i++) {
		input_update(&m, &(PadInput){true, PAD_R, 0, 0, 0, 0});
		input_update(&m, &(PadInput){0});
	}
	CHECK(m.gear == 3, "gear capped at 3");
	CHECK(m.settings_changed, "settings_changed");

	// Steering: d-pad wins, hysteresis on sticks, larger stick counts.
	input_init(&m, 30, 2);
	c = input_update(&m, pad(PAD_LEFT, 1.0f, 0, 0));
	CHECK(c.steer == -1, "d-pad wins");
	c = input_update(&m, pad(PAD_LEFT | PAD_RIGHT, 0, 0, 0));
	CHECK(c.steer == 0, "both d-pad = straight");
	c = input_update(&m, pad(0, 0.45f, 0, 0));
	CHECK(c.steer == 0, "below steer_on");
	c = input_update(&m, pad(0, 0.1f, 0, 0.6f));
	CHECK(c.steer == 1, "right stick");
	c = input_update(&m, pad(0, 0.35f, 0, 0));
	CHECK(c.steer == 1, "held above steer_off");
	c = input_update(&m, pad(0, -0.25f, 0, 0));
	CHECK(c.steer == 0, "released");
	c = input_update(&m, pad(0, -0.9f, 0, 0.2f));
	CHECK(c.steer == -1, "left stick left");

	// Lights toggle on rising edge; reset requires release.
	input_init(&m, 30, 2);
	c = input_update(&m, pad(PAD_A, 0, 0, 0));
	CHECK(c.lights, "lights on");
	c = input_update(&m, pad(PAD_A, 0, 0, 0));
	CHECK(c.lights, "held: no toggle");
	input_reset(&m, PAD_A);
	c = input_update(&m, pad(PAD_A, 0, 0, 0));
	CHECK(c.lights, "held across reset: no toggle");
	c = input_update(&m, pad(0, 0, 0, 0));
	c = input_update(&m, pad(PAD_A, 0, 0, 0));
	CHECK(!c.lights, "lights off");

	// Min speed calibration.
	input_init(&m, 30, 3);
	input_update(&m, pad(PAD_MINUS, 0, 0, 0));
	input_update(&m, pad(PAD_MINUS | PAD_UP, 0, 0, 0));
	CHECK(m.min_speed == 35, "min +5: %d", m.min_speed);
	input_update(&m, pad(PAD_MINUS, 0, 0, 0));
	input_update(&m, pad(PAD_MINUS | PAD_DOWN, 0, 0, 0));
	input_update(&m, pad(PAD_MINUS, 0, 0, 0));
	input_update(&m, pad(PAD_MINUS | PAD_DOWN, 0, 0, 0));
	CHECK(m.min_speed == 25, "min -10: %d", m.min_speed);
	input_init(&m, 5, 9);
	CHECK(m.min_speed == INPUT_MIN_SPEED_LO && m.gear == 3, "init clamps");
	input_init(&m, 60, 0);
	c = input_update(&m, pad(PAD_ZR, 0, 0, 0));
	CHECK(c.speed == 60, "cap never below min speed: %d", c.speed);

	// any_drive.
	input_init(&m, 30, 2);
	CHECK(!input_any_drive(&m, pad(PAD_A | PAD_PLUS, 0.2f, 0.05f, 0)), "no drive input");
	CHECK(input_any_drive(&m, pad(0, 0, 0.11f, 0)), "stick throttle counts");
	CHECK(input_any_drive(&m, pad(PAD_ZL, 0, 0, 0)), "ZL counts");

	// Exhaustive: never conflicting directions, speed within limits.
	const uint32_t combos[] = {0, PAD_ZR, PAD_ZL, PAD_ZR | PAD_ZL, PAD_LEFT, PAD_RIGHT,
	                           PAD_LEFT | PAD_RIGHT, PAD_B, PAD_ZR | PAD_B | PAD_LEFT};
	const float sticks[] = {-1, -0.5f, -0.12f, 0, 0.12f, 0.5f, 1};
	QcarCodec q;
	qcar_init(&q);
	int bad = 0;
	for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); i++)
		for (size_t a = 0; a < 7; a++)
			for (size_t b = 0; b < 7; b++) {
				c = input_update(&m, pad(combos[i], sticks[a], sticks[b], sticks[6 - a]));
				uint8_t pl[16];
				qcar_plaintext(&c, pl);
				if ((pl[4] && pl[5]) || (pl[6] && pl[7]) || pl[9] > 100 ||
				    (c.throttle && c.speed < m.min_speed))
					bad++;
			}
	CHECK(bad == 0, "%d bad frames in exhaustive sweep", bad);
}

static void test_stats(void) {
	static Rolling r;
	memset(&r, 0, sizeof(r));
	Summary s = rolling_summary(&r);
	CHECK(s.n == 0, "empty");
	for (int i = 1; i <= 100; i++)
		rolling_push(&r, (float)i);
	s = rolling_summary(&r);
	CHECK(s.n == 100 && s.p50 == 50 && s.p95 == 95 && s.min == 1 && s.max == 100, "1..100");
	for (int i = 0; i < STATS_CAP; i++)
		rolling_push(&r, 10.0f);
	s = rolling_summary(&r);
	CHECK(s.n == STATS_CAP && s.max == 10.0f, "window rolls over");
}

static void test_steer(void) {
	SteerAssist s;
	steer_assist_init(&s, 150, 60, 30);
	DriveCmd c = {1, 0, 20, false};
	CHECK(steer_assist_apply(&s, c, 0).speed == 20, "straight: untouched");
	c.steer = -1;
	CHECK(steer_assist_apply(&s, c, 100).speed == 60, "turn start: kick");
	CHECK(steer_assist_apply(&s, c, 200).speed == 60, "still kicking at +100 ms");
	CHECK(steer_assist_apply(&s, c, 251).speed == 30, "after kick: floor");
	c.speed = 45;
	CHECK(steer_assist_apply(&s, c, 300).speed == 45, "above floor: untouched");
	c.steer = 1;
	c.speed = 20;
	CHECK(steer_assist_apply(&s, c, 400).speed == 60, "direction flip: new kick");
	DriveCmd n = {0, 1, 0x50, false};
	CHECK(steer_assist_apply(&s, n, 500).speed == 0x50 && steer_assist_apply(&s, n, 500).throttle == 0,
	      "no throttle: untouched");
	c.speed = 90;
	CHECK(steer_assist_apply(&s, c, 600).speed == 90, "fast turn untouched");
	// Timer wrap-around safety.
	steer_assist_init(&s, 150, 60, 0);
	c.steer = -1;
	c.speed = 20;
	CHECK(steer_assist_apply(&s, c, 0xFFFFFFF0u).speed == 60, "kick across wrap");
	CHECK(steer_assist_apply(&s, c, 0x200).speed == 20, "kick ended after wrap");
	steer_assist_init(&s, 0, 0, 0);
	c.steer = 1;
	CHECK(steer_assist_apply(&s, c, 10).speed == 20, "disabled");
}

static void test_autopilot(void) {
	Autopilot a;
	autopilot_start(&a, 12345, 1000, 120000, 20, 60);
	int bad = 0, moving = 0, paused = 0, turning = 0, reverse = 0;
	uint8_t min_sp = 255, max_sp = 0;
	DriveCmd prev = {0};
	uint32_t changes = 0;
	for (uint32_t t = 1000; t < 121000; t += 10) {
		DriveCmd c = autopilot_update(&a, t, true);
		uint8_t p[16];
		qcar_plaintext(&c, p);
		if ((p[4] && p[5]) || (p[6] && p[7]) || !c.lights)
			bad++;
		if (c.throttle) {
			moving++;
			if (c.speed < min_sp)
				min_sp = c.speed;
			if (c.speed > max_sp)
				max_sp = c.speed;
			if (c.throttle < 0)
				reverse++;
		} else {
			paused++;
		}
		if (c.steer)
			turning++;
		if (c.throttle != prev.throttle || c.steer != prev.steer || c.speed != prev.speed)
			changes++;
		prev = c;
	}
	CHECK(bad == 0, "%d bad frames", bad);
	CHECK(min_sp >= 20 && max_sp <= 60, "speeds %u..%u within 20..60", min_sp, max_sp);
	CHECK(moving > paused && paused > 0 && turning > 0 && reverse > 0,
	      "mix: moving %d paused %d turning %d reverse %d", moving, paused, turning, reverse);
	CHECK(a.segments > 50 && changes > 50, "many segments: %u", a.segments);
	CHECK(!a.done && autopilot_remaining_s(&a, 120000) == 1, "1 s left");
	DriveCmd end = autopilot_update(&a, 121000, false);
	CHECK(a.done && end.throttle == 0 && end.steer == 0, "ends neutral");
	CHECK(autopilot_update(&a, 200000, false).throttle == 0, "stays neutral");
	CHECK(autopilot_remaining_s(&a, 121000) == 0, "0 left");

	// Same seed, same drive.
	Autopilot b, c2;
	autopilot_start(&b, 7, 0, 10000, 20, 60);
	autopilot_start(&c2, 7, 0, 10000, 20, 60);
	int diff = 0;
	for (uint32_t t = 0; t < 10000; t += 10) {
		DriveCmd x = autopilot_update(&b, t, false), y = autopilot_update(&c2, t, false);
		diff += x.throttle != y.throttle || x.steer != y.steer || x.speed != y.speed;
	}
	CHECK(diff == 0, "deterministic");
	autopilot_start(&b, 1, 0, 0, 20, 60);
	CHECK(b.done && autopilot_update(&b, 5, false).throttle == 0, "zero duration");
	autopilot_start(&b, 1, 0, 1000, 90, 200);
	CHECK(b.lo == 90 && b.hi == 100, "clamped range");
}

int main(void) {
	test_aes();
	test_qcar();
	test_input();
	test_stats();
	test_steer();
	test_autopilot();
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
