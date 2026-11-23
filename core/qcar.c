#include "qcar.h"

#include <string.h>

const uint8_t QCAR_KEY[16] = {0x34, 0x52, 0x2a, 0x5b, 0x7a, 0x6e, 0x49, 0x2c,
                              0x08, 0x09, 0x0a, 0x9d, 0x8d, 0x2a, 0x23, 0xf8};

void qcar_init(QcarCodec *c) {
	aes128_expand_key(&c->key, QCAR_KEY);
}

void qcar_plaintext(const DriveCmd *cmd, uint8_t p[16]) {
	memset(p, 0, 16);
	p[1] = 'C';
	p[2] = 'T';
	p[3] = 'L';
	p[4] = cmd->throttle > 0;
	p[5] = cmd->throttle < 0;
	p[6] = cmd->steer < 0;
	p[7] = cmd->steer > 0;
	p[8] = cmd->lights ? 0x00 : 0x01;
	uint8_t speed = cmd->speed > QCAR_SPEED_MAX ? QCAR_SPEED_MAX : cmd->speed;
	p[9] = cmd->throttle != 0 ? speed : QCAR_SPEED_NORMAL;
}

void qcar_encode(const QcarCodec *c, const DriveCmd *cmd, uint8_t out[16]) {
	uint8_t p[16];
	qcar_plaintext(cmd, p);
	aes128_encrypt_block(&c->key, p, out);
}

int qcar_decode_battery(const QcarCodec *c, const uint8_t in[16], int *counter) {
	uint8_t p[16];
	aes128_decrypt_block(&c->key, in, p);
	if (p[1] != 'V' || p[2] != 'B' || p[3] != 'T' || p[4] > 100)
		return -1;
	if (counter)
		*counter = p[0];
	return p[4];
}
