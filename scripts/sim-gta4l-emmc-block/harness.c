// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host scenarios for the portable gta4l block request helper.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Gta4lEmmcBlockCore.h"

struct mock {
	uint8_t out[GTA4L_EMMC_BLOCK_MAX_BYTES];
	uint32_t reads;
	uint32_t multi_reads;
	uint32_t copies;
	int fail_read_at;
	int fail_multi_at;
	int fail_copy_at;
	int overlong_copy_at;
	int multi_rc;
	int multi_data_result;
	int multi_short_blocks;
	int multi_requested_delta;
	int multi_completed_extra;
	int multi_stop_attempted;
	int multi_stop_result;
	uint64_t last_block;
	uint64_t last_multi_block;
	uint32_t last_multi_count;
	const void *last_multi_buffer;
	const void *last_read_buffer;
};

static struct gta4l_emmc_block_io g_io;
static uint8_t g_chunk[GTA4L_EMMC_BLOCK_CHUNK_BYTES];

static void reset_mock(struct mock *mock)
{
	memset(mock, 0, sizeof(*mock));
	mock->fail_read_at = -1;
	mock->fail_multi_at = -1;
	mock->fail_copy_at = -1;
	mock->overlong_copy_at = -1;
	mock->multi_stop_attempted = 1;
}

static int mock_read(void *context, uint64_t block, void *buffer)
{
	struct mock *mock = context;
	uint32_t index;

	if ((mock->fail_read_at >= 0) &&
	    (mock->reads == (uint32_t)mock->fail_read_at)) {
		return -5;
	}
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		((uint8_t *)buffer)[index] = (uint8_t)(block + index);
	}
	mock->last_block = block;
	mock->last_read_buffer = buffer;
	mock->reads++;
	return 0;
}

static int mock_read_multi(void *context, uint64_t block, uint32_t count,
    void *buffer, struct gta4l_emmc_block_multi_result *result)
{
	struct mock *mock = context;
	uint32_t index;
	uint32_t completed;

	if (result == 0) {
		return -1;
	}
	result->transport_result = 0;
	result->requested_blocks = count;
	result->completed_blocks = 0;
	result->stop_attempted = 0;
	result->stop_result = 0;
	if (buffer == 0) {
		mock->multi_reads++;
		return -1;
	}
	if ((mock->fail_multi_at >= 0) &&
	    (mock->multi_reads == (uint32_t)mock->fail_multi_at)) {
		mock->multi_reads++;
		result->transport_result = -5;
		return -5;
	}
	for (index = 0; index < count; index++) {
		uint32_t byte;

		for (byte = 0; byte < GTA4L_EMMC_BLOCK_BYTES; byte++) {
			((uint8_t *)buffer)[(index *
			    GTA4L_EMMC_BLOCK_BYTES) + byte] =
			    (uint8_t)(block + index + byte);
		}
	}
	completed = count;
	if (mock->multi_short_blocks > 0) {
		if ((uint32_t)mock->multi_short_blocks >= count) {
			completed = 0;
		} else {
			completed = count -
			    (uint32_t)mock->multi_short_blocks;
		}
	}
	mock->last_block = block + count - 1u;
	mock->last_multi_block = block;
	mock->last_multi_count = count;
	mock->last_multi_buffer = buffer;
	mock->multi_reads++;
	result->transport_result = mock->multi_data_result;
	result->requested_blocks = (uint32_t)((int)count +
	    mock->multi_requested_delta);
	result->completed_blocks = (uint32_t)((int)completed +
	    mock->multi_completed_extra);
	result->stop_attempted = (mock->multi_stop_attempted != 0) ? 1u : 0u;
	result->stop_result = mock->multi_stop_result;
	return mock->multi_rc;
}

static uint32_t mock_copy(void *context, uint32_t offset, const void *source,
    uint32_t length)
{
	struct mock *mock = context;

	if ((mock->fail_copy_at >= 0) &&
	    (mock->copies == (uint32_t)mock->fail_copy_at)) {
		return 100;
	}
	if ((mock->overlong_copy_at >= 0) &&
	    (mock->copies == (uint32_t)mock->overlong_copy_at)) {
		return length + 1u;
	}
	if ((uint64_t)offset + length > sizeof(mock->out)) {
		return 0;
	}
	memcpy(mock->out + offset, source, length);
	mock->copies++;
	return length;
}

