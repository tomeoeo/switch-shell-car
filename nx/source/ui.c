// Shell RC console UI. Design notes (2026-11-23):
//  - The screen is a pit wall: you look at the car, so every value must
//    read in half a second. One bold element only: a center-zero throttle
//    gauge across the whole width (reverse grows left in brake red, forward
//    grows right in signal yellow), mirroring the stick.
//  - The top band's color *is* the connection state (mint / yellow / red).
//  - Buttons look like keycaps; everything secondary is muted teal.
//  - ASCII only: the libnx console font has no Polish letters.
//  - Hierarchy by color only: libnx ignores bold for 24-bit colors.
// libnx wraps lazily (scrolls only when a character follows the last
// column), so every write starts with a cursor move and the last cell is safe.
#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	uint8_t r, g, b;
} Rgb;

static const Rgb BASE = {0x0f, 0x2a, 0x32};   // pit-wall teal
static const Rgb PANEL = {0x1b, 0x44, 0x50};  // panels, keycaps, gauge track
static const Rgb TEXT = {0xee, 0xf3, 0xf1};   // values
static const Rgb MUTED = {0x86, 0xa9, 0xb2};  // labels, help
static const Rgb YELLOW = {0xff, 0xc8, 0x3d}; // throttle forward, active, busy
static const Rgb RED = {0xef, 0x5a, 0x4c};    // reverse, warnings
static const Rgb MINT = {0x5f, 0xd9, 0xa3};   // connected, ok
static const Rgb INK = {0x0f, 0x2a, 0x32};    // text on bright backgrounds

#define GAUGE_L 3   // first gauge column
#define GAUGE_HALF 38 // cells per side; center between 40 and 41

static void style(Rgb f, Rgb b) {
	printf("\x1b[0m\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um", f.r, f.g, f.b, b.r, b.g, b.b);
}

static void at(int row, int col) {
	printf("\x1b[%d;%dH", row, col);
}

// Text at a position. Clipped to the screen width.
static int put(int row, int col, Rgb f, Rgb b, const char *fmt, ...) {
	char buf[160];
	va_list a;
	va_start(a, fmt);
	vsnprintf(buf, sizeof(buf), fmt, a);
	va_end(a);
	int room = UI_COLS - col + 1;
	int len = (int)strlen(buf);
	if (len > room)
		len = room < 0 ? 0 : room;
	at(row, col);
	style(f, b);
	printf("%.*s", len, buf);
	return col + len;
}

static void fill(int row, int col, int width, Rgb b) {
	if (width <= 0)
		return;
	at(row, col);
	style(TEXT, b);
	printf("%*s", width, "");
}

static void blank(int row) {
	fill(row, 1, UI_COLS, BASE);
}

static Rgb tone_color(UiTone t) {
	switch (t) {
	case UI_TONE_OK:
		return MINT;
	case UI_TONE_BUSY:
		return YELLOW;
	case UI_TONE_BAD:
		return RED;
	default:
		return PANEL;
	}
}

// A key cap like " ZR ". Returns the next free column.
static int key(int row, int col, const char *label) {
	int c = put(row, col, TEXT, PANEL, " %s ", label);
	return put(row, c, TEXT, BASE, " ");
}

// Message line: a colored marker block, then text in the same tone.
static void message(int row, UiTone tone, const char *text) {
	blank(row);
	if (!text || !text[0])
		return;
	Rgb c = tone == UI_TONE_NEUTRAL ? MUTED : tone_color(tone);
	fill(row, 3, 2, c);
	put(row, 6, c, BASE, "%s", text);
}

void ui_clear(void) {
	style(TEXT, BASE);
	printf("\x1b[2J"); // libnx clears with the current background
	at(1, 1);
}

// ---------------------------------------------------------------------------

static void band(Rgb color, bool dark_text, const char *left, const char *right) {
	Rgb f = dark_text ? INK : TEXT;
	fill(1, 1, UI_COLS, color);
	put(1, 2, f, color, "%s", left);
	if (right && right[0])
		put(1, UI_COLS - (int)strlen(right), f, color, "%s", right);
}

