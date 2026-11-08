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

static void
partobs_cancel_hook(uint32_t sample, uint32_t visible)
{
	(void)visible;
	if (sample == 1u) {
		g_self->reportCancel = true;
	}
}

static void
partobs_watchdog_hook(uint32_t sample, uint32_t visible)
{
	(void)visible;
	if (sample == 1u) {
		(void)invoke_thread(1);
	}
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

static void
add_partition_media(uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++) {
		add_media(false, (UInt64)(2048u + index * 2048u), 1048576,
		    false, "EFI", true);
	}
}

static void
publish_profile(const uint32_t *counts, uint32_t count)
{
	uint32_t index;

	if (count > GTA4L_TEST_MAX_MEDIA) {
		return;
	}
	for (index = 0; index < count; index++) {
		gta4l_test.part_publish_counts[index] = counts[index];
	}
	gta4l_test.part_publish_count = (int)count;
}

#define PARTCMP_FIRST_USABLE 34u
#define PARTCMP_PARTITION_SECTORS 2048u
#define PARTCMP_SLOTS 84u

enum partcmp_corrupt {
	PARTCMP_CORRUPT_NONE = 0,
	PARTCMP_CORRUPT_PMBR_SIG,
	PARTCMP_CORRUPT_PMBR_NONE,
	PARTCMP_CORRUPT_PMBR_TWO_EE,
	PARTCMP_CORRUPT_PMBR_START,
	PARTCMP_CORRUPT_PMBR_HYBRID,
	PARTCMP_CORRUPT_HDR_SIG,
	PARTCMP_CORRUPT_HDR_SIZE,
	PARTCMP_CORRUPT_HDR_REVISION,
	PARTCMP_CORRUPT_HDR_RESERVED,
	PARTCMP_CORRUPT_HDR_CRC,
	PARTCMP_CORRUPT_HDR_MY_LBA,
	PARTCMP_CORRUPT_HDR_ALT_LBA,
	PARTCMP_CORRUPT_HDR_USABLE,
	PARTCMP_CORRUPT_HDR_SPAN,
	PARTCMP_CORRUPT_ARR_CRC,
	PARTCMP_CORRUPT_BAK_SIG,
	PARTCMP_CORRUPT_BAK_ARR_CRC,
	PARTCMP_CORRUPT_DISAGREE_GUID,
	PARTCMP_CORRUPT_DISAGREE_ENTRY,
	PARTCMP_CORRUPT_ENTRY_REVERSED,
	PARTCMP_CORRUPT_ENTRY_OUTSIDE,
	PARTCMP_CORRUPT_ENTRY_OVERLAP,
	PARTCMP_CORRUPT_ENTRY_DUPLICATE,
	PARTCMP_CORRUPT_ENTRY_NULL_RANGE,
	PARTCMP_CORRUPT_EMPTY_STALE,
	PARTCMP_CORRUPT_SINGLE_SECTOR
};

static uint32_t g_partcmp_capacity;
static uint32_t g_partcmp_slots;
static uint32_t g_partcmp_entry_lba;
static uint32_t g_partcmp_backup_entry_lba;
static uint32_t g_partcmp_array_blocks;
static uint8_t g_partcmp_pmbr[GTA4L_EMMC_BLOCK_BYTES];
static uint8_t g_partcmp_primary[GTA4L_EMMC_BLOCK_BYTES];
static uint8_t g_partcmp_backup[GTA4L_EMMC_BLOCK_BYTES];
static uint8_t g_partcmp_array[GTA4L_EMMC_BLOCK_PARTMAP_ARRAY_BYTES];
static uint8_t
    g_partcmp_backup_array[GTA4L_EMMC_BLOCK_PARTMAP_ARRAY_BYTES];
static int g_partcmp_read_calls;
static int g_partcmp_fail_at;
static int g_partcmp_short_at;
static int g_partcmp_cancel_at;
static uint32_t g_partcmp_advance_ms;
static IOMedia *g_partcmp_whole;
static IOService *g_partcmp_scheme;

static void
partcmp_put32(uint8_t *target, uint32_t value)
{
	target[0] = (uint8_t)value;
	target[1] = (uint8_t)(value >> 8);
	target[2] = (uint8_t)(value >> 16);
	target[3] = (uint8_t)(value >> 24);
}

static void
partcmp_put64(uint8_t *target, uint64_t value)
{
	uint32_t index;

	for (index = 0; index < 8u; index++) {
		target[index] = (uint8_t)(value >> (index * 8u));
	}
}

