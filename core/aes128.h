// Minimal AES-128 single-block (ECB) encrypt/decrypt. Portable C99, no deps.
// Same algorithm as protocol/aes128.ts; verified by tests-c against FIPS-197.
#pragma once
#include <stdint.h>

typedef struct {
	uint8_t w[176]; // 11 round keys
} Aes128Key;

void aes128_expand_key(Aes128Key *k, const uint8_t key[16]);
void aes128_encrypt_block(const Aes128Key *k, const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt_block(const Aes128Key *k, const uint8_t in[16], uint8_t out[16]);
