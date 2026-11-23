#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

void addr_to_str(const BtdrvAddress *a, char out[18]) {
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a->address[0], a->address[1],
	         a->address[2], a->address[3], a->address[4], a->address[5]);
}

bool addr_from_str(const char *s, BtdrvAddress *a) {
	unsigned v[6];
	if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
		return false;
	for (int i = 0; i < 6; i++)
		a->address[i] = (u8)v[i];
	return true;
}

static const char *find_value(const char *json, const char *key) {
	char pat[32];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p)
		return NULL;
	p = strchr(p + strlen(pat), ':');
	if (!p)
		return NULL;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

static void read_int(const char *json, const char *key, int lo, int hi, int *out) {
	const char *v = find_value(json, key);
	if (!v)
		return;
	int x = atoi(v);
	if (x >= lo && x <= hi)
		*out = x;
}

static void read_u8(const char *json, const char *key, int lo, int hi, uint8_t *out) {
	int x = *out;
	read_int(json, key, lo, hi, &x);
	*out = (uint8_t)x;
}

void config_load(Config *c) {
	memset(c, 0, sizeof(*c));
	c->min_speed = 20; // 2026-11-22: 20 % is the practical start threshold, 10 too weak
	c->gear = 2;       // cap 80 = the official "normal" speed
	c->steer_kick_ms = 150;
	c->steer_kick_speed = 60;
	c->steer_floor = 30;
	c->cat_seconds = 120;
	c->cat_lo = 20;
	c->cat_hi = 60;
	c->http_port = 0; // off: Home Assistant users have the shell_rc integration
	c->keep_awake = false;
	c->standby_minutes = 15;
	strcpy(c->lang, "en");

	FILE *f = fopen(CONFIG_PATH, "r");
	if (!f) {
		log_msg("config: brak %s, wartosci domyslne", CONFIG_PATH);
		return;
	}
	char buf[2048];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	strcpy(c->lang, "pl"); // a config without "lang" predates 1.1.0 (Polish only)

	const char *v = find_value(buf, "deviceId");
	if (v && *v == '"')
		c->has_device = addr_from_str(v + 1, &c->device);
	read_u8(buf, "minSpeed", 10, 60, &c->min_speed);
	read_int(buf, "gear", 0, 3, &c->gear);
	read_int(buf, "steerKickMs", 0, 1000, &c->steer_kick_ms);
	read_u8(buf, "steerKickSpeed", 0, 100, &c->steer_kick_speed);
	read_u8(buf, "steerFloor", 0, 100, &c->steer_floor);
	read_int(buf, "catSeconds", 10, 1800, &c->cat_seconds);
	read_u8(buf, "catMin", 10, 100, &c->cat_lo);
	read_u8(buf, "catMax", 10, 100, &c->cat_hi);
	if (c->cat_lo > c->cat_hi)
		c->cat_lo = c->cat_hi;
	read_int(buf, "httpPort", 0, 65535, &c->http_port);
	read_int(buf, "standbyMinutes", 0, 1440, &c->standby_minutes);
	v = find_value(buf, "lang");
	if (v && (!strncmp(v, "\"en\"", 4) || !strncmp(v, "\"pl\"", 4)))
		memcpy(c->lang, v + 1, 2), c->lang[2] = 0;
	v = find_value(buf, "keepAwake");
	if (v)
		c->keep_awake = !strncmp(v, "true", 4);
	v = find_value(buf, "token");
	if (v && *v == '"') {
		size_t k = 0;
		for (v++; *v && *v != '"' && k < sizeof(c->token) - 1; v++)
			c->token[k++] = *v;
		c->token[k] = 0;
	}

	char a[18] = "-";
	if (c->has_device)
		addr_to_str(&c->device, a);
	log_msg("config: deviceId=%s minSpeed=%u gear=%d skret kick=%dms/%u floor=%u kot=%ds %u-%u "
	        "http=%d token=%s keepAwake=%d",
	        a, c->min_speed, c->gear, c->steer_kick_ms, c->steer_kick_speed, c->steer_floor,
	        c->cat_seconds, c->cat_lo, c->cat_hi, c->http_port, c->token[0] ? "tak" : "nie",
	        c->keep_awake);
}

void config_save(const Config *c) {
	FILE *f = fopen(CONFIG_PATH, "w");
	if (!f) {
		log_msg("config: zapis nieudany");
		return;
	}
	char a[18] = "";
	if (c->has_device)
		addr_to_str(&c->device, a);
	fprintf(f, "{\n");
	if (c->has_device)
		fprintf(f, "  \"deviceId\": \"%s\",\n", a);
	fprintf(f, "  \"minSpeed\": %u,\n  \"gear\": %d,\n", c->min_speed, c->gear);
	fprintf(f, "  \"steerKickMs\": %d,\n  \"steerKickSpeed\": %u,\n  \"steerFloor\": %u,\n",
	        c->steer_kick_ms, c->steer_kick_speed, c->steer_floor);
	fprintf(f, "  \"catSeconds\": %d,\n  \"catMin\": %u,\n  \"catMax\": %u,\n", c->cat_seconds,
	        c->cat_lo, c->cat_hi);
	fprintf(f, "  \"httpPort\": %d,\n  \"token\": \"%s\",\n  \"keepAwake\": %s,\n", c->http_port,
	        c->token, c->keep_awake ? "true" : "false");
	fprintf(f, "  \"standbyMinutes\": %d,\n  \"lang\": \"%s\"\n}\n", c->standby_minutes, c->lang);
	fclose(f);
}