static uint32_t
partcmp_crc32(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xffffffffu;
	uint32_t index;
	uint32_t bit;

	for (index = 0; index < length; index++) {
		crc ^= data[index];
		for (bit = 0; bit < 8u; bit++) {
			if ((crc & 1u) != 0u) {
				crc = (crc >> 1) ^ 0xedb88320u;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc ^ 0xffffffffu;
}

static uint64_t
partcmp_first(uint32_t index)
{
	return (uint64_t)PARTCMP_FIRST_USABLE +
	    (uint64_t)index * PARTCMP_PARTITION_SECTORS;
}

static uint64_t
partcmp_last(uint32_t index)
{
	return partcmp_first(index) + PARTCMP_PARTITION_SECTORS - 1u;
}

static void
partcmp_build_header(uint8_t *header, uint64_t my_lba,
    uint64_t alternate_lba, uint64_t entry_lba, uint32_t entry_crc)
{
	memset(header, 0, GTA4L_EMMC_BLOCK_BYTES);
	memcpy(header, "EFI PART", 8);
	partcmp_put32(header + 8u, 0x00010000u);
	partcmp_put32(header + 12u, 92u);
	partcmp_put32(header + 20u, 0u);
	partcmp_put64(header + 24u, my_lba);
	partcmp_put64(header + 32u, alternate_lba);
	partcmp_put64(header + 40u, PARTCMP_FIRST_USABLE);
	partcmp_put64(header + 48u, (uint64_t)g_partcmp_capacity - 34u);
	partcmp_put64(header + 56u, 0x0123456789abcdefull);
	partcmp_put64(header + 64u, 0xfedcba9876543210ull);
	partcmp_put64(header + 72u, entry_lba);
	partcmp_put32(header + 80u, g_partcmp_slots);
	partcmp_put32(header + 84u, 128u);
	partcmp_put32(header + 88u, entry_crc);
	partcmp_put32(header + 16u, partcmp_crc32(header, 92u));
}

static void
partcmp_fix_primary_header(void)
{
	partcmp_put32(g_partcmp_primary + 16u, 0u);
	partcmp_put32(g_partcmp_primary + 16u,
	    partcmp_crc32(g_partcmp_primary, 92u));
}

static void
partcmp_fix_backup_header(void)
{
	partcmp_put32(g_partcmp_backup + 16u, 0u);
	partcmp_put32(g_partcmp_backup + 16u,
	    partcmp_crc32(g_partcmp_backup, 92u));
}

static void
partcmp_fix_header_crc(uint8_t *header)
{
	partcmp_put32(header + 16u, 0u);
	partcmp_put32(header + 16u, partcmp_crc32(header, 92u));
}

static void
partcmp_build_fixture(uint32_t used)
{
	uint32_t index;
	uint32_t crc;

	g_partcmp_capacity = g_self->capacityBlocks;
	g_partcmp_slots = PARTCMP_SLOTS;
	g_partcmp_entry_lba = 2u;
	g_partcmp_array_blocks =
	    (g_partcmp_slots * 128u + 511u) / 512u;
	g_partcmp_backup_entry_lba =
	    g_partcmp_capacity - 1u - g_partcmp_array_blocks;
	memset(g_partcmp_pmbr, 0, sizeof(g_partcmp_pmbr));
	g_partcmp_pmbr[446u + 4u] = 0xeeu;
	partcmp_put32(g_partcmp_pmbr + 446u + 8u, 1u);
	partcmp_put32(g_partcmp_pmbr + 446u + 12u,
	    g_partcmp_capacity - 1u);
	g_partcmp_pmbr[510] = 0x55u;
	g_partcmp_pmbr[511] = 0xaau;
	memset(g_partcmp_array, 0, sizeof(g_partcmp_array));
	for (index = 0; index < used; index++) {
		uint8_t *entry = g_partcmp_array + (index * 128u);
		uint32_t at;

		for (at = 0; at < 16u; at++) {
			entry[at] = (uint8_t)(0xa0u + at + index);
		}
		entry[0] = 0xa5u;
		for (at = 0; at < 16u; at++) {
			entry[16u + at] =
			    (uint8_t)(0x10u + at + (index * 3u));
		}
		partcmp_put64(entry + 32u, partcmp_first(index));
		partcmp_put64(entry + 40u, partcmp_last(index));
	}
	crc = partcmp_crc32(g_partcmp_array, g_partcmp_slots * 128u);
	partcmp_build_header(g_partcmp_primary, 1u,
	    (uint64_t)g_partcmp_capacity - 1u, 2u, crc);
	partcmp_build_header(g_partcmp_backup,
	    (uint64_t)g_partcmp_capacity - 1u, 1u,
	    (uint64_t)g_partcmp_backup_entry_lba, crc);
	memcpy(g_partcmp_backup_array, g_partcmp_array,
	    g_partcmp_slots * 128u);
}

static void
partcmp_rebuild_arrays(void)
{
	uint32_t crc;

	crc = partcmp_crc32(g_partcmp_array, g_partcmp_slots * 128u);
	memcpy(g_partcmp_backup_array, g_partcmp_array,
	    g_partcmp_slots * 128u);
	partcmp_put32(g_partcmp_primary + 88u, crc);
	partcmp_fix_primary_header();
	partcmp_put32(g_partcmp_backup + 88u, crc);
	partcmp_fix_backup_header();
}

static void
partcmp_corrupt(uint32_t kind)
{
	uint32_t crc;

	switch (kind) {
	case PARTCMP_CORRUPT_NONE:
		break;
	case PARTCMP_CORRUPT_PMBR_SIG:
		g_partcmp_pmbr[511] = 0u;
		break;
	case PARTCMP_CORRUPT_PMBR_NONE:
		g_partcmp_pmbr[446u + 4u] = 0u;
		break;
	case PARTCMP_CORRUPT_PMBR_TWO_EE:
		g_partcmp_pmbr[446u + 16u + 4u] = 0xeeu;
		partcmp_put32(g_partcmp_pmbr + 446u + 16u + 8u, 1u);
		partcmp_put32(g_partcmp_pmbr + 446u + 16u + 12u, 1u);
		break;
	case PARTCMP_CORRUPT_PMBR_START:
		partcmp_put32(g_partcmp_pmbr + 446u + 8u, 2u);
		break;
	case PARTCMP_CORRUPT_PMBR_HYBRID:
		g_partcmp_pmbr[446u + 16u + 4u] = 0x83u;
		break;
	case PARTCMP_CORRUPT_HDR_SIG:
		g_partcmp_primary[0] = 'X';
		break;
	case PARTCMP_CORRUPT_HDR_SIZE:
		partcmp_put32(g_partcmp_primary + 12u, 91u);
		break;
	case PARTCMP_CORRUPT_HDR_REVISION:
		partcmp_put32(g_partcmp_primary + 8u, 0x00020000u);
		break;
	case PARTCMP_CORRUPT_HDR_RESERVED:
		partcmp_put32(g_partcmp_primary + 20u, 1u);
		break;
	case PARTCMP_CORRUPT_HDR_CRC:
		g_partcmp_primary[16] ^= 0x5au;
		break;
	case PARTCMP_CORRUPT_HDR_MY_LBA:
		partcmp_put64(g_partcmp_primary + 24u, 2u);
		partcmp_fix_primary_header();
		break;
	case PARTCMP_CORRUPT_HDR_ALT_LBA:
		partcmp_put64(g_partcmp_primary + 32u, 5u);
		partcmp_fix_primary_header();
		break;
	case PARTCMP_CORRUPT_HDR_USABLE:
		partcmp_put64(g_partcmp_primary + 40u, 100u);
		partcmp_put64(g_partcmp_primary + 48u, 50u);
		partcmp_fix_primary_header();
		break;
	case PARTCMP_CORRUPT_HDR_SPAN:
		partcmp_put64(g_partcmp_primary + 72u, 33u);
		partcmp_fix_primary_header();
		break;
	case PARTCMP_CORRUPT_ARR_CRC:
		g_partcmp_array[128u + 32u] ^= 0x5au;
		break;
	case PARTCMP_CORRUPT_BAK_SIG:
		g_partcmp_backup[0] = 'X';
		break;
	case PARTCMP_CORRUPT_BAK_ARR_CRC:
		g_partcmp_backup_array[128u + 32u] ^= 0x5au;
		break;
	case PARTCMP_CORRUPT_DISAGREE_GUID:
		partcmp_put64(g_partcmp_backup + 56u,
		    0x1111111111111111ull);
		partcmp_fix_backup_header();
		break;
	case PARTCMP_CORRUPT_DISAGREE_ENTRY:
		partcmp_put64(g_partcmp_backup_array + 32u,
		    partcmp_first(0));
		partcmp_put64(g_partcmp_backup_array + 40u,
		    partcmp_first(0));
		crc = partcmp_crc32(g_partcmp_backup_array,
		    g_partcmp_slots * 128u);
		partcmp_put32(g_partcmp_backup + 88u, crc);
		partcmp_fix_backup_header();
		break;
	case PARTCMP_CORRUPT_ENTRY_REVERSED:
		partcmp_put64(g_partcmp_array + 32u, partcmp_last(0));
		partcmp_put64(g_partcmp_array + 40u, partcmp_first(0));
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_ENTRY_OUTSIDE:
		partcmp_put64(g_partcmp_array + 40u,
		    (uint64_t)g_partcmp_capacity - 33u);
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_ENTRY_OVERLAP:
		partcmp_put64(g_partcmp_array + 128u + 32u,
		    partcmp_first(0));
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_ENTRY_DUPLICATE:
		memcpy(g_partcmp_array + 128u, g_partcmp_array, 128u);
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_ENTRY_NULL_RANGE:
		partcmp_put64(g_partcmp_array + 32u, 0u);
		partcmp_put64(g_partcmp_array + 40u, 0u);
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_EMPTY_STALE:
		memset(g_partcmp_array + (79u * 128u), 0, 16u);
		partcmp_rebuild_arrays();
		break;
	case PARTCMP_CORRUPT_SINGLE_SECTOR:
		partcmp_put64(g_partcmp_array + (79u * 128u) + 40u,
		    partcmp_first(79));
		partcmp_rebuild_arrays();
		break;
	default:
		break;
	}
}

static IOReturn
partcmp_read_hook(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount)
{
	uint64_t lba = byteStart / GTA4L_EMMC_BLOCK_BYTES;
	uint32_t length = (uint32_t)buffer->getLength();
	const uint8_t *source = 0;
	uint32_t source_bytes = 0;
	uint32_t offset = 0;
	uint32_t call = (uint32_t)g_partcmp_read_calls++;

	(void)media;
	if (g_partcmp_advance_ms != 0u) {
		gta4l_test.now_ms += g_partcmp_advance_ms;
	}
	if ((g_partcmp_fail_at >= 0) &&
	    (call == (uint32_t)g_partcmp_fail_at)) {
		return kIOReturnIOError;
	}
	if ((g_partcmp_cancel_at >= 0) &&
	    (call == (uint32_t)g_partcmp_cancel_at)) {
		g_self->reportCancel = true;
	}
	if (lba == 0u) {
		source = g_partcmp_pmbr;
		source_bytes = GTA4L_EMMC_BLOCK_BYTES;
	} else if (lba == 1u) {
		source = g_partcmp_primary;
		source_bytes = GTA4L_EMMC_BLOCK_BYTES;
		if (gta4l_test.part_publish_count > 0) {
			gta4l_test.part_publish_active = 1;
		}
	} else if ((lba >= g_partcmp_entry_lba) &&
	    (lba < (uint64_t)g_partcmp_entry_lba +
	    g_partcmp_array_blocks)) {
		source = g_partcmp_array;
		source_bytes = g_partcmp_array_blocks *
		    GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(lba - g_partcmp_entry_lba) *
		    GTA4L_EMMC_BLOCK_BYTES;
	} else if (lba == (uint64_t)g_partcmp_capacity - 1u) {
		source = g_partcmp_backup;
		source_bytes = GTA4L_EMMC_BLOCK_BYTES;
	} else if ((lba >= g_partcmp_backup_entry_lba) &&
	    (lba < (uint64_t)g_partcmp_backup_entry_lba +
	    g_partcmp_array_blocks)) {
		source = g_partcmp_backup_array;
		source_bytes = g_partcmp_array_blocks *
		    GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(lba - g_partcmp_backup_entry_lba) *
		    GTA4L_EMMC_BLOCK_BYTES;
	} else {
		return kIOReturnIOError;
	}
	if ((offset + length) > source_bytes) {
		return kIOReturnBadArgument;
	}
	memcpy(buffer->address(), source + offset, length);
	if (actualByteCount != 0) {
		*actualByteCount = ((g_partcmp_short_at >= 0) &&
		    (call == (uint32_t)g_partcmp_short_at)) ?
		    (UInt64)(length - GTA4L_EMMC_BLOCK_BYTES) :
		    (UInt64)length;
	}
	return kIOReturnSuccess;
}

static void
partcmp_reset_fixture(void)
{
	g_partcmp_capacity = 0;
	g_partcmp_slots = PARTCMP_SLOTS;
	g_partcmp_entry_lba = 2u;
	g_partcmp_backup_entry_lba = 0;
	g_partcmp_array_blocks = 0;
	g_partcmp_read_calls = 0;
	g_partcmp_fail_at = -1;
	g_partcmp_short_at = -1;
	g_partcmp_cancel_at = -1;
	g_partcmp_advance_ms = 0u;
	memset(g_partcmp_pmbr, 0, sizeof(g_partcmp_pmbr));
	memset(g_partcmp_primary, 0, sizeof(g_partcmp_primary));
	memset(g_partcmp_backup, 0, sizeof(g_partcmp_backup));
	memset(g_partcmp_array, 0, sizeof(g_partcmp_array));
	memset(g_partcmp_backup_array, 0, sizeof(g_partcmp_backup_array));
}

static IOMedia *
partcmp_add_whole(void)
{
	return add_media(true, 0,
	    (UInt64)g_self->capacityBlocks * GTA4L_EMMC_BLOCK_BYTES,
	    false, "GUID_partition_scheme", true);
}

static IOService *
partcmp_add_scheme_unregistered(IOMedia *whole, const char *uuid)
{
	IOGUIDPartitionScheme *scheme = new IOGUIDPartitionScheme();

	gta4l_test_scheme_configure(scheme, whole, uuid);
	gta4l_test_add_child(whole, scheme);
	gta4l_test_add_scheme(scheme);
	return scheme;
}

static IOService *
partcmp_add_scheme(IOMedia *whole, const char *uuid)
{
	IOService *scheme = partcmp_add_scheme_unregistered(whole, uuid);

	scheme->registerService();
	return scheme;
}

static void
partcmp_setup(uint32_t gpt_used, uint32_t media_count, int reverse,
    bool registered = true)
{
	uint32_t index;

	partcmp_reset_fixture();
	partcmp_build_fixture(gpt_used);
	g_partcmp_whole = partcmp_add_whole();
	g_partcmp_scheme = registered ?
	    partcmp_add_scheme(g_partcmp_whole, "GTA4L-TEST-SCHEME") :
	    partcmp_add_scheme_unregistered(g_partcmp_whole,
	    "GTA4L-TEST-SCHEME");
	for (index = 0; index < media_count; index++) {
		uint32_t at = (reverse != 0) ?
		    (media_count - 1u - index) : index;
		IOMedia *media = add_media(false,
		    partcmp_first(at) * GTA4L_EMMC_BLOCK_BYTES,
		    PARTCMP_PARTITION_SECTORS * GTA4L_EMMC_BLOCK_BYTES,
		    false, "EFI", true);

		media->provider_ = g_partcmp_scheme;
	}
	gta4l_test_media_read_hook = partcmp_read_hook;
}

static void
partcmp_cancel_sample_hook(uint32_t sample, uint32_t visible)
{
	(void)visible;
	if (sample == 1u) {
		g_self->reportCancel = true;
	}
}

static void
partcmp_watchdog_hook(uint32_t sample, uint32_t visible)
{
	(void)visible;
	if (sample == 1u) {
		(void)invoke_thread(1);
	}
}

static int
scenario_partcmp_refused_partobs(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-partcmp=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_partcmp_refused_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-partcmp=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "duplicate gta4l-emmc-partcmp") != 0);
	return 0;
}

static int
scenario_partcmp_refused_value(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=2");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed gta4l-emmc-partcmp") != 0);
	return 0;
}

