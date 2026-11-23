#include "car.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/autopilot.h"
#include "../../core/input.h"
#include "../../core/steer.h"
#include "ble.h"
#include "config.h"
#include "log.h"
#include "pad.h"

#define TICK_NS 10000000ULL // 10 ms, like the official app
#define GAP_MS 200.0        // a longer loop stall means we were suspended
#define CHECK_EVERY 25      // ticks between connection-state checks (250 ms)
#define CONNECT_TIMEOUT_MS 8000

static const int BACKOFF_MS[] = {500, 1000, 2000, 4000, 5000};

static Thread g_thread;
static bool g_thread_live;
static Mutex g_lock;
static CarStatus g_st;
static CarSettings g_set;
static volatile bool g_stop;
static volatile bool g_focus_lost;
static volatile bool g_focus_changed;
static volatile int g_cat_req; // 0 none, 1 start, -1 stop
static volatile int g_cat_seconds;
static BleLink g_link;
static volatile bool g_link_valid;
static QcarCodec g_codec;

static double ms_since(u64 t) {
	return (double)armTicksToNs(armGetSystemTick() - t) / 1e6;
}

static uint32_t now_ms(void) {
	return (uint32_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

static void set_phase(CarPhase p, const char *msg) {
	mutexLock(&g_lock);
	g_st.phase = p;
	snprintf(g_st.message, sizeof(g_st.message), "%s", msg);
	mutexUnlock(&g_lock);
	log_msg("faza %d: %s", (int)p, msg);
}

static bool send_cmd(const DriveCmd *c) {
	u8 frame[16];
	qcar_encode(&g_codec, c, frame);
	return R_SUCCEEDED(ble_write(&g_link, frame));
}

static void neutral_burst(bool lights, int frames) {
	DriveCmd n = drive_neutral(lights);
	for (int i = 0; i < frames; i++) {
		send_cmd(&n);
		svcSleepThread(TICK_NS);
	}
}

static void car_thread(void *arg) {
	(void)arg;
	InputMapper m;
	input_init(&m, g_set.min_speed, g_set.gear);
	SteerAssist steer;
	steer_assist_init(&steer, (uint16_t)g_set.steer_kick_ms, g_set.steer_kick_speed, g_set.steer_floor);
	Autopilot cat;
	bool cat_on = false;
	bool cat_by_human = false;
	u32 cat_sessions = 0;
	PadState pad;
	pad_init(&pad);
	PadInput in;
	Rolling *period = malloc(sizeof(Rolling));
	memset(period, 0, sizeof(*period));
	int backoff = 0;
	u64 retry_at = 0;
	bool first_connect = true;

	set_phase(CAR_CONNECTING, "laczenie...");
	while (!g_stop) {
		CarPhase phase = g_st.phase;

		if (phase == CAR_WAIT_RETRY) {
			if (armGetSystemTick() < retry_at) {
				svcSleepThread(50000000ULL);
				continue;
			}
			set_phase(CAR_CONNECTING, "ponowne laczenie...");
			continue;
		}

		if (phase == CAR_CONNECTING) {
			char err[64] = "";
			u64 t0 = armGetSystemTick();
			Result rc = ble_connect(&g_st.addr, &g_link, CONNECT_TIMEOUT_MS, err, sizeof(err));
			if (g_stop)
				break;
			if (R_FAILED(rc)) {
				int wait = BACKOFF_MS[backoff];
				if (backoff < (int)(sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0])) - 1)
					backoff++;
				retry_at = armGetSystemTick() + armNsToTicks((u64)wait * 1000000ULL);
				char msg[96];
				snprintf(msg, sizeof(msg), "%s, ponowna proba za %.1f s", err, wait / 1000.0);
				set_phase(CAR_WAIT_RETRY, msg);
				continue;
			}
			backoff = 0;
			// Always start from neutral, never from the previous state.
			pad_read(&pad, &in);
			input_reset(&m, in.buttons);
			DriveCmd n = drive_neutral(m.lights);
			send_cmd(&n);
			mutexLock(&g_lock);
			g_st.connect_ms = ms_since(t0);
			g_st.holding = true;
			g_st.cmd = n;
			if (!first_connect)
				g_st.reconnects++;
			mutexUnlock(&g_lock);
			first_connect = false;
			g_link_valid = true;
			set_phase(CAR_DRIVING, "jazda");
			continue;
		}

		// ---- CAR_DRIVING: one tick every 10 ms ----
		bool holding = true;
		u64 last = armGetSystemTick();
		u64 next_ns = armTicksToNs(last) + TICK_NS;
		u32 tick = 0, errors_in_row = 0;
		u8 bat[16];
		while (!g_stop) {
			u64 now_ns = armTicksToNs(armGetSystemTick());
			if (next_ns > now_ns)
				svcSleepThread(next_ns - now_ns);
			next_ns += TICK_NS;
			u64 now = armGetSystemTick();
			if (armTicksToNs(now) > next_ns + 5 * TICK_NS)
				next_ns = armTicksToNs(now) + TICK_NS; // far behind: don't burst to catch up
			double gap = (double)armTicksToNs(now - last) / 1e6;
			last = now;
			rolling_push(period, (float)gap);
			tick++;
			const uint32_t t_ms = now_ms();

			if (gap > GAP_MS) {
				log_msg("przerwa w petli %.0f ms - neutral, puszczaj przyciski", gap);
				holding = true;
			}
			if (g_focus_changed) {
				g_focus_changed = false;
				holding = true;
			}
			pad_read(&pad, &in);
			DriveCmd c = input_update(&m, &in);
			const bool paused = g_focus_lost;
			const bool human = in.connected && input_any_drive(&m, &in);

			// Cat mode requests / lifecycle.
			int req = g_cat_req;
			if (req) {
				g_cat_req = 0;
				if (req > 0 && !paused) {
					autopilot_start(&cat, (uint32_t)armGetSystemTick() | 1u, t_ms,
					                (uint32_t)g_cat_seconds * 1000u, g_set.cat_lo, g_set.cat_hi);
					cat_on = true;
					log_msg("tryb kot: start na %d s, predkosc %u-%u%%", g_cat_seconds, g_set.cat_lo,
					        g_set.cat_hi);
				} else if (cat_on) {
					cat_on = false;
					holding = true;
					cat_by_human = false;
					cat_sessions++;
					log_msg("tryb kot: stop");
				}
			}
			if (cat_on && (paused || human || gap > GAP_MS)) {
				cat_on = false;
				holding = true;
				cat_by_human = human;
				cat_sessions++;
				log_msg("tryb kot: przerwany (%s)", paused ? "fokus" : human ? "pad" : "przerwa w petli");
			}

			DriveCmd out;
			if (cat_on) {
				out = autopilot_update(&cat, t_ms, m.lights);
				if (cat.done) {
					cat_on = false;
					holding = true;
					cat_by_human = false;
					cat_sessions++;
					log_msg("tryb kot: koniec, %u odcinkow", cat.segments);
				}
			} else {
				if (!in.connected || paused || g_set.probe)
					holding = true;
				else if (holding && !human)
					holding = false;
				out = holding ? drive_neutral(c.lights) : c;
			}
			out = steer_assist_apply(&steer, out, t_ms);

			bool ok = send_cmd(&out);
			errors_in_row = ok ? 0 : errors_in_row + 1;

			int pct = -1, counter = 0;
			if (ble_poll_battery(&g_link, bat))
				pct = qcar_decode_battery(&g_codec, bat, &counter);

			bool lost = false;
			if (errors_in_row >= 10 || tick % CHECK_EVERY == 0)
				lost = !ble_connected(&g_link);

			mutexLock(&g_lock);
			g_st.cmd = out;
			g_st.holding = holding && !cat_on;
			g_st.paused = paused;
			g_st.pad_ok = in.connected;
			g_st.gear = m.gear;
			g_st.min_speed = m.min_speed;
			g_st.lights = m.lights;
			g_st.cat_active = cat_on;
			g_st.cat_pending = false;
			g_st.cat_left_s = cat_on ? autopilot_remaining_s(&cat, t_ms) : 0;
			g_st.cat_sessions = cat_sessions;
			g_st.cat_ended_by_human = cat_by_human;
			g_st.writes += ok;
			g_st.write_errors += !ok;
			if (m.settings_changed) {
				g_st.settings_changed = true;
				m.settings_changed = false;
			}
			if (pct >= 0) {
				g_st.battery = pct;
				g_st.battery_counter = counter;
				g_st.battery_age_s = 0;
			} else if (g_st.battery >= 0) {
				g_st.battery_age_s += gap / 1000.0;
			}
			if (tick % 50 == 0)
				g_st.period = rolling_summary(period);
			mutexUnlock(&g_lock);
			if (pct >= 0)
				log_msg("bateria %d%% (licznik %d)", pct, counter);

			if (lost) {
				g_link_valid = false;
				if (cat_on) {
					cat_on = false;
					cat_sessions++;
					log_msg("tryb kot: przerwany (zerwane polaczenie)");
				}
				log_msg("polaczenie zerwane (bledow z rzedu %u)", errors_in_row);
				retry_at = armGetSystemTick() + armNsToTicks(500000000ULL);
				set_phase(CAR_WAIT_RETRY, "polaczenie zerwane, ponowne laczenie...");
				break;
			}
		}
	}

	// Intentional stop: ~100 ms of neutral, then disconnect.
	if (g_link_valid) {
		g_link_valid = false;
		neutral_burst(g_st.lights, 10);
		ble_disconnect(&g_link, 2000);
	}
	Summary s = rolling_summary(period);
	log_msg("odstep ramek: n=%u p50=%.2f p95=%.2f p99=%.2f max=%.2f ms", s.n, s.p50, s.p95, s.p99,
	        s.max);
	free(period);
	mutexLock(&g_lock);
	g_st.cat_active = false;
	g_st.cat_pending = false;
	mutexUnlock(&g_lock);
	set_phase(CAR_IDLE, "rozlaczono");
}