static int signal_level(int rssi) {
	return rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
}

void ui_draw_list(const UiList *s) {
	band(PANEL, false, "Shell RC", "wybierz auto ");
	blank(2);
	blank(3);
	if (s->scanning) {
		static const char spin[] = "|/-\\";
		put(3, 3, YELLOW, BASE, "%c", spin[(s->frame / 2) % 4]);
		put(3, 5, TEXT, BASE, "Szukam aut w poblizu...");
	} else {
		put(3, 3, TEXT, BASE, "Znalezione auta: %d", s->ncars);
		put(3, 30, MUTED, BASE, "inne urzadzenia Bluetooth w poblizu: %d", s->others);
	}
	blank(4);

	int row = 5;
	for (int i = 0; i < s->ncars && i < 8; i++, row += 3) {
		const UiCar *c = &s->cars[i];
		bool sel = i == s->cursor;
		Rgb b = sel ? PANEL : BASE;
		for (int k = 0; k < 2; k++) {
			blank(row + k);
			fill(row + k, 3, 76, b);
			if (sel)
				fill(row + k, 2, 1, YELLOW);
		}
		put(row, 5, TEXT, b, "%s", c->name);
		if (c->last_used)
			put(row, 5 + (int)strlen(c->name) + 2, MINT, b, "ostatnio uzywane");
		put(row + 1, 5, MUTED, b, "%s", c->addr);
		int lvl = signal_level(c->rssi);
		put(row, 60, MUTED, b, "%4d dBm", c->rssi);
		for (int k = 0; k < 4; k++)
			fill(row, 70 + 2 * k, 1, k < lvl ? YELLOW : (sel ? BASE : PANEL));
		blank(row + 2);
	}
	if (s->ncars == 0) {
		blank(row);
		blank(row + 1);
		if (!s->scanning) {
			put(row, 3, TEXT, BASE, "Nie widze zadnego auta.");
			put(row + 1, 3, MUTED, BASE, "Wlacz modul w aucie i nacisnij Y, zeby poszukac jeszcze raz.");
		}
		row += 2;
	}
	for (; row < 33; row++)
		blank(row);

	message(33, s->note_tone, s->note);
	blank(34);
	blank(35);
	if (s->standby && s->standby[0])
		put(35, 3, MUTED, BASE, "%s", s->standby);
	blank(36);
	blank(37);
	blank(38);
	int c = 3;
	c = key(38, c, "A");
	c = put(38, c, TEXT, BASE, "jedz    ");
	c = key(38, c, "X");
	c = put(38, c, TEXT, BASE, "tryb kot    ");
	c = key(38, c, "Y");
	c = put(38, c, TEXT, BASE, "szukaj    ");
	c = key(38, c, "-");
	c = put(38, c, TEXT, BASE, "czuwanie    ");
	c = key(38, c, "+");
	put(38, c, TEXT, BASE, "wyjdz");
	for (int r = 39; r <= 41; r++)
		blank(r);
	blank(42);
	if (s->ha && s->ha[0])
		put(42, 3, MUTED, BASE, "%s", s->ha);
	blank(43);
	blank(44);
	put(44, 3, MUTED, BASE, "Telefon nie moze byc w tym czasie polaczony z autem.");
	put(44, UI_COLS - 1 - (int)strlen(s->version), MUTED, BASE, "%s", s->version);
	blank(45);
}

// ---------------------------------------------------------------------------