static int
scenario_partcmp_refused_orphan(void)
{
	begin("gta4l-emmc-partcmp=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "gta4l-emmc-partcmp requires") != 0);
	return 0;
}

static int
scenario_partcmp_match(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 1);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PARTMAP: ") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=0 slots=84 used=80 "
	    "pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=6 changes=0 "
	    "stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 "
	    "budget=0 reads=25 blocks=45 fails=0 cancel=0 "
	    "complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "PARTOBS") == 0);
	CHECK(g_self->report.partmap_complete == 1);
	CHECK(gta4l_test.sleep_with_lock == 0);
	return 0;
}

static int
scenario_partcmp_wrong_range(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[gta4l_test_media_count - 1]->base_value +=
	    PARTCMP_PARTITION_SECTORS * GTA4L_EMMC_BLOCK_BYTES;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=11") != 0);
	CHECK(strstr(gta4l_test.panic, "media=80 match=0 missing=1 "
	    "extra=1 dup=0") != 0);
	CHECK(strstr(gta4l_test.panic, "stable=1 stable_ms=2000") != 0);
	CHECK(strstr(gta4l_test.panic, "timed_out=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[gta4l_test_media_count - 1]->base_value =
	    gta4l_test_media[1]->base_value;
	gta4l_test_media[gta4l_test_media_count - 1]->size_value =
	    gta4l_test_media[1]->size_value;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=11") != 0);
	CHECK(strstr(gta4l_test.panic, "media=80 match=0 missing=1 "
	    "extra=0 dup=1") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_nested(void)
{
	IOMedia *child;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	child = add_media(false, gta4l_test_media[1]->base_value,
	    gta4l_test_media[1]->size_value, false, "EFI", false);
	child->provider_ = g_partcmp_whole;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "media=80 match=1") != 0);
	CHECK(strstr(gta4l_test.panic, "nested=1") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=1") != 0);
	return 0;
}

static int
scenario_partcmp_zero(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 0, 0);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=11") != 0);
	CHECK(strstr(gta4l_test.panic, "media=0 match=0 missing=80 "
	    "extra=0 dup=0") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=41 changes=0 stable=1 "
	    "stable_ms=2000") != 0);
	CHECK(strstr(gta4l_test.panic, "timed_out=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_partial(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 39, 0);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=11") != 0);
	CHECK(strstr(gta4l_test.panic, "media=39 match=0 missing=41 "
	    "extra=0 dup=0") != 0);
	CHECK(strstr(gta4l_test.panic, "stable=1 stable_ms=2000") != 0);
	CHECK(strstr(gta4l_test.panic, "timed_out=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_late_growth(void)
{
	static const uint32_t profile[] = {
		0, 0, 0, 80, 80, 80, 80, 80, 80
	};

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	publish_profile(profile, 9);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "media=80 match=1") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=9 changes=1 stable=1 "
	    "stable_ms=250") != 0);
	CHECK(strstr(gta4l_test.panic, "timed_out=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=1") != 0);
	return 0;
}

static int
scenario_partcmp_changing(void)
{
	uint32_t profile[41];
	uint32_t index;

	for (index = 0; index < 41u; index++) {
		profile[index] = ((index % 2u) == 0u) ? 80u : 79u;
	}
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	publish_profile(profile, 41);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=12") != 0);
	CHECK(strstr(gta4l_test.panic, "match=0") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=41 changes=40 stable=0 "
	    "stable_ms=0") != 0);
	CHECK(strstr(gta4l_test.panic, "timed_out=1") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_property(void)
{
	static const uint32_t faults[] = {
		GTA4L_TEST_PROP_WHOLE_MISSING,
		GTA4L_TEST_PROP_BASE_MISSING,
		GTA4L_TEST_PROP_SIZE_MISSING,
		GTA4L_TEST_PROP_CONTENT_MISSING,
		GTA4L_TEST_PROP_WHOLE_WRONG,
		GTA4L_TEST_PROP_BASE_WRONG,
		GTA4L_TEST_PROP_SIZE_WRONG,
		GTA4L_TEST_PROP_CONTENT_WRONG
	};
	uint32_t index;

	for (index = 0; index < sizeof(faults) / sizeof(faults[0]);
	    index++) {
		begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
		CHECK(g_started);
		partcmp_setup(80, 80, 0);
		gta4l_test_media[1]->property_faults = faults[index];
		CHECK(invoke_thread(0) == 1);
		CHECK(strstr(gta4l_test.panic, "gpt=1 reason=10") != 0);
		CHECK(strstr(gta4l_test.panic, "prop=1") != 0);
		CHECK(strstr(gta4l_test.panic, "samples=1") != 0);
		CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	}

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[1]->size_value = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "prop=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[1]->base_value += 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "prop=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[1]->size_value += 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "prop=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test_media[1]->base_value =
	    (UInt64)g_self->capacityBlocks * GTA4L_EMMC_BLOCK_BYTES -
	    GTA4L_EMMC_BLOCK_BYTES;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "prop=1") != 0);
	return 0;
}

