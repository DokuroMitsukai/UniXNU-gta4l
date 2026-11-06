// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Portable gta4l read-only block request helper.
//
// No IOKit, no libc, no direct transport calls. The caller owns the one-block
// read callback and the copy-out callback.

#include "Gta4lEmmcBlockCore.h"

static void gta4l_emmc_block_clear_counters(
    struct gta4l_emmc_block_counters *counters)
{
	counters->requests = 0;
	counters->blocks = 0;
	counters->failures = 0;
	counters->max_request_blocks = 0;
	counters->last_error = 0;
	counters->first_fail_block = 0;
	counters->first_fail_error = 0;
	counters->first_fail_valid = 0;
	counters->requested_blocks = 0;
	counters->cmd17_reads = 0;
	counters->cmd18_chunks = 0;
	counters->cmd18_blocks = 0;
	counters->chunk_failures = 0;
	counters->stop_attempts = 0;
	counters->stop_failures = 0;
	counters->last_stop_result = 0;
}

static int
gta4l_emmc_block_record_failure(struct gta4l_emmc_block *block,
    uint64_t fail_block, int32_t error, int result)
{
	block->online = 0;
	block->counters.failures++;
	block->counters.last_error = result;
	if (block->counters.first_fail_valid == 0) {
		block->counters.first_fail_valid = 1;
		block->counters.first_fail_block = fail_block;
		block->counters.first_fail_error = error;
	}
	return result;
}

void gta4l_emmc_block_init(struct gta4l_emmc_block *block,
    const struct gta4l_emmc_block_io *io, uint32_t capacity_blocks)
{
	uint32_t index;

	if (block == 0) {
		return;
	}
	block->io = io;
	block->capacity_blocks = capacity_blocks;
	block->online = 1;
	block->write_protected = 1;
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		block->bounce[index] = 0;
	}
	gta4l_emmc_block_clear_counters(&block->counters);
}

void gta4l_emmc_block_set_online(struct gta4l_emmc_block *block,
    uint8_t online)
{
	if (block == 0) {
		return;
	}
	block->online = (online != 0) ? 1 : 0;
}

int gta4l_emmc_block_read(struct gta4l_emmc_block *block,
    uint64_t block_number, uint32_t nblks, uint64_t buffer_bytes,
    enum gta4l_emmc_block_direction direction, uint64_t *completed_bytes)
{
	uint32_t index;

