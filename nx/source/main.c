// Shell RC — drive the Shell (Marvel 2026) BLE cars from a Nintendo Switch.
// Screens: car list (raw BLE scan) -> driving (status + controls help).
// Cat mode (random slow wandering) from the X button or Home Assistant (HTTP).
// Text is ASCII on purpose: the libnx console font has no Polish letters.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <switch.h>

#include "../../core/input.h"
#include "ble.h"
#include "car.h"
#include "config.h"
#include "http.h"
#include "log.h"
#include "ui.h"

#define VERSION "1.1.0"
#define SCAN_MS 4000
#define COLS 79
// 2026-11-22: at 6-14 % (reading jumps under load) the module dropped the
// link after ~2.5 min and died soon after.
#define LOW_BATTERY 15

typedef enum { SCREEN_LIST, SCREEN_DRIVE, SCREEN_ERROR } Screen;

static Config g_cfg;
static AppletHookCookie g_hook;
static char g_ip[16];

// Last known battery, also while disconnected (standby checks).
static int g_last_pct = -1;
static u64 g_last_pct_tick;

#define BATTERY_CSV "sdmc:/switch/shellrc/battery.csv"

static double secs_since(u64 tick) {
	return (double)armTicksToNs(armGetSystemTick() - tick) / 1e9;
}

static u64 ticks_in(double seconds) {
	return armGetSystemTick() + armNsToTicks((u64)(seconds * 1e9));
}

// unix time, percent (-1 = no reading), event
static void battery_csv(int pct, const char *event) {
	FILE *f = fopen(BATTERY_CSV, "a");
	if (!f)
		return;
	fprintf(f, "%lld,%d,%s\n", (long long)time(NULL), pct, event);
	fclose(f);
}

static void remember_battery(int pct, const char *event) {
	g_last_pct = pct;
	g_last_pct_tick = armGetSystemTick();
	battery_csv(pct, event);
	log_msg("bateria %d%% (%s)", pct, event);
}

static bool standby_enabled(void) {
	return g_cfg.keep_awake && g_cfg.standby_minutes > 0 && g_cfg.has_device;
}

static void on_applet(AppletHookType hook, void *param) {
	(void)param;
	if (hook == AppletHookType_OnFocusState)
		car_set_focus(appletGetFocusState() == AppletFocusState_InFocus);
}

static CarSettings settings(void) {
	CarSettings s = {g_cfg.min_speed,   g_cfg.gear,   g_cfg.steer_kick_ms, g_cfg.steer_kick_speed,
	                 g_cfg.steer_floor, g_cfg.cat_lo, g_cfg.cat_hi,        false};
	return s;
}

static void set_keep_awake(bool on) {
	g_cfg.keep_awake = on;
	Result rc = appletSetAutoSleepDisabled(on);
	log_msg("czuwanie (bez auto-uspienia): %s rc=0x%x", on ? "ON" : "off", rc);
}

static const char *phase_name(CarPhase p) {
	switch (p) {
	case CAR_IDLE:
		return TR("Rozlaczone", "Disconnected");
	case CAR_CONNECTING:
		return TR("Laczenie", "Connecting");
	case CAR_DRIVING:
		return TR("Jazda", "Driving");
	case CAR_WAIT_RETRY:
		return TR("Ponawiam", "Retrying");
	}
	return "?";
}

// Stable state names for the HTTP API (documented in docs/switch.md).
static const char *api_phase(CarPhase p) {
	switch (p) {
	case CAR_IDLE:
		return "ROZLACZONE";
	case CAR_CONNECTING:
		return "LACZENIE";
	case CAR_DRIVING:
		return "JAZDA";
	case CAR_WAIT_RETRY:
		return "PONAWIAM";
	}
	return "?";
}

static const char *ha_line(char *buf, size_t n) {
	if (!http_running())
		buf[0] = 0; // API off (default): nothing to show
	else if (!g_ip[0])
		snprintf(buf, n, "%s", TR("API HTTP: brak Wi-Fi", "HTTP API: no Wi-Fi"));
	else
		snprintf(buf, n, "HTTP API: http://%s:%d/cat/start%s   %s %s", g_ip, g_cfg.http_port,
		         g_cfg.token[0] ? "?token=..." : "", TR("czuwanie", "stay awake"),
		         g_cfg.keep_awake ? TR("wlaczone", "on") : TR("wylaczone", "off"));
	return buf;
}

