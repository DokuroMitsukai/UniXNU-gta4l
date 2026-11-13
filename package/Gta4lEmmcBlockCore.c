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

static uint32_t
gta4l_emmc_block_rotr(uint32_t value, uint32_t count)
{
	return (value >> count) | (value << (32u - count));
}

static void
gta4l_emmc_block_sha256_transform(uint32_t state[8],
    const uint8_t block[64])
{
	static const uint32_t round[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
		0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
		0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
		0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
		0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
		0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
		0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
		0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
		0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
	};
	uint32_t schedule[64];
	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t e = state[4];
	uint32_t f = state[5];
	uint32_t g = state[6];
	uint32_t h = state[7];
	uint32_t index;
	uint32_t s0;
	uint32_t s1;
	uint32_t choose;
	uint32_t majority;
	uint32_t temp1;
	uint32_t temp2;

	for (index = 0; index < 16u; index++) {
		schedule[index] =
		    ((uint32_t)block[index * 4u] << 24) |
		    ((uint32_t)block[index * 4u + 1u] << 16) |
		    ((uint32_t)block[index * 4u + 2u] << 8) |
		    (uint32_t)block[index * 4u + 3u];
	}
	for (index = 16u; index < 64u; index++) {
		s0 = gta4l_emmc_block_rotr(schedule[index - 15u], 7u) ^
		    gta4l_emmc_block_rotr(schedule[index - 15u], 18u) ^
		    (schedule[index - 15u] >> 3);
		s1 = gta4l_emmc_block_rotr(schedule[index - 2u], 17u) ^
		    gta4l_emmc_block_rotr(schedule[index - 2u], 19u) ^
		    (schedule[index - 2u] >> 10);
		schedule[index] = schedule[index - 16u] + s0 +
		    schedule[index - 7u] + s1;
	}
	for (index = 0; index < 64u; index++) {
		s1 = gta4l_emmc_block_rotr(e, 6u) ^
		    gta4l_emmc_block_rotr(e, 11u) ^
		    gta4l_emmc_block_rotr(e, 25u);
		choose = (e & f) ^ ((~e) & g);
		temp1 = h + s1 + choose + round[index] + schedule[index];
		s0 = gta4l_emmc_block_rotr(a, 2u) ^
		    gta4l_emmc_block_rotr(a, 13u) ^
		    gta4l_emmc_block_rotr(a, 22u);
		majority = (a & b) ^ (a & c) ^ (b & c);
		temp2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

static void
gta4l_emmc_block_sha256_copy(uint8_t *target, const uint8_t *source,
    uint32_t length)
{
	uint32_t index;

	for (index = 0; index < length; index++) {
		target[index] = source[index];
	}
}

void
gta4l_emmc_block_sha256_init(struct gta4l_emmc_block_sha256_ctx *ctx)
{
	static const uint32_t initial[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
	};
	uint32_t index;

	if (ctx == 0) {
		return;
	}
	for (index = 0; index < 8u; index++) {
		ctx->state[index] = initial[index];
	}
	ctx->total = 0;
	ctx->used = 0;
}

void
gta4l_emmc_block_sha256_update(struct gta4l_emmc_block_sha256_ctx *ctx,
    const uint8_t *data, uint32_t length)
{
	uint32_t need;

	if (ctx == 0) {
		return;
	}
	if ((data == 0) && (length != 0)) {
		return;
	}
	ctx->total += length;
	if (ctx->used != 0) {
		need = 64u - ctx->used;
		if (length < need) {
			gta4l_emmc_block_sha256_copy(ctx->buffer + ctx->used,
			    data, length);
			ctx->used += length;
			return;
		}
		gta4l_emmc_block_sha256_copy(ctx->buffer + ctx->used, data,
		    need);
		gta4l_emmc_block_sha256_transform(ctx->state, ctx->buffer);
		data += need;
		length -= need;
		ctx->used = 0;
	}
	while (length >= 64u) {
		gta4l_emmc_block_sha256_transform(ctx->state, data);
		data += 64u;
		length -= 64u;
	}
	if (length != 0) {
		gta4l_emmc_block_sha256_copy(ctx->buffer, data, length);
		ctx->used = length;
	}
}

void
gta4l_emmc_block_sha256_final(struct gta4l_emmc_block_sha256_ctx *ctx,
    uint8_t digest[32])
{
	uint8_t tail[128];
	uint32_t padded;
	uint32_t index;
	uint64_t bits;

	if ((ctx == 0) || (digest == 0)) {
		return;
	}
	padded = (ctx->used < 56u) ? 64u : 128u;
	for (index = 0; index < padded; index++) {
		tail[index] = 0;
	}
	for (index = 0; index < ctx->used; index++) {
		tail[index] = ctx->buffer[index];
	}
	tail[ctx->used] = 0x80u;
	bits = ctx->total * 8u;
	tail[padded - 8u] = (uint8_t)(bits >> 56);
	tail[padded - 7u] = (uint8_t)(bits >> 48);
	tail[padded - 6u] = (uint8_t)(bits >> 40);
	tail[padded - 5u] = (uint8_t)(bits >> 32);
	tail[padded - 4u] = (uint8_t)(bits >> 24);
	tail[padded - 3u] = (uint8_t)(bits >> 16);
	tail[padded - 2u] = (uint8_t)(bits >> 8);
	tail[padded - 1u] = (uint8_t)bits;
	gta4l_emmc_block_sha256_transform(ctx->state, tail);
	if (padded == 128u) {
		gta4l_emmc_block_sha256_transform(ctx->state, tail + 64u);
	}
	for (index = 0; index < 8u; index++) {
		digest[index * 4u] = (uint8_t)(ctx->state[index] >> 24);
		digest[index * 4u + 1u] = (uint8_t)(ctx->state[index] >> 16);
		digest[index * 4u + 2u] = (uint8_t)(ctx->state[index] >> 8);
		digest[index * 4u + 3u] = (uint8_t)ctx->state[index];
	}
}

void
gta4l_emmc_block_sha256(const uint8_t *data, uint32_t length,
    uint8_t digest[32])
{
	struct gta4l_emmc_block_sha256_ctx ctx;

	if ((data == 0) && (length != 0)) {
		return;
	}
	gta4l_emmc_block_sha256_init(&ctx);
	gta4l_emmc_block_sha256_update(&ctx, data, length);
	gta4l_emmc_block_sha256_final(&ctx, digest);
}
