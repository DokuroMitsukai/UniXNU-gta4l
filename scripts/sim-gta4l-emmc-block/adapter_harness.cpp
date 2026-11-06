// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host adapter scenarios for the gta4l read-only block driver.
//
// The adapter source is compiled unchanged against the minimal IOKit seam in
// Gta4lEmmcBlockTestSupport.h. These scenarios are behaviour evidence only;
// the real qualified-header compile is a separate ABI contract check.

#include "Gta4lEmmcBlockTestSupport.h"

#define private public
#include "../../package/Gta4lEmmcBlock.cpp"
#undef private

static Gta4lEmmcBlock *g_self;
static IOService *g_driver;
static bool g_started;
static int g_panicked;

struct completion_record {
	int calls;
	IOReturn status;
	IOByteCount actual;
	int lock_depth;
};

static completion_record g_completion;
static uint8_t g_buffer[GTA4L_EMMC_BLOCK_MAX_BYTES + 1024];

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("  check failed: %s\n", #condition); \
			return 1; \
		} \
	} while (0)

static void
completion_action(void *target, void *parameter, IOReturn status,
    IOByteCount actual)
{
	(void)target;
	(void)parameter;
	g_completion.calls++;
	g_completion.status = status;
	g_completion.actual = actual;
	g_completion.lock_depth = gta4l_test.lock_depth;
}

static int
begin(const char *arguments)
{
	gta4l_test_reset();
	gta4l_test.boot_args = arguments;
	g_self = new Gta4lEmmcBlock();
	g_started = false;
	g_panicked = 0;
	if (setjmp(gta4l_test.panic_jump) == 0) {
		g_started = g_self->start(0);
		return 0;
	}
	g_panicked = 1;
	return 1;
}

static int
invoke_thread(int index)
{
	if ((index < 0) || (index >= gta4l_test.thread_count)) {
		return -1;
	}
	if (setjmp(gta4l_test.panic_jump) == 0) {
		gta4l_test.threads[index].func(
		    gta4l_test.threads[index].context, 0);
		return 0;
	}
	return 1;
}

static IOReturn
issue_read(UInt64 block, UInt64 nblks, int direction, UInt64 length,
    bool null_buffer)
{
	IOMemoryDescriptor *descriptor = 0;
	IOStorageCompletion completion;

	memset(&g_completion, 0, sizeof(g_completion));
	completion.target = 0;
	completion.action = completion_action;
	completion.parameter = 0;
	if (!null_buffer) {
		descriptor = IOMemoryDescriptor::withAddress(g_buffer, length,
		    direction);
	}
	return g_self->doAsyncReadWrite(descriptor, block, nblks, 0,
	    &completion);
}

static IOMedia *
add_media(bool whole, UInt64 base, UInt64 size, bool writable,
    const char *content, bool chain)
{
	IOMedia *media = new IOMedia();

	gta4l_test_media_configure(media, whole, base, size, writable, content);
	if (chain) {
		if (g_driver == 0) {
			g_driver = new IOService();
		}
		gta4l_test_media_chain(media, g_driver, g_self);
	}
	gta4l_test_add_media(media);
	return media;
}