static void init_block(struct gta4l_emmc_block *block, struct mock *mock,
    uint32_t capacity)
{
	g_io.context = mock;
	g_io.read_block = mock_read;
	g_io.read_multi = 0;
	g_io.chunk_buffer = 0;
	g_io.chunk_buffer_bytes = 0;
	g_io.copy_out = mock_copy;
	gta4l_emmc_block_init(block, &g_io, capacity);
}

static void init_chunk_block(struct gta4l_emmc_block *block, struct mock *mock,
    uint32_t capacity)
{
	init_block(block, mock, capacity);
	g_io.read_multi = mock_read_multi;
	g_io.chunk_buffer = g_chunk;
	g_io.chunk_buffer_bytes = sizeof(g_chunk);
}

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("  check failed: %s\n", #condition); \
			return 1; \
		} \
	} while (0)

static int scenario_single(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;
	uint32_t index;

	reset_mock(&mock);
	init_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 7, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(mock.reads == 1);
	CHECK(mock.copies == 1);
	CHECK(mock.last_block == 7);
	CHECK(block.counters.requests == 1);
	CHECK(block.counters.blocks == 1);
	CHECK(block.counters.failures == 0);
	CHECK(block.counters.max_request_blocks == 1);
	CHECK(block.counters.first_fail_valid == 0);
	CHECK(block.online == 1);
	CHECK(block.write_protected == 1);
	CHECK(block.capacity_blocks == 100);
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		CHECK(mock.out[index] == (uint8_t)(7 + index));
	}
	return 0;
}