// The list screen's message line: text + its tone.
static char g_note[112];
static UiTone g_note_tone;

static void note(UiTone tone, const char *pl, const char *en, ...) {
	va_list a;
	va_start(a, en);
	vsnprintf(g_note, sizeof(g_note), TR(pl, en), a);
	va_end(a);
	g_note_tone = tone;
}

static void draw_list(const ScanEntry *cars, int n, int cursor, unsigned frame) {
	UiCar ui_cars[SCAN_MAX];
	for (int i = 0; i < n && i < SCAN_MAX; i++) {
		snprintf(ui_cars[i].name, sizeof(ui_cars[i].name), "%s", cars[i].name);
		addr_to_str(&cars[i].addr, ui_cars[i].addr);
		ui_cars[i].rssi = cars[i].rssi;
		ui_cars[i].last_used =
		    g_cfg.has_device && !memcmp(&g_cfg.device, &cars[i].addr, sizeof(BtdrvAddress));
	}
	char standby[96] = "", ha[128];
	if (standby_enabled()) {
		if (g_last_pct >= 0)
			snprintf(standby, sizeof(standby),
			         TR("Czuwanie: bateria auta sprawdzana co %d min, ostatnio %d%% (%.0f min temu)",
			            "Standby: car battery checked every %d min, last %d%% (%.0f min ago)"),
			         g_cfg.standby_minutes, g_last_pct, secs_since(g_last_pct_tick) / 60);
		else
			snprintf(standby, sizeof(standby),
			         TR("Czuwanie: bateria auta sprawdzana co %d min", "Standby: car battery checked every %d min"),
			         g_cfg.standby_minutes);
	}
	UiList l = {"Shell RC " VERSION, ui_cars, n, cursor, ble_scan_running(), ble_scan_other_count(),
	            g_note, g_note_tone, standby, ha_line(ha, sizeof(ha)), frame};
	ui_draw_list(&l);
}

static void draw_drive(const CarStatus *s, const char *name) {
	char a[18], ha[128];
	addr_to_str(&s->addr, a);
	UiTone tone = s->phase == CAR_DRIVING ? UI_TONE_OK : s->phase == CAR_IDLE ? UI_TONE_BAD : UI_TONE_BUSY;
	UiDrive d = {
	    .version = "Shell RC " VERSION,
	    .name = name,
	    .addr = a,
	    .state = phase_name(s->phase),
	    .state_tone = tone,
	    .state_msg = s->message,
	    .battery = s->battery,
	    .battery_low = s->battery >= 0 && s->battery <= LOW_BATTERY,
	    .throttle = s->cmd.throttle,
	    .speed = s->cmd.speed,
	    .steer = s->cmd.steer,
	    .gear = s->gear + 1,
	    .gear_cap = INPUT_GEAR_CAP[s->gear],
	    .min_speed = s->min_speed,
	    .lights = s->lights,
	    .driving = s->phase == CAR_DRIVING,
	    .holding = s->holding,
	    .paused = s->paused,
	    .pad_ok = s->pad_ok,
	    .cat_active = s->cat_active,
	    .cat_pending = s->cat_pending,
	    .cat_left_s = s->cat_left_s,
	    .cat_seconds = g_cfg.cat_seconds,
	    .cat_lo = g_cfg.cat_lo,
	    .cat_hi = g_cfg.cat_hi,
	    .writes = s->writes,
	    .errors = s->write_errors,
	    .reconnects = s->reconnects,
	    .connect_ms = s->connect_ms,
	    .period_p50 = s->period.p50,
	    .period_p95 = s->period.p95,
	    .ha = ha_line(ha, sizeof(ha)),
	};
	ui_draw_drive(&d);
}

