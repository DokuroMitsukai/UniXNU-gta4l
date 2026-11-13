// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Portable gta4l read-only block request helper.
//
// This file is deliberately independent of IOKit and of the SDHCI transport:
// the caller supplies a one-block read callback and a copy-out callback. The
// XNU adapter maps those to gta4l_emmc_read_block() and
// IOMemoryDescriptor::writeBytes(); the host simulator maps them to a byte
// array. The helper owns validation, the per-block loop, the bounce buffer,
// counters, completed-byte accounting and the fail-closed state.

#ifndef GTA4L_EMMC_BLOCK_CORE_H
#define GTA4L_EMMC_BLOCK_CORE_H

#include <stdint.h>
#include "Gta4lEmmc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GTA4L_EMMC_BLOCK_MAX_BLOCKS 64u
#define GTA4L_EMMC_BLOCK_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_MAX_BLOCKS * GTA4L_EMMC_BLOCK_BYTES)
#define GTA4L_EMMC_BLOCK_CHUNK_BLOCKS 64u
#define GTA4L_EMMC_BLOCK_CHUNK_BYTES \
	(GTA4L_EMMC_BLOCK_CHUNK_BLOCKS * GTA4L_EMMC_BLOCK_BYTES)

enum gta4l_emmc_block_result {
	GTA4L_EMMC_BLOCK_OK = 0,
	GTA4L_EMMC_BLOCK_BAD_ARGUMENT = 1,
	GTA4L_EMMC_BLOCK_NOT_WRITABLE = 2,
	GTA4L_EMMC_BLOCK_NO_MEDIA = 3,
	GTA4L_EMMC_BLOCK_TOO_LARGE = 4,
	GTA4L_EMMC_BLOCK_IO_ERROR = 5,
	GTA4L_EMMC_BLOCK_SHORT_COPY = 6
};

enum gta4l_emmc_block_direction {
	GTA4L_EMMC_BLOCK_READ = 0,
	GTA4L_EMMC_BLOCK_WRITE = 1,
	GTA4L_EMMC_BLOCK_INVALID = 2
};

struct gta4l_emmc_block_counters {
	uint64_t requests;
	uint64_t blocks;
	uint64_t failures;
	uint32_t max_request_blocks;
	int32_t last_error;
	uint64_t first_fail_block;
	int32_t first_fail_error;
	uint8_t first_fail_valid;
	uint64_t requested_blocks;
	uint64_t cmd17_reads;
	uint64_t cmd18_chunks;
	uint64_t cmd18_blocks;
	uint64_t chunk_failures;
	uint64_t stop_attempts;
	uint64_t stop_failures;
	int32_t last_stop_result;
};

/* Explicit CMD18 chunk outcome. A chunk is qualified only when the
 * transport result is zero, the requested and completed sector counts are
 * exact, the mandatory CMD12 was attempted and the stop result is zero.
 * `stop_result` is preserved separately from a data-phase failure. */
struct gta4l_emmc_block_multi_result {
	int32_t transport_result;
	uint32_t requested_blocks;
	uint32_t completed_blocks;
	uint32_t stop_attempted;
	int32_t stop_result;
};

struct gta4l_emmc_block_io {
	void *context;
	/* Read one 512-byte block into buffer. Returns zero on success. */
	int (*read_block)(void *context, uint64_t block, void *buffer);
	/* Optional multi-sector CMD18 read into a caller-owned chunk buffer.
	 * Returns zero when the callback executed the request and filled
	 * `result` with a valid outcome, including a failed data phase or
	 * stop. Nonzero means the callback refused the chunk and `result`
	 * is not an execution record. The helper copies no chunk byte to
	 * the request until transport, exact counts and stop all qualify. */
	int (*read_multi)(void *context, uint64_t block, uint32_t count,
	    void *buffer, struct gta4l_emmc_block_multi_result *result);
	/* Caller-owned bounce for one chunk. It must hold at least
	 * GTA4L_EMMC_BLOCK_CHUNK_BYTES when read_multi is supplied. */
	void *chunk_buffer;
	uint32_t chunk_buffer_bytes;
	/* Copy length bytes from source to the request at offset. Returns the
	 * number of bytes copied. A value below length is a short or failed
	 * copy; the helper counts the returned bytes as completed. */
	uint32_t (*copy_out)(void *context, uint32_t offset, const void *source,
	    uint32_t length);
};

struct gta4l_emmc_block {
	const struct gta4l_emmc_block_io *io;
	uint32_t capacity_blocks;
	uint8_t online;
	uint8_t write_protected;
	uint8_t bounce[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_block_counters counters;
};

/* Initialize the helper and clear the counters. The io object must outlive
 * the block object. */
void gta4l_emmc_block_init(struct gta4l_emmc_block *block,
    const struct gta4l_emmc_block_io *io, uint32_t capacity_blocks);

/* Set the media state. A failed request clears online; only an explicit
 * recovery may set it again. */
void gta4l_emmc_block_set_online(struct gta4l_emmc_block *block,
    uint8_t online);

/* Validate and execute one request. Only READ is accepted; WRITE returns
 * NOT_WRITABLE and INVALID returns BAD_ARGUMENT without touching the
 * callbacks. A singleton uses read_block (CMD17). When read_multi is present,
 * a multi-block request is issued as chunks of up to CHUNK_BLOCKS whole
 * blocks; a one-block remainder after a full chunk uses read_block (CMD17
 * tail). With the current 64-block limit and 64-block chunk every accepted
 * multi-block request is exactly one CMD18 chunk. Otherwise the existing
 * one-block loop is used. On
 * return, completed_bytes (when non-null) is the number of bytes copied for
 * this request, including a partial final copy. A callback that reports more
 * than the copied length is counted at most that length. A chunk contributes
 * no bytes unless its transport, exact count and mandatory stop all qualify.
 * On a callback failure the helper clears online, preserves the first failure
 * record and returns IO_ERROR or SHORT_COPY. */
int gta4l_emmc_block_read(struct gta4l_emmc_block *block,
    uint64_t block_number, uint32_t nblks, uint64_t buffer_bytes,
    enum gta4l_emmc_block_direction direction, uint64_t *completed_bytes);

/* One-shot SHA-256 (FIPS 180-4) over a caller-owned buffer. This is the
 * pinned-content oracle for the read-only IOMedia qualification; it performs
 * no allocation and no transport access. */
void gta4l_emmc_block_sha256(const uint8_t *data, uint32_t length,
    uint8_t digest[32]);

#ifdef __cplusplus
}
#endif

#endif
