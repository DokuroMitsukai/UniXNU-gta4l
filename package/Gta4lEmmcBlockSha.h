/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Bulk SHA-256 provider for the gta4l read-only block driver.
 *
 * The portable core implementation in Gta4lEmmcBlockCore.c is always
 * available and is the host-test path. In the kernel the adapter prefers
 * the libkern SHA-256 API, which dispatches through corecrypto and is
 * expected to be much faster than the portable transform. The kernel
 * SHA256_* entry points dereference g_crypto_funcs unconditionally, so
 * the provider checks that corecrypto has registered before using them
 * and falls back to the portable implementation otherwise. The choice is
 * made once per bulk run and latched in the context, so init/update/final
 * can never disagree.
 */
#ifndef GTA4L_EMMC_BLOCK_SHA_H
#define GTA4L_EMMC_BLOCK_SHA_H

#include <stdint.h>

#include "Gta4lEmmcBlockCore.h"

#if !defined(GTA4L_EMMC_BLOCK_HOST_TEST) && \
    !defined(GTA4L_EMMC_BLOCK_SHA_STUB_TEST)
#include <libkern/crypto/sha2.h>
#include <libkern/crypto/crypto_internal.h>
#endif

struct gta4l_emmc_bulk_sha_ctx {
	uint32_t libkern;
#if defined(GTA4L_EMMC_BLOCK_HOST_TEST)
	struct gta4l_emmc_block_sha256_ctx portable;
#else
	SHA256_CTX libkern_ctx;
	struct gta4l_emmc_block_sha256_ctx portable;
#endif
};

static inline void
gta4l_emmc_bulk_sha_init(struct gta4l_emmc_bulk_sha_ctx *ctx)
{
	if (ctx == 0) {
		return;
	}
#if !defined(GTA4L_EMMC_BLOCK_HOST_TEST)
	if (g_crypto_funcs != 0) {
		ctx->libkern = 1u;
		SHA256_Init(&ctx->libkern_ctx);
		return;
	}
#endif
	ctx->libkern = 0u;
	gta4l_emmc_block_sha256_init(&ctx->portable);
}

static inline void
gta4l_emmc_bulk_sha_update(struct gta4l_emmc_bulk_sha_ctx *ctx,
    const uint8_t *data, uint32_t length)
{
	if (ctx == 0) {
		return;
	}
	if (ctx->libkern != 0u) {
#if !defined(GTA4L_EMMC_BLOCK_HOST_TEST)
		SHA256_Update(&ctx->libkern_ctx, data, length);
		return;
#endif
	}
	gta4l_emmc_block_sha256_update(&ctx->portable, data, length);
}

static inline void
gta4l_emmc_bulk_sha_final(struct gta4l_emmc_bulk_sha_ctx *ctx,
    uint8_t digest[32])
{
	if (ctx == 0) {
		return;
	}
	if (ctx->libkern != 0u) {
#if !defined(GTA4L_EMMC_BLOCK_HOST_TEST)
		SHA256_Final(digest, &ctx->libkern_ctx);
		return;
#endif
	}
	gta4l_emmc_block_sha256_final(&ctx->portable, digest);
}

static inline int
gta4l_emmc_bulk_sha_is_libkern(const struct gta4l_emmc_bulk_sha_ctx *ctx)
{
	return (ctx != 0) && (ctx->libkern != 0u);
}

#endif /* GTA4L_EMMC_BLOCK_SHA_H */