static void gauge(const UiDrive *s) {
	int fwd = s->throttle > 0 ? (s->speed * GAUGE_HALF + 50) / 100 : 0;
	int rev = s->throttle < 0 ? (s->speed * GAUGE_HALF + 50) / 100 : 0;
	if (s->throttle > 0 && fwd == 0) // tiny but non-zero: show 1 cell
		fwd = 1;
	if (s->throttle < 0 && rev == 0)
		rev = 1;
	const int center = GAUGE_L + GAUGE_HALF; // first forward column (41)
	blank(4);
	put(4, GAUGE_L, MUTED, BASE, "wstecz");
	put(4, center - 2, MUTED, BASE, "gaz");
	put(4, GAUGE_L + 2 * GAUGE_HALF - 5, MUTED, BASE, "przod");
	for (int r = 5; r <= 9; r++) {
		blank(r);
		fill(r, GAUGE_L, 2 * GAUGE_HALF, PANEL);
		if (rev)
			fill(r, center - rev, rev, RED);
		if (fwd)
			fill(r, center, fwd, YELLOW);
		if (!fwd && !rev && (r == 5 || r == 9)) // zero notch, top and bottom
			fill(r, center - 1, 2, MUTED);
	}
	// Scale: 100 50 0 50 100
	blank(10);
	put(10, GAUGE_L, MUTED, BASE, "100");
	put(10, center - GAUGE_HALF / 2 - 1, MUTED, BASE, "50");
	put(10, center - 1, MUTED, BASE, "0");
	put(10, center + GAUGE_HALF / 2 - 1, MUTED, BASE, "50");
	put(10, GAUGE_L + 2 * GAUGE_HALF - 3, MUTED, BASE, "100");
}

static void steering(int row, int steer) {
	static const char *labels[3] = {" <<< ", "  |  ", " >>> "};
	put(row, 50, MUTED, BASE, "skret");
	for (int i = 0; i < 3; i++) {
		bool on = steer == i - 1;
		bool turn = on && i != 1; // only an actual turn is highlighted
		put(row, 57 + 7 * i, turn ? INK : on ? TEXT : MUTED, turn ? YELLOW : PANEL, "%s", labels[i]);
	}
}

