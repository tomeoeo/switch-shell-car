#include "input.h"

#include <math.h>

const uint8_t INPUT_GEAR_CAP[INPUT_GEARS] = {40, 60, 80, 100};

void input_init(InputMapper *m, uint8_t min_speed, int gear) {
	m->min_speed = min_speed < INPUT_MIN_SPEED_LO   ? INPUT_MIN_SPEED_LO
	               : min_speed > INPUT_MIN_SPEED_HI ? INPUT_MIN_SPEED_HI
	                                                : min_speed;
	m->gear = gear < 0 ? 0 : gear >= INPUT_GEARS ? INPUT_GEARS - 1 : gear;
	m->throttle_on = 0.15f;
	m->throttle_off = 0.10f;
	m->steer_on = 0.5f;
	m->steer_off = 0.3f;
	m->stick_dir = 0;
	m->stick_steer = 0;
	m->lights = false;
	m->prev_buttons = 0;
	m->settings_changed = false;
}

void input_reset(InputMapper *m, uint32_t held_buttons) {
	m->stick_dir = 0;
	m->stick_steer = 0;
	m->prev_buttons = held_buttons;
}

static float finite_or_zero(float v) {
	return isfinite(v) ? v : 0.0f;
}

uint8_t input_speed_cap(const InputMapper *m, const PadInput *pad) {
	uint8_t cap = (pad->buttons & PAD_B) ? QCAR_SPEED_MAX : INPUT_GEAR_CAP[m->gear];
	return cap < m->min_speed ? m->min_speed : cap;
}

bool input_any_drive(const InputMapper *m, const PadInput *pad) {
	const uint32_t drive = PAD_ZR | PAD_ZL | PAD_LEFT | PAD_RIGHT;
	if (pad->buttons & drive)
		return true;
	float ly = fabsf(finite_or_zero(pad->ly));
	float lx = fabsf(finite_or_zero(pad->lx)), rx = fabsf(finite_or_zero(pad->rx));
	return ly >= m->throttle_off || lx >= m->steer_off || rx >= m->steer_off;
}

DriveCmd input_update(InputMapper *m, const PadInput *pad) {
	const uint32_t b = pad->buttons;
	const uint32_t down = b & ~m->prev_buttons; // rising edges
	m->prev_buttons = b;

	// Settings: min speed calibration (- held + Up/Down), gears (L/R).
	if (b & PAD_MINUS) {
		if ((down & PAD_UP) && m->min_speed + 5 <= INPUT_MIN_SPEED_HI) {
			m->min_speed += 5;
			m->settings_changed = true;
		}
		if ((down & PAD_DOWN) && m->min_speed - 5 >= INPUT_MIN_SPEED_LO) {
			m->min_speed -= 5;
			m->settings_changed = true;
		}
	}
	if ((down & PAD_L) && m->gear > 0) {
		m->gear--;
		m->settings_changed = true;
	}
	if ((down & PAD_R) && m->gear < INPUT_GEARS - 1) {
		m->gear++;
		m->settings_changed = true;
	}
	if (down & PAD_A)
		m->lights = !m->lights;

	const uint8_t cap = input_speed_cap(m, pad);
	DriveCmd cmd = drive_neutral(m->lights);

	// Stick throttle with hysteresis around the dead zone.
	const float ly = finite_or_zero(pad->ly);
	const float aly = fabsf(ly);
	if (m->stick_dir == 0) {
		if (aly >= m->throttle_on)
			m->stick_dir = ly > 0 ? 1 : -1;
	} else if (aly < m->throttle_off || (ly > 0) != (m->stick_dir > 0)) {
		m->stick_dir = aly >= m->throttle_on ? (ly > 0 ? 1 : -1) : 0;
	}

	const bool zr = b & PAD_ZR, zl = b & PAD_ZL;
	if (zr || zl) {
		// Digital triggers override the stick; both held = stop.
		cmd.throttle = zr && !zl ? 1 : zl && !zr ? -1 : 0;
		cmd.speed = cap;
	} else if (m->stick_dir != 0) {
		float t = (aly - m->throttle_off) / (1.0f - m->throttle_off);
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		cmd.throttle = m->stick_dir;
		cmd.speed = (uint8_t)(m->min_speed + (cap - m->min_speed) * t + 0.5f);
	}
	if (cmd.throttle == 0)
		cmd.speed = QCAR_SPEED_NORMAL;

	// Steering: D-pad first (both = straight), else the stick deflected more.
	const bool dl = b & PAD_LEFT, dr = b & PAD_RIGHT;
	float lx = finite_or_zero(pad->lx), rx = finite_or_zero(pad->rx);
	float x = fabsf(lx) >= fabsf(rx) ? lx : rx;
	if (m->stick_steer == 0) {
		if (x >= m->steer_on)
			m->stick_steer = 1;
		else if (x <= -m->steer_on)
			m->stick_steer = -1;
	} else if (m->stick_steer == 1) {
		m->stick_steer = x <= -m->steer_on ? -1 : x < m->steer_off ? 0 : 1;
	} else {
		m->stick_steer = x >= m->steer_on ? 1 : x > -m->steer_off ? 0 : -1;
	}
	if (dl || dr)
		cmd.steer = dl && dr ? 0 : dl ? -1 : 1;
	else
		cmd.steer = m->stick_steer;
	return cmd;
}
