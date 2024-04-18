// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

 #include "ilitek_crypto.h"

/*
 * The lookup-tables are marked const so they can be placed in read-only storage instead of RAM
 * The numbers below can be computed dynamically trading ROM for RAM -
 * This can be useful in (embedded) bootloader applications, where ROM is often limited.
 */
static const uint8_t sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

static const uint8_t rsbox[256] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d };

/*
 * The round constant word array, contains the values given by
 * x to the power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8)
 */
static const uint8_t rcon[11] = {
	0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

/* array holding the intermediate results during decryption. */
typedef uint8_t state_t[4][4];

struct crypto_aes_ctx {
	uint8_t key_dec[240];
};

static uint8_t xtime(uint8_t x)
{
  return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void inv_shift_rows(state_t *state)
{
	uint8_t tmp;

	/* Rotate first row 1 columns to right */
	tmp = (*state)[3][1];
	(*state)[3][1] = (*state)[2][1];
	(*state)[2][1] = (*state)[1][1];
	(*state)[1][1] = (*state)[0][1];
	(*state)[0][1] = tmp;

	/* Rotate second row 2 columns to right */
	tmp = (*state)[0][2];
	(*state)[0][2] = (*state)[2][2];
	(*state)[2][2] = tmp;
	tmp = (*state)[1][2];
	(*state)[1][2] = (*state)[3][2];
	(*state)[3][2] = tmp;

	/* Rotate third row 3 columns to right */
	tmp = (*state)[0][3];
	(*state)[0][3] = (*state)[1][3];
	(*state)[1][3] = (*state)[2][3];
	(*state)[2][3] = (*state)[3][3];
	(*state)[3][3] = tmp;
}

static void inv_sub_bytes(state_t *state)
{
	uint8_t i, j;

	for (i = 0; i < 4; ++i)
		for (j = 0; j < 4; ++j)
			(*state)[j][i] = rsbox[(*state)[j][i]];
}

/*
 * multiply is used to multiply numbers in the field GF(2^8)
 * Note: The last call to xtime() is unneeded, but often ends up generating a smaller binary
 *       The compiler seems to be able to vectorize the operation better this way.
 *       See https://github.com/kokke/tiny-AES-c/pull/34
 */
static uint8_t multiply(uint8_t x, uint8_t y)
{
	return (((y & 1) * x) ^
	        ((y>>1 & 1) * xtime(x)) ^
	        ((y>>2 & 1) * xtime(xtime(x))) ^
		((y>>3 & 1) * xtime(xtime(xtime(x)))) ^
		((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))));
}

/*
 * inv_mix_cols function mixes the columns of the state matrix.
 */
static void inv_mix_cols(state_t* state)
{
	int i;
	uint8_t a, b, c, d;

	for (i = 0; i < 4; ++i) {
		a = (*state)[i][0];
		b = (*state)[i][1];
		c = (*state)[i][2];
		d = (*state)[i][3];

		(*state)[i][0] = multiply(a, 0x0e) ^ multiply(b, 0x0b) ^
			multiply(c, 0x0d) ^ multiply(d, 0x09);
		(*state)[i][1] = multiply(a, 0x09) ^ multiply(b, 0x0e) ^
			multiply(c, 0x0b) ^ multiply(d, 0x0d);
		(*state)[i][2] = multiply(a, 0x0d) ^ multiply(b, 0x09) ^
			multiply(c, 0x0e) ^ multiply(d, 0x0b);
		(*state)[i][3] = multiply(a, 0x0b) ^ multiply(b, 0x0d) ^
			multiply(c, 0x09) ^ multiply(d, 0x0e);
	}
}

/*
 * This function adds the round key to state.
 * The round key is added to the state by an XOR function.
 */
static void add_round_key(uint8_t round, state_t *state, const uint8_t *key)
{
	uint8_t i, j;

	for (i = 0; i < 4; ++i)
		for (j = 0; j < 4; ++j)
			(*state)[i][j] ^= key[(round * Nb * 4) + (i * Nb) + j];
}

static void aes_decrypt(state_t *state, const uint8_t *key)
{
	uint8_t round;

	/* Add the first round key to the state before starting the rounds. */
	add_round_key(Nr, state, key);

	/*
	 * There will be Nr rounds.
	 * The first Nr-1 rounds are identical.
	 * These Nr rounds are executed in the loop below.
	 * Last one without InvMixColumn()
	 */
	for (round = Nr - 1; ; --round) {
		inv_shift_rows(state);
		inv_sub_bytes(state);
		add_round_key(round, state, key);

		if (!round)
			break;

		inv_mix_cols(state);
	}
}

/*
 * Produces Nb(Nr+1) round keys, used in each round to decrypt the states.
 */
static void aes_expandkey(uint8_t *round_key, const uint8_t *key)
{
	unsigned i, j, k;
	uint8_t tmp_a[4];
	uint8_t tmp_b;

	/* The first round key is the key itself. */
	for (i = 0; i < Nk; ++i) {
		round_key[(i * 4) + 0] = key[(i * 4) + 0];
		round_key[(i * 4) + 1] = key[(i * 4) + 1];
		round_key[(i * 4) + 2] = key[(i * 4) + 2];
		round_key[(i * 4) + 3] = key[(i * 4) + 3];
	}

	/* All other round keys are found from the previous round keys. */
	for (i = Nk; i < Nb * (Nr + 1); ++i) {
		k = (i - 1) * 4;
		tmp_a[0] = round_key[k + 0];
		tmp_a[1] = round_key[k + 1];
		tmp_a[2] = round_key[k + 2];
		tmp_a[3] = round_key[k + 3];

		if (!(i % Nk)) {
			/*
			 * Shifts the 4 bytes in a word to the left once.
			 * [a0,a1,a2,a3] becomes [a1,a2,a3,a0]
			 */
			tmp_b = tmp_a[0];
			tmp_a[0] = tmp_a[1];
			tmp_a[1] = tmp_a[2];
			tmp_a[2] = tmp_a[3];
			tmp_a[3] = tmp_b;

			tmp_a[0] = sbox[tmp_a[0]];
			tmp_a[1] = sbox[tmp_a[1]];
			tmp_a[2] = sbox[tmp_a[2]];
			tmp_a[3] = sbox[tmp_a[3]];

			tmp_a[0] = tmp_a[0] ^ rcon[i/Nk];
		}

		j = i * 4;
		k = (i - Nk) * 4;
		round_key[j + 0] = round_key[k + 0] ^ tmp_a[0];
		round_key[j + 1] = round_key[k + 1] ^ tmp_a[1];
		round_key[j + 2] = round_key[k + 2] ^ tmp_a[2];
		round_key[j + 3] = round_key[k + 3] ^ tmp_a[3];
	}
}

uint8_t crypto_key[AES_KEY_LEN] = { 0x0, 0x1, 0x2, 0x3,
				    0x4, 0x5, 0x6, 0x7,
				    0x8, 0x9, 0xa, 0xb,
				    0xc, 0xd, 0xe, 0xf };

uint8_t crypto_iv[AES_KEY_LEN] = { 0x0, 0x1, 0x2, 0x3,
				   0x4, 0x5, 0x6, 0x7,
				   0x8, 0x9, 0xa, 0xb,
				   0xc, 0xd, 0xe, 0xf };


void ilitek_decrypt(uint8_t *buf, uint32_t len)
{
	uint8_t key[AES_KEY_LEN];
	uint8_t iv[AES_KEY_LEN];
	uint8_t *tmp = buf;
	uint8_t iv_tmp[AES_KEY_LEN];
	struct crypto_aes_ctx ctx;

	uint32_t i, j;

	memcpy(key, crypto_key, AES_KEY_LEN);
	memcpy(iv, crypto_iv, AES_KEY_LEN);
	
	aes_expandkey(ctx.key_dec, key);

	for (i = 0; i < len; i += AES_KEY_LEN, tmp += AES_KEY_LEN) {
		memcpy(iv_tmp, tmp, AES_KEY_LEN);

		aes_decrypt((state_t *)tmp, ctx.key_dec);
		for (j = 0; j < AES_KEY_LEN; j++)
			tmp[j] ^= iv[j];

		memcpy(iv, iv_tmp, AES_KEY_LEN);
	}
}

static void sha256_init(sha256_ctx *ctx)
{
	ctx->datalen = 0;
	ctx->bitlen[0] = 0;
	ctx->bitlen[1] = 0;

	/*
	 * 8 hash value for sha256
	 * square root of prime number 2,3,5,7,11,13,17,19
	 * then take 32 bit numbers after the decimal point
	 */
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
}

static void sha256_transform(sha256_ctx *ctx, uint8_t *data)
{
	static uint32_t key[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

	uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

	for (i = 0, j = 0; i < 16; ++i, j += 4)
		m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);

	for (; i < 64; ++i)
		m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];

	for (i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e, f, g) + key[i] + m[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

static void sha256_update(sha256_ctx *ctx, uint8_t byte)
{
	ctx->data[ctx->datalen] = byte;
	ctx->datalen++;
	if (ctx->datalen == 64) {
		sha256_transform(ctx, ctx->data);
		DBL_INT_ADD(ctx->bitlen[0], ctx->bitlen[1], 512);
		ctx->datalen = 0;
	}
}

static void sha256_final(sha256_ctx *ctx, uint8_t *gu8Hash)
{
	uint32_t i;

	i = ctx->datalen;

	/* Pad whatever data is left in the buffer. */
	if (ctx->datalen < 56) {
		ctx->data[i++] = 0x80;
		while (i < 56)
			ctx->data[i++] = 0x00;
	} else {
		ctx->data[i++] = 0x80;
		while (i < 64)
			ctx->data[i++] = 0x00;
		sha256_transform(ctx, ctx->data);
		memset(ctx->data, 0, 56);
	}

	/* Append to the padding the total message's length in bits and transform. */
	DBL_INT_ADD(ctx->bitlen[0], ctx->bitlen[1], ctx->datalen * 8);
	ctx->data[63] = ctx->bitlen[0];
	ctx->data[62] = ctx->bitlen[0] >> 8;
	ctx->data[61] = ctx->bitlen[0] >> 16;
	ctx->data[60] = ctx->bitlen[0] >> 24;
	ctx->data[59] = ctx->bitlen[1];
	ctx->data[58] = ctx->bitlen[1] >> 8;
	ctx->data[57] = ctx->bitlen[1] >> 16;
	ctx->data[56] = ctx->bitlen[1] >> 24;
	sha256_transform(ctx, ctx->data);

	/*
	 * Since this implementation uses little endian byte ordering and SHA uses big endian,
	 * reverse all the bytes when copying the final state to the output hash.
	 */
	for (i = 0; i < 4; ++i) {
		gu8Hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
		gu8Hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
	}
}

void get_sha256(uint32_t start, uint32_t end, uint8_t *buf, uint32_t buf_size, uint8_t sha256[32])
{
	sha256_ctx ctx;
	uint32_t i;

	sha256_init(&ctx);

	if (end >= buf_size || start > buf_size || end < start) {
		TP_ERR(NULL, "start/end addr: %#x/%#x buf size: %#x OOB\n", start, end, buf_size);
		return;
	}

	for (i = start; i <= end && i < buf_size; i++)
		sha256_update(&ctx, buf[i]);

	sha256_final(&ctx, sha256);

	TP_MSG_ARR(NULL, "sha256:", TYPE_U8, 32, sha256);
}