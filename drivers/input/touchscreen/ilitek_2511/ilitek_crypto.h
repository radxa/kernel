/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#ifndef __ILITEK_CRYPTO_H__
#define __ILITEK_CRYPTO_H__

#include "ilitek_def.h"

#define Nb	( 4 )
#define Nk	( 4 )       /* The number of 32 bit words in a key. */
#define Nr	( 10 )      /* The number of rounds in AES Cipher. */

#define AES_KEY_LEN      ( 16 ) /* Key length in bytes */

extern uint8_t crypto_key[AES_KEY_LEN];
extern uint8_t crypto_iv[AES_KEY_LEN];

#define DBL_INT_ADD(a, b, c)			\
	do {					\
		if ((a) > 0xffffffff - (c))	\
			++(b);			\
		(a) += (c);			\
	} while (0)

#define ROTLEFT(a,b)	(((a) << (b)) | ((a) >> (32 - (b))))
#define ROTRIGHT(a,b)	(((a) >> (b)) | ((a) << (32 - (b))))

#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint32_t bitlen[2];
	uint32_t state[8];
} sha256_ctx;

#ifdef __cplusplus
extern "C" {
#endif

void __DLL ilitek_decrypt(uint8_t *buf, uint32_t len);

void __DLL get_sha256(uint32_t start, uint32_t end,
		      uint8_t *buf, uint32_t buf_size, uint8_t sha256[32]);

#ifdef __cplusplus
}
#endif

#endif
