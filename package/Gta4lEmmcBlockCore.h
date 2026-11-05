// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Portable gta4l read-only block request helper.
//
// This file is deliberately independent of IOKit and of the SDHCI transport:
// the caller supplies a one-block read callback and a copy-out callback. The
// XNU adapter maps those to gta4l_emmc_read_block() and
// IOMemoryDescriptor::writeBytes(); the host simulator maps them to a byte
// array. The helper owns validation, the per-block loop, the bounce buffer,
// counters and the fail-closed state.

#ifndef GTA4L_EMMC_BLOCK_CORE_H
#define GTA4L_EMMC_BLOCK_CORE_H

#include <stdint.h>
#include "Gta4lEmmc.h"

#define GTA4L_EMMC_BLOCK_MAX_BLOCKS 64u
#define GTA4L_EMMC_BLOCK_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_MAX_BLOCKS * GTA4L_EMMC_BLOCK_BYTES)

enum gta4l_emmc_block_result {
	GTA4L_EMMC_BLOCK_OK = 0,
	GTA4L_EMMC_BLOCK_BAD_ARGUMENT = 1,
	GTA4L_EMMC_BLOCK_NOT_WRITABLE = 2,
	GTA4L_EMMC_BLOCK_NO_MEDIA = 3,
	GTA4L_EMMC_BLOCK_TOO_LARGE = 4,
	GTA4L_EMMC_BLOCK_IO_ERROR = 5,
	GTA4L_EMMC_BLOCK_SHORT_COPY = 6
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
};

struct gta4l_emmc_block_io {
	void *context;
	/* Read one 512-byte block into buffer. Returns zero on success. */
	int (*read_block)(void *context, uint64_t block, void *buffer);
	/* Copy length bytes from source to the request at offset. Returns zero
	 * on success. A non-zero return is a short or failed copy. */
	int (*copy_out)(void *context, uint32_t offset, const void *source,
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

/* Validate and execute one read request. write must be zero; writes are
 * rejected without touching the callbacks. Returns one of the result codes
 * above. On a callback failure the helper clears online, preserves the first
 * failure record and returns IO_ERROR or SHORT_COPY. */
int gta4l_emmc_block_read(struct gta4l_emmc_block *block,
    uint64_t block_number, uint32_t nblks, uint64_t buffer_bytes, int write);

#endif