static int
scenario_partcmp_whole_no_base(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_whole->property_faults = GTA4L_TEST_PROP_BASE_MISSING;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 MEDIA: whole=1 base=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=0 slots=84 used=80 "
	    "pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=6 changes=0 "
	    "stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 "
	    "budget=0 reads=25 blocks=45 fails=0 cancel=0 "
	    "complete=1") != 0);
	CHECK(g_self->report.partmap_complete == 1);
	CHECK(gta4l_test.sleep_with_lock == 0);
	return 0;
}

static int
scenario_partcmp_whole_base_invalid(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_whole->base_value = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10 slots=0 used=0 "
	    "pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 "
	    "reads=0 blocks=0 fails=0 cancel=0 complete=0") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_whole->property_faults = GTA4L_TEST_PROP_BASE_WRONG;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10 slots=0 used=0 "
	    "pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 "
	    "reads=0 blocks=0 fails=0 cancel=0 complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_scheme_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 MEDIA: whole=1 base=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10 slots=0 used=0 "
	    "pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 "
	    "reads=0 blocks=0 fails=0 cancel=0 complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_scheme_unregistered(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 1, false);
	CHECK(gta4l_test.registered == 1);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PARTMAP: ") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=0 slots=84 used=80 "
	    "pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=6 changes=0 "
	    "stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 "
	    "budget=0 reads=25 blocks=45 fails=0 cancel=0 "
	    "complete=1") != 0);
	CHECK(g_self->report.partmap_complete == 1);
	CHECK(gta4l_test.sleep_with_lock == 0);
	return 0;
}

