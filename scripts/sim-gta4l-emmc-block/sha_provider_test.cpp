// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host-only equivalence test for the bulk SHA-256 provider seam.
//
// The kernel path calls libkern SHA256_* (corecrypto) when g_crypto_funcs is
// registered and falls back to the portable core otherwise. This test
// supplies a stub SHA256_CTX and stub SHA256_* functions implemented with the
// portable core, so it checks the provider selection, the init/update/final
// state latch and the digest against known SHA-256 vectors. It does not
// exercise corecrypto itself; the device run measures that path.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "Gta4lEmmcBlockCore.h"

struct sha_stub_ctx {
	struct gta4l_emmc_block_sha256_ctx portable;
};
typedef struct sha_stub_ctx SHA256_CTX;

extern const void *g_crypto_funcs;

extern "C" {
void SHA256_Init(SHA256_CTX *ctx);
void SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len);
void SHA256_Final(void *digest, SHA256_CTX *ctx);
}

#define GTA4L_EMMC_BLOCK_SHA_STUB_TEST 1
#include "Gta4lEmmcBlockSha.h"

const void *g_crypto_funcs = nullptr;

extern "C" void
SHA256_Init(SHA256_CTX *ctx)
{
	gta4l_emmc_block_sha256_init(&ctx->portable);
}

extern "C" void
SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len)
{
	gta4l_emmc_block_sha256_update(&ctx->portable,
	    (const uint8_t *)data, (uint32_t)len);
}

extern "C" void
SHA256_Final(void *digest, SHA256_CTX *ctx)
{
	gta4l_emmc_block_sha256_final(&ctx->portable, (uint8_t *)digest);
}

static int
hex_digest(const uint8_t digest[32], const char *expected)
{
	char text[65];
	static const char digits[] = "0123456789abcdef";
	uint32_t index;

	for (index = 0; index < 32; index++) {
		text[index * 2u] = digits[digest[index] >> 4];
		text[index * 2u + 1u] = digits[digest[index] & 0x0fu];
	}
	text[64] = '\0';
	if (std::strcmp(text, expected) != 0) {
		std::printf("  digest %s != %s\n", text, expected);
		return 0;
	}
	return 1;
}

static int
run_one(const char *name, const uint8_t *data, size_t length,
    const char *expected, int libkern, size_t chunk)
{
	struct gta4l_emmc_bulk_sha_ctx ctx;
	uint8_t digest[32];
	size_t offset = 0;

	g_crypto_funcs = libkern ? (const void *)1 : nullptr;
	gta4l_emmc_bulk_sha_init(&ctx);
	if (gta4l_emmc_bulk_sha_is_libkern(&ctx) != libkern) {
		std::printf("  %s: provider selection mismatch\n", name);
		return 0;
	}
	while (offset < length) {
		size_t take = length - offset;
		if ((chunk != 0) && (take > chunk)) {
			take = chunk;
		}
		gta4l_emmc_bulk_sha_update(&ctx, data + offset,
		    (uint32_t)take);
		offset += take;
	}
	gta4l_emmc_bulk_sha_final(&ctx, digest);
	if (!hex_digest(digest, expected)) {
		std::printf("  %s: libkern=%d chunk=%zu\n", name, libkern, chunk);
		return 0;
	}
	return 1;
}

static int
run_vector(const char *name, const uint8_t *data, size_t length,
    const char *expected)
{
	static const size_t chunks[] = { 0, 1, 3, 7, 64, 4096 };
	size_t index;
	int ok = 1;

	for (index = 0; index < sizeof(chunks) / sizeof(chunks[0]); index++) {
		if ((chunks[index] == 0) || (chunks[index] <= length)) {
			ok &= run_one(name, data, length, expected, 0,
			    chunks[index]);
			ok &= run_one(name, data, length, expected, 1,
			    chunks[index]);
		}
	}
	return ok;
}

int
main(void)
{
	static const char empty[] = "";
	static const char abc[] = "abc";
	static const char two_block[] =
	    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	std::vector<uint8_t> million(1000000u, 'a');
	int ok = 1;

	ok &= run_vector("empty", (const uint8_t *)empty, 0,
	    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	ok &= run_vector("abc", (const uint8_t *)abc, 3,
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	ok &= run_vector("56-byte", (const uint8_t *)two_block,
	    sizeof(two_block) - 1u,
	    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	ok &= run_vector("1M-a", million.data(), million.size(),
	    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	if (!ok) {
		std::printf("FAIL sha-provider test\n");
		return 1;
	}

	g_crypto_funcs = (const void *)1;
	{
		struct gta4l_emmc_bulk_sha_ctx ctx;
		gta4l_emmc_bulk_sha_init(&ctx);
		if (!gta4l_emmc_bulk_sha_is_libkern(&ctx)) {
			std::printf("FAIL: corecrypto selection did not latch\n");
			return 1;
		}
	}
	g_crypto_funcs = nullptr;
	{
		struct gta4l_emmc_bulk_sha_ctx ctx;
		gta4l_emmc_bulk_sha_init(&ctx);
		if (gta4l_emmc_bulk_sha_is_libkern(&ctx)) {
			std::printf("FAIL: portable fallback did not latch\n");
			return 1;
		}
	}
	std::printf("PASS sha-provider: 4 vectors x 2 providers x chunking\n");
	return 0;
}