void car_start(const BtdrvAddress *addr, const CarSettings *s) {
	car_stop();
	static bool once;
	if (!once) {
		mutexInit(&g_lock);
		qcar_init(&g_codec);
		once = true;
	}
	g_set = *s;
	mutexLock(&g_lock);
	bool pending = g_st.cat_pending;
	memset(&g_st, 0, sizeof(g_st));
	g_st.addr = *addr;
	g_st.battery = -1;
	g_st.min_speed = s->min_speed;
	g_st.gear = s->gear;
	g_st.cmd = drive_neutral(false);
	g_st.cat_pending = pending;
	mutexUnlock(&g_lock);
	g_stop = false;
	g_link_valid = false;
	// Higher priority than the main thread (0x2C), own core.
	Result rc = threadCreate(&g_thread, car_thread, NULL, NULL, 0x10000, 0x2A, 2);
	if (R_FAILED(rc))
		rc = threadCreate(&g_thread, car_thread, NULL, NULL, 0x10000, 0x2A, -2);
	if (R_SUCCEEDED(rc))
		rc = threadStart(&g_thread);
	if (R_FAILED(rc)) {
		log_msg("car thread rc=0x%x", rc);
		set_phase(CAR_IDLE, "nie udalo sie uruchomic watku");
		return;
	}
	g_thread_live = true;
}