static int
scenario_partcmp_scheme_wrong_class(void)
{
	IOService *decoy;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	decoy = new IOService();
	gta4l_test_scheme_configure(decoy, g_partcmp_whole,
	    "GTA4L-TEST-DECOY");
	gta4l_test_add_child(g_partcmp_whole, decoy);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 "
	    "content=GUID_partition_scheme writable=0") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10 slots=0 used=0 "
	    "pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 "
	    "reads=0 blocks=0 fails=0 cancel=0 complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_scheme_no_uuid(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	(void)partcmp_add_scheme(g_partcmp_whole, 0);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=0 blocks=0 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_scheme_wrong_provider(void)
{
	IOMedia *other;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	other = add_media(false, 0, 4096, false, "EFI", true);
	(void)partcmp_add_scheme(other, "GTA4L-TEST-SCHEME");
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=0 blocks=0 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_scheme_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	(void)partcmp_add_scheme(g_partcmp_whole, "GTA4L-TEST-A");
	(void)partcmp_add_scheme(g_partcmp_whole, "GTA4L-TEST-B");
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=0 blocks=0 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_open_refused(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_whole->open_refuse = true;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 MEDIA: whole=1 base=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10 slots=0 used=0 "
	    "pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0 "
	    "missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 "
	    "reads=0 blocks=0 fails=0 cancel=0 complete=0") != 0);
	CHECK(g_partcmp_whole->open_calls == 1);
	return 0;
}

struct partcmp_invalid_case {
	uint32_t kind;
	uint32_t reason;
};

static int
scenario_partcmp_gpt_invalid(void)
{
	static const struct partcmp_invalid_case cases[] = {
		{ PARTCMP_CORRUPT_PMBR_SIG, 1u },
		{ PARTCMP_CORRUPT_PMBR_NONE, 1u },
		{ PARTCMP_CORRUPT_PMBR_TWO_EE, 1u },
		{ PARTCMP_CORRUPT_PMBR_START, 1u },
		{ PARTCMP_CORRUPT_PMBR_HYBRID, 1u },
		{ PARTCMP_CORRUPT_HDR_SIG, 2u },
		{ PARTCMP_CORRUPT_HDR_SIZE, 2u },
		{ PARTCMP_CORRUPT_HDR_REVISION, 2u },
		{ PARTCMP_CORRUPT_HDR_RESERVED, 2u },
		{ PARTCMP_CORRUPT_HDR_CRC, 2u },
		{ PARTCMP_CORRUPT_HDR_MY_LBA, 2u },
		{ PARTCMP_CORRUPT_HDR_ALT_LBA, 2u },
		{ PARTCMP_CORRUPT_HDR_USABLE, 2u },
		{ PARTCMP_CORRUPT_HDR_SPAN, 2u },
		{ PARTCMP_CORRUPT_ARR_CRC, 3u },
		{ PARTCMP_CORRUPT_BAK_SIG, 4u },
		{ PARTCMP_CORRUPT_BAK_ARR_CRC, 5u },
		{ PARTCMP_CORRUPT_DISAGREE_GUID, 6u },
		{ PARTCMP_CORRUPT_DISAGREE_ENTRY, 6u },
		{ PARTCMP_CORRUPT_ENTRY_REVERSED, 7u },
		{ PARTCMP_CORRUPT_ENTRY_OUTSIDE, 7u },
		{ PARTCMP_CORRUPT_ENTRY_OVERLAP, 7u },
		{ PARTCMP_CORRUPT_ENTRY_DUPLICATE, 7u },
		{ PARTCMP_CORRUPT_ENTRY_NULL_RANGE, 7u }
	};
	char expected[32];
	uint32_t index;

	for (index = 0; index < sizeof(cases) / sizeof(cases[0]);
	    index++) {
		begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
		CHECK(g_started);
		partcmp_setup(80, 80, 0);
		partcmp_corrupt(cases[index].kind);
		CHECK(invoke_thread(0) == 1);
		snprintf(expected, sizeof(expected), "gpt=0 reason=%u",
		    cases[index].reason);
		CHECK(strstr(gta4l_test.panic, expected) != 0);
		CHECK(strstr(gta4l_test.panic, "samples=0") != 0);
		CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	}
	return 0;
}