static int scenario_max(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 3, GTA4L_EMMC_BLOCK_MAX_BLOCKS,
	    GTA4L_EMMC_BLOCK_MAX_BYTES, GTA4L_EMMC_BLOCK_READ,
	    &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(mock.reads == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(mock.copies == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(block.counters.blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(block.counters.max_request_blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(mock.out[0] == 3);
	CHECK(mock.out[GTA4L_EMMC_BLOCK_MAX_BYTES - 1] ==
	    (uint8_t)(3 + 63 + 511));
	return 0;
}

static int scenario_last(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 9, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(mock.last_block == 9);
	CHECK(block.counters.blocks == 1);
	return 0;
}

static int scenario_write(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_WRITE, &completed) ==
	    GTA4L_EMMC_BLOCK_NOT_WRITABLE);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	CHECK(block.counters.failures == 0);
	CHECK(block.online == 1);
	return 0;
}

static int scenario_zero(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 0, 0,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_oversized(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 0,
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS + 1,
	    GTA4L_EMMC_BLOCK_MAX_BYTES + 512, GTA4L_EMMC_BLOCK_READ,
	    &completed) == GTA4L_EMMC_BLOCK_TOO_LARGE);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_out_of_range(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 11, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_overflow(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 9, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_short_buffer(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 2, 1023,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_invalid_direction(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_INVALID, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	return 0;
}

static int scenario_read_fail_first(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	mock.fail_read_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 0, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 1);
	CHECK(block.counters.blocks == 0);
	CHECK(block.counters.failures == 1);
	CHECK(block.counters.last_error == GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(block.counters.first_fail_valid == 1);
	CHECK(block.counters.first_fail_block == 0);
	CHECK(block.counters.first_fail_error == -5);
	CHECK(block.online == 0);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 1, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_NO_MEDIA);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(block.counters.requests == 1);
	return 0;
}

static int scenario_read_fail_later(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	mock.fail_read_at = 1;
	CHECK(gta4l_emmc_block_read(&block, 0, 3, 1536,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 512);
	CHECK(mock.reads == 1);
	CHECK(mock.copies == 1);
	CHECK(block.counters.blocks == 1);
	CHECK(block.counters.failures == 1);
	CHECK(block.counters.first_fail_block == 1);
	CHECK(block.counters.first_fail_error == -5);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_copy_fail(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	mock.fail_copy_at = 1;
	CHECK(gta4l_emmc_block_read(&block, 0, 3, 1536,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_SHORT_COPY);
	CHECK(completed == 612);
	CHECK(mock.reads == 2);
	CHECK(mock.copies == 1);
	CHECK(block.counters.blocks == 1);
	CHECK(block.counters.failures == 1);
	CHECK(block.counters.last_error == GTA4L_EMMC_BLOCK_SHORT_COPY);
	CHECK(block.counters.first_fail_block == 1);
	CHECK(block.counters.first_fail_error == 100);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_offline_init(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	gta4l_emmc_block_set_online(&block, 0);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_NO_MEDIA);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	gta4l_emmc_block_set_online(&block, 1);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(block.counters.requests == 1);
	CHECK(block.counters.blocks == 1);
	return 0;
}

static int scenario_bad_io(void)
{
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	memset(&block, 0, sizeof(block));
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	gta4l_emmc_block_init(&block, 0, 10);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	return 0;
}

static int scenario_null_completed(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, 0) == GTA4L_EMMC_BLOCK_OK);
	CHECK(mock.reads == 1);
	CHECK(mock.copies == 1);
	CHECK(gta4l_emmc_block_read(&block, 20, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, 0) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	return 0;
}

static int scenario_first_failure_preserved(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	mock.fail_read_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(block.counters.first_fail_block == 0);
	gta4l_emmc_block_set_online(&block, 1);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 5, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(block.counters.first_fail_block == 0);
	CHECK(block.counters.first_fail_error == -5);
	CHECK(block.counters.failures == 2);
	mock.fail_read_at = -1;
	gta4l_emmc_block_set_online(&block, 1);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 5, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(block.counters.requests == 3);
	CHECK(block.counters.blocks == 1);
	CHECK(block.counters.failures == 2);
	CHECK(block.counters.first_fail_valid == 1);
	CHECK(block.counters.first_fail_block == 0);
	CHECK(block.counters.first_fail_error == -5);
	return 0;
}

static int scenario_max_counter_preserved(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 0, GTA4L_EMMC_BLOCK_MAX_BLOCKS,
	    GTA4L_EMMC_BLOCK_MAX_BYTES, GTA4L_EMMC_BLOCK_READ,
	    &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(block.counters.max_request_blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(block.counters.max_request_blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(block.counters.requests == 2);
	CHECK(block.counters.blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS + 1);
	return 0;
}

static int scenario_chunk_single(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 7, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 512);
	CHECK(mock.multi_reads == 0);
	CHECK(mock.reads == 1);
	CHECK(mock.copies == 1);
	CHECK(block.counters.requested_blocks == 1);
	CHECK(block.counters.cmd17_reads == 1);
	CHECK(block.counters.cmd18_chunks == 0);
	CHECK(mock.last_block == 7);
	CHECK(mock.out[0] == 7);
	return 0;
}

static int scenario_chunk_pair(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 3, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 1024);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 1);
	CHECK(mock.last_block == 4);
	CHECK(block.counters.blocks == 2);
	CHECK(block.counters.requested_blocks == 2);
	CHECK(block.counters.cmd18_chunks == 1);
	CHECK(block.counters.cmd18_blocks == 2);
	CHECK(block.counters.cmd17_reads == 0);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(block.counters.stop_failures == 0);
	CHECK(block.counters.last_stop_result == 0);
	CHECK(mock.out[0] == 3);
	CHECK(mock.out[512] == 4);
	CHECK(mock.out[1023] == (uint8_t)(4 + 511));
	return 0;
}

static int scenario_chunk_three(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 2, 3, 1536,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 1536);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 1);
	CHECK(mock.last_block == 4);
	CHECK(mock.last_multi_block == 2);
	CHECK(mock.last_multi_count == 3);
	CHECK(mock.last_multi_buffer == g_chunk);
	CHECK(block.counters.blocks == 3);
	CHECK(block.counters.cmd18_chunks == 1);
	CHECK(block.counters.cmd18_blocks == 3);
	CHECK(block.counters.cmd17_reads == 0);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(mock.out[0] == 2);
	CHECK(mock.out[512] == 3);
	CHECK(mock.out[1024] == 4);
	CHECK(mock.out[1535] == (uint8_t)(4 + 511));
	return 0;
}

static int scenario_chunk_boundary(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 2, 4, 2048,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 2048);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 1);
	CHECK(block.counters.blocks == 4);
	CHECK(block.counters.cmd18_blocks == 4);
	CHECK(block.counters.cmd17_reads == 0);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(mock.out[1024] == 4);
	CHECK(mock.out[2047] == (uint8_t)(5 + 511));
	return 0;
}

static int scenario_chunk_max(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;
	uint32_t index;
	uint32_t byte;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 0, GTA4L_EMMC_BLOCK_MAX_BLOCKS,
	    GTA4L_EMMC_BLOCK_MAX_BYTES, GTA4L_EMMC_BLOCK_READ,
	    &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(mock.multi_reads ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS / GTA4L_EMMC_BLOCK_CHUNK_BLOCKS);
	CHECK(mock.reads == 0);
	CHECK(mock.copies ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS / GTA4L_EMMC_BLOCK_CHUNK_BLOCKS);
	CHECK(block.counters.blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(block.counters.cmd18_blocks == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(block.counters.cmd17_reads == 0);
	CHECK(block.counters.stop_attempts ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS / GTA4L_EMMC_BLOCK_CHUNK_BLOCKS);
	CHECK(block.counters.max_request_blocks ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	for (index = 0; index < GTA4L_EMMC_BLOCK_MAX_BLOCKS; index++) {
		for (byte = 0; byte < GTA4L_EMMC_BLOCK_BYTES; byte++) {
			CHECK(mock.out[(index * GTA4L_EMMC_BLOCK_BYTES) + byte] ==
			    (uint8_t)(index + byte));
		}
	}
	return 0;
}

static int scenario_chunk_21(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 100);
	CHECK(gta4l_emmc_block_read(&block, 2, 21, 10752,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 10752);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 1);
	CHECK(mock.last_block == 22);
	CHECK(block.counters.blocks == 21);
	CHECK(block.counters.cmd18_chunks == 1);
	CHECK(block.counters.cmd18_blocks == 21);
	CHECK(block.counters.cmd17_reads == 0);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(mock.out[0] == 2);
	CHECK(mock.out[10240] == 22);
	CHECK(mock.out[10751] == (uint8_t)(22 + 511));
	return 0;
}

static int scenario_chunk_last(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, 8, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 1024);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.last_block == 9);
	CHECK(mock.out[1023] == (uint8_t)(9 + 511));
	return 0;
}

static int scenario_chunk_fail_first(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.fail_multi_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 2, 4, 2048,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.blocks == 0);
	CHECK(block.counters.cmd18_blocks == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.stop_attempts == 0);
	CHECK(block.counters.first_fail_valid == 1);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == -5);
	CHECK(block.online == 0);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 3, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_NO_MEDIA);
	CHECK(completed == 0);
	CHECK(mock.multi_reads == 1);
	CHECK(block.counters.requests == 1);
	return 0;
}

