// Host preview of the Switch console UI: compiles nx/source/ui.c unchanged and
// writes each sample screen's escape sequences to <outdir>/<name>.ans.
// render.py turns them into 1280x720 PNGs (16x16 px cells, like the Switch).
#include <stdio.h>
#include <string.h>

#include "../../nx/source/ui.c"

static FILE *open_screen(const char *dir, const char *name) {
	char path[512];
	// dir is either "DIR" or "DIR/en-" (prefix for the English variant)
	size_t n = strlen(dir);
	snprintf(path, sizeof(path), n && dir[n - 1] == '-' ? "%s%s.ans" : "%s/%s.ans", dir, name);
	fflush(stdout);
	return freopen(path, "w", stdout);
}

static const char *HA = ""; // HTTP API is off by default

static UiDrive base_drive(void) {
	UiDrive d;
	memset(&d, 0, sizeof(d));
	d.version = "Shell RC 1.1.0";
	d.name = "QCAR-B5FDA2";
	d.addr = "7a:87:25:b5:fd:a2";
	d.state = TR("Jazda", "Driving");
	d.state_tone = UI_TONE_OK;
	d.state_msg = "";
	d.battery = 88;
	d.gear = 3;
	d.gear_cap = 80;
	d.min_speed = 20;
	d.driving = true;
	d.pad_ok = true;
	d.cat_seconds = 120;
	d.cat_lo = 20;
	d.cat_hi = 60;
	d.writes = 18342;
	d.connect_ms = 1064;
	d.period_p50 = 10.0f;
	d.period_p95 = 10.0f;
	d.ha = HA;
	return d;
}

static void screens(const char *dir);

// Every screen twice: <name>.ans (Polish) and en-<name>.ans (English).
int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : ".";
	char en_dir[480];
	g_ui_english = false;
	screens(dir);
	g_ui_english = true;
	snprintf(en_dir, sizeof(en_dir), "%s/en-", dir);
	screens(en_dir);
	return 0;
}

static void screens(const char *dir) {

	UiCar cars[] = {
	    {"QCAR-B5FDA2", "7a:87:25:b5:fd:a2", -54, true},
	    {"QCAR-1C02E9", "4c:11:9e:1c:02:e9", -77, false},
	};
	UiList l = {"Shell RC 1.1.0", cars, 2, 0, false, 5, TR("Rozlaczono z QCAR-B5FDA2.", "Disconnected from QCAR-B5FDA2."), UI_TONE_NEUTRAL,
	            "", HA, 0};
	open_screen(dir, "list");
	ui_clear();
	ui_draw_list(&l);

	UiList scan = {"Shell RC 1.1.0", cars, 0, 0, true, 3, "", UI_TONE_NEUTRAL,
	               TR("Czuwanie: bateria auta sprawdzana co 30 min, ostatnio 88% (12 min temu)",
	                  "Standby: car battery checked every 30 min, last 88% (12 min ago)"),
	               HA, 3};
	open_screen(dir, "list-scanning");
	ui_clear();
	ui_draw_list(&scan);

	UiDrive d = base_drive();
	d.throttle = 1;
	d.speed = 55;
	d.steer = -1;
	d.lights = true;
	open_screen(dir, "drive-forward");
	ui_clear();
	ui_draw_drive(&d);

	d = base_drive();
	d.throttle = -1;
	d.speed = 35;
	open_screen(dir, "drive-reverse");
	ui_clear();
	ui_draw_drive(&d);

	d = base_drive();
	d.holding = true;
	open_screen(dir, "drive-holding");
	ui_clear();
	ui_draw_drive(&d);

	d = base_drive();
	d.cat_active = true;
	d.cat_left_s = 95;
	d.throttle = 1;
	d.speed = 42;
	d.steer = 1;
	open_screen(dir, "drive-cat");
	ui_clear();
	ui_draw_drive(&d);

	d = base_drive();
	d.state = TR("Laczenie", "Connecting");
	d.state_tone = UI_TONE_BUSY;
	d.state_msg = TR("polaczenie zerwane, ponowna proba za 1.0 s", "connection lost, retrying in 1.0 s");
	d.driving = false;
	d.battery = 12;
	d.battery_low = true;
	open_screen(dir, "drive-reconnect");
	ui_clear();
	ui_draw_drive(&d);

	open_screen(dir, "error");
	ui_clear();
	ui_draw_error("Shell RC 1.1.0", TR("Bluetooth jest niedostepny (rc=0x1234).", "Bluetooth is unavailable (rc=0x1234)."));
}