void ui_draw_drive(const UiDrive *s) {
	char right[48] = "";
	if (s->battery >= 0)
		snprintf(right, sizeof(right), "bateria %d%% ", s->battery);
	else
		snprintf(right, sizeof(right), "bateria ? ");
	char left[64];
	snprintf(left, sizeof(left), "%s   %s", s->state, s->name);
	Rgb band_color = tone_color(s->state_tone);
	band(band_color, s->state_tone != UI_TONE_NEUTRAL, left, right);
	if (s->battery_low)
		put(1, UI_COLS - (int)strlen(right), TEXT, RED, "%s", right);
	blank(2);
	blank(3);
	if (!s->driving && s->state_msg && s->state_msg[0])
		put(2, 3, MUTED, BASE, "%s", s->state_msg);

	gauge(s);

	// Readout + steering.
	blank(11);
	blank(12);
	if (s->throttle)
		put(12, GAUGE_L, s->throttle > 0 ? YELLOW : RED, BASE, "%3d%%  %s", s->speed,
		    s->throttle > 0 ? "do przodu" : "do tylu");
	else
		put(12, GAUGE_L, MUTED, BASE, "  0%%  stoi");
	steering(12, s->steer);
	blank(13);

	blank(14);
	int c = put(14, GAUGE_L, MUTED, BASE, "bieg ");
	c = put(14, c, TEXT, BASE, "%d/4", s->gear);
	c = put(14, c, MUTED, BASE, " (do %d%%)      prog ruszania ", s->gear_cap);
	c = put(14, c, TEXT, BASE, "%d%%", s->min_speed);
	c = put(14, c, MUTED, BASE, "      swiatla ");
	put(14, c, s->lights ? YELLOW : TEXT, BASE, "%s", s->lights ? "wlaczone" : "wylaczone");
	blank(15);

	// What the car is doing and why.
	char msg[96] = "";
	UiTone tone = UI_TONE_NEUTRAL;
	if (s->paused) {
		snprintf(msg, sizeof(msg), "Aplikacja w tle, auto stoi.");
		tone = UI_TONE_BAD;
	} else if (s->cat_active) {
		snprintf(msg, sizeof(msg), "Tryb kot: auto jezdzi samo jeszcze %u s. Ruch pada przerywa.",
		         s->cat_left_s);
		tone = UI_TONE_BUSY;
	} else if (s->cat_pending) {
		snprintf(msg, sizeof(msg), "Tryb kot ruszy zaraz po polaczeniu.");
		tone = UI_TONE_BUSY;
	} else if (s->driving && !s->pad_ok) {
		snprintf(msg, sizeof(msg), "Nie widze pada, auto stoi. Tryb kot dziala bez pada.");
		tone = UI_TONE_BAD;
	} else if (s->driving && s->holding) {
		snprintf(msg, sizeof(msg), "Pusc gaz i skret, zeby ruszyc.");
		tone = UI_TONE_NEUTRAL;
	} else if (s->battery_low) {
		snprintf(msg, sizeof(msg), "Slaba bateria: naladuj modul, polaczenie moze sie zrywac.");
		tone = UI_TONE_BAD;
	}
	message(16, tone, msg);
	for (int r = 17; r <= 20; r++)
		blank(r);

	// Controls, two columns of key caps.
	put(20, GAUGE_L, TEXT, BASE, "Sterowanie");
	struct {
		const char *k1, *k2, *what;
	} left_col[] = {{"galka L", NULL, "gaz i wsteczny, plynnie"},
	                {"D-pad", NULL, "skret (albo galki w bok)"},
	                {"B", NULL, "boost 100%"},
	                {"X", NULL, "tryb kot"},
	                {"-", "gora/dol", "prog ruszania"}},
	  right_col[] = {{"ZR", "ZL", "pelny gaz / wsteczny"},
	                 {"L", "R", "bieg nizej / wyzej"},
	                 {"A", NULL, "swiatla"},
	                 {"Y", NULL, "czuwanie"},
	                 {"+", NULL, "rozlacz"}};
	for (int i = 0; i < 5; i++) {
		int r = 22 + 2 * i;
		blank(r);
		blank(r + 1);
		int col = key(r, GAUGE_L, left_col[i].k1);
		if (left_col[i].k2)
			col = key(r, col, left_col[i].k2);
		put(r, 20, TEXT, BASE, "%s", left_col[i].what);
		col = key(r, 46, right_col[i].k1);
		if (right_col[i].k2)
			col = key(r, col, right_col[i].k2);
		put(r, 56, TEXT, BASE, "%s", right_col[i].what);
	}
	put(32, 20, MUTED, BASE, "tryb kot: %d s, %d-%d%%", s->cat_seconds, s->cat_lo, s->cat_hi);
	for (int r = 33; r <= 39; r++)
		blank(r);

	blank(40);
	if (s->driving || s->writes)
		put(40, GAUGE_L, MUTED, BASE,
		    "ramki %u  bledy %u  co %.1f ms (p95 %.1f)  polaczono w %.0f ms  zerwania %u", s->writes,
		    s->errors, s->period_p50, s->period_p95, s->connect_ms, s->reconnects);
	blank(41);
	blank(42);
	if (s->ha && s->ha[0])
		put(42, GAUGE_L, MUTED, BASE, "%s", s->ha);
	blank(43);
	blank(44);
	put(44, GAUGE_L, MUTED, BASE, "%s", s->addr);
	put(44, UI_COLS - 1 - (int)strlen(s->version), MUTED, BASE, "%s", s->version);
	blank(45);
}

void ui_draw_error(const char *version, const char *message_text) {
	band(RED, true, "Shell RC", "");
	for (int r = 2; r <= UI_ROWS; r++)
		blank(r);
	put(4, 3, TEXT, BASE, "%s", message_text);
	int c = key(7, 3, "+");
	put(7, c, TEXT, BASE, "wyjdz");
	put(44, UI_COLS - 1 - (int)strlen(version), MUTED, BASE, "%s", version);
}