static int scenario_chunk_stop_fail(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_stop_result = -7;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(block.counters.stop_failures == 1);
	CHECK(block.counters.last_stop_result == -7);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == -7);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_stop_missing(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_stop_attempted = 0;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.stop_attempts == 0);
	CHECK(block.counters.stop_failures == 0);
	CHECK(block.counters.first_fail_error == -1);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_stop_fail_return(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_stop_result = -7;
	mock.multi_rc = -7;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(block.counters.stop_failures == 1);
	CHECK(block.counters.last_stop_result == -7);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == -7);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_data_stop_fail(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_data_result = -9;
	mock.multi_stop_result = -7;
	mock.multi_rc = -9;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.stop_attempts == 1);
	CHECK(block.counters.stop_failures == 1);
	CHECK(block.counters.last_stop_result == -7);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == -9);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_requested_mismatch(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_requested_delta = 1;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.first_fail_error == -1);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_completed_overclaim(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_completed_extra = 1;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.first_fail_error == -1);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_stale_bounce(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;
	uint32_t index;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 20);
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) == GTA4L_EMMC_BLOCK_OK);
	CHECK(completed == 1024);
	memset(mock.out + 1024, 0xaa, 1024);
	mock.multi_stop_result = -7;
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 4, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 1);
	for (index = 1024; index < 2048; index++) {
		CHECK(mock.out[index] == 0xaa);
	}
	CHECK(block.counters.blocks == 2);
	CHECK(block.counters.chunk_failures == 1);
	return 0;
}