static int
scenario_partcmp_entry_variants(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 79, 0);
	partcmp_corrupt(PARTCMP_CORRUPT_EMPTY_STALE);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "slots=84 used=79") != 0);
	CHECK(strstr(gta4l_test.panic, "media=79 match=1") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 79, 0);
	partcmp_corrupt(PARTCMP_CORRUPT_SINGLE_SECTOR);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "slots=84 used=80") != 0);
	CHECK(strstr(gta4l_test.panic, "media=79 match=0 missing=1 "
	    "extra=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_geometry(void)
{
	static const uint32_t slot_cases[] = { 0u, 1u, 84u, 128u, 129u,
	    4096u, 0x02000000u };
	static const uint32_t slot_expected[] = { 0u, 1u, 1u, 1u, 0u, 0u,
	    0u };
	static const uint32_t size_cases[] = { 127u, 128u, 129u };
	static const uint32_t size_expected[] = { 0u, 1u, 0u };
	static const uint32_t header_cases[] = { 91u, 92u, 93u };
	static const uint32_t header_expected[] = { 0u, 1u, 0u };
	uint8_t header[GTA4L_EMMC_BLOCK_BYTES];
	uint64_t entry_lba;
	uint32_t slots;
	uint32_t capacity;
	uint32_t array_blocks;
	uint32_t index;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	capacity = g_partcmp_capacity;
	array_blocks = (PARTCMP_SLOTS * 128u + 511u) / 512u;

	partcmp_build_header(header, 1u, (uint64_t)capacity - 1u, 2u, 0u);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 1, &entry_lba,
	    &slots) == 1);
	CHECK(entry_lba == 2u);
	CHECK(slots == PARTCMP_SLOTS);

	partcmp_build_header(header, (uint64_t)capacity - 1u, 1u,
	    (uint64_t)capacity - 1u - array_blocks, 0u);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 0, &entry_lba,
	    &slots) == 1);
	CHECK(entry_lba == (uint64_t)capacity - 1u - array_blocks);
	CHECK(slots == PARTCMP_SLOTS);

	for (index = 0; index < sizeof(slot_cases) / sizeof(slot_cases[0]);
	    index++) {
		partcmp_build_header(header, 1u, (uint64_t)capacity - 1u,
		    2u, 0u);
		partcmp_put32(header + 80u, slot_cases[index]);
		partcmp_fix_header_crc(header);
		CHECK((uint32_t)gta4l_emmc_block_gpt_geometry(header,
		    capacity, 1, &entry_lba, &slots) == slot_expected[index]);
	}
	for (index = 0; index < sizeof(size_cases) / sizeof(size_cases[0]);
	    index++) {
		partcmp_build_header(header, 1u, (uint64_t)capacity - 1u,
		    2u, 0u);
		partcmp_put32(header + 84u, size_cases[index]);
		partcmp_fix_header_crc(header);
		CHECK((uint32_t)gta4l_emmc_block_gpt_geometry(header,
		    capacity, 1, &entry_lba, &slots) == size_expected[index]);
	}
	for (index = 0;
	    index < sizeof(header_cases) / sizeof(header_cases[0]);
	    index++) {
		partcmp_build_header(header, 1u, (uint64_t)capacity - 1u,
		    2u, 0u);
		partcmp_put32(header + 12u, header_cases[index]);
		partcmp_fix_header_crc(header);
		CHECK((uint32_t)gta4l_emmc_block_gpt_geometry(header,
		    capacity, 1, &entry_lba, &slots) == header_expected[index]);
	}

	partcmp_build_header(header, 1u, (uint64_t)capacity - 1u, 2u, 0u);
	CHECK(gta4l_emmc_block_gpt_geometry(header, 0, 1, &entry_lba,
	    &slots) == 0);
	CHECK(gta4l_emmc_block_gpt_geometry(header, 1, 1, &entry_lba,
	    &slots) == 0);
	CHECK(gta4l_emmc_block_gpt_geometry(header, 2, 1, &entry_lba,
	    &slots) == 0);

	partcmp_build_header(header, (uint64_t)capacity - 1u, 1u,
	    (uint64_t)capacity - 1u - array_blocks, 0u);
	partcmp_put64(header + 72u, (uint64_t)capacity);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 0, &entry_lba,
	    &slots) == 0);
	partcmp_build_header(header, (uint64_t)capacity - 1u, 1u,
	    (uint64_t)capacity - 1u - array_blocks, 0u);
	partcmp_put64(header + 72u, (uint64_t)capacity - 1u);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 0, &entry_lba,
	    &slots) == 0);

	partcmp_build_header(header, 1u, (uint64_t)capacity - 1u, 2u, 0u);
	partcmp_put64(header + 72u, PARTCMP_FIRST_USABLE - array_blocks + 1u);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 1, &entry_lba,
	    &slots) == 0);
	partcmp_build_header(header, 1u, (uint64_t)capacity - 1u, 2u, 0u);
	partcmp_put64(header + 72u, 0u);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 1, &entry_lba,
	    &slots) == 0);
	partcmp_build_header(header, 1u, (uint64_t)capacity - 1u, 2u, 0u);
	partcmp_put64(header + 72u, 1u);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 1, &entry_lba,
	    &slots) == 0);

	partcmp_build_header(header, (uint64_t)capacity - 1u, 1u,
	    (uint64_t)capacity - 1u - array_blocks, 0u);
	partcmp_put64(header + 72u, (uint64_t)capacity - 34u);
	partcmp_fix_header_crc(header);
	CHECK(gta4l_emmc_block_gpt_geometry(header, capacity, 0, &entry_lba,
	    &slots) == 0);
	return 0;
}

static int
scenario_partcmp_media_invalid(void)
{
	uint32_t index;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	for (index = 0; index < 80u; index++) {
		add_media(false,
		    partcmp_first(index) * GTA4L_EMMC_BLOCK_BYTES,
		    PARTCMP_PARTITION_SECTORS * GTA4L_EMMC_BLOCK_BYTES,
		    false, "EFI", true);
	}
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	(void)partcmp_add_whole();
	(void)partcmp_add_whole();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	for (index = 0; index < 80u; index++) {
		IOMedia *media = add_media(false,
		    partcmp_first(index) * GTA4L_EMMC_BLOCK_BYTES,
		    PARTCMP_PARTITION_SECTORS * GTA4L_EMMC_BLOCK_BYTES,
		    false, "EFI", true);

		media->provider_ = g_partcmp_whole;
	}
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_reset_fixture();
	partcmp_build_fixture(80);
	g_partcmp_whole = partcmp_add_whole();
	(void)partcmp_add_scheme(g_partcmp_whole, "GTA4L-TEST-A");
	(void)partcmp_add_scheme(g_partcmp_whole, "GTA4L-TEST-B");
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=10") != 0);
	return 0;
}

static int
scenario_partcmp_read_failure(void)
{
	char expected[32];
	uint32_t index;

	for (index = 0; index < 25u; index++) {
		begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
		CHECK(g_started);
		partcmp_setup(80, 80, 0);
		g_partcmp_fail_at = (int)index;
		CHECK(invoke_thread(0) == 1);
		CHECK(strstr(gta4l_test.panic, "reason=8") != 0);
		CHECK(strstr(gta4l_test.panic, "fails=1") != 0);
		snprintf(expected, sizeof(expected), "reads=%u", index + 1u);
		CHECK(strstr(gta4l_test.panic, expected) != 0);
		CHECK(strstr(gta4l_test.panic, "samples=0") != 0);
		CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	}
	return 0;
}