void car_stop(void) {
	if (!g_thread_live)
		return;
	g_stop = true;
	ble_abort(true); // cut a connection attempt short
	threadWaitForExit(&g_thread);
	ble_abort(false);
	threadClose(&g_thread);
	g_thread_live = false;
	g_cat_req = 0;
}

bool car_running(void) {
	return g_thread_live;
}

void car_status(CarStatus *out) {
	mutexLock(&g_lock);
	*out = g_st;
	mutexUnlock(&g_lock);
}

void car_clear_settings_changed(void) {
	mutexLock(&g_lock);
	g_st.settings_changed = false;
	mutexUnlock(&g_lock);
}

void car_cat(bool on, int seconds) {
	if (seconds > 0)
		g_cat_seconds = seconds;
	g_cat_req = on ? 1 : -1;
	mutexLock(&g_lock);
	g_st.cat_pending = on;
	mutexUnlock(&g_lock);
}

void car_set_focus(bool in_focus) {
	// Always track focus, so a car started later knows it too.
	g_focus_lost = !in_focus;
	g_focus_changed = true;
	if (!g_thread_live)
		return;
	if (!in_focus && g_link_valid) {
		// Don't wait for the car thread: we may be suspended right after
		// this hook returns. Two neutral frames straight away.
		DriveCmd n = drive_neutral(g_st.lights);
		u8 frame[16];
		qcar_encode(&g_codec, &n, frame);
		ble_write(&g_link, frame);
		ble_write(&g_link, frame);
		log_msg("utrata fokusu - neutral");
	}
}