static int scenario_chunk_first_failure_preserved(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 20);
	mock.fail_multi_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(block.counters.first_fail_block == 2);
	gta4l_emmc_block_set_online(&block, 1);
	mock.fail_multi_at = 1;
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 8, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(block.counters.failures == 2);
	CHECK(block.counters.first_fail_valid == 1);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == -5);
	return 0;
}

static int scenario_chunk_short_count(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;
	uint32_t index;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	memset(mock.out, 0xaa, sizeof(mock.out));
	mock.multi_short_blocks = 1;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	for (index = 0; index < 1024; index++) {
		CHECK(mock.out[index] == 0xaa);
	}
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.cmd18_blocks == 0);
	CHECK(block.counters.first_fail_error == -1);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_data_result(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.multi_data_result = -9;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_IO_ERROR);
	CHECK(completed == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.chunk_failures == 1);
	CHECK(block.counters.first_fail_error == -9);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_copy_fail(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.fail_copy_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 2, 4, 2048,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_SHORT_COPY);
	CHECK(completed == 100);
	CHECK(mock.multi_reads == 1);
	CHECK(mock.copies == 0);
	CHECK(block.counters.blocks == 0);
	CHECK(block.counters.cmd18_blocks == 0);
	CHECK(block.counters.first_fail_block == 2);
	CHECK(block.counters.first_fail_error == 100);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_overlong_copy(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	mock.overlong_copy_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 2, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_SHORT_COPY);
	CHECK(completed == 1024);
	CHECK(mock.copies == 0);
	CHECK(block.counters.blocks == 0);
	CHECK(block.counters.cmd18_blocks == 0);
	CHECK(block.counters.first_fail_error == 1025);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_chunk_bounce_missing(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	g_io.chunk_buffer = 0;
	CHECK(gta4l_emmc_block_read(&block, 0, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.multi_reads == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	CHECK(block.online == 1);
	return 0;
}

static int scenario_chunk_bounce_small(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	g_io.chunk_buffer_bytes = GTA4L_EMMC_BLOCK_CHUNK_BYTES - 1u;
	CHECK(gta4l_emmc_block_read(&block, 0, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.multi_reads == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	return 0;
}

static int scenario_chunk_offline(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_chunk_block(&block, &mock, 10);
	gta4l_emmc_block_set_online(&block, 0);
	CHECK(gta4l_emmc_block_read(&block, 0, 2, 1024,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_NO_MEDIA);
	CHECK(completed == 0);
	CHECK(mock.multi_reads == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	return 0;
}

static int scenario_copy_overlong(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	mock.overlong_copy_at = 0;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_SHORT_COPY);
	CHECK(completed == 512);
	CHECK(mock.reads == 1);
	CHECK(mock.copies == 0);
	CHECK(block.counters.blocks == 0);
	CHECK(block.counters.first_fail_valid == 1);
	CHECK(block.counters.first_fail_block == 0);
	CHECK(block.counters.first_fail_error == 513);
	CHECK(block.online == 0);
	return 0;
}

static int scenario_null_read_callback(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	struct gta4l_emmc_block_io io;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	io.context = &mock;
	io.read_block = 0;
	io.copy_out = mock_copy;
	gta4l_emmc_block_init(&block, &io, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	return 0;
}

static int scenario_null_copy_callback(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	struct gta4l_emmc_block_io io;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	io.context = &mock;
	io.read_block = mock_read;
	io.copy_out = 0;
	gta4l_emmc_block_init(&block, &io, 10);
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	CHECK(mock.copies == 0);
	CHECK(block.counters.requests == 0);
	return 0;
}

static int scenario_extreme_lba(void)
{
	struct mock mock;
	struct gta4l_emmc_block block;
	uint64_t completed = 0xdead;

	reset_mock(&mock);
	init_block(&block, &mock, 10);
	CHECK(gta4l_emmc_block_read(&block, UINT64_MAX, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, UINT64_MAX - 1u, UINT32_MAX,
	    UINT64_MAX, GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	init_block(&block, &mock, 0);
	completed = 0xdead;
	CHECK(gta4l_emmc_block_read(&block, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	CHECK(mock.reads == 0);
	return 0;
}

static int scenario_null_block(void)
{
	uint64_t completed = 0xdead;

	CHECK(gta4l_emmc_block_read(0, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_READ, &completed) ==
	    GTA4L_EMMC_BLOCK_BAD_ARGUMENT);
	CHECK(completed == 0);
	return 0;
}

static int run(const char *name, int (*scenario)(void))
{
	if (scenario() == 0) {
		printf("PASS %s\n", name);
		return 0;
	}
	printf("FAIL %s\n", name);
	return 1;
}

struct scenario_entry {
	const char *name;
	int (*run)(void);
};

static const struct scenario_entry scenarios[] = {
	{ "single", scenario_single },
	{ "max", scenario_max },
	{ "last", scenario_last },
	{ "write", scenario_write },
	{ "zero", scenario_zero },
	{ "oversized", scenario_oversized },
	{ "out-of-range", scenario_out_of_range },
	{ "overflow", scenario_overflow },
	{ "short-buffer", scenario_short_buffer },
	{ "invalid-direction", scenario_invalid_direction },
	{ "read-fail-first", scenario_read_fail_first },
	{ "read-fail-later", scenario_read_fail_later },
	{ "copy-fail", scenario_copy_fail },
	{ "copy-overlong", scenario_copy_overlong },
	{ "offline-init", scenario_offline_init },
	{ "bad-io", scenario_bad_io },
	{ "null-read", scenario_null_read_callback },
	{ "null-copy", scenario_null_copy_callback },
	{ "null-block", scenario_null_block },
	{ "null-completed", scenario_null_completed },
	{ "extreme-lba", scenario_extreme_lba },
	{ "first-failure-preserved", scenario_first_failure_preserved },
	{ "max-counter-preserved", scenario_max_counter_preserved },
	{ "chunk-single", scenario_chunk_single },
	{ "chunk-pair", scenario_chunk_pair },
	{ "chunk-three", scenario_chunk_three },
	{ "chunk-boundary", scenario_chunk_boundary },
	{ "chunk-max", scenario_chunk_max },
	{ "chunk-21", scenario_chunk_21 },
	{ "chunk-last", scenario_chunk_last },
	{ "chunk-fail-first", scenario_chunk_fail_first },
	{ "chunk-stop-fail", scenario_chunk_stop_fail },
	{ "chunk-stop-missing", scenario_chunk_stop_missing },
	{ "chunk-stop-fail-return", scenario_chunk_stop_fail_return },
	{ "chunk-data-stop-fail", scenario_chunk_data_stop_fail },
	{ "chunk-requested-mismatch", scenario_chunk_requested_mismatch },
	{ "chunk-completed-overclaim", scenario_chunk_completed_overclaim },
	{ "chunk-stale-bounce", scenario_chunk_stale_bounce },
	{ "chunk-first-failure-preserved", scenario_chunk_first_failure_preserved },
	{ "chunk-short-count", scenario_chunk_short_count },
	{ "chunk-data-result", scenario_chunk_data_result },
	{ "chunk-copy-fail", scenario_chunk_copy_fail },
	{ "chunk-overlong-copy", scenario_chunk_overlong_copy },
	{ "chunk-bounce-missing", scenario_chunk_bounce_missing },
	{ "chunk-bounce-small", scenario_chunk_bounce_small },
	{ "chunk-offline", scenario_chunk_offline },
};

int main(int argc, char **argv)
{
	unsigned int index;
	int failures = 0;

	if (argc > 1) {
		for (index = 0;
		    index < sizeof(scenarios) / sizeof(scenarios[0]); index++) {
			if (strcmp(argv[1], scenarios[index].name) == 0) {
				return run(scenarios[index].name,
				    scenarios[index].run) == 0 ? 0 : 1;
			}
		}
		fprintf(stderr, "unknown scenario: %s\n", argv[1]);
		return 2;
	}
	for (index = 0;
	    index < sizeof(scenarios) / sizeof(scenarios[0]); index++) {
		failures += run(scenarios[index].name, scenarios[index].run);
	}
	if (failures != 0) {
		printf("FAIL %d scenarios\n", failures);
		return 1;
	}
	printf("PASS gta4l eMMC block core\n");
	return 0;
}