static int
scenario_disabled(void)
{
	begin("");
	CHECK(!g_started);
	CHECK(!g_panicked);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.registered == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_conflict_probe(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-probe=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.registered == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_conflict_other(void)
{
	begin("gta4l-emmc-block=1 gta4l-sdhci-snapshot=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-block=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_publish_deferred(void)
{
	begin("gta4l-emmc-block=2");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_malformed_value(void)
{
	begin("gta4l-emmc-block=1x");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed") != 0);
	return 0;
}

static int
scenario_init_failure(void)
{
	gta4l_test_reset();
	gta4l_test.boot_args = "gta4l-emmc-block=1";
	gta4l_test.init_result = -1;
	g_self = new Gta4lEmmcBlock();
	g_started = false;
	if (setjmp(gta4l_test.panic_jump) == 0) {
		g_started = g_self->start(0);
		CHECK(false);
	}
	CHECK(!g_started);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=11") != 0);
	CHECK(gta4l_test.registered == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(gta4l_test.platform_maps == 1);
	return 0;
}

static int
scenario_lock_failure(void)
{
	gta4l_test_reset();
	gta4l_test.boot_args = "gta4l-emmc-block=1";
	gta4l_test.lock_alloc_fail = 1;
	g_self = new Gta4lEmmcBlock();
	CHECK(!g_self->start(0));
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	return 0;
}

static int
scenario_thread_failure(void)
{
	gta4l_test_reset();
	gta4l_test.boot_args = "gta4l-emmc-block=1";
	gta4l_test.reporter_start_result = -1;
	g_self = new Gta4lEmmcBlock();
	if (setjmp(gta4l_test.panic_jump) == 0) {
		(void)g_self->start(0);
		CHECK(false);
	}
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(gta4l_test.registered == 1);
	CHECK(g_self->property_count_ == 3);
	return 0;
}

static int
scenario_start_ok(void)
{
	bool writeProtected = false;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(!g_panicked);
	CHECK(gta4l_test.registered == 1);
	CHECK(gta4l_test.thread_count == 2);
	CHECK(g_self->capacityBlocks == 61071360u);
	CHECK(g_self->online);
	CHECK(g_self->reportWriteProtection(&writeProtected) ==
	    kIOReturnSuccess);
	CHECK(writeProtected);
	CHECK(g_self->property_count_ == 3);
	CHECK(strcmp(g_self->properties_[0].key,
	    kIOMaximumBlockCountReadKey) == 0);
	CHECK(g_self->properties_[0].value == GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(g_self->properties_[1].value == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(g_self->properties_[2].value == 1);
	return 0;
}

static int
scenario_properties(void)
{
	UInt64 value = 0;
	bool flag = true;
	UInt64 capacities[2];

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(g_self->reportBlockSize(&value) == kIOReturnSuccess);
	CHECK(value == GTA4L_EMMC_BLOCK_BYTES);
	CHECK(g_self->reportMaxValidBlock(&value) == kIOReturnSuccess);
	CHECK(value == g_self->capacityBlocks - 1u);
	CHECK(g_self->reportMediaState(&flag, 0) == kIOReturnSuccess);
	CHECK(flag);
	CHECK(g_self->reportWriteProtection(&flag) == kIOReturnSuccess);
	CHECK(flag);
	CHECK(g_self->reportRemovability(&flag) == kIOReturnSuccess);
	CHECK(!flag);
	CHECK(g_self->reportEjectability(&flag) == kIOReturnSuccess);
	CHECK(!flag);
	CHECK(g_self->doGetFormatCapacities(capacities, 2) == 0);
	CHECK(g_self->getWriteCacheState(&flag) == kIOReturnSuccess);
	CHECK(!flag);
	CHECK(g_self->setWriteCacheState(true) == kIOReturnUnsupported);
	CHECK(g_self->doSynchronizeCache() == kIOReturnSuccess);
	CHECK(g_self->doEjectMedia() == kIOReturnUnsupported);
	CHECK(g_self->doFormatMedia(0) == kIOReturnUnsupported);
	CHECK(strcmp(g_self->getVendorString(), "UniXNU") == 0);
	CHECK(strcmp(g_self->getProductString(), "gta4l eMMC (read-only)") == 0);
	CHECK(strcmp(g_self->getRevisionString(), "1.0") == 0);
	return 0;
}

static int
scenario_write_refused(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	status = issue_read(5, 1, kIODirectionOut, 512, false);
	CHECK(status == kIOReturnNotWritable);
	CHECK(g_completion.calls == 0);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(g_self->online);
	return 0;
}

static int
scenario_invalid_requests(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(issue_read(0, 1, kIODirectionIn, 512, true) ==
	    kIOReturnBadArgument);
	CHECK(g_completion.calls == 0);
	CHECK(issue_read(0, 0, kIODirectionIn, 512, false) ==
	    kIOReturnBadArgument);
	CHECK(issue_read(g_self->capacityBlocks, 1, kIODirectionIn, 512,
	    false) == kIOReturnBadArgument);
	CHECK(issue_read(g_self->capacityBlocks - 1u, 2, kIODirectionIn, 1024,
	    false) == kIOReturnBadArgument);
	CHECK(issue_read(0, GTA4L_EMMC_BLOCK_MAX_BLOCKS + 1u, kIODirectionIn,
	    GTA4L_EMMC_BLOCK_MAX_BYTES + 512, false) ==
	    kIOReturnMessageTooLarge);
	CHECK(issue_read(0, 2, kIODirectionIn, 1023, false) ==
	    kIOReturnBadArgument);
	CHECK(issue_read(0, 1, kIODirectionNone, 512, false) ==
	    kIOReturnBadArgument);
	CHECK(g_completion.calls == 0);
	CHECK(gta4l_test.read_calls == 1);
	return 0;
}

static int
scenario_read_one(void)
{
	IOReturn status;
	uint32_t index;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	status = issue_read(5, 1, kIODirectionIn, 512, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 512);
	CHECK(g_completion.lock_depth == 0);
	CHECK(g_self->requestBuffer == nullptr);
	CHECK(gta4l_test.read_calls == 2);
	CHECK(g_self->block.counters.requests == 1);
	CHECK(g_self->block.counters.blocks == 1);
	CHECK(g_self->block.counters.failures == 0);
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		CHECK(g_buffer[index] == (uint8_t)(5 + index));
	}
	return 0;
}

static int
scenario_read_multi(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	status = issue_read(2, 3, kIODirectionIn, 1536, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 1536);
	CHECK(gta4l_test.read_calls == 4);
	CHECK(gta4l_test.multi_calls == 0);
	CHECK(g_buffer[0] == 2);
	CHECK(g_buffer[512] == 3);
	CHECK(g_buffer[1024] == 4);
	return 0;
}

static int
scenario_chunked_start(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	CHECK(!g_panicked);
	CHECK(g_self->reportMode);
	CHECK(g_self->chunkedMode);
	CHECK(g_self->io.read_multi != 0);
	CHECK(g_self->io.chunk_buffer == g_self->chunk);
	CHECK(g_self->io.chunk_buffer_bytes ==
	    GTA4L_EMMC_BLOCK_CHUNK_BYTES);
	CHECK(gta4l_test.registered == 1);
	CHECK(gta4l_test.thread_count == 2);
	return 0;
}

static int
scenario_chunked_conflict(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-probe=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "REFUSED") != 0);
	return 0;
}

static int
scenario_chunked_read(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	status = issue_read(2, 5, kIODirectionIn, 2560, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 2560);
	CHECK(g_completion.lock_depth == 0);
	CHECK(gta4l_test.multi_calls == 2);
	CHECK(gta4l_test.read_calls == 2);
	CHECK(g_self->block.counters.requests == 1);
	CHECK(g_self->block.counters.blocks == 5);
	CHECK(g_self->block.counters.requested_blocks == 5);
	CHECK(g_self->block.counters.cmd18_chunks == 2);
	CHECK(g_self->block.counters.cmd18_blocks == 4);
	CHECK(g_self->block.counters.cmd17_reads == 1);
	CHECK(g_self->block.counters.stop_attempts == 2);
	CHECK(g_self->block.counters.stop_failures == 0);
	CHECK(g_self->block.counters.chunk_failures == 0);
	CHECK(g_self->block.counters.last_stop_result == 0);
	CHECK(g_buffer[0] == 2);
	CHECK(g_buffer[512] == 3);
	CHECK(g_buffer[1024] == 4);
	CHECK(g_buffer[1536] == 5);
	CHECK(g_buffer[2048] == 6);
	return 0;
}

static int
scenario_chunked_tail(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	status = issue_read(2, 3, kIODirectionIn, 1536, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 1536);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 2);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(g_self->block.counters.cmd18_blocks == 2);
	CHECK(g_self->block.counters.cmd17_reads == 1);
	CHECK(g_buffer[0] == 2);
	CHECK(g_buffer[512] == 3);
	CHECK(g_buffer[1024] == 4);
	return 0;
}

static int
scenario_chunked_max(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	status = issue_read(0, GTA4L_EMMC_BLOCK_MAX_BLOCKS, kIODirectionIn,
	    GTA4L_EMMC_BLOCK_MAX_BYTES, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(gta4l_test.multi_calls ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS / GTA4L_EMMC_BLOCK_CHUNK_BLOCKS);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(g_self->block.counters.cmd18_blocks ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	CHECK(g_self->block.counters.cmd17_reads == 0);
	CHECK(g_self->block.counters.stop_attempts ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS / GTA4L_EMMC_BLOCK_CHUNK_BLOCKS);
	return 0;
}

static int
scenario_chunked_21(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	status = issue_read(2, 21, kIODirectionIn, 10752, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 10752);
	CHECK(gta4l_test.multi_calls == 10);
	CHECK(gta4l_test.read_calls == 2);
	CHECK(g_self->block.counters.blocks == 21);
	CHECK(g_self->block.counters.cmd18_chunks == 10);
	CHECK(g_self->block.counters.cmd18_blocks == 20);
	CHECK(g_self->block.counters.cmd17_reads == 1);
	CHECK(g_self->block.counters.stop_attempts == 10);
	CHECK(g_buffer[0] == 2);
	CHECK(g_buffer[10240] == 22);
	CHECK(g_buffer[10751] == (uint8_t)(22 + 511));
	return 0;
}

static int
scenario_chunked_failure(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	gta4l_test.fail_multi_at = 0;
	status = issue_read(2, 4, kIODirectionIn, 2048, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 0);
	CHECK(g_completion.lock_depth == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(!g_self->online);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->block.counters.chunk_failures == 1);
	CHECK(g_self->block.counters.first_fail_block == 2);
	CHECK(g_self->block.counters.cmd18_blocks == 0);
	status = issue_read(6, 1, kIODirectionIn, 512, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.status == kIOReturnNoMedia);
	CHECK(gta4l_test.read_calls == 1);
	return 0;
}

static int
scenario_chunked_partial(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	gta4l_test.fail_multi_at = 1;
	status = issue_read(2, 6, kIODirectionIn, 3072, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 1024);
	CHECK(gta4l_test.multi_calls == 2);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(!g_self->online);
	CHECK(g_self->block.counters.blocks == 2);
	CHECK(g_self->block.counters.cmd18_blocks == 2);
	CHECK(g_self->block.counters.first_fail_block == 4);
	CHECK(g_buffer[0] == 2);
	CHECK(g_buffer[512] == 3);
	return 0;
}

static int
scenario_chunked_stop_failure(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	gta4l_test.multi_stop_result = -7;
	status = issue_read(2, 2, kIODirectionIn, 1024, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(!g_self->online);
	CHECK(g_self->block.counters.chunk_failures == 1);
	CHECK(g_self->block.counters.stop_attempts == 1);
	CHECK(g_self->block.counters.stop_failures == 1);
	CHECK(g_self->block.counters.last_stop_result == -7);
	CHECK(g_self->block.counters.first_fail_block == 2);
	return 0;
}

static int
scenario_chunked_short_count(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	gta4l_test.multi_short_blocks = 1;
	status = issue_read(2, 2, kIODirectionIn, 1024, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(!g_self->online);
	CHECK(g_self->block.counters.chunk_failures == 1);
	CHECK(g_self->block.counters.cmd18_blocks == 0);
	return 0;
}

static int
scenario_read_max(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	status = issue_read(0, GTA4L_EMMC_BLOCK_MAX_BLOCKS, kIODirectionIn,
	    GTA4L_EMMC_BLOCK_MAX_BYTES, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == GTA4L_EMMC_BLOCK_MAX_BYTES);
	CHECK(gta4l_test.read_calls == GTA4L_EMMC_BLOCK_MAX_BLOCKS + 1u);
	CHECK(g_self->block.counters.max_request_blocks ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	return 0;
}

static int
scenario_read_failure(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	gta4l_test.fail_read_at = 1;
	status = issue_read(5, 2, kIODirectionIn, 1024, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 0);
	CHECK(g_completion.lock_depth == 0);
	CHECK(!g_self->online);
	CHECK(gta4l_test.message_count == 1);
	CHECK(gta4l_test.message_type == kIOMessageMediaStateHasChanged);
	CHECK((uintptr_t)gta4l_test.message_argument ==
	    (uintptr_t)kIOMediaStateOffline);
	CHECK(gta4l_test.message_lock_depth == 0);
	CHECK(g_self->block.counters.failures == 1);
	CHECK(g_self->block.counters.first_fail_block == 5);
	status = issue_read(6, 1, kIODirectionIn, 512, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnNoMedia);
	CHECK(g_completion.actual == 0);
	CHECK(gta4l_test.read_calls == 1);
	return 0;
}

static int
scenario_read_partial(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	gta4l_test.fail_read_at = 2;
	status = issue_read(5, 3, kIODirectionIn, 1536, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnIOError);
	CHECK(g_completion.actual == 512);
	CHECK(!g_self->online);
	CHECK(g_self->block.counters.blocks == 1);
	CHECK(g_self->block.counters.first_fail_block == 6);
	return 0;
}

static int
scenario_copy_short(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	gta4l_test.short_copy_at = 0;
	status = issue_read(5, 1, kIODirectionIn, 512, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.calls == 1);
	CHECK(g_completion.status == kIOReturnNoMemory);
	CHECK(g_completion.actual == 511);
	CHECK(!g_self->online);
	return 0;
}

static int
scenario_media_absent(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=12") != 0);
	CHECK(strstr(gta4l_test.panic, "media=0") == 0);
	CHECK(strstr(gta4l_test.panic, "attempted=0") != 0);
	return 0;
}

static int
scenario_media_read_unopened(void)
{
	IOMedia *media;
	IOMemoryDescriptor *descriptor;
	UInt64 actual = 0;
	IOReturn status;

	begin("gta4l-emmc-block=1");
	media = add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	descriptor = IOMemoryDescriptor::withAddress(g_buffer,
	    GTA4L_EMMC_BLOCK_BYTES, kIODirectionIn);
	status = media->read(g_self, GTA4L_EMMC_BLOCK_BYTES, descriptor,
	    0, &actual);
	descriptor->release();
	CHECK(status == kIOReturnNotOpen);
	CHECK(actual == 0);
	CHECK(media->open_calls == 0);
	CHECK(media->close_calls == 0);
	return 0;
}

static int
scenario_media_open_refused(void)
{
	IOMedia *media;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	media = add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	media->open_refuse = true;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(strstr(gta4l_test.panic, "stage=6") != 0);
	CHECK(strstr(gta4l_test.panic, "attempted=0") != 0);
	CHECK(strstr(gta4l_test.panic, "ok=0") != 0);
	CHECK(media->open_calls == 1);
	CHECK(media->close_calls == 0);
	CHECK(!media->open_value);
	return 0;
}

static int
scenario_media_descriptor_failure(void)
{
	IOMedia *media;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	media = add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.descriptor_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(strstr(gta4l_test.panic, "stage=3") != 0);
	CHECK(strstr(gta4l_test.panic, "attempted=0") != 0);
	CHECK(media->open_calls == 1);
	CHECK(media->close_calls == 1);
	CHECK(!media->open_value);
	return 0;
}

static int
scenario_media_open_balance(void)
{
	IOMedia *media;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	media = add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	CHECK(strstr(gta4l_test.panic, "same=1") != 0);
	CHECK(media->open_calls == 1);
	CHECK(media->close_calls == 1);
	CHECK(!media->open_value);
	CHECK(media->open_access == kIOStorageAccessReader);
	return 0;
}

static int
scenario_media_geometry(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, 4096, false, "GUID_partition_scheme", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=13") != 0);
	return 0;
}

static IOReturn
media_read_fail(IOMedia *media, UInt64 byteStart, IOMemoryDescriptor *buffer,
    UInt64 *actualByteCount)
{
	(void)media;
	(void)byteStart;
	(void)buffer;
	if (actualByteCount != 0) {
		*actualByteCount = 0;
	}
	return kIOReturnIOError;
}

static IOReturn
media_read_short(IOMedia *media, UInt64 byteStart, IOMemoryDescriptor *buffer,
    UInt64 *actualByteCount)
{
	(void)media;
	(void)byteStart;
	(void)buffer;
	if (actualByteCount != 0) {
		*actualByteCount = 100;
	}
	return kIOReturnSuccess;
}

static IOReturn
media_read_divergent(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount)
{
	uint8_t *bytes = static_cast<uint8_t *>(buffer->address());

	(void)media;
	(void)byteStart;
	memset(bytes, 0x5a, GTA4L_EMMC_BLOCK_BYTES);
	if (actualByteCount != 0) {
		*actualByteCount = GTA4L_EMMC_BLOCK_BYTES;
	}
	return kIOReturnSuccess;
}

static int
scenario_media_read_fail(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test_media_read_hook = media_read_fail;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(strstr(gta4l_test.panic, "attempted=1") != 0);
	CHECK(strstr(gta4l_test.panic, "ok=0") != 0);
	return 0;
}

static int
scenario_media_read_short(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test_media_read_hook = media_read_short;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(strstr(gta4l_test.panic, "bytes=100") != 0);
	return 0;
}

static int
scenario_media_divergence(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test_media_read_hook = media_read_divergent;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(strstr(gta4l_test.panic, "same=0") != 0);
	return 0;
}

static int
scenario_media_success(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_media(false, 2048, 1048576, false, "EFI", true);
	add_media(false, 1050624, 2097152, false, "Apple_APFS", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	CHECK(strstr(gta4l_test.panic, "same=1") != 0);
	CHECK(strstr(gta4l_test.panic, "attempted=1") != 0);
	CHECK(strstr(gta4l_test.panic, "ok=1") != 0);
	CHECK(strstr(gta4l_test.panic, "count=2") != 0);
	CHECK(strstr(gta4l_test.panic, "expected=84") != 0);
	CHECK(strstr(gta4l_test.panic, "GUID_partition_scheme") != 0);
	CHECK(strstr(gta4l_test.panic, "Apple_APFS") != 0);
	return 0;
}

static int
scenario_media_partition_absent(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	CHECK(strstr(gta4l_test.panic, "count=0") != 0);
	CHECK(strstr(gta4l_test.panic, "expected=84") != 0);
	return 0;
}

static int
scenario_watchdog(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(gta4l_test.thread_count == 2);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(g_self->reportDone);
	return 0;
}

static int
scenario_watchdog_start_failure(void)
{
	gta4l_test_reset();
	gta4l_test.boot_args = "gta4l-emmc-block=1";
	gta4l_test.watchdog_start_result = -1;
	g_self = new Gta4lEmmcBlock();
	g_started = false;
	g_panicked = 0;
	if (setjmp(gta4l_test.panic_jump) == 0) {
		g_started = g_self->start(0);
		CHECK(false);
	}
	CHECK(!g_started);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "stage=17") != 0);
	CHECK(strstr(gta4l_test.log, "WATCHDOG") != 0);
	return 0;
}

static int
scenario_watchdog_snapshot_blocked(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	IOLockLock(g_self->gate);
	CHECK(invoke_thread(1) == 1);
	IOLockUnlock(g_self->gate);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SNAP: locked=0") != 0);
	return 0;
}

static int
scenario_watchdog_terminal_race(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->terminalClaimed);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	return 0;
}

static int
scenario_media_adapter_route(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	CHECK(g_self->block.counters.requests == 1);
	CHECK(g_self->block.counters.cmd17_reads == 1);
	CHECK(gta4l_test.read_calls == 2);
	return 0;
}

static int
scenario_qual_success(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_media(false, 2048, 1048576, false, "EFI", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL: ref=1 "
	    "ref_lba=2 ref_blocks=3 two_att=1 two_ok=1 two_reason=0 "
	    "two_bytes=1024 two_mismatch=0 two_off=0 two_match=1024 "
	    "two_c18=1 two_c17=0 two_stops=1 two_sfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=1 "
	    "reason=0 bytes=1536 mismatch=0 off=0 match=1536 c18=1 "
	    "c17=1 stops=1 sfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SNAP: locked=1") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	return 0;
}

static int
scenario_qual_incidental(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	status = issue_read(2, 2, kIODirectionIn, 1024, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=1 "
	    "two_reason=0") != 0);
	CHECK(strstr(gta4l_test.panic, "two_c18=1 two_c17=0 "
	    "two_stops=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=1 "
	    "reason=0") != 0);
	return 0;
}

static int
scenario_qual_two_mismatch(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.multi_corrupt_at = 0;
	gta4l_test.multi_corrupt_block = 3;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=0 "
	    "two_reason=3 two_bytes=1024 two_mismatch=1 two_off=512 "
	    "two_match=1023 two_c18=1 two_c17=0 two_stops=1 "
	    "two_sfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=1 "
	    "reason=0") != 0);
	return 0;
}

static int
scenario_qual_sum_preserving_mismatch(void)
{
	uint8_t expected[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t changed[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t index;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		expected[index] = (uint8_t)(3 + index);
	}
	memcpy(changed, expected, sizeof(changed));
	changed[31] = (uint8_t)(changed[31] + 1u);
	changed[32] = (uint8_t)(changed[32] - 31u);
	CHECK(memcmp(expected, changed, sizeof(expected)) != 0);
	CHECK(gta4l_emmc_block_sum(expected, sizeof(expected)) ==
	    gta4l_emmc_block_sum(changed, sizeof(changed)));
	gta4l_test.multi_corrupt_at = 0;
	gta4l_test.multi_corrupt_block = 3;
	gta4l_test.multi_corrupt_pair = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=0 "
	    "two_reason=3 two_bytes=1024 two_mismatch=1 two_off=543 "
	    "two_match=1022 two_c18=1 two_c17=0 two_stops=1 "
	    "two_sfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=1 "
	    "reason=0") != 0);
	return 0;
}

static int
scenario_qual_three_tail_mismatch(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.read_corrupt_at = 4;
	gta4l_test.read_corrupt_block = 4;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=1 "
	    "two_reason=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=0 "
	    "reason=3 bytes=1536 mismatch=1 off=1024 match=1535 c18=1 "
	    "c17=1 stops=1 sfail=0") != 0);
	return 0;
}

static int
scenario_qual_wrong_shape(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	g_self->io.read_multi = 0;
	g_self->io.chunk_buffer = 0;
	g_self->io.chunk_buffer_bytes = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=0 "
	    "two_reason=4 two_bytes=1024 two_mismatch=0 two_off=0 "
	    "two_match=1024 two_c18=0 two_c17=2 two_stops=0 "
	    "two_sfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=0 "
	    "reason=4 bytes=1536 mismatch=0 off=0 match=1536 c18=0 c17=3 "
	    "stops=0 sfail=0") != 0);
	return 0;
}

