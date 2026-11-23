#include "steer.h"

void steer_assist_init(SteerAssist *s, uint16_t kick_ms, uint8_t kick_speed, uint8_t floor) {
	s->kick_ms = kick_ms;
	s->kick_speed = kick_speed > QCAR_SPEED_MAX ? QCAR_SPEED_MAX : kick_speed;
	s->floor = floor > QCAR_SPEED_MAX ? QCAR_SPEED_MAX : floor;
	s->prev_steer = 0;
	s->kick_until_ms = 0;
}

DriveCmd steer_assist_apply(SteerAssist *s, DriveCmd c, uint32_t now_ms) {
	if (c.steer != 0 && c.steer != s->prev_steer)
		s->kick_until_ms = now_ms + s->kick_ms;
	s->prev_steer = c.steer;
	if (c.throttle == 0 || c.steer == 0)
		return c;
	if (c.speed < s->floor)
		c.speed = s->floor;
	if (s->kick_ms && (int32_t)(s->kick_until_ms - now_ms) > 0 && c.speed < s->kick_speed)
		c.speed = s->kick_speed;
	return c;
}