	if (completed_bytes != 0) {
		*completed_bytes = 0;
	}
	if (block == 0) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (block->io == 0) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (block->io->read_block == 0) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (block->io->copy_out == 0) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (direction != GTA4L_EMMC_BLOCK_READ) {
		if (direction == GTA4L_EMMC_BLOCK_WRITE) {
			return GTA4L_EMMC_BLOCK_NOT_WRITABLE;
		}
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (nblks == 0) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (block_number >= (uint64_t)block->capacity_blocks) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if ((uint64_t)nblks >
	    (uint64_t)block->capacity_blocks - block_number) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (nblks > GTA4L_EMMC_BLOCK_MAX_BLOCKS) {
		return GTA4L_EMMC_BLOCK_TOO_LARGE;
	}
	if ((uint64_t)nblks * GTA4L_EMMC_BLOCK_BYTES > buffer_bytes) {
		return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
	}
	if (block->online == 0) {
		return GTA4L_EMMC_BLOCK_NO_MEDIA;
	}
	if ((nblks > 1u) && (block->io->read_multi != 0)) {
		if ((block->io->chunk_buffer == 0) ||
		    (block->io->chunk_buffer_bytes <
		    GTA4L_EMMC_BLOCK_CHUNK_BYTES)) {
			return GTA4L_EMMC_BLOCK_BAD_ARGUMENT;
		}
	}

	block->counters.requests++;
	block->counters.requested_blocks += nblks;
	if (nblks > block->counters.max_request_blocks) {
		block->counters.max_request_blocks = nblks;
	}
	if ((nblks > 1u) && (block->io->read_multi != 0)) {
		uint32_t chunk_index = 0;

		while (chunk_index < nblks) {
			uint32_t chunk_blocks;
			uint32_t copied;
			uint32_t counted;
			struct gta4l_emmc_block_multi_result multi;
			int result;

			chunk_blocks = nblks - chunk_index;
			if (chunk_blocks > GTA4L_EMMC_BLOCK_CHUNK_BLOCKS) {
				chunk_blocks = GTA4L_EMMC_BLOCK_CHUNK_BLOCKS;
			}
			if (chunk_blocks == 1u) {
				result = block->io->read_block(block->io->context,
				    block_number + chunk_index, block->bounce);
				if (result != 0) {
					return gta4l_emmc_block_record_failure(
					    block, block_number + chunk_index,
					    result, GTA4L_EMMC_BLOCK_IO_ERROR);
				}
				block->counters.cmd17_reads++;
				copied = block->io->copy_out(block->io->context,
				    chunk_index * GTA4L_EMMC_BLOCK_BYTES,
				    block->bounce, GTA4L_EMMC_BLOCK_BYTES);
				counted = (copied > GTA4L_EMMC_BLOCK_BYTES) ?
				    GTA4L_EMMC_BLOCK_BYTES : copied;
				if (completed_bytes != 0) {
					*completed_bytes += counted;
				}
				if (copied != GTA4L_EMMC_BLOCK_BYTES) {
					return gta4l_emmc_block_record_failure(
					    block, block_number + chunk_index,
					    (int32_t)copied,
					    GTA4L_EMMC_BLOCK_SHORT_COPY);
				}
				block->counters.blocks++;
				chunk_index++;
				continue;
			}

			multi.transport_result = 0;
			multi.requested_blocks = 0;
			multi.completed_blocks = 0;
			multi.stop_attempted = 0;
			multi.stop_result = 0;
			result = block->io->read_multi(block->io->context,
			    block_number + chunk_index, chunk_blocks,
			    block->io->chunk_buffer, &multi);
			block->counters.cmd18_chunks++;
			/* Account for the mandatory stop before any return
			 * branch. A callback that reports the collapsed
			 * transport status still attempted the stop, and a
			 * stop failure must be visible independently of the
			 * primary data/transport error. */
			if (multi.stop_attempted != 0) {
				block->counters.stop_attempts++;
				if (multi.stop_result != 0) {
					block->counters.stop_failures++;
				}
			}
			block->counters.last_stop_result = multi.stop_result;
			if (result != 0) {
				block->counters.chunk_failures++;
				return gta4l_emmc_block_record_failure(block,
				    block_number + chunk_index, result,
				    GTA4L_EMMC_BLOCK_IO_ERROR);
			}
			if ((multi.transport_result != 0) ||
			    (multi.requested_blocks != chunk_blocks) ||
			    (multi.completed_blocks != chunk_blocks) ||
			    (multi.stop_attempted == 0) ||
			    (multi.stop_result != 0)) {
				block->counters.chunk_failures++;
				return gta4l_emmc_block_record_failure(block,
				    block_number + chunk_index,
				    (multi.transport_result != 0) ?
				    multi.transport_result :
				    ((multi.stop_result != 0) ?
				    multi.stop_result : -1),
				    GTA4L_EMMC_BLOCK_IO_ERROR);
			}
			copied = block->io->copy_out(block->io->context,
			    chunk_index * GTA4L_EMMC_BLOCK_BYTES,
			    block->io->chunk_buffer,
			    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES);
			counted = (copied >
			    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES) ?
			    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES : copied;
			if (completed_bytes != 0) {
				*completed_bytes += counted;
			}
			if (copied !=
			    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES) {
				return gta4l_emmc_block_record_failure(block,
				    block_number + chunk_index,
				    (int32_t)copied,
				    GTA4L_EMMC_BLOCK_SHORT_COPY);
			}
			block->counters.blocks += chunk_blocks;
			block->counters.cmd18_blocks += chunk_blocks;
			chunk_index += chunk_blocks;
		}
		return GTA4L_EMMC_BLOCK_OK;
	}
	for (index = 0; index < nblks; index++) {
		uint32_t copied;
		uint32_t counted;
		int result;

		result = block->io->read_block(block->io->context,
		    block_number + index, block->bounce);
		if (result != 0) {
			block->online = 0;
			block->counters.failures++;
			block->counters.last_error = GTA4L_EMMC_BLOCK_IO_ERROR;
			if (block->counters.first_fail_valid == 0) {
				block->counters.first_fail_valid = 1;
				block->counters.first_fail_block =
				    block_number + index;
				block->counters.first_fail_error = result;
			}
			return GTA4L_EMMC_BLOCK_IO_ERROR;
		}
		copied = block->io->copy_out(block->io->context,
		    index * GTA4L_EMMC_BLOCK_BYTES, block->bounce,
		    GTA4L_EMMC_BLOCK_BYTES);
		/* A callback that claims more than the block length cannot have
		 * copied more than one block through this interface. Count at
		 * most one block so completed_bytes never overstates the data
		 * actually transferred. */
		counted = (copied > GTA4L_EMMC_BLOCK_BYTES) ?
		    GTA4L_EMMC_BLOCK_BYTES : copied;
		if (completed_bytes != 0) {
			*completed_bytes += counted;
		}
		if (copied != GTA4L_EMMC_BLOCK_BYTES) {
			block->online = 0;
			block->counters.failures++;
			block->counters.last_error = GTA4L_EMMC_BLOCK_SHORT_COPY;
			if (block->counters.first_fail_valid == 0) {
				block->counters.first_fail_valid = 1;
				block->counters.first_fail_block =
				    block_number + index;
				block->counters.first_fail_error = (int32_t)copied;
			}
			return GTA4L_EMMC_BLOCK_SHORT_COPY;
		}
		block->counters.blocks++;
		block->counters.cmd17_reads++;
	}

	return GTA4L_EMMC_BLOCK_OK;
}