static int
scenario_qual_reference_unavailable(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.fail_read_at = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL: ref=0 "
	    "ref_lba=2 ref_blocks=3 two_att=0 two_ok=0 two_reason=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=0 ok=0 "
	    "reason=1") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	return 0;
}

static int
scenario_qual_ambiguous(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.media_read_replay = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=0 "
	    "two_reason=5") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=1 "
	    "reason=0") != 0);
	return 0;
}

static int
scenario_qual_stop_failure(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	gta4l_test.multi_stop_result = -7;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=0 "
	    "two_reason=2 two_bytes=0 two_mismatch=0 two_off=0 "
	    "two_match=0 two_c18=1 two_c17=0 two_stops=1 "
	    "two_sfail=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=0 "
	    "reason=2") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=14") != 0);
	CHECK(g_self->block.counters.stop_failures == 1);
	return 0;
}

static int
scenario_policy(void)
{
	char buffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	struct gta4l_emmc_block_report report;
	uint32_t length;
	uint32_t index;

	CHECK(gta4l_emmc_block_token_count("a=1 gta4l-emmc-block=1", 
	    GTA4L_EMMC_BLOCK_BOOT_ARG) == 1);
	CHECK(gta4l_emmc_block_token_count("xgta4l-emmc-block=1", 
	    GTA4L_EMMC_BLOCK_BOOT_ARG) == 0);
	CHECK(gta4l_emmc_block_token_count("gta4l-emmc-block=1x", 
	    GTA4L_EMMC_BLOCK_BOOT_ARG) == 1);
	CHECK(gta4l_emmc_block_mode_select(1, -1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_token_count("gta4l-emmc-block=1 "
	    "gta4l-emmc-block=2", GTA4L_EMMC_BLOCK_BOOT_ARG) == 2);
	CHECK(gta4l_emmc_block_mode_select(0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_INERT);
	CHECK(gta4l_emmc_block_mode_select(1, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REPORT);
	CHECK(gta4l_emmc_block_mode_select(1, 1, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_mode_select(1, 1, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_mode_select(1, 1, 0, 0, 1) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_mode_select(1, 2, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_mode_select(1, 3, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED);
	CHECK(gta4l_emmc_block_mode_select(1, 3, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_mode_select(1, 3, 0, 0, 1) ==
	    GTA4L_EMMC_BLOCK_SELECT_REFUSED);
	CHECK(gta4l_emmc_block_request_check(10, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_DIRECTION_READ, 1) ==
	    GTA4L_EMMC_BLOCK_REQUEST_OK);
	CHECK(gta4l_emmc_block_request_check(10, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_DIRECTION_WRITE, 1) ==
	    GTA4L_EMMC_BLOCK_REQUEST_NOT_WRITABLE);
	CHECK(gta4l_emmc_block_request_check(10, 0, 1, 512,
	    GTA4L_EMMC_BLOCK_DIRECTION_READ, 0) ==
	    GTA4L_EMMC_BLOCK_REQUEST_NO_MEDIA);
	CHECK(gta4l_emmc_block_media_check(0, 5120, 10, 1, 0) == 0);
	CHECK(gta4l_emmc_block_media_check(1, 5120, 10, 1, 0) == 0x02);
	CHECK(gta4l_emmc_block_media_check(0, 5120, 10, 0, 0) == 0x01);
	CHECK(gta4l_emmc_block_media_check(0, 5120, 10, 1, 1) == 0x08);
	CHECK(gta4l_emmc_block_media_check(0, 0, 10, 1, 0) == 0x04);
	CHECK(gta4l_emmc_block_media_check(0, 0, 0, 1, 0) == 0x14);

	memset(&report, 0, sizeof(report));
	report.capacity_blocks = 0xffffffffu;
	report.mode = 3;
	report.block_size = 512;
	report.bytes = 0xffffffffffffffffull;
	report.bus_width = 1;
	report.clock_hz = 400000;
	report.online = 1;
	report.write_protected = 1;
	report.requests = 0xffffffffffffffffull;
	report.blocks = 0xffffffffffffffffull;
	report.failures = 0xffffffffffffffffull;
	report.max_request_blocks = 0xffffffffu;
	report.first_fail_valid = 1;
	report.first_fail_block = 0xffffffffffffffffull;
	report.first_fail_error = -2147483647;
	report.requested_blocks = 0xffffffffffffffffull;
	report.cmd17_reads = 0xffffffffffffffffull;
	report.cmd18_chunks = 0xffffffffffffffffull;
	report.cmd18_blocks = 0xffffffffffffffffull;
	report.chunk_failures = 0xffffffffffffffffull;
	report.stop_attempts = 0xffffffffffffffffull;
	report.stop_failures = 0xffffffffffffffffull;
	report.last_stop_result = -2147483647;
	report.media_valid = 1;
	report.whole = 1;
	report.base = 0xffffffffffffffffull;
	report.size = 0xffffffffffffffffull;
	report.content = "untrusted-content";
	report.part_count = 0xffffffffu;
	report.part_expected = 0xffffffffu;
	report.part_wait_ms = 0xffffffffu;
	report.part_error = 0xffffffffu;
	report.part_reported = GTA4L_EMMC_BLOCK_PART_REPORT;
	for (index = 0; index < GTA4L_EMMC_BLOCK_PART_REPORT; index++) {
		report.part[index].base = 0xffffffffffffffffull;
		report.part[index].size = 0xffffffffffffffffull;
		report.part[index].content = "untrusted-content";
	}
	report.read_attempted = 1;
	report.read_ok = 1;
	report.read_bytes = 0xffffffffffffffffull;
	report.read_sum = 0xffffffffu;
	report.baseline_sum = 0xffffffffu;
	report.same = 1;
	report.fail_valid = 1;
	report.fail_stage = 0xffffffffu;
	report.fail_code = -2147483647;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "untrusted-content") == 0);
	CHECK(strstr(buffer, "content=other") != 0);
	CHECK(strstr(buffer, "CID") == 0);
	CHECK(strstr(buffer, "serial") == 0);
	CHECK(strstr(buffer, "UUID") == 0);
	return 0;
}

static int
run(const char *name, int (*scenario)(void))
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
	{ "disabled", scenario_disabled },
	{ "conflict-probe", scenario_conflict_probe },
	{ "conflict-other", scenario_conflict_other },
	{ "duplicate", scenario_duplicate },
	{ "publish-deferred", scenario_publish_deferred },
	{ "malformed-value", scenario_malformed_value },
	{ "init-failure", scenario_init_failure },
	{ "lock-failure", scenario_lock_failure },
	{ "thread-failure", scenario_thread_failure },
	{ "start-ok", scenario_start_ok },
	{ "properties", scenario_properties },
	{ "write-refused", scenario_write_refused },
	{ "invalid-requests", scenario_invalid_requests },
	{ "read-one", scenario_read_one },
	{ "read-multi", scenario_read_multi },
	{ "read-max", scenario_read_max },
	{ "chunked-start", scenario_chunked_start },
	{ "chunked-conflict", scenario_chunked_conflict },
	{ "chunked-read", scenario_chunked_read },
	{ "chunked-tail", scenario_chunked_tail },
	{ "chunked-max", scenario_chunked_max },
	{ "chunked-21", scenario_chunked_21 },
	{ "chunked-failure", scenario_chunked_failure },
	{ "chunked-partial", scenario_chunked_partial },
	{ "chunked-stop-failure", scenario_chunked_stop_failure },
	{ "chunked-short-count", scenario_chunked_short_count },
	{ "read-failure", scenario_read_failure },
	{ "read-partial", scenario_read_partial },
	{ "copy-short", scenario_copy_short },
	{ "media-absent", scenario_media_absent },
	{ "media-read-unopened", scenario_media_read_unopened },
	{ "media-open-refused", scenario_media_open_refused },
	{ "media-descriptor-failure", scenario_media_descriptor_failure },
	{ "media-open-balance", scenario_media_open_balance },
	{ "media-geometry", scenario_media_geometry },
	{ "media-read-fail", scenario_media_read_fail },
	{ "media-read-short", scenario_media_read_short },
	{ "media-divergence", scenario_media_divergence },
	{ "media-success", scenario_media_success },
	{ "media-partition-absent", scenario_media_partition_absent },
	{ "watchdog", scenario_watchdog },
	{ "watchdog-start-failure", scenario_watchdog_start_failure },
	{ "watchdog-snapshot-blocked", scenario_watchdog_snapshot_blocked },
	{ "watchdog-terminal-race", scenario_watchdog_terminal_race },
	{ "media-adapter-route", scenario_media_adapter_route },
	{ "qual-success", scenario_qual_success },
	{ "qual-incidental", scenario_qual_incidental },
	{ "qual-two-mismatch", scenario_qual_two_mismatch },
	{ "qual-sum-preserving-mismatch", scenario_qual_sum_preserving_mismatch },
	{ "qual-three-tail-mismatch", scenario_qual_three_tail_mismatch },
	{ "qual-wrong-shape", scenario_qual_wrong_shape },
	{ "qual-reference-unavailable", scenario_qual_reference_unavailable },
	{ "qual-ambiguous", scenario_qual_ambiguous },
	{ "qual-stop-failure", scenario_qual_stop_failure },
	{ "policy", scenario_policy },
};

int
main(int argc, char **argv)
{
	unsigned int index;
	int failures = 0;

	if (argc > 1) {
		for (index = 0;
		    index < sizeof(scenarios) / sizeof(scenarios[0]); index++) {
			if (strcmp(argv[1], scenarios[index].name) == 0) {
				return run(scenarios[index].name,
				    scenarios[index].run);
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
	printf("PASS gta4l eMMC block adapter\n");
	return 0;
}