static int
scenario_partcmp_budget(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_advance_ms = 1000u;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=9") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=4") != 0);
	CHECK(strstr(gta4l_test.panic, "fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	return 0;
}

static int
scenario_partcmp_cancel(void)
{
	static const uint32_t profile[] = { 80 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.partmap_cancel == 1);
	CHECK(g_self->report.partmap_complete == 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_cancel_at = 5;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->reportDone);
	CHECK(g_self->report.partmap_cancel == 1);
	CHECK(g_self->report.partmap_reads == 6);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	publish_profile(profile, 1);
	gta4l_test_partition_sample_hook = partcmp_cancel_sample_hook;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->reportDone);
	CHECK(g_self->report.partmap_cancel == 1);
	CHECK(g_self->report.partmap_samples == 2);
	return 0;
}

static int
scenario_partcmp_watchdog(void)
{
	static const uint32_t profile[] = { 80 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	publish_profile(profile, 1);
	gta4l_test_partition_sample_hook = partcmp_watchdog_hook;
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT: rc=16 "
	    "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PARTMAP: ") != 0);
	return 0;
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
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART: ") != 0);
	CHECK(strstr(gta4l_test.panic, "PARTOBS") == 0);
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
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART: ") != 0);
	CHECK(strstr(gta4l_test.panic, "PARTOBS") == 0);
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
scenario_partobs_incremental(void)
{
	static const uint32_t profile[] = { 0, 1, 5, 22, 80, 84, 84 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(84);
	publish_profile(profile, 7);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PARTOBS: ") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=11 first=0 first_ms=0 "
	    "first_nz=1 first_nz_ms=50 final=84 final_ms=500 changes=5 "
	    "stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 "
	    "error=0 slots=84 reported=3 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART1:") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART: ") == 0);
	CHECK(gta4l_test.part_publish_calls == 11);
	CHECK(gta4l_test.sleep_with_lock == 0);
	return 0;
}

static int
scenario_partobs_delayed(void)
{
	static const uint32_t profile[] = { 0, 0, 0, 0, 0, 0, 84 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(84);
	publish_profile(profile, 7);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=12 first=0 first_ms=0 "
	    "first_nz=84 first_nz_ms=300 final=84 final_ms=550 changes=1 "
	    "stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 "
	    "error=0 slots=84 reported=3 complete=0") != 0);
	return 0;
}

static int
scenario_partobs_stable_partial(void)
{
	static const uint32_t profile[] = { 0, 22, 22, 22, 22, 22, 22, 84 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(84);
	publish_profile(profile, 8);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=7 first=0 first_ms=0 "
	    "first_nz=22 first_nz_ms=50 final=22 final_ms=300 changes=1 "
	    "stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 "
	    "error=0 slots=84 reported=3 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "final=84") == 0);
	CHECK(gta4l_test.part_publish_calls == 7);
	return 0;
}

static int
scenario_partobs_late_growth(void)
{
	static const uint32_t profile[] = { 0, 22, 22, 22, 84, 84 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(84);
	publish_profile(profile, 6);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=10 first=0 first_ms=0 "
	    "first_nz=22 first_nz_ms=50 final=84 final_ms=450 changes=2 "
	    "stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 "
	    "error=0 slots=84 reported=3 complete=0") != 0);
	return 0;
}

static int
scenario_partobs_none(void)
{
	static const uint32_t profile[] = { 0 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	publish_profile(profile, 1);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=81 first=0 first_ms=0 "
	    "first_nz=0 first_nz_ms=0 final=0 final_ms=4000 changes=0 "
	    "stable=0 stable_ms=4000 deadline_ms=4000 timed_out=1 none=1 "
	    "error=0 slots=84 reported=0 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	CHECK(gta4l_test.sleep_with_lock == 0);
	return 0;
}

static int
scenario_partobs_changing(void)
{
	uint32_t index;

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(2);
	for (index = 0; index < 81; index++) {
		gta4l_test.part_publish_counts[index] =
		    ((index % 2u) == 0u) ? 1u : 2u;
	}
	gta4l_test.part_publish_count = 81;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=81 first=1 first_ms=0 "
	    "first_nz=1 first_nz_ms=0 final=1 final_ms=4000 changes=80 "
	    "stable=0 stable_ms=0 deadline_ms=4000 timed_out=1 none=0 "
	    "error=0 slots=84 reported=1 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=0") != 0);
	return 0;
}

