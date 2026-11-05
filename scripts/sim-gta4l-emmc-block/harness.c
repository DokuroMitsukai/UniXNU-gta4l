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
	uint32_t copies;
	int fail_read_at;
	int fail_copy_at;
	uint64_t last_block;
};

static struct gta4l_emmc_block_io g_io;

static void reset_mock(struct mock *mock)
{
	memset(mock, 0, sizeof(*mock));
	mock->fail_read_at = -1;
	mock->fail_copy_at = -1;
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
	mock->reads++;
	return 0;
}

static uint32_t mock_copy(void *context, uint32_t offset, const void *source,
    uint32_t length)
{
	struct mock *mock = context;

	if ((mock->fail_copy_at >= 0) &&
	    (mock->copies == (uint32_t)mock->fail_copy_at)) {
		return 100;
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
	g_io.copy_out = mock_copy;
	gta4l_emmc_block_init(block, &g_io, capacity);
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

static int run(const char *name, int (*scenario)(void))
{
	if (scenario() == 0) {
		printf("PASS %s\n", name);
		return 0;
	}
	printf("FAIL %s\n", name);
	return 1;
}

int main(void)
{
	int failures = 0;

	failures += run("single", scenario_single);
	failures += run("max", scenario_max);
	failures += run("last", scenario_last);
	failures += run("write", scenario_write);
	failures += run("zero", scenario_zero);
	failures += run("oversized", scenario_oversized);
	failures += run("out-of-range", scenario_out_of_range);
	failures += run("overflow", scenario_overflow);
	failures += run("short-buffer", scenario_short_buffer);
	failures += run("invalid-direction", scenario_invalid_direction);
	failures += run("read-fail-first", scenario_read_fail_first);
	failures += run("read-fail-later", scenario_read_fail_later);
	failures += run("copy-fail", scenario_copy_fail);
	failures += run("offline-init", scenario_offline_init);
	failures += run("bad-io", scenario_bad_io);
	failures += run("null-completed", scenario_null_completed);
	failures += run("first-failure-preserved", scenario_first_failure_preserved);
	failures += run("max-counter-preserved", scenario_max_counter_preserved);
	if (failures != 0) {
		printf("FAIL %d scenarios\n", failures);
		return 1;
	}
	printf("PASS gta4l eMMC block core\n");
	return 0;
}