static void status_json(char *out, size_t n, Screen screen, const char *name, int pending_cat_s,
                        bool probing) {
	CarStatus s;
	car_status(&s);
	bool running = screen == SCREEN_DRIVE;
	const char *state = running ? api_phase(s.phase) : probing ? "POMIAR" : "CZUWANIE";
	if (!running && !standby_enabled() && !probing)
		state = "LISTA";
	long age = g_last_pct >= 0 ? (long)secs_since(g_last_pct_tick) : -1;
	snprintf(out, n,
	         "{\"ok\":true,\"car\":\"%s\",\"state\":\"%s\",\"battery\":%d,\"batteryAgeS\":%ld,"
	         "\"cat\":%s,\"catPending\":%s,\"catLeft\":%u,\"keepAwake\":%s,\"standby\":%s}",
	         running ? name : "", state, g_last_pct, age, running && s.cat_active ? "true" : "false",
	         (running && s.cat_pending) || pending_cat_s ? "true" : "false",
	         running ? s.cat_left_s : 0, g_cfg.keep_awake ? "true" : "false",
	         standby_enabled() ? "true" : "false");
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	consoleInit(NULL);
	padConfigureInput(8, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeAny(&pad);

	// Let us stop the car before exiting, and tell us about HOME / sleep.
	appletLockExit();
	appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
	appletHook(&g_hook, on_applet, NULL);

	log_open();
	log_msg("Shell RC %s start, appletType=%d", VERSION, (int)appletGetAppletType());
	config_load(&g_cfg);
	g_ui_english = !strcmp(g_cfg.lang, "en");
	bool sockets = false;
	if ((g_cfg.http_port > 0 || __nxlink_host.s_addr != 0) && R_SUCCEEDED(socketInitializeDefault())) {
		sockets = true;
		if (__nxlink_host.s_addr != 0)
			nxlinkStdioForDebug(); // log copy to the PC with nxlink -s
		http_start(g_cfg.http_port);
	}
	http_local_ip(g_ip);
	if (g_cfg.keep_awake)
		set_keep_awake(true);

	Screen screen = SCREEN_LIST;
	Result rc = ble_init();
	if (R_FAILED(rc)) {
		screen = SCREEN_ERROR;
		note(UI_TONE_BAD, "Bluetooth jest niedostepny (rc=0x%x). Uruchom Switcha ponownie i sprobuj jeszcze raz.",
		     "Bluetooth is unavailable (rc=0x%x). Restart the Switch and try again.", rc);
	} else {
		ble_scan_start(SCAN_MS);
	}
	// Auto-connect to the last car on start, except in standby: there the
	// car should stay disconnected until needed (saves its battery).
	bool auto_connect = g_cfg.has_device && !standby_enabled();
	int pending_cat_s = 0;                // cat mode to start once connected
	bool ha_session = false;              // cat started by HA from the list: disconnect after
	bool probe_search = false, probing = false;
	u64 probe_start = 0, next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
	ScanEntry cars[SCAN_MAX];
	int ncars = 0, cursor = 0;
	char car_name[24] = "";
	u64 last_draw = 0, last_ip = 0;
	unsigned frame = 0;
	ui_clear();

	while (appletMainLoop()) {
		padUpdate(&pad);
		u64 down = padGetButtonsDown(&pad);
		bool redraw = armTicksToNs(armGetSystemTick() - last_draw) > 100000000ULL; // 10 Hz
		if (armTicksToNs(armGetSystemTick() - last_ip) > 5000000000ULL) {
			http_local_ip(g_ip); // Wi-Fi may come and go
			last_ip = armGetSystemTick();
		}

		// --- Home Assistant / HTTP ---
		HttpRequest req;
		if (http_poll(&req, g_cfg.token)) {
			char json[320];
			int secs = req.seconds > 0 && req.seconds <= 1800 ? req.seconds : g_cfg.cat_seconds;
			if (!req.authorized) {
				http_respond(403, "{\"ok\":false,\"error\":\"zly token\"}");
			} else if (req.cmd == HTTP_CAT_START) {
				if (screen == SCREEN_DRIVE) {
					car_cat(true, secs);
					status_json(json, sizeof(json), screen, car_name, 0, false);
					http_respond(200, json);
				} else if (screen == SCREEN_LIST && g_cfg.has_device) {
					if (probing)
						car_stop(); // a battery check was running; reconnect to drive
					probing = probe_search = false;
					pending_cat_s = secs;
					ha_session = true;
					auto_connect = true;
					if (!ble_scan_running())
						ble_scan_start(SCAN_MS);
					note(UI_TONE_BUSY, "Tryb kot z HTTP: szukam auta...", "Cat mode via HTTP: looking for the car...");
					status_json(json, sizeof(json), screen, car_name, pending_cat_s, false);
					http_respond(200, json);
				} else {
					http_respond(409, "{\"ok\":false,\"error\":\"brak zapisanego auta albo Bluetooth\"}");
				}
			} else if (req.cmd == HTTP_CAT_STOP) {
				pending_cat_s = 0;
				if (screen == SCREEN_DRIVE)
					car_cat(false, 0);
				status_json(json, sizeof(json), screen, car_name, 0, probing);
				http_respond(200, json);
			} else if (req.cmd == HTTP_STATUS) {
				status_json(json, sizeof(json), screen, car_name, pending_cat_s, probing);
				http_respond(200, json);
			} else {
				http_respond(404, "{\"ok\":false,\"error\":\"/cat/start, /cat/stop, /status\"}");
			}
		}

		if (screen == SCREEN_ERROR) {
			if (redraw)
				ui_draw_error("Shell RC " VERSION, g_note);
			if (down & HidNpadButton_Plus)
				break;
		} else if (screen == SCREEN_LIST) {
			ncars = ble_scan_results(cars, SCAN_MAX);
			if (cursor >= ncars)
				cursor = ncars ? ncars - 1 : 0;
			int pick = -1;

			// --- standby: periodic battery check ---
			if (standby_enabled() && !probing && !probe_search && !auto_connect &&
			    armGetSystemTick() >= next_probe) {
				probe_search = true;
				ble_scan_start(SCAN_MS);
				log_msg("czuwanie: szukam auta do pomiaru baterii");
			}
			if (probe_search) {
				int found = -1;
				for (int i = 0; i < ncars; i++)
					if (!memcmp(&cars[i].addr, &g_cfg.device, sizeof(BtdrvAddress)))
						found = i;
				if (found >= 0) {
					ble_scan_stop();
					CarSettings cs = settings();
					cs.probe = true; // never drives, only reads the battery
					car_start(&cars[found].addr, &cs);
					probing = true;
					probe_search = false;
					probe_start = armGetSystemTick();
					note(UI_TONE_BUSY, "Czuwanie: pomiar baterii...", "Standby: checking the battery...");
				} else if (!ble_scan_running()) {
					probe_search = false;
					next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
					battery_csv(-1, "auto niewidoczne");
					log_msg("czuwanie: auto niewidoczne (wylaczone albo rozladowane?)");
					note(UI_TONE_BAD, "Czuwanie: auto niewidoczne przy ostatnim pomiarze.",
					     "Standby: the car was not visible at the last check.");
				}
			}
			if (probing) {
				CarStatus ps;
				car_status(&ps);
				bool got = ps.battery >= 0;
				if (got || secs_since(probe_start) > 75) {
					car_stop();
					probing = false;
					next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
					if (got) {
						remember_battery(ps.battery, "czuwanie");
						note(UI_TONE_NEUTRAL, "Czuwanie: bateria %d%%.", "Standby: battery %d%%.", ps.battery);
					} else {
						battery_csv(-1, "brak odczytu");
						note(UI_TONE_BAD, "Czuwanie: brak odczytu baterii.", "Standby: no battery reading.");
					}
				}
			}
			// Any manual action cancels a running check.
			if ((probing || probe_search) &&
			    (down & (HidNpadButton_A | HidNpadButton_X | HidNpadButton_Y))) {
				if (probing)
					car_stop();
				probing = probe_search = false;
				next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
			}
			if (auto_connect) {
				for (int i = 0; i < ncars; i++)
					if (!memcmp(&cars[i].addr, &g_cfg.device, sizeof(BtdrvAddress)))
						pick = i;
				if (pick < 0 && !ble_scan_running()) {
					auto_connect = false;
					if (pending_cat_s)
						log_msg("tryb kot z HA: nie znaleziono auta");
					pending_cat_s = 0;
					ha_session = false;
					note(UI_TONE_BAD, "Ostatnie auto nie odpowiada - wybierz z listy.",
					     "The last car does not answer - pick one from the list.");
				}
			}
			if ((down & HidNpadButton_AnyDown) && cursor + 1 < ncars)
				cursor++;
			if ((down & HidNpadButton_AnyUp) && cursor > 0)
				cursor--;
			if ((down & HidNpadButton_A) && ncars > 0)
				pick = cursor;
			if ((down & HidNpadButton_X) && ncars > 0) {
				pick = cursor;
				pending_cat_s = g_cfg.cat_seconds;
			}
			if (down & HidNpadButton_Y) {
				auto_connect = false;
				pending_cat_s = 0;
				g_note[0] = 0;
				ble_scan_start(SCAN_MS);
			}
			if (down & HidNpadButton_ZL) {
				g_ui_english = !g_ui_english;
				strcpy(g_cfg.lang, g_ui_english ? "en" : "pl");
				config_save(&g_cfg);
				ui_clear();
			}
			if (down & HidNpadButton_Minus) {
				set_keep_awake(!g_cfg.keep_awake);
				config_save(&g_cfg);
			}
			if (down & HidNpadButton_Plus)
				break;
			if (pick >= 0 && !probing) {
				auto_connect = false;
				ble_scan_stop(); // never scan raw while connected
				snprintf(car_name, sizeof(car_name), "%s", cars[pick].name);
				log_msg("wybrano %s%s", car_name, pending_cat_s ? " (tryb kot)" : "");
				CarSettings cs = settings();
				car_start(&cars[pick].addr, &cs);
				if (pending_cat_s)
					car_cat(true, pending_cat_s); // runs once connected
				pending_cat_s = 0;
				g_cfg.has_device = true;
				g_cfg.device = cars[pick].addr;
				config_save(&g_cfg);
				screen = SCREEN_DRIVE;
				ui_clear();
				continue;
			}
			if (redraw)
				draw_list(cars, ncars, cursor, ++frame);
		} else {
			CarStatus s;
			car_status(&s);
			if (s.settings_changed) {
				g_cfg.min_speed = s.min_speed;
				g_cfg.gear = s.gear;
				config_save(&g_cfg);
				car_clear_settings_changed();
			}
			if (s.battery >= 0 && s.battery_age_s < 1.0 &&
			    (s.battery != g_last_pct || secs_since(g_last_pct_tick) > 30))
				remember_battery(s.battery, "polaczone");
			// HA session: once cat mode ends on its own, back to standby
			// (disconnected) so neither battery drains. A human taking over
			// keeps the connection.
			bool ha_done = ha_session && s.cat_sessions > 0 && !s.cat_active && !s.cat_pending;
			if (ha_done && s.cat_ended_by_human)
				ha_session = false;
			else if (ha_done) {
				ha_session = false;
				log_msg("HA: koniec trybu kot, rozlaczam (czuwanie)");
				car_stop();
				note(UI_TONE_NEUTRAL, "Tryb kot z HTTP zakonczony.", "Cat mode via HTTP finished.");
				screen = SCREEN_LIST;
				next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
				ui_clear();
				ble_scan_start(SCAN_MS);
				continue;
			}
			if (down & HidNpadButton_X) {
				ha_session = false;
				car_cat(!(s.cat_active || s.cat_pending), g_cfg.cat_seconds);
			}
			if (down & HidNpadButton_Y) {
				set_keep_awake(!g_cfg.keep_awake);
				config_save(&g_cfg);
			}
			if (down & HidNpadButton_Plus) {
				car_stop();
				car_status(&s);
				g_cfg.min_speed = s.min_speed;
				g_cfg.gear = s.gear;
				config_save(&g_cfg);
				note(UI_TONE_NEUTRAL, "Rozlaczono z %s.", "Disconnected from %s.", car_name);
				ha_session = false;
				next_probe = ticks_in(g_cfg.standby_minutes * 60.0);
				screen = SCREEN_LIST;
				ui_clear();
				ble_scan_start(SCAN_MS);
				continue;
			}
			if (redraw)
				draw_drive(&s, car_name);
		}
		if (redraw)
			last_draw = armGetSystemTick();
		consoleUpdate(NULL);
	}

	log_msg("wyjscie");
	car_stop(); // neutral ~100 ms + disconnect if driving
	ble_scan_stop();
	ble_exit();
	http_stop();
	if (g_cfg.keep_awake)
		appletSetAutoSleepDisabled(false);
	log_close();
	if (sockets)
		socketExit();
	appletUnhook(&g_hook);
	appletUnlockExit();
	consoleExit(NULL);
	return 0;
}