static int
scenario_partobs_error(void)
{
	static const uint32_t profile[] = { 0, 5, 84, 84 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(5);
	publish_profile(profile, 4);
	gta4l_test.part_publish_error_at = 2;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "samples=3 first=0 first_ms=0 "
	    "first_nz=5 first_nz_ms=50 final=5 final_ms=50 changes=1 "
	    "stable=0 stable_ms=0 deadline_ms=4000 timed_out=0 none=0 "
	    "error=1 slots=84 reported=3 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "stop=15") != 0);
	CHECK(gta4l_test.part_publish_errors == 1);
	return 0;
}

static int
scenario_partobs_cancel(void)
{
	static const uint32_t profile[] = { 0 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(1);
	publish_profile(profile, 1);
	gta4l_test_partition_sample_hook = partobs_cancel_hook;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(gta4l_test.part_publish_calls == 2);
	return 0;
}

static int
scenario_partobs_watchdog(void)
{
	static const uint32_t profile[] = { 0 };

	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1");
	CHECK(g_started);
	add_media(true, 0, (UInt64)g_self->capacityBlocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	add_partition_media(1);
	publish_profile(profile, 1);
	gta4l_test_partition_sample_hook = partobs_watchdog_hook;
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PARTOBS: ") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=1 first=0 first_ms=0 "
	    "first_nz=0 first_nz_ms=0 final=0 final_ms=0 changes=0 "
	    "stable=0 stable_ms=0 deadline_ms=4000 timed_out=0 none=0 "
	    "error=0 slots=84 reported=0 complete=0") != 0);
	return 0;
}

static int
scenario_policy(void)
{
	char buffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	char legacy[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	char small[64];
	struct gta4l_emmc_block_report report;
	const char *line;
	const char *end;
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
	CHECK(gta4l_emmc_block_partobs_select(0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_OFF);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 0, 1) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_ON);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 1, 1) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partobs_select(1, 2, 0, 1) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_OFF);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_ON);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 1, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 2, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 1, 1) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
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

	report.part_obs = 1;
	report.part_obs_samples = 81;
	report.part_obs_first = 0xffffffffu;
	report.part_obs_first_ms = 0xffffffffu;
	report.part_obs_first_nz = 0xffffffffu;
	report.part_obs_first_nz_ms = 0xffffffffu;
	report.part_obs_final = 0xffffffffu;
	report.part_obs_final_ms = 0xffffffffu;
	report.part_obs_changes = 0xffffffffu;
	report.part_obs_stable = 1;
	report.part_obs_stable_ms = 0xffffffffu;
	report.part_obs_deadline_ms = 4000;
	report.part_obs_timed_out = 1;
	report.part_obs_none = 0;
	report.part_obs_error = 0;
	report.part_obs_slots = 1024;
	report.part_obs_reported = GTA4L_EMMC_BLOCK_PART_REPORT;
	report.part_obs_complete = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES - 128);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 PARTOBS: ") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 PART: ") == 0);
	CHECK(strstr(buffer, "complete=0") != 0);

	report.part_obs = 0;
	report.partmap = 0;
	report.partmap_gpt = 0;
	report.partmap_reason = 0;
	report.partmap_slots = 0;
	report.partmap_used = 0;
	report.partmap_pmbr = 0;
	report.partmap_hdr = 0;
	report.partmap_arr = 0;
	report.partmap_bak = 0;
	report.partmap_xcheck = 0;
	report.partmap_media = 0;
	report.partmap_match = 0;
	report.partmap_missing = 0;
	report.partmap_extra = 0;
	report.partmap_dup = 0;
	report.partmap_nested = 0;
	report.partmap_prop = 0;
	report.partmap_samples = 0;
	report.partmap_changes = 0;
	report.partmap_stable = 0;
	report.partmap_stable_ms = 0;
	report.partmap_deadline_ms = 0;
	report.partmap_timed_out = 0;
	report.partmap_budget = 0;
	report.partmap_reads = 0;
	report.partmap_blocks = 0;
	report.partmap_fails = 0;
	report.partmap_cancel = 0;
	report.partmap_complete = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	memcpy(legacy, buffer, (size_t)length + 1u);
	report.partmap_gpt = 1u;
	report.partmap_reason = 13u;
	report.partmap_slots = 128u;
	report.partmap_used = 128u;
	report.partmap_pmbr = 1u;
	report.partmap_hdr = 1u;
	report.partmap_arr = 1u;
	report.partmap_bak = 1u;
	report.partmap_xcheck = 1u;
	report.partmap_media = 128u;
	report.partmap_match = 1u;
	report.partmap_missing = 128u;
	report.partmap_extra = 128u;
	report.partmap_dup = 128u;
	report.partmap_nested = 1u;
	report.partmap_prop = 255u;
	report.partmap_samples = 41u;
	report.partmap_changes = 41u;
	report.partmap_stable = 1u;
	report.partmap_stable_ms = 2000u;
	report.partmap_deadline_ms = 2000u;
	report.partmap_timed_out = 1u;
	report.partmap_budget = 1u;
	report.partmap_reads = 25u;
	report.partmap_blocks = 45u;
	report.partmap_fails = 1u;
	report.partmap_cancel = 1u;
	report.partmap_complete = 1u;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strcmp(legacy, buffer) == 0);
	CHECK(strstr(buffer, "PARTMAP") == 0);

	report.partmap = 1;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 PARTMAP: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_PARTMAP_LINE_BYTES);
	CHECK(strstr(buffer, "GUID") == 0);
	CHECK(strstr(buffer, "label") == 0);
	CHECK(gta4l_emmc_block_report_format(small, sizeof(small),
	    &report) == 0);
	CHECK(small[0] == 0);
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
	{ "partobs-incremental", scenario_partobs_incremental },
	{ "partobs-delayed", scenario_partobs_delayed },
	{ "partobs-stable-partial", scenario_partobs_stable_partial },
	{ "partobs-late-growth", scenario_partobs_late_growth },
	{ "partobs-none", scenario_partobs_none },
	{ "partobs-changing", scenario_partobs_changing },
	{ "partobs-error", scenario_partobs_error },
	{ "partobs-cancel", scenario_partobs_cancel },
	{ "partobs-watchdog", scenario_partobs_watchdog },
	{ "partcmp-refused-partobs", scenario_partcmp_refused_partobs },
	{ "partcmp-refused-duplicate", scenario_partcmp_refused_duplicate },
	{ "partcmp-refused-value", scenario_partcmp_refused_value },
	{ "partcmp-refused-orphan", scenario_partcmp_refused_orphan },
	{ "partcmp-match", scenario_partcmp_match },
	{ "partcmp-wrong-range", scenario_partcmp_wrong_range },
	{ "partcmp-duplicate", scenario_partcmp_duplicate },
	{ "partcmp-nested", scenario_partcmp_nested },
	{ "partcmp-zero", scenario_partcmp_zero },
	{ "partcmp-partial", scenario_partcmp_partial },
	{ "partcmp-late-growth", scenario_partcmp_late_growth },
	{ "partcmp-changing", scenario_partcmp_changing },
	{ "partcmp-property", scenario_partcmp_property },
	{ "partcmp-whole-no-base", scenario_partcmp_whole_no_base },
	{ "partcmp-whole-base-invalid", scenario_partcmp_whole_base_invalid },
	{ "partcmp-scheme-absent", scenario_partcmp_scheme_absent },
	{ "partcmp-scheme-unregistered", scenario_partcmp_scheme_unregistered },
	{ "partcmp-scheme-wrong-class", scenario_partcmp_scheme_wrong_class },
	{ "partcmp-scheme-no-uuid", scenario_partcmp_scheme_no_uuid },
	{ "partcmp-scheme-wrong-provider",
	    scenario_partcmp_scheme_wrong_provider },
	{ "partcmp-scheme-duplicate", scenario_partcmp_scheme_duplicate },
	{ "partcmp-open-refused", scenario_partcmp_open_refused },
	{ "partcmp-gpt-invalid", scenario_partcmp_gpt_invalid },
	{ "partcmp-entry-variants", scenario_partcmp_entry_variants },
	{ "partcmp-geometry", scenario_partcmp_geometry },
	{ "partcmp-media-invalid", scenario_partcmp_media_invalid },
	{ "partcmp-read-failure", scenario_partcmp_read_failure },
	{ "partcmp-budget", scenario_partcmp_budget },
	{ "partcmp-cancel", scenario_partcmp_cancel },
	{ "partcmp-watchdog", scenario_partcmp_watchdog },
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
