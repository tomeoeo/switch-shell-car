#include "aes128.h"

#include <string.h>

static uint8_t SBOX[256];
static uint8_t INV_SBOX[256];
static int g_ready;

static uint8_t rotl8(uint8_t x, int s) {
	return (uint8_t)((x << s) | (x >> (8 - s)));
}

// Generate the S-boxes from GF(2^8) inverses + affine transform instead of
// pasting tables (less room for typos). Not thread-safe on first call; the
// app calls aes128_expand_key once at startup.
static void build_sboxes(void) {
	uint8_t p = 1, q = 1;
	do {
		p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
		q ^= (uint8_t)(q << 1);
		q ^= (uint8_t)(q << 2);
		q ^= (uint8_t)(q << 4);
		if (q & 0x80)
			q ^= 0x09;
		SBOX[p] = (uint8_t)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
	} while (p != 1);
	SBOX[0] = 0x63;
	for (int i = 0; i < 256; i++)
		INV_SBOX[SBOX[i]] = (uint8_t)i;
	g_ready = 1;
}

static uint8_t xtime(uint8_t a) {
	return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
}

static uint8_t mul(uint8_t a, uint8_t b) {
	uint8_t r = 0;
	while (b) {
		if (b & 1)
			r ^= a;
		a = xtime(a);
		b >>= 1;
	}
	return r;
}

void aes128_expand_key(Aes128Key *k, const uint8_t key[16]) {
	if (!g_ready)
		build_sboxes();
	uint8_t *w = k->w;
	memcpy(w, key, 16);
	uint8_t rcon = 1;
	for (int i = 16; i < 176; i += 4) {
		uint8_t t0 = w[i - 4], t1 = w[i - 3], t2 = w[i - 2], t3 = w[i - 1];
		if (i % 16 == 0) {
			uint8_t tmp = t0;
			t0 = (uint8_t)(SBOX[t1] ^ rcon);
			t1 = SBOX[t2];
			t2 = SBOX[t3];
			t3 = SBOX[tmp];
			rcon = xtime(rcon);
		}
		w[i] = w[i - 16] ^ t0;
		w[i + 1] = w[i - 15] ^ t1;
		w[i + 2] = w[i - 14] ^ t2;
		w[i + 3] = w[i - 13] ^ t3;
	}
}

static void add_round_key(uint8_t s[16], const uint8_t *w, int round) {
	for (int i = 0; i < 16; i++)
		s[i] ^= w[round * 16 + i];
}

// State layout: s[col*4 + row].
static void shift_rows(uint8_t s[16]) {
	uint8_t t = s[1];
	s[1] = s[5], s[5] = s[9], s[9] = s[13], s[13] = t;
	t = s[2], s[2] = s[10], s[10] = t;
	t = s[6], s[6] = s[14], s[14] = t;
	t = s[15];
	s[15] = s[11], s[11] = s[7], s[7] = s[3], s[3] = t;
}

static void inv_shift_rows(uint8_t s[16]) {
	uint8_t t = s[13];
	s[13] = s[9], s[9] = s[5], s[5] = s[1], s[1] = t;
	t = s[2], s[2] = s[10], s[10] = t;
	t = s[6], s[6] = s[14], s[14] = t;
	t = s[3];
	s[3] = s[7], s[7] = s[11], s[11] = s[15], s[15] = t;
}

static void mix_columns(uint8_t s[16]) {
	for (int c = 0; c < 16; c += 4) {
		uint8_t a0 = s[c], a1 = s[c + 1], a2 = s[c + 2], a3 = s[c + 3];
		uint8_t all = a0 ^ a1 ^ a2 ^ a3;
		s[c] ^= all ^ xtime(a0 ^ a1);
		s[c + 1] ^= all ^ xtime(a1 ^ a2);
		s[c + 2] ^= all ^ xtime(a2 ^ a3);
		s[c + 3] ^= all ^ xtime(a3 ^ a0);
	}
}

static void inv_mix_columns(uint8_t s[16]) {
	for (int c = 0; c < 16; c += 4) {
		uint8_t a0 = s[c], a1 = s[c + 1], a2 = s[c + 2], a3 = s[c + 3];
		s[c] = mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9);
		s[c + 1] = mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13);
		s[c + 2] = mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11);
		s[c + 3] = mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14);
	}
}

void aes128_encrypt_block(const Aes128Key *k, const uint8_t in[16], uint8_t out[16]) {
	uint8_t s[16];
	memcpy(s, in, 16);
	add_round_key(s, k->w, 0);
	for (int round = 1; round < 10; round++) {
		for (int i = 0; i < 16; i++)
			s[i] = SBOX[s[i]];
		shift_rows(s);
		mix_columns(s);
		add_round_key(s, k->w, round);
	}
	for (int i = 0; i < 16; i++)
		s[i] = SBOX[s[i]];
	shift_rows(s);
	add_round_key(s, k->w, 10);
	memcpy(out, s, 16);
}

void aes128_decrypt_block(const Aes128Key *k, const uint8_t in[16], uint8_t out[16]) {
	uint8_t s[16];
	memcpy(s, in, 16);
	add_round_key(s, k->w, 10);
	for (int round = 9; round >= 1; round--) {
		inv_shift_rows(s);
		for (int i = 0; i < 16; i++)
			s[i] = INV_SBOX[s[i]];
		add_round_key(s, k->w, round);
		inv_mix_columns(s);
	}
	inv_shift_rows(s);
	for (int i = 0; i < 16; i++)
		s[i] = INV_SBOX[s[i]];
	add_round_key(s, k->w, 0);
	memcpy(out, s, 16);
}
