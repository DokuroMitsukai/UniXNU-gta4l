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
			printf("  panic: %s\n", gta4l_test.panic); \
			return 1; \
		} \
	} while (0)

static uint32_t report_line_bytes(const char *buffer, const char *label);

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
begin_with(const char *arguments, void (*configure)(void))
{
	gta4l_test_reset();
	gta4l_test.boot_args = arguments;
	if (configure != 0) {
		configure();
	}
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
begin(const char *arguments)
{
	return begin_with(arguments, 0);
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
static int g_partcmp_short_bytes;
static int g_partcmp_cancel_at;
static int g_partcmp_watchdog_at;
static int g_partcmp_enforce_breakup;
static int g_partcmp_breakup_rejects;
static int g_partcmp_unaligned_reads;
static uint32_t g_partcmp_advance_ms;
static IOMedia *g_partcmp_whole;
static IOService *g_partcmp_scheme;

/* Worst-case model of IOBlockStorageDriver::breakUpRequest() under the
 * advertised one-segment read limit: each virtual page is a separate
 * physical segment, only the first segment is taken, and the break size is
 * truncated to the 512-byte request block size. A zero break size returns
 * kIOReturnDMAError before the adapter sees the request. */
static bool
partcmp_breakup_rejects(IOMemoryDescriptor *buffer)
{
	uintptr_t address = (uintptr_t)buffer->address();
	uint32_t length = (uint32_t)buffer->getLength();
	uint32_t offset = 0;

	while (offset < length) {
		uint32_t segment = (uint32_t)(PAGE_SIZE -
		    ((address + offset) & (PAGE_SIZE - 1u)));
		uint32_t break_size;

		if (segment > (length - offset)) {
			segment = length - offset;
		}
		break_size = segment &
		    ~(uint32_t)(GTA4L_EMMC_BLOCK_BYTES - 1u);
		if (break_size == 0u) {
			return true;
		}
		offset += break_size;
	}
	return false;
}

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
	if ((g_partcmp_watchdog_at >= 0) &&
	    (call == (uint32_t)g_partcmp_watchdog_at)) {
		(void)invoke_thread(1);
	}
	if ((g_partcmp_fail_at >= 0) &&
	    (call == (uint32_t)g_partcmp_fail_at)) {
		return kIOReturnDMAError;
	}
	if ((g_partcmp_cancel_at >= 0) &&
	    (call == (uint32_t)g_partcmp_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((((uintptr_t)buffer->address()) & (PAGE_SIZE - 1u)) != 0u) {
		g_partcmp_unaligned_reads++;
	}
	if (g_partcmp_enforce_breakup && partcmp_breakup_rejects(buffer)) {
		g_partcmp_breakup_rejects++;
		return kIOReturnDMAError;
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
		if ((g_partcmp_short_at >= 0) &&
		    (call == (uint32_t)g_partcmp_short_at)) {
			*actualByteCount = (g_partcmp_short_bytes >= 0) ?
			    (UInt64)g_partcmp_short_bytes :
			    (UInt64)(length - GTA4L_EMMC_BLOCK_BYTES);
		} else {
			*actualByteCount = (UInt64)length;
		}
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
	g_partcmp_short_bytes = -1;
	g_partcmp_cancel_at = -1;
	g_partcmp_watchdog_at = -1;
	g_partcmp_enforce_breakup = 0;
	g_partcmp_breakup_rejects = 0;
	g_partcmp_unaligned_reads = 0;
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
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=0 lba=0 blocks=0 bytes=0 "
	    "status=0 actual=0 desc_len=0 desc_dir=0") != 0);
	CHECK(g_partcmp_whole->open_calls == 1);
	return 0;
}

static int
scenario_partcmp_descriptor_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	gta4l_test.descriptor_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "gpt=0 reason=10") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=0 blocks=0 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=0 lba=0 blocks=0 bytes=0 "
	    "status=0 actual=0 desc_len=0 desc_dir=0") != 0);
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
		CHECK(strstr(gta4l_test.panic, "state=2") != 0);
		CHECK(strstr(gta4l_test.panic, "status=3758097108") != 0);
		CHECK(strstr(gta4l_test.panic, "actual=0") != 0);
		CHECK(strstr(gta4l_test.panic, "desc_dir=1") != 0);
		if (index == 0u) {
			CHECK(strstr(gta4l_test.panic,
			    "GTA4L-EMMC P5 READDETAIL: state=2 lba=0 "
			    "blocks=1 bytes=512 status=3758097108 actual=0 "
			    "desc_len=512 desc_dir=1") != 0);
		}
	}
	return 0;
}

static int
scenario_partcmp_read_short(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_short_at = 0;
	g_partcmp_short_bytes = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=8") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=1") != 0);
	CHECK(strstr(gta4l_test.panic, "fails=1") != 0);
	CHECK(strstr(gta4l_test.panic, "samples=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=3 lba=0 blocks=1 bytes=512 "
	    "status=0 actual=0 desc_len=512 desc_dir=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_short_at = 0;
	g_partcmp_short_bytes = 511;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=8") != 0);
	CHECK(strstr(gta4l_test.panic, "state=3") != 0);
	CHECK(strstr(gta4l_test.panic, "status=0") != 0);
	CHECK(strstr(gta4l_test.panic, "actual=511") != 0);
	CHECK(strstr(gta4l_test.panic, "bytes=512") != 0);
	CHECK(strstr(gta4l_test.panic, "desc_len=512") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_short_at = 2;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "reason=8") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=3") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=3 lba=2 blocks=2 bytes=1024 "
	    "status=0 actual=512 desc_len=1024 desc_dir=1") != 0);
	return 0;
}

static int
scenario_partcmp_read_inflight(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 0);
	g_partcmp_watchdog_at = 0;
	(void)invoke_thread(0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=1 lba=0 blocks=1 bytes=512 "
	    "status=0 actual=0 desc_len=512 desc_dir=1") != 0);
	return 0;
}

static int
scenario_partcmp_page_aligned(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 1);
	g_partcmp_enforce_breakup = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(g_partcmp_breakup_rejects == 0);
	CHECK(g_partcmp_unaligned_reads == 0);
	CHECK(g_partcmp_read_calls == 25);
	CHECK(strstr(gta4l_test.panic, "gpt=1 reason=0") != 0);
	CHECK(strstr(gta4l_test.panic, "reads=25 blocks=45 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic, "complete=1") != 0);
	return 0;
}

static int
scenario_partcmp_page_split(void)
{
	static uint8_t page[PAGE_SIZE * 2u] __attribute__((aligned(PAGE_SIZE)));
	uint8_t *straddle = page + 3608u;

	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1");
	CHECK(g_started);
	partcmp_setup(80, 80, 1);
	g_partcmp_enforce_breakup = 1;
	g_self->partmapReadStart = gta4l_emmc_platform_counter();
	CHECK(g_partcmp_whole->open(g_self, 0, kIOStorageAccessReader));
	memset(straddle, 0, GTA4L_EMMC_BLOCK_BYTES);
	CHECK(g_self->readPartmapSectors(g_partcmp_whole, 0, 1,
	    straddle) == 0);
	g_partcmp_whole->close(g_self, 0);
	CHECK(g_partcmp_breakup_rejects == 0);
	CHECK(g_partcmp_unaligned_reads == 0);
	CHECK(memcmp(straddle, g_partcmp_pmbr, GTA4L_EMMC_BLOCK_BYTES) == 0);
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
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READDETAIL: state=4") != 0);
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
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(g_self->block.counters.requests == 1);
	CHECK(g_self->block.counters.blocks == 5);
	CHECK(g_self->block.counters.requested_blocks == 5);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(g_self->block.counters.cmd18_blocks == 5);
	CHECK(g_self->block.counters.cmd17_reads == 0);
	CHECK(g_self->block.counters.stop_attempts == 1);
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
scenario_chunked_three(void)
{
	IOReturn status;

	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	status = issue_read(2, 3, kIODirectionIn, 1536, false);
	CHECK(status == kIOReturnSuccess);
	CHECK(g_completion.status == kIOReturnSuccess);
	CHECK(g_completion.actual == 1536);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(g_self->block.counters.cmd18_blocks == 3);
	CHECK(g_self->block.counters.cmd17_reads == 0);
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
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 1);
	CHECK(g_self->block.counters.blocks == 21);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(g_self->block.counters.cmd18_blocks == 21);
	CHECK(g_self->block.counters.cmd17_reads == 0);
	CHECK(g_self->block.counters.stop_attempts == 1);
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
	    "c17=0 stops=1 sfail=0") != 0);
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
	gta4l_test.multi_corrupt_at = 1;
	gta4l_test.multi_corrupt_block = 4;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "two_att=1 two_ok=1 "
	    "two_reason=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3: att=1 ok=0 "
	    "reason=3 bytes=1536 mismatch=1 off=1024 match=1535 c18=1 "
	    "c17=0 stops=1 sfail=0") != 0);
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

static int g_partread_read_calls;
static int g_partread_fail_at = -1;
static int g_partread_short_at = -1;
static int g_partread_corrupt_at = -1;
static int g_partread_cancel_at = -1;
static uint32_t g_partread_advance_ms;
static int g_partread_unaligned_reads;
static IOMedia *g_partread_whole_media;
static IOMedia *g_partread_boot_media;

static uint8_t
partread_pattern_byte(uint64_t absolute_byte)
{
	uint64_t lba = absolute_byte / GTA4L_EMMC_BLOCK_BYTES;
	uint32_t offset = (uint32_t)(absolute_byte %
	    GTA4L_EMMC_BLOCK_BYTES);

	return (uint8_t)(((lba * 131u) + offset) & 0xffu);
}

static void
partread_expected_for_case(
    const struct gta4l_emmc_block_partread_case *item, uint8_t digest[32])
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES];
	uint64_t start;
	uint64_t absolute;
	uint32_t length;
	uint32_t index;

	if (item->target == GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE) {
		start = (item->from_end_blocks != 0u) ?
		    ((uint64_t)gta4l_test.capacity_blocks -
		    item->from_end_blocks) * GTA4L_EMMC_BLOCK_BYTES :
		    item->offset;
	} else {
		start = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
		    item->offset;
	}
	length = item->blocks * GTA4L_EMMC_BLOCK_BYTES;
	for (index = 0; index < length; index++) {
		absolute = start + index;
		scratch[index] = partread_pattern_byte(absolute);
	}
	gta4l_emmc_block_sha256(scratch, length, digest);
}

static IOReturn
partread_read_hook(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount)
{
	uint8_t *destination = (uint8_t *)buffer->address();
	uint32_t length = (uint32_t)buffer->getLength();
	uint64_t absolute = media->base_value + byteStart;
	uint32_t call = (uint32_t)g_partread_read_calls++;
	uint32_t index;

	if ((((uintptr_t)destination) & (PAGE_SIZE - 1u)) != 0u) {
		g_partread_unaligned_reads++;
	}
	if (g_partread_advance_ms != 0u) {
		gta4l_test.now_ms += g_partread_advance_ms;
	}
	if ((g_partread_cancel_at >= 0) &&
	    (call == (uint32_t)g_partread_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((g_partread_fail_at >= 0) &&
	    (call == (uint32_t)g_partread_fail_at)) {
		return kIOReturnIOError;
	}
	for (index = 0; index < length; index++) {
		destination[index] = partread_pattern_byte(absolute + index);
	}
	if ((g_partread_corrupt_at >= 0) &&
	    (call == (uint32_t)g_partread_corrupt_at)) {
		destination[length / 2u] ^= 0x5au;
	}
	if (actualByteCount != 0) {
		*actualByteCount = ((g_partread_short_at >= 0) &&
		    (call == (uint32_t)g_partread_short_at)) ?
		    (length - GTA4L_EMMC_BLOCK_BYTES) : length;
	}
	return kIOReturnSuccess;
}

static void
partread_reset_fixture(void)
{
	g_partread_read_calls = 0;
	g_partread_fail_at = -1;
	g_partread_short_at = -1;
	g_partread_corrupt_at = -1;
	g_partread_cancel_at = -1;
	g_partread_advance_ms = 0u;
	g_partread_unaligned_reads = 0;
	g_partread_whole_media = 0;
	g_partread_boot_media = 0;
}

static void
partread_setup(bool withWhole, bool withBoot)
{
	uint32_t index;

	partread_reset_fixture();
	gta4l_test_media_read_hook = partread_read_hook;
	if (withWhole) {
		g_partread_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_partread_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
	for (index = 0; index < gta4l_emmc_block_partread_case_count();
	    index++) {
		struct gta4l_emmc_block_partread_case *item =
		    gta4l_emmc_block_partread_case_mutable(index);

		partread_expected_for_case(item, item->expected);
	}
}

static void
partread_digest_hex(const uint8_t *digest, char *text)
{
	static const char hex[] = "0123456789abcdef";
	uint32_t index;

	for (index = 0; index < 16u; index++) {
		text[index * 2u] = hex[digest[index] >> 4];
		text[index * 2u + 1u] = hex[digest[index] & 0x0fu];
	}
	text[32] = 0;
}

static int
scenario_partread_refused_orphan(void)
{
	begin("gta4l-emmc-partread=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "gta4l-emmc-partread requires") != 0);
	return 0;
}

static int
scenario_partread_refused_value(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=2");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed gta4l-emmc-partread") != 0);
	return 0;
}

static int
scenario_partread_refused_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1 "
	    "gta4l-emmc-partread=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "duplicate gta4l-emmc-partread") != 0);
	return 0;
}

static int
scenario_partread_refused_partobs(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-partread=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_partread_refused_partcmp(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-partread=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_partread_match(void)
{
	const struct gta4l_emmc_block_partread_case *last;
	char digest[33];
	char expected[160];

	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 PARTREAD: cases=14 passed=14 failed=0 reason=0 "
	    "complete=1 reads=14 blocks=334 bytes=171008 fails=0 cancel=0 "
	    "budget=0 media=0 part=0 cap=61071360") != 0);
	last = gta4l_emmc_block_partread_case_at(13);
	CHECK(last != 0);
	partread_digest_hex(last->expected, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 READTEST: index=13 target=1 blocks=64 "
	    "off=1048576 ok=1 status=0 actual=32768 digest=%s", digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READFAIL: index=0 reason=0 status=0 actual=0 "
	    "digest=00000000000000000000000000000000") != 0);
	CHECK(g_partread_unaligned_reads == 0);
	CHECK(g_partread_whole_media != 0);
	CHECK(g_partread_whole_media->open_calls == 1);
	CHECK(g_partread_whole_media->close_calls == 1);
	CHECK(g_partread_boot_media != 0);
	CHECK(g_partread_boot_media->open_calls == 1);
	CHECK(g_partread_boot_media->close_calls == 1);
	return 0;
}

static int
scenario_partread_hash_mismatch(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_corrupt_at = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=13 failed=1 reason=7 complete=0 reads=14 "
	    "blocks=334 bytes=171008 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READFAIL: index=0 reason=7 status=0 "
	    "actual=17408") != 0);
	CHECK(strstr(gta4l_test.panic, "digest=00000000000000000000000000000000")
	    == 0);
	return 0;
}

static int
scenario_partread_read_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_fail_at = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=13 failed=1 reason=5 complete=0 reads=14 "
	    "blocks=334 bytes=153600 fails=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READFAIL: index=1 reason=5 "
	    "status=3758097098 actual=0") != 0);
	return 0;
}

static int
scenario_partread_short_read(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_short_at = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=13 failed=1 reason=6 complete=0 reads=14 "
	    "blocks=334 bytes=170496 fails=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READFAIL: index=1 reason=6 status=0 "
	    "actual=16896") != 0);
	return 0;
}

static int
scenario_partread_media_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(false, false);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=0 failed=0 reason=1 complete=0 reads=0 "
	    "blocks=0 bytes=0 fails=0 cancel=0 budget=0 media=1 part=0") != 0);
	return 0;
}

static int
scenario_partread_part_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, false);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=8 failed=0 reason=2 complete=0 reads=8 "
	    "blocks=136 bytes=69632 fails=0 cancel=0 budget=0 media=0 "
	    "part=1") != 0);
	return 0;
}

static int
scenario_partread_open_refused(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_whole_media->open_refuse = true;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=0 failed=0 reason=3 complete=0 reads=0 "
	    "blocks=0 bytes=0 fails=0") != 0);
	return 0;
}

static int
scenario_partread_descriptor_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=0 failed=14 reason=4 complete=0 reads=0 "
	    "blocks=0 bytes=0 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 READFAIL: index=0 reason=4 status=0 actual=0") != 0);
	return 0;
}

static int
scenario_partread_budget(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_advance_ms = 5000u;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=14 passed=1 failed=0 reason=8 complete=0 reads=1 "
	    "blocks=34 bytes=17408 fails=0 cancel=0 budget=1") != 0);
	return 0;
}

static int
scenario_partread_cancel(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_partread_cancel_at = 0;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.partread_cancel == 1);
	CHECK(g_self->report.partread_complete == 0);
	CHECK(g_self->report.partread_reads == 1);
	CHECK(g_partread_whole_media->close_calls == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	partread_setup(true, true);
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->report.partread_cancel == 1);
	CHECK(g_self->report.partread_reads == 0);
	return 0;
}

static int g_deblock_fail_at = -1;
static int g_deblock_short_at = -1;
static int g_deblock_corrupt_at = -1;
static int g_deblock_cancel_at = -1;
static int g_deblock_watchdog_at = -1;
static uint32_t g_deblock_advance_ms;
static IOMedia *g_deblock_whole_media;
static IOMedia *g_deblock_boot_media;

static void
deblock_fill_sector(uint64_t block, uint8_t *bytes)
{
	uint32_t index;

	memset(bytes, 0, GTA4L_EMMC_BLOCK_BYTES);
	if (block == GTA4L_EMMC_BLOCK_REPORT_LBA) {
		memcpy(bytes, "EFI PART", 8);
		bytes[0x50] = 84;
		bytes[0x54] = 128;
		return;
	}
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		bytes[index] = (uint8_t)(block + index);
	}
}

static void
deblock_expected_for_case(
    const struct gta4l_emmc_block_deblock_case *item, uint8_t digest[32])
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES];
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint64_t absolute;
	uint64_t block;
	uint64_t last_block = ~0ull;
	uint64_t position;
	uint32_t length;
	uint32_t index;
	uint32_t offset;

	absolute = (item->target == GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT) ?
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES + item->start :
	    item->start;
	length = item->length;
	for (index = 0; index < length; index++) {
		position = absolute + index;
		block = position / GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(position % GTA4L_EMMC_BLOCK_BYTES);
		if (block != last_block) {
			deblock_fill_sector(block, sector);
			last_block = block;
		}
		scratch[index] = sector[offset];
	}
	gta4l_emmc_block_sha256(scratch, length, digest);
}

static void
deblock_hook(uint32_t call, uint32_t phase)
{
	if ((phase == 2u) && (g_deblock_cancel_at >= 0) &&
	    (call == (uint32_t)g_deblock_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((phase == 0u) && (g_deblock_watchdog_at >= 0) &&
	    (call == (uint32_t)g_deblock_watchdog_at)) {
		(void)invoke_thread(1);
	}
}

static void
deblock_reset_fixture(void)
{
	g_deblock_fail_at = -1;
	g_deblock_short_at = -1;
	g_deblock_corrupt_at = -1;
	g_deblock_cancel_at = -1;
	g_deblock_watchdog_at = -1;
	g_deblock_advance_ms = 0u;
	g_deblock_whole_media = 0;
	g_deblock_boot_media = 0;
}

static void
deblock_setup(bool withWhole, bool withBoot)
{
	uint32_t index;

	deblock_reset_fixture();
	gta4l_test.deblock_model = 1;
	gta4l_test_deblock_hook = deblock_hook;
	if (withWhole) {
		g_deblock_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_deblock_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
	for (index = 0; index < gta4l_emmc_block_deblock_case_count();
	    index++) {
		struct gta4l_emmc_block_deblock_case *item =
		    gta4l_emmc_block_deblock_case_mutable(index);

		deblock_expected_for_case(item, item->expected);
	}
}

static void
deblock_apply_fixture(void)
{
	gta4l_test.deblock_fail_at = g_deblock_fail_at;
	gta4l_test.deblock_short_at = g_deblock_short_at;
	gta4l_test.deblock_corrupt_at = g_deblock_corrupt_at;
	gta4l_test.deblock_cancel_at = g_deblock_cancel_at;
	gta4l_test.deblock_advance_ms = g_deblock_advance_ms;
}

static int
scenario_deblock_refused_orphan(void)
{
	begin("gta4l-emmc-deblock=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "gta4l-emmc-deblock requires") != 0);
	return 0;
}

static int
scenario_deblock_refused_value(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=2");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed gta4l-emmc-deblock") != 0);
	return 0;
}

static int
scenario_deblock_refused_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1 "
	    "gta4l-emmc-deblock=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "duplicate gta4l-emmc-deblock") != 0);
	return 0;
}

static int
scenario_deblock_refused_partobs(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-deblock=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_deblock_refused_partcmp(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-deblock=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_deblock_refused_partread(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1 "
	    "gta4l-emmc-deblock=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_deblock_segment_property(void)
{
	OSNumber *property;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS);
	return 0;
}

static int
scenario_deblock_match(void)
{
	const struct gta4l_emmc_block_deblock_case *last;
	char digest[33];
	char expected[256];

	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 failed=0 reason=0 "
	    "complete=1 reads=11 bytes=54364 span=56832 blocks=111 fails=0 "
	    "cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360") != 0);
	last = gta4l_emmc_block_deblock_case_at(10);
	CHECK(last != 0);
	partread_digest_hex(last->expected, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 DEBLOCKREAD: index=10 target=1 start=4096 "
	    "length=1024 span=1024 actual=1024 status=0 digest=%s",
	    digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "PARTREAD") == 0);
	CHECK(gta4l_test.deblock_read_calls == 11);
	CHECK(gta4l_test.deblock_adapter_calls == 11);
	CHECK(gta4l_test.deblock_dma_errors == 0);
	CHECK(gta4l_test.read_calls == 112);
	CHECK(g_deblock_whole_media != 0);
	CHECK(g_deblock_whole_media->open_calls == 1);
	CHECK(g_deblock_whole_media->close_calls == 1);
	CHECK(g_deblock_boot_media != 0);
	CHECK(g_deblock_boot_media->open_calls == 1);
	CHECK(g_deblock_boot_media->close_calls == 1);
	return 0;
}

static int
scenario_deblock_range_invalid(void)
{
	struct gta4l_emmc_block_deblock_case *first;
	uint64_t saved_start;
	uint64_t whole_size;

	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	first = gta4l_emmc_block_deblock_case_mutable(0);
	CHECK(first != 0);
	saved_start = first->start;
	whole_size = (uint64_t)gta4l_test.capacity_blocks *
	    GTA4L_EMMC_BLOCK_BYTES;
	first->start = whole_size - 100u;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->start = saved_start;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DEBLOCK: cases=11 passed=10 failed=1 reason=10 "
	    "complete=0 reads=10 bytes=53440 span=55808 blocks=109 fails=0 "
	    "cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.deblock_fail_index == 0);
	CHECK(g_self->report.deblock_fail_reason ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG);
	return 0;
}

static int
scenario_deblock_seg1(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	deblock_apply_fixture();
	gta4l_test.deblock_segment_override = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DEBLOCK: cases=11 passed=4 failed=7 reason=5 "
	    "complete=0 reads=11 bytes=54364 span=56832 blocks=111 fails=7 "
	    "cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.deblock_dma_errors == 7);
	CHECK(gta4l_test.deblock_adapter_calls == 6);
	return 0;
}

static int
scenario_deblock_unaligned_bounce(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	deblock_apply_fixture();
	gta4l_test.deblock_unaligned_bounce = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DEBLOCK: cases=11 passed=2 failed=9 reason=5 "
	    "complete=0 reads=11 bytes=54364 span=56832 blocks=111 fails=9 "
	    "cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.deblock_dma_errors == 9);
	CHECK(gta4l_test.deblock_adapter_calls == 2);
	return 0;
}

static int
scenario_deblock_hash_mismatch(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_corrupt_at = 0;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=7 complete=0 reads=11 "
	    "bytes=54364 span=56832 blocks=111 fails=0") != 0);
	CHECK(g_self->report.deblock_fail_index == 0);
	CHECK(g_self->report.deblock_fail_reason ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_HASH);
	return 0;
}

static int
scenario_deblock_read_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_fail_at = 0;
	deblock_apply_fixture();
	gta4l_test.deblock_fail_status = kIOReturnIOError;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=5 complete=0 reads=11 "
	    "bytes=54364 span=56832 blocks=111 fails=1") != 0);
	CHECK(g_self->report.deblock_fail_index == 0);
	CHECK(g_self->report.deblock_fail_status ==
	    (int32_t)kIOReturnIOError);
	return 0;
}

static int
scenario_deblock_read_nomedia(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_fail_at = 6;
	deblock_apply_fixture();
	gta4l_test.deblock_fail_status = kIOReturnNoMedia;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=5 complete=0 reads=11") != 0);
	CHECK(g_self->report.deblock_fail_index == 6);
	CHECK(g_self->report.deblock_fail_status ==
	    (int32_t)kIOReturnNoMedia);
	return 0;
}

static int
scenario_deblock_short_read(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_short_at = 0;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=6 complete=0 reads=11 "
	    "bytes=54364 span=56832 blocks=111 fails=1") != 0);
	CHECK(g_self->report.deblock_fail_actual == 923);
	return 0;
}

static int
scenario_deblock_short_zero(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_short_at = 0;
	gta4l_test.deblock_short_mode = 1;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=6 complete=0") != 0);
	CHECK(g_self->report.deblock_fail_actual == 0);
	return 0;
}

static int
scenario_deblock_short_over(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_short_at = 0;
	gta4l_test.deblock_short_mode = 2;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=10 failed=1 reason=6 complete=0") != 0);
	CHECK(g_self->report.deblock_fail_actual == 925);
	return 0;
}

static int
scenario_deblock_media_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(false, false);
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=0 reason=1 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0 cancel=0 budget=0 media=1 "
	    "part=0 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_two_whole(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	add_media(true, 0, (UInt64)gta4l_test.capacity_blocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=0 reason=1 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0 cancel=0 budget=0 media=1 "
	    "part=0 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_wrong_profile(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_whole_media->writable_value = true;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=0 reason=1 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0 cancel=0 budget=0 media=1 "
	    "part=0 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_whole_no_base(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_whole_media->property_faults |=
	    GTA4L_TEST_PROP_BASE_MISSING;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=11 failed=0 reason=0 complete=1") != 0);
	return 0;
}

static int
scenario_deblock_whole_base_invalid(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_whole_media->property_faults |=
	    GTA4L_TEST_PROP_BASE_WRONG;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=0 reason=1 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0 cancel=0 budget=0 media=1 "
	    "part=0 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_part_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, false);
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=6 failed=0 reason=2 complete=0 reads=6 "
	    "bytes=19348 span=20992 blocks=41 fails=0 cancel=0 budget=0 "
	    "media=0 part=1 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_part_ambiguous(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	add_media(false, GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES, false,
	    "Apple_Boot", true);
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=6 failed=0 reason=2 complete=0 reads=6") != 0);
	CHECK(strstr(gta4l_test.panic, "part=1") != 0);
	return 0;
}

static int
scenario_deblock_open_refused(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_whole_media->open_refuse = true;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=0 reason=3 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0") != 0);
	return 0;
}

static int
scenario_deblock_descriptor_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=0 failed=11 reason=4 complete=0 reads=0 "
	    "bytes=0 span=0 blocks=0 fails=0") != 0);
	CHECK(g_self->report.deblock_fail_index == 0);
	CHECK(g_self->report.deblock_fail_reason ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_DESC);
	return 0;
}

static int
scenario_deblock_budget(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_advance_ms = 5000u;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "cases=11 passed=1 failed=0 reason=8 complete=0 reads=1 "
	    "bytes=924 span=1024 blocks=2 fails=0 cancel=0 budget=1 "
	    "media=0 part=0 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_deblock_cancel(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_cancel_at = 0;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.deblock_cancel == 1);
	CHECK(g_self->report.deblock_reason ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_CANCEL);
	CHECK(g_self->report.deblock_complete == 0);
	CHECK(g_self->report.deblock_reads == 1);
	CHECK(g_deblock_whole_media->close_calls == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	deblock_apply_fixture();
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->report.deblock_cancel == 1);
	CHECK(g_self->report.deblock_reason ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_CANCEL);
	CHECK(g_self->report.deblock_reads == 0);
	return 0;
}

static int
scenario_deblock_watchdog(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	deblock_setup(true, true);
	g_deblock_watchdog_at = 0;
	deblock_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DEBLOCK: ") != 0);
	return 0;
}

static int g_breakup_fail_at = -1;
static int g_breakup_short_at = -1;
static int g_breakup_corrupt_at = -1;
static int g_breakup_cancel_at = -1;
static int g_breakup_watchdog_at = -1;
static int g_breakup_foreign_at = -1;
static int g_breakup_foreign_mode;
static uint32_t g_breakup_advance_ms;
static IOMedia *g_breakup_whole_media;
static IOMedia *g_breakup_boot_media;

static void
breakup_expected_for_case(
    const struct gta4l_emmc_block_breakup_case *item, uint8_t digest[32])
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES];
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint64_t absolute;
	uint64_t block;
	uint64_t last_block = ~0ull;
	uint64_t position;
	uint32_t length;
	uint32_t index;
	uint32_t offset;

	absolute = (item->target == GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT) ?
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES + item->start :
	    item->start;
	length = item->length;
	for (index = 0; index < length; index++) {
		position = absolute + index;
		block = position / GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(position % GTA4L_EMMC_BLOCK_BYTES);
		if (block != last_block) {
			deblock_fill_sector(block, sector);
			last_block = block;
		}
		scratch[index] = sector[offset];
	}
	gta4l_emmc_block_sha256(scratch, length, digest);
}

static void
breakup_hook(uint32_t call, uint32_t phase)
{
	if ((phase == 2u) && (g_breakup_cancel_at >= 0) &&
	    (call == (uint32_t)g_breakup_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((phase == 0u) && (g_breakup_watchdog_at >= 0) &&
	    (call == (uint32_t)g_breakup_watchdog_at)) {
		(void)invoke_thread(1);
	}
}

static void
breakup_reset_fixture(void)
{
	g_breakup_fail_at = -1;
	g_breakup_short_at = -1;
	g_breakup_corrupt_at = -1;
	g_breakup_cancel_at = -1;
	g_breakup_watchdog_at = -1;
	g_breakup_foreign_at = -1;
	g_breakup_foreign_mode = 0;
	g_breakup_advance_ms = 0u;
	g_breakup_whole_media = 0;
	g_breakup_boot_media = 0;
}

static void
breakup_setup(bool withWhole, bool withBoot)
{
	uint32_t index;

	breakup_reset_fixture();
	gta4l_test.breakup_model = 1;
	gta4l_test_breakup_hook = breakup_hook;
	if (withWhole) {
		g_breakup_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_breakup_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
	for (index = 0; index < gta4l_emmc_block_breakup_case_count();
	    index++) {
		struct gta4l_emmc_block_breakup_case *item =
		    gta4l_emmc_block_breakup_case_mutable(index);

		breakup_expected_for_case(item, item->expected);
	}
}

static void
breakup_apply_fixture(void)
{
	gta4l_test.breakup_fail_at = g_breakup_fail_at;
	gta4l_test.breakup_short_at = g_breakup_short_at;
	gta4l_test.breakup_corrupt_at = g_breakup_corrupt_at;
	gta4l_test.breakup_cancel_at = g_breakup_cancel_at;
	gta4l_test.breakup_advance_ms = g_breakup_advance_ms;
	gta4l_test.breakup_foreign_at = g_breakup_foreign_at;
	gta4l_test.breakup_foreign_mode = g_breakup_foreign_mode;
}

static int
scenario_breakup_refused_orphan(void)
{
	begin("gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "gta4l-emmc-breakup requires") != 0);
	return 0;
}

static int
scenario_breakup_refused_value(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=2");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed gta4l-emmc-breakup") != 0);
	return 0;
}

static int
scenario_breakup_refused_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1 "
	    "gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "duplicate gta4l-emmc-breakup") != 0);
	return 0;
}

static int
scenario_breakup_refused_partobs(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_breakup_refused_partcmp(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_breakup_refused_partread(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1 "
	    "gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_breakup_refused_deblock(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1 "
	    "gta4l-emmc-breakup=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_breakup_properties(void)
{
	OSNumber *property;

	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_MAX_BYTES);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_MAX_BYTES);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_MAX_BYTES);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS);
	return 0;
}

static int
scenario_breakup_pinned(void)
{
	static const uint8_t pinned[4][32] = {
		{
			0x58, 0x78, 0xbc, 0xf8, 0xa6, 0xaf, 0xe4, 0xdd,
			0x79, 0xa0, 0x02, 0xca, 0xa8, 0xb6, 0xae, 0xea,
			0xe1, 0x61, 0x25, 0xb8, 0x90, 0xab, 0x75, 0x0b,
			0x67, 0xd6, 0xab, 0x3c, 0x25, 0xf3, 0xb1, 0xb2,
		},
		{
			0xb4, 0xc3, 0xc6, 0x43, 0xe5, 0xa4, 0x3d, 0x82,
			0xa5, 0x2e, 0xc8, 0xc1, 0x01, 0xba, 0xcf, 0xa5,
			0x12, 0x10, 0x24, 0x4d, 0x7b, 0xf4, 0x0e, 0x97,
			0xef, 0x14, 0x36, 0x44, 0x78, 0x80, 0xfa, 0x9c,
		},
		{
			0x5f, 0x97, 0x04, 0x72, 0x40, 0x2e, 0x70, 0xb5,
			0x4b, 0xb0, 0xf8, 0xb4, 0x94, 0x5f, 0x5f, 0x02,
			0x0d, 0x54, 0x79, 0x8b, 0x42, 0xca, 0x32, 0xa2,
			0x17, 0x42, 0xa4, 0x57, 0x0f, 0x61, 0xa9, 0x5c,
		},
		{
			0xbc, 0x0b, 0x9b, 0xb9, 0xc6, 0xdd, 0x11, 0xc3,
			0xae, 0xa6, 0x65, 0xd0, 0x1d, 0xc5, 0xf1, 0x08,
			0x14, 0xa4, 0x60, 0xd4, 0x1e, 0xb7, 0x7e, 0x0a,
			0x7a, 0x01, 0xbb, 0x2b, 0xe7, 0x26, 0xdb, 0x17,
		},
	};
	static const struct gta4l_emmc_block_breakup_case expected[4] = {
		{ GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 4096u,
		  4096u, 8u, 1u, 8u, { 0 } },
		{ GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 65536u,
		  65536u, 128u, 16u, 8u, { 0 } },
		{ GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 65024u,
		  65024u, 127u, 16u, 8u, { 0 } },
		{ GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4196u, 65024u,
		  65536u, 128u, 16u, 8u, { 0 } },
	};
	const struct gta4l_emmc_block_breakup_case *item;
	uint32_t index;

	CHECK(gta4l_emmc_block_breakup_case_count() == 4);
	for (index = 0; index < 4u; index++) {
		item = gta4l_emmc_block_breakup_case_at(index);
		CHECK(item != 0);
		CHECK(item->target == expected[index].target);
		CHECK(item->start == expected[index].start);
		CHECK(item->length == expected[index].length);
		CHECK(item->span == expected[index].span);
		CHECK(item->blocks == expected[index].blocks);
		CHECK(item->subrequests == expected[index].subrequests);
		CHECK(item->max_blocks == expected[index].max_blocks);
		CHECK(memcmp(item->expected, pinned[index],
		    sizeof(pinned[index])) == 0);
	}
	return 0;
}

static int
scenario_breakup_match(void)
{
	const struct gta4l_emmc_block_breakup_case *last;
	char digest[33];
	char expected[256];

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 "
	    "complete=1 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	last = gta4l_emmc_block_breakup_case_at(3);
	CHECK(last != 0);
	partread_digest_hex(last->expected, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 BREAKUPREAD: index=3 target=1 start=4196 "
	    "length=65024 span=65536 actual=65024 status=0 "
	    "subrequests=16 blocks=128 max=8 digest=%s", digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "DEBLOCK") == 0);
	CHECK(strstr(gta4l_test.panic, "PARTREAD") == 0);
	CHECK(gta4l_test.breakup_read_calls == 4);
	CHECK(gta4l_test.breakup_adapter_calls == 49);
	CHECK(gta4l_test.breakup_dma_errors == 0);
	CHECK(g_breakup_whole_media != 0);
	CHECK(g_breakup_whole_media->open_calls == 1);
	CHECK(g_breakup_whole_media->close_calls == 1);
	CHECK(g_breakup_boot_media != 0);
	CHECK(g_breakup_boot_media->open_calls == 1);
	CHECK(g_breakup_boot_media->close_calls == 1);
	return 0;
}

static int
scenario_breakup_block3(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 "
	    "complete=1 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_breakup_production_limit(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	gta4l_test.breakup_limit_blocks = 64;
	gta4l_test.breakup_limit_bytes = 32768;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=1 failed=3 reason=12 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=7 max=64 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER);
	CHECK(g_self->report.breakup_fail_subrequests == 2);
	CHECK(g_self->report.breakup_fail_blocks == 128);
	CHECK(g_self->report.breakup_fail_max == 64);
	return 0;
}

static int
scenario_breakup_zero_length(void)
{
	struct gta4l_emmc_block_breakup_case *first;
	uint32_t saved_length;

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	first = gta4l_emmc_block_breakup_case_mutable(0);
	CHECK(first != 0);
	saved_length = first->length;
	first->length = 0;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->length = saved_length;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=10 "
	    "complete=0 reads=3 bytes=195584 span=196096 blocks=383 "
	    "subrequests=48 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG);
	return 0;
}

static int
scenario_breakup_range_invalid(void)
{
	struct gta4l_emmc_block_breakup_case *first;
	uint64_t saved_start;

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	first = gta4l_emmc_block_breakup_case_mutable(0);
	CHECK(first != 0);
	saved_start = first->start;
	first->start = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 100u;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->start = saved_start;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=10 "
	    "complete=0 reads=3 bytes=195584 span=196096 blocks=383 "
	    "subrequests=48 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG);
	return 0;
}

static int
scenario_breakup_seg1(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	gta4l_test.breakup_limit_segments = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=5 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=33 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_dma_errors == 1);
	CHECK(gta4l_test.breakup_adapter_calls == 33);
	CHECK(g_self->report.breakup_fail_index == 3);
	CHECK(g_self->report.breakup_fail_status ==
	    (int32_t)kIOReturnDMAError);
	return 0;
}

static int
scenario_breakup_unaligned_bounce(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	gta4l_test.breakup_unaligned_bounce = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=5 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=33 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_dma_errors == 1);
	CHECK(gta4l_test.breakup_adapter_calls == 33);
	CHECK(g_self->report.breakup_fail_index == 3);
	return 0;
}

static int
scenario_breakup_hash_mismatch(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_corrupt_at = 0;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=7 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_HASH);
	return 0;
}

static int
scenario_breakup_read_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_fail_at = 0;
	breakup_apply_fixture();
	gta4l_test.breakup_fail_status = kIOReturnIOError;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=5 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=48 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_status ==
	    (int32_t)kIOReturnIOError);
	CHECK(g_self->report.breakup_fail_subrequests == 0);
	CHECK(g_self->report.breakup_fail_blocks == 0);
	CHECK(gta4l_test.breakup_adapter_calls == 48);
	return 0;
}

static int
scenario_breakup_read_nomedia(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_fail_at = 1;
	breakup_apply_fixture();
	gta4l_test.breakup_fail_status = kIOReturnNoMedia;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=5 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=33 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_status ==
	    (int32_t)kIOReturnNoMedia);
	CHECK(gta4l_test.breakup_adapter_calls == 33);
	return 0;
}

static int
scenario_breakup_short(void)
{
	uint64_t expected_actual[3] = { 4095u, 0u, 4097u };
	uint32_t mode;

	for (mode = 0; mode < 3u; mode++) {
		begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
		CHECK(g_started);
		breakup_setup(true, true);
		g_breakup_short_at = 0;
		gta4l_test.breakup_short_mode = (int)mode;
		breakup_apply_fixture();
		CHECK(invoke_thread(0) == 1);
		CHECK(strstr(gta4l_test.panic,
		    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 "
		    "reason=6 complete=0 reads=4 bytes=199680 span=200192 "
		    "blocks=391 subrequests=49 max=8 foreign=0 fails=1") != 0);
		CHECK(g_self->report.breakup_fail_index == 0);
		CHECK(g_self->report.breakup_fail_actual ==
		    expected_actual[mode]);
	}
	return 0;
}

static int
scenario_breakup_media(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(false, false);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=0 failed=0 reason=1 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=1 part=0 "
	    "limit=8 seg=3 cap=61071360") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	add_media(true, 0, (UInt64)gta4l_test.capacity_blocks *
	    GTA4L_EMMC_BLOCK_BYTES, false, "GUID_partition_scheme", true);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=1 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(strstr(gta4l_test.panic, "media=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_whole_media->writable_value = true;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=1 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(strstr(gta4l_test.panic, "media=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_whole_media->property_faults |=
	    GTA4L_TEST_PROP_BASE_WRONG;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=1 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(strstr(gta4l_test.panic, "media=1") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_whole_media->property_faults |=
	    GTA4L_TEST_PROP_BASE_MISSING;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 "
	    "complete=1") != 0);
	return 0;
}

static int
scenario_breakup_part(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, false);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=0 failed=0 reason=2 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=1 "
	    "limit=8 seg=3 cap=61071360") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	add_media(false, GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES, false,
	    "Apple_Boot", true);
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(strstr(gta4l_test.panic, "part=1") != 0);
	return 0;
}

static int
scenario_breakup_open(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_whole_media->open_refuse = true;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=0 failed=0 reason=3 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "limit=8 seg=3 cap=61071360") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_boot_media->open_refuse = true;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(g_breakup_whole_media->open_calls == 1);
	CHECK(g_breakup_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_breakup_descriptor_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=0 failed=4 reason=4 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_DESC);
	return 0;
}

static int
scenario_breakup_budget(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_advance_ms = 7000u;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=1 failed=0 reason=8 "
	    "complete=0 reads=1 bytes=4096 span=4096 blocks=8 "
	    "subrequests=1 max=8 foreign=0 fails=0 cancel=0 budget=1 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	return 0;
}

static int
scenario_breakup_cancel(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_cancel_at = 0;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.breakup_cancel == 1);
	CHECK(g_self->report.breakup_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_CANCEL);
	CHECK(g_self->report.breakup_complete == 0);
	CHECK(g_self->report.breakup_reads == 1);
	CHECK(g_breakup_whole_media->close_calls == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	breakup_apply_fixture();
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->report.breakup_cancel == 1);
	CHECK(g_self->report.breakup_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_CANCEL);
	CHECK(g_self->report.breakup_reads == 0);
	return 0;
}

static int
scenario_breakup_watchdog(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_watchdog_at = 0;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: ") != 0);
	return 0;
}

static int
scenario_breakup_split_missing(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 1;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=2 failed=2 reason=11 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=47 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_SPLIT);
	CHECK(g_self->report.breakup_fail_subrequests == 15);
	CHECK(g_self->report.breakup_fail_blocks == 120);
	CHECK(gta4l_test.breakup_adapter_calls == 47);
	return 0;
}

static int
scenario_breakup_split_extra(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 2;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=1 failed=3 reason=12 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=52 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER);
	CHECK(g_self->report.breakup_fail_subrequests == 2);
	CHECK(g_self->report.breakup_fail_blocks == 16);
	CHECK(gta4l_test.breakup_adapter_calls == 52);
	return 0;
}

static int
scenario_breakup_split_reorder(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 3;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=2 failed=2 reason=12 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER);
	return 0;
}

static int
scenario_breakup_split_overlap(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 4;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=2 failed=2 reason=7 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_HASH);
	return 0;
}

static int
scenario_breakup_split_oversize(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 5;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=1 failed=3 reason=12 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=9 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER);
	CHECK(g_self->report.breakup_fail_subrequests == 1);
	CHECK(g_self->report.breakup_fail_blocks == 9);
	CHECK(g_self->report.breakup_fail_max == 9);
	return 0;
}

static int
scenario_breakup_split_single(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	gta4l_test.breakup_split_mode = 6;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=2 failed=2 reason=5 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=19 max=128 foreign=0 fails=2 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 1);
	CHECK(g_self->report.breakup_fail_status ==
	    (int32_t)kIOReturnMessageTooLarge);
	CHECK(gta4l_test.breakup_adapter_calls == 19);
	return 0;
}

static int
scenario_breakup_order_inspan(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_foreign_at = 0;
	g_breakup_foreign_mode = 3;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=3 failed=1 reason=12 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=50 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(g_self->report.breakup_fail_index == 0);
	CHECK(g_self->report.breakup_fail_reason ==
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER);
	CHECK(g_self->report.breakup_fail_subrequests == 2);
	CHECK(g_self->report.breakup_fail_blocks == 9);
	CHECK(gta4l_test.breakup_adapter_calls == 50);
	return 0;
}

static int
scenario_breakup_foreign(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_foreign_at = 0;
	g_breakup_foreign_mode = 1;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=13 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=1 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_adapter_calls == 50);

	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1");
	CHECK(g_started);
	breakup_setup(true, true);
	g_breakup_foreign_at = 0;
	g_breakup_foreign_mode = 2;
	breakup_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=13 "
	    "complete=0 reads=4 bytes=199680 span=200192 blocks=391 "
	    "subrequests=49 max=8 foreign=1 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 limit=8 seg=3 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_adapter_calls == 50);
	return 0;
}

static int g_sweep_fail_at = -1;
static int g_sweep_short_at = -1;
static int g_sweep_corrupt_at = -1;
static int g_sweep_cancel_at = -1;
static int g_sweep_watchdog_at = -1;
static int g_sweep_foreign_at = -1;
static int g_sweep_foreign_mode;
static uint32_t g_sweep_advance_ms;
static IOMedia *g_sweep_whole_media;
static IOMedia *g_sweep_boot_media;

static void
sweep_expected_digest(const struct gta4l_emmc_block_sweep_case *item,
    uint32_t runtimeIndex, uint32_t bootBlocks, uint8_t digest[32])
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES];
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint64_t absolute;
	uint64_t block;
	uint64_t last_block = ~0ull;
	uint64_t position;
	uint64_t start_block = 0;
	uint32_t length;
	uint32_t index;
	uint32_t offset;

	if (item->runtime != 0u) {
		(void)gta4l_emmc_block_sweep_derive(runtimeIndex,
		    item->blocks, bootBlocks, &start_block);
		absolute = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
		    start_block * GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
	} else {
		absolute = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
		    item->start;
	}
	length = item->length;
	for (index = 0; index < length; index++) {
		position = absolute + index;
		block = position / GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(position % GTA4L_EMMC_BLOCK_BYTES);
		if (block != last_block) {
			deblock_fill_sector(block, sector);
			last_block = block;
		}
		scratch[index] = sector[offset];
	}
	gta4l_emmc_block_sha256(scratch, length, digest);
}

static void
sweep_hook(uint32_t call, uint32_t phase)
{
	if ((phase == 2u) && (g_sweep_cancel_at >= 0) &&
	    (call == (uint32_t)g_sweep_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((phase == 0u) && (g_sweep_watchdog_at >= 0) &&
	    (call == (uint32_t)g_sweep_watchdog_at)) {
		(void)invoke_thread(1);
	}
}

static void
sweep_reset_fixture(void)
{
	g_sweep_fail_at = -1;
	g_sweep_short_at = -1;
	g_sweep_corrupt_at = -1;
	g_sweep_cancel_at = -1;
	g_sweep_watchdog_at = -1;
	g_sweep_foreign_at = -1;
	g_sweep_foreign_mode = 0;
	g_sweep_advance_ms = 0u;
	g_sweep_whole_media = 0;
	g_sweep_boot_media = 0;
}

static void
sweep_setup(bool withWhole, bool withBoot)
{
	uint32_t index;

	sweep_reset_fixture();
	gta4l_test.breakup_model = 1;
	gta4l_test_breakup_hook = sweep_hook;
	if (withWhole) {
		g_sweep_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_sweep_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
	for (index = 0; index < gta4l_emmc_block_sweep_case_count();
	    index++) {
		struct gta4l_emmc_block_sweep_case *item =
		    gta4l_emmc_block_sweep_case_mutable(index);

		sweep_expected_digest(item,
		    (index >= GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) ?
		    (index - GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) : 0u,
		    (uint32_t)(GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES /
		    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES), item->expected);
	}
}

static void
sweep_apply_fixture(void)
{
	gta4l_test.breakup_fail_at = g_sweep_fail_at;
	gta4l_test.breakup_short_at = g_sweep_short_at;
	gta4l_test.breakup_corrupt_at = g_sweep_corrupt_at;
	gta4l_test.breakup_cancel_at = g_sweep_cancel_at;
	gta4l_test.breakup_advance_ms = g_sweep_advance_ms;
	gta4l_test.breakup_foreign_at = g_sweep_foreign_at;
	gta4l_test.breakup_foreign_mode = g_sweep_foreign_mode;
}

static int
scenario_sweep_refused_orphan(void)
{
	begin("gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "gta4l-emmc-sweep requires") != 0);
	return 0;
}

static int
scenario_sweep_refused_value(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=2");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "malformed gta4l-emmc-sweep") != 0);
	return 0;
}

static int
scenario_sweep_refused_duplicate(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "duplicate gta4l-emmc-sweep") != 0);
	return 0;
}

static int
scenario_sweep_refused_partobs(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_sweep_refused_partcmp(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_sweep_refused_partread(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-partread=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_sweep_refused_deblock(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-deblock=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_sweep_refused_breakup(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-breakup=1 "
	    "gta4l-emmc-sweep=1");
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_sweep_properties(void)
{
	OSNumber *property;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_READ_MAX_BYTES);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS);

	begin("gta4l-emmc-block=3 gta4l-emmc-sweep=1");
	CHECK(g_started);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_READ_MAX_BYTES);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK(property != 0);
	CHECK(property->unsigned64BitValue() ==
	    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS);
	return 0;
}

static int
scenario_sweep_pinned(void)
{
	static const uint8_t pinned[2][32] = {
		{
			0x58, 0x78, 0xbc, 0xf8, 0xa6, 0xaf, 0xe4, 0xdd,
			0x79, 0xa0, 0x02, 0xca, 0xa8, 0xb6, 0xae, 0xea,
			0xe1, 0x61, 0x25, 0xb8, 0x90, 0xab, 0x75, 0x0b,
			0x67, 0xd6, 0xab, 0x3c, 0x25, 0xf3, 0xb1, 0xb2,
		},
		{
			0xfa, 0x43, 0xaa, 0x5d, 0x40, 0x59, 0x1e, 0x9b,
			0x61, 0x9c, 0x99, 0xc4, 0xe4, 0x90, 0x05, 0x4f,
			0xea, 0xc8, 0x5a, 0xc5, 0x45, 0xf5, 0xc8, 0x4a,
			0x38, 0xf8, 0xaa, 0x04, 0x82, 0x07, 0x82, 0xda,
		},
	};
	static const uint64_t starts[4] = {
		51528u, 102928u, 154296u, 9128u
	};
	const struct gta4l_emmc_block_sweep_case *item;
	uint64_t startBlock;
	uint32_t index;

	CHECK(gta4l_emmc_block_sweep_case_count() == 6);
	CHECK(gta4l_emmc_block_sweep_case_at(6) == 0);
	for (index = 0; index < 2u; index++) {
		item = gta4l_emmc_block_sweep_case_at(index);
		CHECK(item != 0);
		CHECK(item->target == GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT);
		CHECK(item->runtime == 0u);
		CHECK(item->start == 4096u);
		CHECK(item->length == ((index == 0u) ? 4096u : 16384u));
		CHECK(item->span == item->length);
		CHECK(item->blocks == ((index == 0u) ? 8u : 32u));
		CHECK(item->subrequests == ((index == 0u) ? 1u : 4u));
		CHECK(item->max_blocks == 8u);
		CHECK(memcmp(item->expected, pinned[index],
		    sizeof(pinned[index])) == 0);
	}
	for (index = 2; index < 6u; index++) {
		item = gta4l_emmc_block_sweep_case_at(index);
		CHECK(item != 0);
		CHECK(item->target == GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT);
		CHECK(item->runtime == 1u);
		CHECK(item->start == 0u);
		CHECK(item->length ==
		    ((index == 2u) ? 16384u : (index == 3u) ? 8192u :
		    4096u));
		CHECK(item->span == item->length);
		CHECK(item->blocks ==
		    ((index == 2u) ? 32u : (index == 3u) ? 16u : 8u));
		CHECK(item->subrequests ==
		    ((index == 2u) ? 4u : (index == 3u) ? 2u : 1u));
		CHECK(item->max_blocks == 8u);
		CHECK(gta4l_emmc_block_sweep_derive(index - 2u,
		    item->blocks, 196608u, &startBlock) == 0);
		CHECK(startBlock == starts[index - 2u]);
	}
	CHECK(gta4l_emmc_block_sweep_derive(0, 32u, 196608u,
	    &startBlock) == 0);
	CHECK(startBlock == 51528u);
	CHECK(gta4l_emmc_block_sweep_derive(0, 32u, 100000u,
	    &startBlock) == 0);
	CHECK(startBlock == 38088u);
	CHECK(gta4l_emmc_block_sweep_derive(0, 0u, 196608u,
	    &startBlock) != 0);
	CHECK(gta4l_emmc_block_sweep_derive(0, 33u, 196608u,
	    &startBlock) != 0);
	CHECK(gta4l_emmc_block_sweep_derive(0, 32u, 24u,
	    &startBlock) != 0);
	CHECK(gta4l_emmc_block_sweep_derive(0, 32u, 196607u,
	    &startBlock) != 0);
	CHECK(gta4l_emmc_block_sweep_validate(
	    gta4l_emmc_block_sweep_case_at(0), 4096u, 196608u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES) == 0);
	CHECK(gta4l_emmc_block_sweep_validate(
	    gta4l_emmc_block_sweep_case_at(0), 4097u, 196608u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES) != 0);
	CHECK(gta4l_emmc_block_sweep_validate(
	    gta4l_emmc_block_sweep_case_at(0), 4096u, 196607u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES) != 0);
	CHECK(gta4l_emmc_block_sweep_validate(
	    gta4l_emmc_block_sweep_case_at(1), 4096u, 196608u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES) == 0);
	CHECK(gta4l_emmc_block_sweep_validate(
	    gta4l_emmc_block_sweep_case_at(5),
	    9128u * GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES, 196608u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES) == 0);
	return 0;
}

static int
scenario_sweep_match(void)
{
	const struct gta4l_emmc_block_sweep_case *last;
	uint8_t lastdigest[32];
	char digest[33];
	char expected[512];

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=0 "
	    "complete=1 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	last = gta4l_emmc_block_sweep_case_at(5);
	CHECK(last != 0);
	sweep_expected_digest(last, 3u, 196608u, lastdigest);
	partread_digest_hex(lastdigest, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 SWEEPREAD: index=5 target=1 start=4673536 "
	    "length=4096 span=4096 actual=4096 status=0 subrequests=1 "
	    "blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0 "
	    "mismatch=0 digest=%s refdigest=%s", digest, digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "BREAKUP") == 0);
	CHECK(strstr(gta4l_test.panic, "DEBLOCK") == 0);
	CHECK(strstr(gta4l_test.panic, "PARTREAD") == 0);
	CHECK(gta4l_test.breakup_read_calls == 6);
	CHECK(gta4l_test.breakup_adapter_calls == 13);
	CHECK(gta4l_test.breakup_dma_errors == 0);
	CHECK(g_sweep_whole_media != 0);
	CHECK(g_sweep_whole_media->open_calls == 1);
	CHECK(g_sweep_whole_media->close_calls == 1);
	CHECK(g_sweep_boot_media != 0);
	CHECK(g_sweep_boot_media->open_calls == 1);
	CHECK(g_sweep_boot_media->close_calls == 1);
	CHECK(g_self->report.sweep_complete == 1);
	CHECK(g_self->report.sweep_anchor == 1);
	CHECK(g_self->report.sweep_mismatches == 0);
	return 0;
}

static int
scenario_sweep_block3(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=0 "
	    "complete=1 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_adapter_calls == 13);
	return 0;
}

static int
scenario_sweep_production_limit(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	sweep_apply_fixture();
	gta4l_test.breakup_limit_blocks = 64;
	gta4l_test.breakup_limit_bytes = 32768;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=3 failed=3 reason=12 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=6 max=32 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 1);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER);
	CHECK(g_self->report.sweep_fail_subrequests == 1);
	CHECK(g_self->report.sweep_fail_blocks == 32);
	CHECK(g_self->report.sweep_fail_max == 32);
	return 0;
}

static int
scenario_sweep_zero_length(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint32_t saved_length;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved_length = first->length;
	first->length = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->length = saved_length;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=10 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG);
	return 0;
}

static int
scenario_sweep_range_invalid(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint64_t saved_start;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved_start = first->start;
	first->start = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 100u;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->start = saved_start;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=10 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG);
	return 0;
}

static int
scenario_sweep_wrong_target(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint32_t saved_target;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved_target = first->target;
	first->target = GTA4L_EMMC_BLOCK_SWEEP_TARGET_WHOLE;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->target = saved_target;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=10 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG);
	return 0;
}

static int
scenario_sweep_unaligned_start(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint64_t saved_start;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved_start = first->start;
	first->start = 4097u;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->start = saved_start;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=10 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG);
	return 0;
}

static int
scenario_sweep_length_invalid(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint32_t saved_length;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved_length = first->length;
	first->length = 4095u;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->length = saved_length;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=10 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG);
	return 0;
}

static int
scenario_sweep_hash_mismatch(void)
{
	struct gta4l_emmc_block_sweep_case *first;
	uint8_t saved;

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	first = gta4l_emmc_block_sweep_case_mutable(0);
	CHECK(first != 0);
	saved = first->expected[0];
	first->expected[0] ^= 0x5au;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	first->expected[0] = saved;
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=7 "
	    "complete=0 reads=1 bytes=4096 span=4096 blocks=8 "
	    "subrequests=1 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=1 refbytes=4096 anchor=0 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_HASH);
	return 0;
}

static int
scenario_sweep_read_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_fail_at = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=5 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=12 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_READ);
	return 0;
}

static int
scenario_sweep_short_read(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_short_at = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=6 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_SHORT);
	return 0;
}

static int
scenario_sweep_short_reference(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.sweep_reference_short_at = gta4l_test.read_calls;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=15 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=1 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_REF);
	CHECK(g_self->report.sweep_fail_ref == 0);
	CHECK(g_self->report.sweep_fail_refbytes == 1);
	CHECK(g_self->report.sweep_fail_refstatus == 0);
	return 0;
}

static int
scenario_sweep_reference_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.fail_read_at = gta4l_test.read_calls;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=1 reason=15 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=4294967291") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_REF);
	CHECK(g_self->report.sweep_fail_ref == 0);
	CHECK(g_self->report.sweep_fail_refbytes == 0);
	CHECK(g_self->report.sweep_fail_refstatus == -5);
	return 0;
}

static int
scenario_sweep_reference_wrong_lba(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.sweep_reference_corrupt_at = gta4l_test.read_calls;
	gta4l_test.sweep_reference_corrupt_count = 8;
	gta4l_test.sweep_reference_corrupt_mode = 1;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=14 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=1 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH);
	CHECK(g_self->report.sweep_fail_mvalid == 1);
	CHECK(g_self->report.sweep_fail_mismatch == 0);
	return 0;
}

static int
scenario_sweep_reference_shifted(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.sweep_reference_corrupt_at = gta4l_test.read_calls;
	gta4l_test.sweep_reference_corrupt_count = 8;
	gta4l_test.sweep_reference_corrupt_mode = 2;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=14 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=1 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH);
	CHECK(g_self->report.sweep_fail_mvalid == 1);
	CHECK(g_self->report.sweep_fail_mismatch == 0);
	return 0;
}

static int
scenario_sweep_reference_stale(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.sweep_reference_corrupt_at = gta4l_test.read_calls + 16;
	gta4l_test.sweep_reference_corrupt_count = 32;
	gta4l_test.sweep_reference_corrupt_mode = 3;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=14 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=1 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 1);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH);
	CHECK(g_self->report.sweep_fail_mvalid == 1);
	CHECK(g_self->report.sweep_fail_mismatch == 0);
	return 0;
}

static int
scenario_sweep_mismatch(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_corrupt_at = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=5 failed=1 reason=14 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=1 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH);
	CHECK(g_self->report.sweep_fail_mvalid == 1);
	CHECK(g_self->report.sweep_fail_mismatch == 2048u);
	return 0;
}

static int
scenario_sweep_split_missing(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.breakup_split_mode = 1;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=3 failed=3 reason=11 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=10 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 1);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_SPLIT);
	CHECK(g_self->report.sweep_fail_subrequests == 3);
	CHECK(g_self->report.sweep_fail_blocks == 24);
	CHECK(gta4l_test.breakup_adapter_calls == 10);
	return 0;
}

static int
scenario_sweep_split_overlap(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.breakup_split_mode = 4;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=3 failed=3 reason=14 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=0 mismatches=3 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 1);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH);
	CHECK(g_self->report.sweep_fail_mvalid == 1);
	CHECK(g_self->report.sweep_fail_mismatch == 4608);
	return 0;
}

static int
scenario_sweep_split_oversize(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.breakup_split_mode = 5;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=6 reason=12 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=9 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER);
	CHECK(g_self->report.sweep_fail_subrequests == 1);
	CHECK(g_self->report.sweep_fail_blocks == 9);
	CHECK(g_self->report.sweep_fail_max == 9);
	return 0;
}

static int
scenario_sweep_order(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.breakup_split_mode = 2;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=6 reason=12 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=19 max=8 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER);
	CHECK(g_self->report.sweep_fail_subrequests == 2);
	CHECK(g_self->report.sweep_fail_blocks == 16);
	return 0;
}

static int
scenario_sweep_foreign(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_foreign_at = 0;
	g_sweep_foreign_mode = 1;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=13 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=1 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	CHECK(gta4l_test.breakup_adapter_calls == 14);

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_foreign_at = 0;
	g_sweep_foreign_mode = 2;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=13 "
	    "complete=0 reads=6 bytes=53248 span=53248 blocks=104 "
	    "subrequests=13 max=8 foreign=1 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	return 0;
}

static int
scenario_sweep_descriptor_failure(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=6 reason=4 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=6 refbytes=53248 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(g_self->report.sweep_fail_index == 0);
	CHECK(g_self->report.sweep_fail_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_DESC);
	CHECK(g_self->report.sweep_fail_ref == 1);
	CHECK(g_self->report.sweep_fail_refbytes == 4096);
	return 0;
}

static int
scenario_sweep_budget(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_advance_ms = 4000u;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=1 failed=0 reason=8 "
	    "complete=0 reads=1 bytes=4096 span=4096 blocks=8 "
	    "subrequests=1 max=8 foreign=0 fails=0 cancel=0 budget=1 "
	    "media=0 part=0 refs=1 refbytes=4096 anchor=0 mismatches=0 "
	    "limit=8 seg=1 cap=61071360") != 0);
	return 0;
}

static int
scenario_sweep_cancel(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_cancel_at = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.sweep_cancel == 1);
	CHECK(g_self->report.sweep_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_CANCEL);
	CHECK(g_self->report.sweep_complete == 0);
	CHECK(g_self->report.sweep_reads == 1);
	CHECK(g_sweep_whole_media->close_calls == 1);
	CHECK(g_sweep_boot_media->close_calls == 1);

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	sweep_apply_fixture();
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->report.sweep_cancel == 1);
	CHECK(g_self->report.sweep_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_CANCEL);
	CHECK(g_self->report.sweep_reads == 0);
	return 0;
}

static int
scenario_sweep_watchdog(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_watchdog_at = 0;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: ") != 0);
	return 0;
}

static int
scenario_sweep_media_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(false, false);
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=0 reason=1 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=1 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEPREAD: index=0 target=0 start=0 "
	    "length=0 span=0 actual=0 status=0 subrequests=0 blocks=0 "
	    "max=0 ref=0 refbytes=0 refstatus=0 mvalid=0 mismatch=0 "
	    "digest=00000000000000000000000000000000 "
	    "refdigest=00000000000000000000000000000000") != 0);
	return 0;
}

static int
scenario_sweep_part_absent(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, false);
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=0 reason=2 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=1 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	return 0;
}

static int
scenario_sweep_open_refused(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_whole_media->open_refuse = true;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=0 reason=3 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);

	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	g_sweep_boot_media->open_refuse = true;
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	return 0;
}

static int
scenario_sweep_two_whole(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-sweep=1");
	CHECK(g_started);
	sweep_setup(true, true);
	(void)add_media(true, 0,
	    (UInt64)gta4l_test.capacity_blocks * GTA4L_EMMC_BLOCK_BYTES,
	    false, "GUID_partition_scheme", true);
	sweep_apply_fixture();
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SWEEP: cases=6 passed=0 failed=0 reason=1 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=1 part=0 "
	    "refs=0 refbytes=0 anchor=0 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	return 0;
}

static IOMedia *g_consume_whole_media;
static IOMedia *g_consume_boot_media;
static int g_consume_cancel_at;
static int g_consume_watchdog_at;

static void
consume_hook(uint32_t call, uint32_t phase)
{
	if ((phase == 2u) && (g_consume_cancel_at >= 0) &&
	    (call == (uint32_t)g_consume_cancel_at)) {
		g_self->reportCancel = true;
	}
	if ((phase == 0u) && (g_consume_watchdog_at >= 0) &&
	    (call == (uint32_t)g_consume_watchdog_at)) {
		(void)invoke_thread(1);
	}
}

static void
consume_reset_fixture(void)
{
	g_consume_cancel_at = -1;
	g_consume_watchdog_at = -1;
	g_consume_whole_media = 0;
	g_consume_boot_media = 0;
}

static void
consume_setup(bool withWhole, bool withBoot)
{
	consume_reset_fixture();
	/* The breakup model splits the request against the advertised
	 * 64-block / 32 KiB / one-segment limits before issuing it through
	 * the adapter, so the production advertisement is exercised. */
	gta4l_test.breakup_model = 1;
	gta4l_test_breakup_hook = consume_hook;
	if (withWhole) {
		g_consume_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_consume_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
}

static void
consume_expected_digest(uint8_t *digest)
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES];
	uint64_t first = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;

	gta4l_test_fill_sector(first, scratch);
	gta4l_test_fill_sector(first + 1u,
	    scratch + GTA4L_EMMC_BLOCK_BYTES);
	gta4l_emmc_block_sha256(scratch,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES, digest);
}

static int
scenario_consume_refused_orphan(void)
{
	begin("gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-consume requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_consume_refused_block1(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-consume requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_consume_refused_value(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=2");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "malformed gta4l-emmc-consume value") != 0);
	return 0;
}

static int
scenario_consume_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-consume token") != 0);
	return 0;
}

static int
scenario_consume_refused_partobs(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_refused_partcmp(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_refused_partread(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partread=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_refused_deblock(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-deblock=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_refused_breakup(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-breakup=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_refused_sweep(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-sweep=1 "
	    "gta4l-emmc-consume=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log, "mutually exclusive") != 0);
	return 0;
}

static int
scenario_consume_match(void)
{
	uint8_t expected_digest[32];
	char digest[33];
	char expected[512];
	OSNumber *property;

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 64));
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 32768));
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 1));
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	consume_expected_digest(expected_digest);
	partread_digest_hex(expected_digest, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 CONSUMEREAD: index=0 target=1 start=0 "
	    "length=1024 span=1024 actual=1024 status=0 subrequests=1 "
	    "blocks=2 max=2 digest=%s "
	    "expected=00000000000000000000000000000000 mismatch=0",
	    digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL") == 0);
	CHECK(g_consume_whole_media != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	CHECK(g_consume_boot_media != 0);
	CHECK(g_consume_boot_media->open_calls == 1);
	CHECK(g_consume_boot_media->close_calls == 1);
	CHECK((g_consume_boot_media->close_client != 0) &&
	    (g_consume_boot_media->close_client != g_self));
	CHECK(g_consume_whole_media->close_client == g_self);
	CHECK(g_consume_boot_media->open_access == kIOStorageAccessReader);
	CHECK(g_consume_whole_media->open_access == kIOStorageAccessReader);
	CHECK(g_self->report.consume_complete == 1);
	CHECK(gta4l_test.consume_alloc_calls == 1);
	CHECK(gta4l_test.consume_alloc_size ==
	    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);
	CHECK(gta4l_test.consume_alloc_alignment == PAGE_SIZE);
	CHECK(gta4l_test.consume_free_calls == 1);
	CHECK(gta4l_test.consume_free_size ==
	    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);
	CHECK(gta4l_test.consume_live_alloc == 0);
	CHECK(gta4l_test.descriptor_alloc_calls == 1);
	CHECK(gta4l_test.descriptor_release_calls == 1);
	CHECK(gta4l_test.consumer_release_calls == 1);
	CHECK(g_self->consumeClient == 0);
	CHECK(g_self->consumeBoot == 0);
	CHECK(g_self->consumeWhole == 0);
	return 0;
}

static int
scenario_consume_media_absent(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(false, false);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=1 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=1 part=0 "
	    "open=0 close=0 alloc=0 client=0 mismatches=0 limit=64 "
	    "seg=1 cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUMEFAIL: reason=1 case=0 target=0 "
	    "start=0 length=0 span=0 requested=0 completed=0 actual=0 "
	    "status=0 digest=00000000000000000000000000000000 "
	    "expected=00000000000000000000000000000000 mismatch=0") != 0);
	return 0;
}

static int
scenario_consume_two_whole(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	(void)add_media(true, 0,
	    (UInt64)gta4l_test.capacity_blocks * GTA4L_EMMC_BLOCK_BYTES,
	    false, "GUID_partition_scheme", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=1 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=1 part=0 open=0 close=0 alloc=0 client=0") != 0);
	return 0;
}

static int
scenario_consume_part_absent(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, false);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=2 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=1 "
	    "open=0 close=0 alloc=0 client=0 mismatches=0 limit=64 "
	    "seg=1 cap=61071360") != 0);
	CHECK(g_consume_whole_media != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_part_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=2 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=1 "
	    "open=0 close=0 alloc=0 client=0 mismatches=0 limit=64 "
	    "seg=1 cap=61071360") != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_part_wrong_base(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
	    GTA4L_EMMC_BLOCK_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=1 open=0 close=0 alloc=0 client=0") != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_part_wrong_size(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES -
	    GTA4L_EMMC_BLOCK_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=1 open=0 close=0 alloc=0 client=0") != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_part_writable(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    true, "Apple_Boot", true);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=1 open=0 close=0 alloc=0 client=0") != 0);
	CHECK(g_consume_whole_media->open_calls == 1);
	CHECK(g_consume_whole_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_open_refused(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	g_consume_whole_media->open_refuse = true;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=0 close=0 alloc=0 client=0") != 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	g_consume_boot_media->open_refuse = true;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=0 close=0 alloc=0 client=1") != 0);
	return 0;
}

static int
scenario_consume_client_failure(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_client_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=14 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=0 close=0 alloc=0 client=0") != 0);
	CHECK(g_consume_boot_media->open_calls == 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_client_init_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=14 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=0 close=0 alloc=0 client=0") != 0);
	return 0;
}

static int
scenario_consume_alloc_failure(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=4 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 "
	    "open=1 close=1 alloc=1 client=1 mismatches=0 limit=64 "
	    "seg=1 cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUMEFAIL: reason=4 case=0 target=1 "
	    "start=0 length=1024 span=1024 requested=0 completed=0 "
	    "actual=0 status=0") != 0);
	CHECK(gta4l_test.consume_alloc_calls == 1);
	CHECK(gta4l_test.consume_free_calls == 0);
	CHECK(g_consume_boot_media->close_calls == 1);
	return 0;
}

static int
scenario_consume_descriptor_failure(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=5 complete=0 reads=0 bytes=0 span=0 blocks=0 "
	    "subrequests=0 max=0 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1") != 0);
	CHECK(gta4l_test.consume_alloc_calls == 1);
	CHECK(gta4l_test.consume_free_calls == 1);
	CHECK(gta4l_test.consume_live_alloc == 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_segment_zero = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=5 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(gta4l_test.consume_live_alloc == 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_segment_short = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=5 complete=0 reads=0 bytes=0 span=0 blocks=0") != 0);
	CHECK(gta4l_test.consume_live_alloc == 0);
	return 0;
}

static int
scenario_consume_read_failure(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_fail_at = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=6 "
	    "complete=0 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=0 max=0 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUMEFAIL: reason=6 case=0 target=1 "
	    "start=0 length=1024 span=1024 requested=2 completed=0 "
	    "actual=0") != 0);
	CHECK(gta4l_test.consume_live_alloc == 0);
	return 0;
}

static int
scenario_consume_short_read(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_short_at = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=7 "
	    "complete=0 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=1 max=2 foreign=0 fails=1 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUMEFAIL: reason=7 case=0 target=1 "
	    "start=0 length=1024 span=1024 requested=2 completed=1 "
	    "actual=1023") != 0);
	return 0;
}

static int
scenario_consume_digest_mismatch(void)
{
	uint8_t expected_digest[32];
	char digest[33];

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_corrupt_at = 0;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	consume_expected_digest(expected_digest);
	partread_digest_hex(expected_digest, digest);
	CHECK(strstr(gta4l_test.panic, digest) == 0);
	return 0;
}

static int
scenario_consume_foreign(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_foreign_at = 0;
	gta4l_test.breakup_foreign_mode = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=13 "
	    "complete=0 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=1 max=2 foreign=1 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUMEFAIL: reason=13 case=0 target=1 "
	    "start=0 length=1024 span=1024 requested=2 completed=2 "
	    "actual=1024") != 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_foreign_at = 0;
	gta4l_test.breakup_foreign_mode = 2;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0 reads=1 bytes=1024 span=1024 "
	    "blocks=2 subrequests=1 max=2 foreign=1") != 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_foreign_at = 0;
	gta4l_test.breakup_foreign_mode = 3;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0 reads=1 bytes=1024 span=1024 "
	    "blocks=2 subrequests=1 max=2 foreign=1") != 0);
	return 0;
}

static int
scenario_consume_order(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.breakup_split_mode = 2;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=12 "
	    "complete=0 reads=1 bytes=1024 span=1024 blocks=2 "
	    "subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0 "
	    "media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	return 0;
}

static int
scenario_consume_budget(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	gta4l_test.consume_advance_on_open = 3000;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: cases=1 passed=0 failed=1 reason=9 "
	    "complete=0 reads=0 bytes=0 span=0 blocks=0 subrequests=0 "
	    "max=0 foreign=0 fails=0 cancel=0 budget=1 media=0 part=0 "
	    "open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 "
	    "seg=1 cap=61071360") != 0);
	CHECK(gta4l_test.consume_alloc_calls == 0);
	return 0;
}

static int
scenario_consume_cancel(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	g_consume_cancel_at = 0;
	gta4l_test.breakup_cancel_at = 0;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.consume_cancel == 1);
	CHECK(g_self->report.consume_reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_CANCEL);
	CHECK(g_self->report.consume_complete == 0);
	CHECK(g_consume_whole_media->close_calls == 1);
	CHECK(g_consume_boot_media->close_calls == 1);
	CHECK(gta4l_test.consume_live_alloc == 0);
	CHECK(gta4l_test.descriptor_alloc_calls == 1);
	CHECK(gta4l_test.descriptor_release_calls == 1);
	CHECK(gta4l_test.consumer_release_calls == 1);
	CHECK(g_self->consumeClient == 0);
	CHECK(g_self->consumeBoot == 0);
	CHECK(g_self->consumeWhole == 0);

	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->report.consume_cancel == 1);
	CHECK(g_self->report.consume_reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_CANCEL);
	CHECK(g_self->report.consume_reads == 0);
	CHECK(gta4l_test.descriptor_alloc_calls == 0);
	CHECK(gta4l_test.descriptor_release_calls == 0);
	CHECK(gta4l_test.consumer_release_calls == 0);
	CHECK(g_self->consumeClient == 0);
	CHECK(g_self->consumeBoot == 0);
	CHECK(g_self->consumeWhole == 0);
	return 0;
}

static int
scenario_consume_watchdog(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1");
	CHECK(g_started);
	consume_setup(true, true);
	g_consume_watchdog_at = 0;
	CHECK(invoke_thread(0) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 CONSUME: ") != 0);
	CHECK(gta4l_test.consume_free_calls == 1);
	CHECK(gta4l_test.descriptor_alloc_calls == 1);
	CHECK(gta4l_test.descriptor_release_calls == 1);
	CHECK(gta4l_test.consumer_release_calls == 1);
	return 0;
}

static void
width_expected_digest(uint8_t *digest)
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES];
	uint64_t first = GTA4L_EMMC_BLOCK_WIDTH_LBA;

	gta4l_test_fill_sector(first, scratch);
	gta4l_test_fill_sector(first + 1u,
	    scratch + GTA4L_EMMC_BLOCK_BYTES);
	gta4l_emmc_block_sha256(scratch,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, digest);
}

static void
width_fail_line(char *text, size_t size, uint32_t stage, int32_t status,
    uint64_t actual, const uint8_t *digest)
{
	static const uint8_t zero[16] = { 0 };
	char digestText[33];

	partread_digest_hex((digest != 0) ? digest : zero, digestText);
	snprintf(text, size,
	    "GTA4L-EMMC P5 WIDTHFAIL: stage=%u status=%u actual=%llu "
	    "digest=%s", stage, (uint32_t)status,
	    (unsigned long long)actual, digestText);
}

static IOMedia *g_widthhold_whole_media;
static IOMedia *g_widthhold_boot_media;

static void
widthhold_media_reset(void)
{
	g_widthhold_whole_media = 0;
	g_widthhold_boot_media = 0;
}

static void
widthhold_setup(bool withWhole, bool withBoot)
{
	widthhold_media_reset();
	/* The production storage driver splits requests against the
	 * advertised 64-block / 32 KiB / one-segment limits before the
	 * adapter sees them. A two-block case must arrive as one part. */
	gta4l_test.breakup_model = 1;
	if (withWhole) {
		g_widthhold_whole_media = add_media(true, 0,
		    (UInt64)gta4l_test.capacity_blocks *
		    GTA4L_EMMC_BLOCK_BYTES, false,
		    "GUID_partition_scheme", true);
	}
	if (withBoot) {
		g_widthhold_boot_media = add_media(false,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
		    false, "Apple_Boot", true);
	}
}

static void
widthhold_expected_digest(uint64_t offset, uint32_t length, uint8_t *digest)
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES];
	uint64_t absolute = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
	    offset;
	uint64_t block = absolute / GTA4L_EMMC_BLOCK_BYTES;
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t index;

	for (index = 0; index < length; index++) {
		if ((index % GTA4L_EMMC_BLOCK_BYTES) == 0u) {
			gta4l_test_fill_sector(
			    block + (index / GTA4L_EMMC_BLOCK_BYTES),
			    sector);
		}
		scratch[index] =
		    sector[index % GTA4L_EMMC_BLOCK_BYTES];
	}
	gta4l_emmc_block_sha256(scratch, length, digest);
}

static int
scenario_widthhold_match(void)
{
	uint8_t reference[32];
	uint8_t last[32];
	char referenceText[33];
	char lastText[33];
	char expected[512];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(invoke_thread(1) == 1);
	CHECK(gta4l_test.panic != 0);
	widthhold_expected_digest(0,
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES, reference);
	widthhold_expected_digest(3072,
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES, last);
	partread_digest_hex(reference, referenceText);
	partread_digest_hex(last, lastText);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=4 failed=0 reason=0 "
	    "complete=1 switch=4 verified=1 hb=0 ha=2 ");
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	snprintf(expected, sizeof(expected),
	    "cmd6=1 refs=1 refbytes=4096 refblocks=8 refstatus=0 "
	    "refchunks=1 refstops=1 refstopfail=0 refdigest=%s "
	    "reads=4 bytes=4096 blocks=8 subrequests=4 max=2 c18=4 "
	    "stops=4 stopfail=0 match=1 mismatches=0 fails=0 "
	    "foreign=0 cancel=0 budget=0 obsdigest=%s "
	    "partbase=1154744320 partsize=100663296 partro=1 "
	    "partdiag=0 revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 "
	    "rstatus=0 limit=64 seg=1 cap=61071360",
	    referenceText, referenceText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 WIDTHHOLDREAD: index=3 lba=2255366 "
	    "start=3072 length=1024 span=1024 actual=1024 status=0 "
	    "subrequests=1 blocks=2 max=2 digest=%s predigest=%s "
	    "match=1", lastText, lastText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDFAIL") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTH:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART1:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART2:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART3:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL3") == 0);
	CHECK(g_self->report.widthhold_complete == 1);
	CHECK(g_self->report.widthhold_partdiag == 0u);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.width_host_control == 0);
	CHECK(g_self->block.counters.cmd18_chunks == 5);
	CHECK(g_self->block.counters.stop_failures == 0);
	CHECK(g_widthhold_whole_media != 0);
	CHECK(g_widthhold_whole_media->open_calls == 0);
	CHECK(g_widthhold_boot_media != 0);
	CHECK(g_widthhold_boot_media->open_calls == 1);
	CHECK(g_widthhold_boot_media->close_calls == 1);
	CHECK(g_widthhold_boot_media->open_access ==
	    kIOStorageAccessReader);
	CHECK(g_widthhold_boot_media->close_client != g_self);
	CHECK(g_self->widthholdBoot == 0);
	CHECK(g_self->widthholdWhole == 0);
	CHECK(g_self->widthholdClient == 0);
	return 0;
}

static int
scenario_widthhold_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3") == 0);
	CHECK(g_started);
	CHECK(g_self->widthholdMode == false);
	CHECK(g_self->report.widthhold == 0);
	CHECK(gta4l_test.width_ext_csd_calls == 0);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTH") == 0);
	return 0;
}

static int
scenario_widthhold_refused_orphan(void)
{
	begin("gta4l-emmc-widthhold=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-widthhold requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_widthhold_refused_block(void)
{
	static const char *arguments[] = {
		"gta4l-emmc-widthhold=1",
		"gta4l-emmc-block=1 gta4l-emmc-widthhold=1",
		"gta4l-emmc-block=2 gta4l-emmc-widthhold=1",
		"gta4l-emmc-block=4 gta4l-emmc-widthhold=1",
	};
	unsigned int index;

	for (index = 0;
	    index < sizeof(arguments) / sizeof(arguments[0]); index++) {
		begin(arguments[index]);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK((strstr(gta4l_test.log,
		    "requires gta4l-emmc-block=3") != 0) ||
		    (strstr(gta4l_test.log,
		    "malformed gta4l-emmc-block value") != 0) ||
		    (strstr(gta4l_test.log,
		    "publish-only mode 2 is deferred") != 0));
	}
	return 0;
}

static int
scenario_widthhold_refused_value(void)
{
	static const int values[] = { 0, 2 };
	char arguments[64];
	unsigned int index;

	for (index = 0;
	    index < sizeof(values) / sizeof(values[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-widthhold=%d",
		    values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-widthhold value must be 1") != 0);
	}
	return 0;
}

static int
scenario_widthhold_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1 "
	    "gta4l-emmc-widthhold=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-widthhold token") != 0);
	return 0;
}

static int
scenario_widthhold_refused_conflicts(void)
{
	static const struct {
		const char *token;
		const char *name;
	} conflicts[] = {
		{ "gta4l-emmc-partobs=1", "partobs" },
		{ "gta4l-emmc-partcmp=1", "partcmp" },
		{ "gta4l-emmc-partread=1", "partread" },
		{ "gta4l-emmc-deblock=1", "deblock" },
		{ "gta4l-emmc-breakup=1", "breakup" },
		{ "gta4l-emmc-sweep=1", "sweep" },
		{ "gta4l-emmc-consume=1", "consume" },
		{ "gta4l-emmc-width=4", "width" },
	};
	char arguments[128];
	char expected[128];
	unsigned int index;

	for (index = 0;
	    index < sizeof(conflicts) / sizeof(conflicts[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 %s gta4l-emmc-widthhold=1",
		    conflicts[index].token);
		snprintf(expected, sizeof(expected),
		    "gta4l-emmc-%s and gta4l-emmc-widthhold are mutually "
		    "exclusive", conflicts[index].name);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log, expected) != 0);
	}
	return 0;
}

static void
widthhold_fail_line(char *text, size_t size, uint32_t index,
    uint32_t status, uint32_t actual, uint32_t start, uint32_t length,
    uint32_t span, uint32_t blocks, uint32_t max, const uint8_t *digest)
{
	static const uint8_t zero[16] = { 0 };
	char digestText[33];

	partread_digest_hex((digest != 0) ? digest : zero, digestText);
	snprintf(text, size,
	    "GTA4L-EMMC P5 WIDTHHOLDFAIL: index=%u status=%u actual=%u "
	    "start=%u length=%u span=%u blocks=%u max=%u digest=%s",
	    index, status, actual, start, length, span, blocks, max,
	    digestText);
}

static void
widthhold_baseline_fixture(void)
{
	gta4l_test.width_ext_csd_fail = -5;
}

static int
scenario_widthhold_baseline_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_baseline_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=0 failed=1 reason=1 "
	    "complete=0 switch=0 verified=0 hb=0 ha=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=0 refchunks=0 refstops=0 refstopfail=0") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    (uint32_t)-5, 0, 0, 0, 0, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDREAD") == 0);
	return 0;
}

static void
widthhold_preread_fixture(void)
{
	gta4l_test.fail_multi_at = 0;
}

static int
scenario_widthhold_preread_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_preread_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0 switch=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=4294967291 refchunks=1 refstops=0 "
	    "refstopfail=0") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    (uint32_t)-5, 0, 0, 0, 0, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
widthhold_preread_short_fixture(void)
{
	gta4l_test.multi_short_at = 0;
	gta4l_test.multi_short_blocks = 1;
}

static int
scenario_widthhold_preread_short(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_preread_short_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=4294967295 refchunks=1 refstops=1 "
	    "refstopfail=0") != 0);
	return 0;
}

static void
widthhold_preread_stop_missing_fixture(void)
{
	gta4l_test.multi_stop_attempted = 0;
}

static int
scenario_widthhold_preread_stop_missing(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_preread_stop_missing_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=4294967295 refchunks=1 refstops=0 "
	    "refstopfail=0") != 0);
	return 0;
}

static void
widthhold_preread_stop_failure_fixture(void)
{
	gta4l_test.multi_stop_at = 0;
	gta4l_test.multi_stop_result = -5;
}

static int
scenario_widthhold_preread_stop_failure(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_preread_stop_failure_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=2 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=4294967291 refchunks=1 refstops=1 "
	    "refstopfail=1") != 0);
	return 0;
}

static void
widthhold_switch_failure_fixture(void)
{
	gta4l_test.width_switch_fail = GTA4L_EMMC_ERR_BUS_WIDTH;
}

static int
scenario_widthhold_switch_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_switch_failure_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 switch=4 verified=0 hb=0 ha=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, " cmd6=1 ") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    (uint32_t)GTA4L_EMMC_ERR_BUS_WIDTH, 0, 0, 0, 0, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
widthhold_switch_unverified_fixture(void)
{
	gta4l_test.width_switch_verified = 0;
}

static int
scenario_widthhold_switch_unverified(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_switch_unverified_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 switch=4 verified=0 hb=0 ha=2 ") != 0);
	return 0;
}

static void
widthhold_switch_host_fixture(void)
{
	gta4l_test.width_switch_host_bit = 0;
}

static int
scenario_widthhold_switch_host(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_switch_host_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 switch=4 verified=1 hb=0 ha=0 ") != 0);
	return 0;
}

static void
widthhold_switch_restore_fixture(void)
{
	gta4l_test.width_switch_fail = GTA4L_EMMC_ERR_WIDTH_RESTORE;
}

static int
scenario_widthhold_switch_restore(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_switch_restore_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    (uint32_t)GTA4L_EMMC_ERR_WIDTH_RESTORE, 0, 0, 0, 0, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
widthhold_base4_fixture(void)
{
	gta4l_test.fail_read_at = 0;
}

static int
scenario_widthhold_base4(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_base4_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.width_host_control == 0);
	CHECK(strstr(gta4l_test.panic,
	    "reason=18 complete=0 switch=4 verified=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 rstatus=0") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    (uint32_t)-5, 0, 0, 0, 0, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
widthhold_advert_fixture(void)
{
	gta4l_test.property_fail_at = 0;
}

static int
scenario_widthhold_advert(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_advert_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=19 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static void
widthhold_watchdog_thread_fixture(void)
{
	gta4l_test.thread_start_fail_at = 0;
	gta4l_test.thread_start_fail_result = -1;
}

static int
scenario_widthhold_thread_watchdog(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_watchdog_thread_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=20 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "status=16") != 0);
	return 0;
}

static void
widthhold_reporter_thread_fixture(void)
{
	gta4l_test.thread_start_fail_at = 1;
	gta4l_test.thread_start_fail_result = -1;
}

static int
scenario_widthhold_thread_reporter(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_reporter_thread_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.thread_count == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=20 complete=0") != 0);
	return 0;
}

static void
widthhold_alloc_start_fixture(void)
{
	gta4l_test.consume_alloc_fail = 1;
}

static int
scenario_widthhold_alloc_start(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_alloc_start_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_ext_csd_calls == 0);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.consume_alloc_calls == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=21 complete=0") != 0);
	return 0;
}

static uint32_t g_widthhold_advance_call;
static uint32_t g_widthhold_advance_ms;
static int g_widthhold_cancel_call;
static int g_widthhold_watchdog_call;
static int g_widthhold_gate_call;

static void
widthhold_hook_reset(void)
{
	g_widthhold_advance_call = 0xffffffffu;
	g_widthhold_advance_ms = 0;
	g_widthhold_cancel_call = -1;
	g_widthhold_watchdog_call = -1;
	g_widthhold_gate_call = -1;
}

static void
widthhold_multi_hook(uint32_t call)
{
	if ((g_widthhold_advance_call != 0xffffffffu) &&
	    (call == g_widthhold_advance_call)) {
		gta4l_test.now_ms += g_widthhold_advance_ms;
	}
	if ((g_widthhold_cancel_call >= 0) &&
	    (call == (uint32_t)g_widthhold_cancel_call)) {
		g_self->reportCancel = true;
	}
	if ((g_widthhold_watchdog_call >= 0) &&
	    (call == (uint32_t)g_widthhold_watchdog_call)) {
		(void)invoke_thread(0);
	}
	if ((g_widthhold_gate_call >= 0) &&
	    (call == (uint32_t)g_widthhold_gate_call)) {
		IOLockLock(g_self->gate);
	}
}

static void
widthhold_cancel_reference_fixture(void)
{
	widthhold_hook_reset();
	g_widthhold_cancel_call = 0;
	gta4l_test_multi_hook = widthhold_multi_hook;
}

static int
scenario_widthhold_cancel_pre_switch(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthhold=1",
	    widthhold_cancel_reference_fixture) == 0);
	CHECK(!g_started);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(g_self->reportCancel);
	return 0;
}

static int
scenario_widthhold_media_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(false, false);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=0 failed=1 reason=4 "
	    "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=0 refchunks=1 refstops=1 refstopfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS);
	return 0;
}

static int
scenario_widthhold_media_writable(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_whole_media->writable_value = true;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "whole=1") != 0);
	CHECK(g_widthhold_boot_media->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_media_writable_missing(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_whole_media->property_faults =
	    GTA4L_TEST_PROP_WRITABLE_MISSING;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	CHECK(g_widthhold_boot_media->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_media_writable_wrong(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_whole_media->property_faults =
	    GTA4L_TEST_PROP_WRITABLE_WRONG;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	return 0;
}

static int
scenario_widthhold_media_size_missing(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_whole_media->property_faults =
	    GTA4L_TEST_PROP_SIZE_MISSING;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS);
	return 0;
}

static int
scenario_widthhold_media_two_whole(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	(void)add_media(true, 0,
	    (UInt64)gta4l_test.capacity_blocks *
	    GTA4L_EMMC_BLOCK_BYTES, false,
	    "GUID_partition_scheme", true);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	CHECK(g_widthhold_boot_media->open_calls == 0);
	return 0;
}

static int
widthhold_part_failed(uint32_t diag)
{
	char expected[32];

	snprintf(expected, sizeof(expected), "partdiag=%u ", diag);
	CHECK(strstr(gta4l_test.panic, "reason=5 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic,
	    "partbase=0 partsize=0 partro=0") != 0);
	CHECK(g_self->report.widthhold_partdiag == diag);
	CHECK(g_self->report.widthhold_reads == 0u);
	CHECK(g_self->report.widthhold_failed == 1u);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_part_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND) == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(g_widthhold_whole_media->open_calls == 0);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_DEADLINE_MS);
	return 0;
}

static int
scenario_widthhold_part_duplicate(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_DUPLICATE) == 0);
	CHECK(g_widthhold_boot_media->open_calls == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_wrong_base(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
	    GTA4L_EMMC_BLOCK_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND) == 0);
	return 0;
}

static int
scenario_widthhold_part_wrong_size(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES -
	    GTA4L_EMMC_BLOCK_BYTES,
	    false, "Apple_Boot", true);
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND) == 0);
	return 0;
}

static int
scenario_widthhold_part_base_late(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_BASE_MISSING_LATE;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_BASE) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_size_late(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_SIZE_WRONG_LATE;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_SIZE) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_writable(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    true, "Apple_Boot", true);
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE) == 0);
	return 0;
}

static int
scenario_widthhold_part_writable_missing(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_WRITABLE_MISSING;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_writable_wrong(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_WRITABLE_WRONG;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_content_missing(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_CONTENT_MISSING;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_widthhold_part_content_wrong(void)
{
	IOMedia *boot;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_CONTENT_WRONG;
	CHECK(invoke_thread(1) == 1);
	CHECK(widthhold_part_failed(
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT) == 0);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
widthhold_content_accepted(const char *content)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, content, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHHOLD: cases=4 passed=4 failed=0 reason=0 "
	    "complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "partdiag=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "partro=1 partdiag=0") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDFAIL") == 0);
	CHECK(g_self->report.widthhold_partdiag == 0u);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static int
scenario_widthhold_part_content_name(void)
{
	return widthhold_content_accepted("Apple_Boot");
}

static int
scenario_widthhold_part_content_guid(void)
{
	/* Synthetic, deliberately non-device type-GUID string. */
	return widthhold_content_accepted(
	    "11111111-2222-4333-8444-555555555555");
}

static int
scenario_widthhold_open_refused(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_boot_media->open_refuse = true;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=6 complete=0") != 0);
	CHECK(g_widthhold_boot_media->open_calls == 1);
	CHECK(g_widthhold_boot_media->close_calls == 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_alloc_case(void)
{
	char expected[256];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.consume_alloc_fail = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=21 complete=0 switch=4 verified=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    " reads=0 bytes=0 blocks=0 subrequests=0 max=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(g_widthhold_boot_media->close_calls == 1);
	widthhold_fail_line(expected, sizeof(expected), 0, 0, 0, 0,
	    1024, 1024, 2, 2, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static int
scenario_widthhold_descriptor(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=8 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    " reads=0 bytes=0 blocks=0 subrequests=0 max=0 ") != 0);
	CHECK(gta4l_test.consume_live_alloc == 0);

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.consume_segment_zero = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=8 complete=0") != 0);

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.consume_segment_short = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=8 complete=0") != 0);
	return 0;
}

static int
scenario_widthhold_read_error(void)
{
	char expected[256];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.fail_multi_at = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=9 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=1 bytes=0 blocks=2 subrequests=1 max=2 c18=0 "
	    "stops=0 stopfail=0 match=0 mismatches=0 fails=1") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDFAIL: index=0 status=") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0,
	    0xe00002cau, 0, 0, 1024, 1024, 2, 2, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static IOReturn
widthhold_short_read_hook(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount)
{
	uint64_t absolute = media->base_value + byteStart;
	uint64_t block = absolute / GTA4L_EMMC_BLOCK_BYTES;
	uint32_t offset;
	uint8_t *target = static_cast<uint8_t *>(buffer->address());

	for (offset = 0; offset < buffer->getLength();
	    offset += GTA4L_EMMC_BLOCK_BYTES) {
		gta4l_test_fill_sector(block +
		    (offset / GTA4L_EMMC_BLOCK_BYTES), target + offset);
	}
	if (actualByteCount != 0) {
		*actualByteCount = buffer->getLength() - 1u;
	}
	return kIOReturnSuccess;
}

static int
scenario_widthhold_short(void)
{
	char expected[256];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_media_read_hook = widthhold_short_read_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=10 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=1 bytes=1023 blocks=0 subrequests=0 max=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "fails=1") != 0);
	widthhold_fail_line(expected, sizeof(expected), 0, 0, 1023, 0,
	    1024, 1024, 2, 2, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_match_mismatch(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.multi_corrupt_at = 1;
	gta4l_test.multi_corrupt_count = 1;
	gta4l_test.multi_corrupt_block =
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;
	gta4l_test.multi_corrupt_pair = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=11 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "match=0 mismatches=1 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHHOLDFAIL: index=0 status=0 actual=1024 start=0 "
	    "length=1024 span=1024 blocks=2 max=2 digest=") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHHOLDFAIL: index=0 status=0 actual=1024 start=0 "
	    "length=1024 span=1024 blocks=2 max=2 "
	    "digest=00000000000000000000000000000000") == 0);
	CHECK(strstr(gta4l_test.panic,
	    "obsdigest=00000000000000000000000000000000") != 0);
	CHECK(g_self->report.widthhold_complete == 0);
	CHECK(g_self->report.widthhold_last_match == 0);
	return 0;
}

static int
scenario_widthhold_foreign_tag(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_tag_delta = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "subrequests=0 max=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "foreign=1 cancel=0 budget=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_foreign_block(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_block_delta = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "foreign=1 cancel=0 budget=0") != 0);
	return 0;
}

static int
scenario_widthhold_order_extra(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_extra = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=12 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=1 bytes=1024 blocks=2 subrequests=1 max=2 c18=1 "
	    "stops=1 stopfail=0 match=0 mismatches=0 fails=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "foreign=0 cancel=0 budget=0") != 0);
	return 0;
}

static int
scenario_widthhold_foreign_extra(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_extra = 2;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "foreign=1 cancel=0 budget=0") != 0);
	return 0;
}

static int
scenario_widthhold_budget(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 1;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_READ_BUDGET_MS;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=1 failed=1 "
	    "reason=14 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=1 bytes=1024 blocks=2") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_budget_boundary(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 1;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_READ_BUDGET_MS - 1u;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=4 failed=0 "
	    "reason=0 complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=0") != 0);
	return 0;
}

static int
scenario_widthhold_cancel_post_switch(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_self->reportCancel = true;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=0 failed=1 "
	    "reason=15 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDREAD") == 0);
	CHECK(gta4l_test.panic_calls == 1);
	return 0;
}

static int
scenario_widthhold_cancel_case(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_cancel_call = 1;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=1 failed=1 "
	    "reason=15 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.panic_calls == 1);
	return 0;
}

static void
widthhold_cancel_before_revert_hook(uint32_t phase)
{
	if (phase == GTA4L_TEST_WIDTHHOLD_PHASE_BEFORE_REVERT) {
		g_self->reportCancel = true;
	}
}

static int
scenario_widthhold_cancel_before_revert(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_widthhold_hook = widthhold_cancel_before_revert_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "passed=4 failed=1 reason=15 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static void
widthhold_cancel_after_revert_hook(uint32_t phase)
{
	if (phase == GTA4L_TEST_WIDTHHOLD_PHASE_AFTER_REVERT) {
		g_self->reportCancel = true;
	}
}

static int
scenario_widthhold_cancel_after_revert(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_widthhold_hook = widthhold_cancel_after_revert_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "passed=4 failed=1 reason=15 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.panic_calls == 1);
	return 0;
}

static int
scenario_widthhold_cancel_first_failure(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_cancel_call = 1;
	gta4l_test_multi_hook = widthhold_multi_hook;
	gta4l_test.fail_multi_at = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=9 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_widthhold_gate_timeout(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_gate_call = 4;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=16 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	CHECK(gta4l_test.now_ms >= GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_MS);
	return 0;
}

static int
scenario_widthhold_revert_failure(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.width_recover_fail = -5;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "passed=4 failed=1 reason=17 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "revert=1 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "rstatus=4294967291") != 0);
	CHECK(!g_self->online);
	return 0;
}

static int
scenario_widthhold_revert_unverified(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.width_recover_verified = 0;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=17 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "revert=1 rverified=0") != 0);
	return 0;
}

static int
scenario_widthhold_revert_card(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.width_recover_card_keep = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=17 complete=0") != 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	return 0;
}

static int
scenario_widthhold_watchdog_before(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(0) == 1);
	CHECK(g_self->terminalClaimed);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHHOLD: ") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDREAD") == 0);
	CHECK(g_self->report.widthhold_complete == 0);
	return 0;
}

static int
scenario_widthhold_watchdog_case(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_watchdog_call = 4;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDREAD") == 0);
	return 0;
}

static void
widthhold_watchdog_after_revert_hook(uint32_t phase)
{
	if (phase == GTA4L_TEST_WIDTHHOLD_PHASE_AFTER_REVERT) {
		(void)invoke_thread(0);
	}
}

static int
scenario_widthhold_watchdog_after_revert(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthhold=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_widthhold_hook = widthhold_watchdog_after_revert_hook;
	CHECK(invoke_thread(1) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLD") != 0);
	return 0;
}

static void
widthpub_fail_line(char *text, size_t size, uint32_t index,
    uint32_t status, uint32_t actual, uint32_t start, uint32_t length,
    uint32_t span, uint32_t blocks, uint32_t max, const uint8_t *digest)
{
	static const uint8_t zero[16] = { 0 };
	char digestText[33];

	partread_digest_hex((digest != 0) ? digest : zero, digestText);
	snprintf(text, size,
	    "GTA4L-EMMC P5 WIDTHPUBFAIL: index=%u status=%u actual=%u "
	    "start=%u length=%u span=%u blocks=%u max=%u digest=%s",
	    index, status, actual, start, length, span, blocks, max,
	    digestText);
}

static int
scenario_widthpub_match(void)
{
	uint8_t reference[32];
	uint8_t last[32];
	char referenceText[33];
	char lastText[33];
	char expected[768];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	CHECK(g_self->widthpubMode);
	CHECK(!g_self->widthholdMode);
	CHECK(g_self->report.widthpub == 1u);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(gta4l_test.panic != 0);
	widthhold_expected_digest(0,
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES, reference);
	widthhold_expected_digest(3072,
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES, last);
	partread_digest_hex(reference, referenceText);
	partread_digest_hex(last, lastText);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=8 failed=0 reason=0 "
	    "complete=1 pub=1 switch=4 verified=1 hb=0 ha=2 ") != 0);
	snprintf(expected, sizeof(expected),
	    "cmd6=1 refs=1 refbytes=4096 refblocks=8 refstatus=0 "
	    "refchunks=1 refstops=1 refstopfail=0 refdigest=%s "
	    "reads=8 bytes=8192 blocks=16 subrequests=8 max=2 c18=8 "
	    "stops=8 stopfail=0 match=1 mismatches=0 fails=0 "
	    "foreign=0 cancel=0 budget=0 obsdigest=%s "
	    "partbase=1154744320 partsize=100663296 partro=1 "
	    "partdiag=0 revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 "
	    "rstatus=0 limit=64 seg=1 cap=61071360",
	    referenceText, referenceText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 WIDTHPUBREAD: index=7 lba=2255366 "
	    "start=3072 length=1024 span=1024 actual=1024 status=0 "
	    "subrequests=1 blocks=2 max=2 digest=%s predigest=%s "
	    "match=1", lastText, lastText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHPUBFAIL") == 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLD") == 0);
	CHECK(g_self->report.widthpub_pub == 1u);
	CHECK(g_self->report.widthhold_complete == 1);
	CHECK(g_self->report.widthhold_cases ==
	    GTA4L_EMMC_BLOCK_WIDTHPUB_CASES);
	CHECK(g_self->report.widthhold_passed ==
	    GTA4L_EMMC_BLOCK_WIDTHPUB_CASES);
	CHECK(g_self->report.widthhold_last_index == 7u);
	CHECK(g_self->report.widthhold_last_start == 3072u);
	CHECK(g_self->report.widthhold_last_lba == 2255366u);
	CHECK(gta4l_test.multi_calls == 9);
	CHECK(gta4l_test.message_count == 0);
	CHECK(!g_self->offlineNotified);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.width_host_control == 0);
	CHECK(g_self->block.counters.cmd18_chunks == 9);
	CHECK(g_self->block.counters.stop_failures == 0);
	CHECK(g_widthhold_whole_media != 0);
	CHECK(g_widthhold_whole_media->open_calls == 0);
	CHECK(g_widthhold_boot_media != 0);
	CHECK(g_widthhold_boot_media->open_calls == 1);
	CHECK(g_widthhold_boot_media->close_calls == 1);
	CHECK(g_widthhold_boot_media->open_access ==
	    kIOStorageAccessReader);
	CHECK(g_self->widthholdBoot == 0);
	CHECK(g_self->widthholdWhole == 0);
	CHECK(g_self->widthholdClient == 0);
	return 0;
}

static int
scenario_widthpub_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3") == 0);
	CHECK(g_started);
	CHECK(!g_self->widthpubMode);
	CHECK(!g_self->widthholdMode);
	CHECK(g_self->report.widthpub == 0);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic, "WIDTHPUB") == 0);
	return 0;
}

static int
scenario_widthpub_media_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(false, false);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=0 failed=1 reason=4 "
	    "complete=0 pub=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "refstatus=0 refchunks=1 refstops=1 refstopfail=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS);
	CHECK(g_self->report.widthpub_pub == 0u);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->offlineNotified);
	return 0;
}

static int
scenario_widthpub_refused_orphan(void)
{
	begin("gta4l-emmc-widthpub=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-widthpub requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_widthpub_refused_block(void)
{
	static const char *arguments[] = {
		"gta4l-emmc-widthpub=1",
		"gta4l-emmc-block=1 gta4l-emmc-widthpub=1",
		"gta4l-emmc-block=2 gta4l-emmc-widthpub=1",
		"gta4l-emmc-block=4 gta4l-emmc-widthpub=1",
	};
	unsigned int index;

	for (index = 0;
	    index < sizeof(arguments) / sizeof(arguments[0]); index++) {
		begin(arguments[index]);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK((strstr(gta4l_test.log,
		    "requires gta4l-emmc-block=3") != 0) ||
		    (strstr(gta4l_test.log,
		    "malformed gta4l-emmc-block value") != 0) ||
		    (strstr(gta4l_test.log,
		    "publish-only mode 2 is deferred") != 0));
	}
	return 0;
}

static int
scenario_widthpub_refused_value(void)
{
	static const int values[] = { 0, 2 };
	char arguments[64];
	unsigned int index;

	for (index = 0;
	    index < sizeof(values) / sizeof(values[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-widthpub=%d",
		    values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-widthpub value must be 1") != 0);
	}
	return 0;
}

static int
scenario_widthpub_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1 "
	    "gta4l-emmc-widthpub=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-widthpub token") != 0);
	return 0;
}

static int
scenario_widthpub_refused_conflicts(void)
{
	static const struct {
		const char *token;
		const char *name;
	} conflicts[] = {
		{ "gta4l-emmc-partobs=1", "partobs" },
		{ "gta4l-emmc-partcmp=1", "partcmp" },
		{ "gta4l-emmc-partread=1", "partread" },
		{ "gta4l-emmc-deblock=1", "deblock" },
		{ "gta4l-emmc-breakup=1", "breakup" },
		{ "gta4l-emmc-sweep=1", "sweep" },
		{ "gta4l-emmc-consume=1", "consume" },
		{ "gta4l-emmc-width=4", "width" },
		{ "gta4l-emmc-widthhold=1", "widthhold" },
	};
	char arguments[128];
	char expected[128];
	unsigned int index;

	for (index = 0;
	    index < sizeof(conflicts) / sizeof(conflicts[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 %s gta4l-emmc-widthpub=1",
		    conflicts[index].token);
		snprintf(expected, sizeof(expected),
		    "gta4l-emmc-%s and gta4l-emmc-widthpub are mutually "
		    "exclusive", conflicts[index].name);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log, expected) != 0);
	}
	return 0;
}

static int
scenario_widthpub_budget(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 4;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_READ_BUDGET_MS;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=4 failed=1 "
	    "reason=14 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=4 bytes=4096 blocks=8") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->offlineNotified);
	CHECK(g_self->report.widthpub_pub == 1u);
	return 0;
}

static int
scenario_widthpub_budget_boundary(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 4;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_READ_BUDGET_MS - 1u;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=8 failed=0 "
	    "reason=0 complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=0") != 0);
	CHECK(gta4l_test.message_count == 0);
	return 0;
}

static int
scenario_widthpub_read_error(void)
{
	char expected[256];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.fail_multi_at = 5;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=9 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=5 bytes=4096 blocks=10 subrequests=5 max=2 c18=4 "
	    "stops=4 stopfail=0 match=0 mismatches=0 fails=1") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHPUBFAIL: index=4 status=") != 0);
	widthpub_fail_line(expected, sizeof(expected), 4,
	    0xe00002cau, 0, 0, 1024, 1024, 2, 2, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->offlineNotified);
	return 0;
}

static int
scenario_widthpub_short(void)
{
	char expected[256];

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_media_read_hook = widthhold_short_read_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=10 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "reads=1 bytes=1023 blocks=0 subrequests=0 max=0 ") != 0);
	CHECK(strstr(gta4l_test.panic, "fails=1") != 0);
	widthpub_fail_line(expected, sizeof(expected), 0, 0, 1023, 0,
	    1024, 1024, 2, 2, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 1);
	return 0;
}

static int
scenario_widthpub_match_mismatch(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.multi_corrupt_at = 5;
	gta4l_test.multi_corrupt_count = 1;
	gta4l_test.multi_corrupt_block =
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;
	gta4l_test.multi_corrupt_pair = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=11 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "match=0 mismatches=1 fails=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHPUBFAIL: index=4 status=0 actual=1024 start=0 "
	    "length=1024 span=1024 blocks=2 max=2 digest=") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "WIDTHPUBFAIL: index=4 status=0 actual=1024 start=0 "
	    "length=1024 span=1024 blocks=2 max=2 "
	    "digest=00000000000000000000000000000000") == 0);
	CHECK(strstr(gta4l_test.panic,
	    "obsdigest=00000000000000000000000000000000") != 0);
	CHECK(g_self->report.widthhold_complete == 0);
	CHECK(g_self->report.widthhold_last_match == 0);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->offlineNotified);
	return 0;
}

static int
scenario_widthpub_foreign_tag(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_tag_delta = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=13 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "foreign=1 cancel=0 budget=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 1);
	return 0;
}

static int
scenario_widthpub_cancel_case(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_cancel_call = 5;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=5 failed=1 "
	    "reason=15 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 1);
	return 0;
}

static int
scenario_widthpub_gate_timeout(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_gate_call = 8;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=16 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	CHECK(gta4l_test.now_ms >= GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_MS);
	CHECK(gta4l_test.message_count == 1);
	return 0;
}

static int
scenario_widthpub_revert_failure(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-widthpub=1") == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.width_recover_fail = -5;
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "passed=8 failed=1 reason=17 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "revert=1 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "rstatus=4294967291") != 0);
	CHECK(!g_self->online);
	CHECK(gta4l_test.message_count == 1);
	CHECK(g_self->offlineNotified);
	return 0;
}

static int
scenario_widthpub_baseline_failure(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthpub=1",
	    widthhold_baseline_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=0 failed=1 reason=1 "
	    "complete=0 pub=0 switch=0 verified=0 hb=0 ha=0") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHPUBREAD") == 0);
	CHECK(gta4l_test.message_count == 0);
	return 0;
}

static int
scenario_widthpub_switch_failure(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-widthpub=1",
	    widthhold_switch_failure_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "reason=3 complete=0 pub=0 switch=4 verified=0 hb=0 ha=0 ") != 0);
	return 0;
}

static void
speed_default_multi_hook(uint32_t call)
{
	(void)call;
	/* A report pass must record a non-zero platform-counter delta even
	 * when the host model never sleeps; one millisecond per case keeps
	 * the eight-case loop far inside its 2,000 ms budget. */
	gta4l_test.now_ms += 1u;
}

static void
speed_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test_multi_hook = speed_default_multi_hook;
}

static void
speed_print_line(const char *tag)
{
	const char *start = strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SPEED: ");
	const char *end;

	if (start == 0) {
		return;
	}
	end = strchr(start, '\n');
	if (end == 0) {
		end = start + strlen(start);
	}
	printf("%s %.*s\n", tag, (int)(end - start), start);
}

static void
speed_window_expected_digest(uint8_t *digest)
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_SPEED_WINDOW_BYTES];
	uint64_t first = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;
	uint32_t index;

	for (index = 0; index < GTA4L_EMMC_BLOCK_SPEED_WINDOW_BLOCKS;
	    index++) {
		gta4l_test_fill_sector(first + index,
		    scratch + (index * GTA4L_EMMC_BLOCK_BYTES));
	}
	gta4l_emmc_block_sha256(scratch, sizeof(scratch), digest);
}

static int
speed_counter_positive(const char *key)
{
	const char *found = strstr(gta4l_test.panic, key);
	unsigned long long value;

	if (found == 0) {
		return 0;
	}
	if (sscanf(found + strlen(key), "%llu", &value) != 1) {
		return 0;
	}
	return value > 0u;
}

static IOReturn
speed_short_read_hook(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount)
{
	uint64_t absolute = media->base_value + byteStart;
	uint64_t block = absolute / GTA4L_EMMC_BLOCK_BYTES;
	uint8_t *target = static_cast<uint8_t *>(buffer->address());
	uint32_t offset;

	for (offset = 0; offset < buffer->getLength();
	    offset += GTA4L_EMMC_BLOCK_BYTES) {
		gta4l_test_fill_sector(block +
		    (offset / GTA4L_EMMC_BLOCK_BYTES), target + offset);
	}
	if (actualByteCount != 0) {
		*actualByteCount = buffer->getLength() - 1u;
	}
	return kIOReturnSuccess;
}

static void
speed_crc_hook(uint32_t call)
{
	gta4l_test.now_ms += 1u;
	if (call == 3u) {
		gta4l_test.multi_data_result = GTA4L_EMMC_ERR_DATA_CRC;
	}
}

static void
speed_timeout_hook(uint32_t call)
{
	gta4l_test.now_ms += 1u;
	if (call == 6u) {
		gta4l_test.multi_data_result = GTA4L_EMMC_ERR_DATA_TIMEOUT;
	}
}

static void
speed_fallback_report_fixture(void)
{
	speed_fixture();
	gta4l_test.speed_switch8_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	gta4l_test.speed_switch8_verified = 0;
	gta4l_test.speed_switch8_host_bit = 0;
}

static int
scenario_speed_match_8bit(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[512];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	CHECK(g_self->speedMode);
	CHECK(!g_self->widthpubMode);
	CHECK(!g_self->widthholdMode);
	CHECK(g_self->report.widthpub == 0);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(gta4l_test.panic != 0);
	speed_window_expected_digest(digest);
	partread_digest_hex(digest, digestText);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SPEED: hs=1 hsrb=0 hsa=1 rcg=0000020b "
	    "hispd=00 wtry=8 wfall=0 wroll=0 wid=8 verified=1 hb=00 "
	    "ha=20 cmd6=1 base=1 bytes1=4096 ticks1=") != 0);
	snprintf(expected, sizeof(expected),
	    " hz=1000000 dig1=%s bytes2=4096 ticks2=", digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	snprintf(expected, sizeof(expected),
	    " dig2=%s req=8 fails=0 cancel=0 budget=0 partial=0 pub=1 "
	    "partdiag=0 revert=1 rverified=1 rhb=20 rha=00 rcmd6=2 "
	    "rstatus=0 hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000 "
	    "limit=64 seg=1 cap=61071360", digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 PART:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHHOLD") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHPUB") == 0);
	CHECK(speed_counter_positive("ticks1="));
	CHECK(speed_counter_positive("ticks2="));
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SPEEDIO: req=8 blk=16 inh=") != 0);
	CHECK(strstr(gta4l_test.panic, " core=") != 0);
	CHECK(strstr(gta4l_test.panic, " win=") != 0);
	CHECK(g_self->report.speedio_req == 8u);
	CHECK(g_self->report.speedio_blk == 16u);
	CHECK(g_self->report.speedio_core > 0u);
	CHECK(g_self->report.speedio_bus >= g_self->report.speedio_core);
	CHECK(g_self->report.speedio_io >= g_self->report.speedio_bus);
	CHECK(g_self->report.speedio_win >= g_self->report.speedio_io);
	CHECK(g_self->report.speedio_win ==
	    g_self->report.speed_ticks1 + g_self->report.speed_ticks2);
	CHECK(g_self->report.speedio_inh + g_self->report.speedio_cmd +
	    g_self->report.speedio_dat + g_self->report.speedio_stop <=
	    g_self->report.speedio_core);
	CHECK(g_self->report.speed == 1u);
	CHECK(g_self->report.speed_wid == 8u);
	CHECK(g_self->report.speed_wfall == 0u);
	CHECK(g_self->report.speed_wroll == 0u);
	CHECK(g_self->report.speed_req == 8u);
	CHECK(g_self->report.speed_fails == 0u);
	CHECK(g_self->report.speed_bytes1 == 4096u);
	CHECK(g_self->report.speed_bytes2 == 4096u);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_NONE);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(gta4l_test.speed_recover_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(!g_self->host.deadline_active);
	CHECK(gta4l_test.message_count == 0);
	CHECK(!g_self->offlineNotified);
	CHECK(g_widthhold_boot_media != 0);
	CHECK(g_widthhold_boot_media->open_calls == 1);
	CHECK(g_widthhold_boot_media->close_calls == 1);
	CHECK(g_widthhold_boot_media->open_access ==
	    kIOStorageAccessReader);
	CHECK(g_self->widthholdBoot == 0);
	CHECK(g_self->widthholdWhole == 0);
	CHECK(g_self->widthholdClient == 0);
	speed_print_line("SPEEDMATCH");
	return 0;
}

static int
scenario_speed_absent(void)
{
	CHECK(begin("gta4l-emmc-block=3") == 0);
	CHECK(g_started);
	CHECK(!g_self->speedMode);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT:") != 0);
	return 0;
}

static int
scenario_speed_refused_orphan(void)
{
	begin("gta4l-emmc-speed=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-speed requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_speed_refused_block(void)
{
	static const char *arguments[] = {
		"gta4l-emmc-speed=1",
		"gta4l-emmc-block=1 gta4l-emmc-speed=1",
		"gta4l-emmc-block=2 gta4l-emmc-speed=1",
		"gta4l-emmc-block=4 gta4l-emmc-speed=1",
	};
	unsigned int index;

	for (index = 0;
	    index < sizeof(arguments) / sizeof(arguments[0]); index++) {
		begin(arguments[index]);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK((strstr(gta4l_test.log,
		    "requires gta4l-emmc-block=3") != 0) ||
		    (strstr(gta4l_test.log,
		    "malformed gta4l-emmc-block value") != 0) ||
		    (strstr(gta4l_test.log,
		    "publish-only mode 2 is deferred") != 0));
	}
	return 0;
}

static int
scenario_speed_refused_value(void)
{
	static const int values[] = { 0, 2 };
	char arguments[64];
	unsigned int index;

	for (index = 0;
	    index < sizeof(values) / sizeof(values[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-speed=%d",
		    values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-speed value must be 1") != 0);
	}
	return 0;
}

static int
scenario_speed_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-speed=1 "
	    "gta4l-emmc-speed=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-speed token") != 0);
	return 0;
}

static int
scenario_speed_refused_conflicts(void)
{
	static const struct {
		const char *token;
		const char *name;
	} conflicts[] = {
		{ "gta4l-emmc-partobs=1", "partobs" },
		{ "gta4l-emmc-partcmp=1", "partcmp" },
		{ "gta4l-emmc-partread=1", "partread" },
		{ "gta4l-emmc-deblock=1", "deblock" },
		{ "gta4l-emmc-breakup=1", "breakup" },
		{ "gta4l-emmc-sweep=1", "sweep" },
		{ "gta4l-emmc-consume=1", "consume" },
		{ "gta4l-emmc-width=4", "width" },
		{ "gta4l-emmc-widthhold=1", "widthhold" },
		{ "gta4l-emmc-widthpub=1", "widthpub" },
	};
	char arguments[128];
	char expected[128];
	unsigned int index;

	for (index = 0;
	    index < sizeof(conflicts) / sizeof(conflicts[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 %s gta4l-emmc-speed=1",
		    conflicts[index].token);
		snprintf(expected, sizeof(expected),
		    "gta4l-emmc-%s and gta4l-emmc-speed are mutually "
		    "exclusive", conflicts[index].name);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log, expected) != 0);
	}
	return 0;
}

static int
scenario_speed_start_8bit(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	CHECK(gta4l_test.speed_hs_calls == 1);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_switch4_calls == 0);
	CHECK(gta4l_test.speed_recover_calls == 0);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8);
	CHECK(g_self->report.speed == 1);
	CHECK(g_self->report.speed_hs == 1);
	CHECK(g_self->report.speed_hsrb == 0);
	CHECK(g_self->report.speed_hsa == 1);
	CHECK(g_self->report.speed_rcg ==
	    (GTA4L_EMMC_RCG_50M_CFG &
	    (GTA4L_EMMC_RCG_SRC_SEL_MASK |
	    GTA4L_EMMC_RCG_SRC_DIV_MASK |
	    GTA4L_EMMC_RCG_MODE_MASK)));
	CHECK(g_self->report.speed_hispd == 0);
	CHECK(g_self->report.speed_wtry == 8);
	CHECK(g_self->report.speed_wid == 8);
	CHECK(g_self->report.speed_verified == 1);
	CHECK(g_self->report.speed_hb == 0);
	CHECK(g_self->report.speed_ha == GTA4L_EMMC_HOST_CONTROL_8BIT);
	CHECK(g_self->report.speed_cmd6 == 1);
	CHECK(g_self->report.speed_base == 1);
	CHECK(g_self->report.speed_wfall == 0);
	CHECK(g_self->report.speed_wroll == 0);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_NONE);
	CHECK(gta4l_test.deadline_arm_calls == 1);
	CHECK(gta4l_test.deadline_last_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS);
	CHECK(gta4l_test.deadline_clear_calls >= 1);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static void
speed_hispd_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_hispd =
	    GTA4L_EMMC_HOST_CONTROL_8BIT | 0x04;
}

static int
scenario_speed_start_hispd_record(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_hispd_fixture) == 0);
	CHECK(g_started);
	CHECK(g_self->report.speed_hispd ==
	    (uint32_t)(GTA4L_EMMC_HOST_CONTROL_8BIT | 0x04));
	return 0;
}

static void
speed_fallback_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_switch8_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	gta4l_test.speed_switch8_verified = 0;
	gta4l_test.speed_switch8_host_bit = 0;
}

static int
scenario_speed_start_fallback(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fallback_fixture) == 0);
	CHECK(g_started);
	CHECK(gta4l_test.speed_hs_calls == 1);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_switch4_calls == 1);
	CHECK(gta4l_test.speed_recover_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	CHECK(g_self->report.speed_wtry == 8);
	CHECK(g_self->report.speed_wid == 4);
	CHECK(g_self->report.speed_verified == 1);
	CHECK(g_self->report.speed_wfall == 1);
	CHECK(g_self->report.speed_wroll == 1);
	CHECK(g_self->report.speed_ha == GTA4L_EMMC_HOST_CONTROL_4BIT);
	CHECK(g_self->report.speed_base == 1);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static void
speed_restore_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_switch8_rc = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	gta4l_test.speed_switch8_verified = 0;
	gta4l_test.speed_switch8_rollback = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	gta4l_test.speed_switch8_host_bit = 0;
}

static int
scenario_speed_start_width_restore(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_restore_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_switch4_calls == 0);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_WIDTH);
	CHECK(g_self->report.speed_revert == 1);
	CHECK(g_self->report.speed_rverified == 1);
	CHECK(g_self->report.speed_hsrevert == 1);
	CHECK(g_self->report.speed_rhs == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.deadline_last_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static void
speed_fallback_fail_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_switch8_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	gta4l_test.speed_switch8_verified = 0;
	gta4l_test.speed_switch8_host_bit = 0;
	gta4l_test.speed_switch4_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	gta4l_test.speed_switch4_verified = 0;
	gta4l_test.speed_switch4_host_bit = 0;
}

static int
scenario_speed_start_fallback_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fallback_fail_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_switch4_calls == 1);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_FALLBACK);
	CHECK(g_self->report.speed_wfall == 1);
	CHECK(g_self->report.speed_wroll == 1);
	CHECK(g_self->report.speed_wid == 4);
	CHECK(g_self->report.speed_verified == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static void
speed_hs_fail_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_hs_rc = GTA4L_EMMC_ERR_TIMING;
}

static int
scenario_speed_start_hs_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_hs_fail_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_hs_calls == 1);
	CHECK(gta4l_test.speed_switch8_calls == 0);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_HS);
	CHECK(g_self->report.speed_fail_status == GTA4L_EMMC_ERR_TIMING);
	CHECK(g_self->report.speed_revert == 1);
	CHECK(g_self->report.speed_rverified == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.deadline_last_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static void
speed_hs_readback_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_hs_after = 0;
}

static int
scenario_speed_start_hs_readback(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_hs_readback_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_HS);
	CHECK(g_self->report.speed_fail_status == GTA4L_EMMC_ERR_TIMING);
	CHECK(g_self->report.speed_hsa == 0);
	CHECK(g_self->report.speed_rverified == 1);
	return 0;
}

static void
speed_hs_clock_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_hs_clock_rc = GTA4L_EMMC_ERR_CLOCK;
}

static int
scenario_speed_start_hs_clock(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_hs_clock_fixture) == 1);
	CHECK(!g_started);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_HS);
	CHECK(g_self->report.speed_fail_status == GTA4L_EMMC_ERR_CLOCK);
	CHECK(g_self->report.speed_rverified == 1);
	return 0;
}

static void
speed_baseline_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_ext_csd_fail = GTA4L_EMMC_ERR_DATA_CRC;
}

static int
scenario_speed_start_baseline_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_baseline_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_hs_calls == 0);
	CHECK(gta4l_test.speed_recover_calls == 0);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_BASELINE);
	CHECK(g_self->report.speed_fail_status == GTA4L_EMMC_ERR_DATA_CRC);
	CHECK(g_self->report.speed_revert == 0);
	CHECK(gta4l_test.deadline_arm_calls == 1);
	CHECK(gta4l_test.deadline_clear_calls >= 1);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static void
speed_base_fail_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.fail_read_at = 0;
}

static int
scenario_speed_start_base_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_base_fail_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_BASE);
	CHECK(g_self->report.speed_base == 0);
	CHECK(g_self->report.speed_revert == 1);
	CHECK(g_self->report.speed_rverified == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static void
speed_advert_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.property_fail_at = 0;
}

static int
scenario_speed_start_advert(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_advert_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_ADVERT);
	CHECK(g_self->report.speed_revert == 1);
	CHECK(g_self->report.speed_rverified == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static void
speed_gate_hook(uint32_t phase)
{
	if (phase == GTA4L_TEST_SPEED_PHASE_START) {
		g_self->gate->depth = 1;
	}
}

static void
speed_gate_fixture(void)
{
	gta4l_test.speed_active = 1;
	gta4l_test.speed_hs_rc = GTA4L_EMMC_ERR_TIMING;
	gta4l_test_speed_hook = speed_gate_hook;
}

static int
scenario_speed_start_gate_timeout(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_gate_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.speed_recover_calls == 0);
	/* The HS failure stays the first latched stage; the gate timeout is
	 * visible only by the unlocked snapshot and the skipped revert. */
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_HS);
	CHECK(g_self->report.speed_revert == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SNAP: locked=0") != 0);
	CHECK(gta4l_test.deadline_last_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS);
	CHECK(!g_self->host.deadline_active);
	g_self->gate->depth = 0;
	return 0;
}

#define SPEED_FAILURE_COMMON(stage) \
	do { \
		CHECK(invoke_thread(1) == 1); \
		CHECK(g_self->report.speed_fail_stage == (stage)); \
		CHECK(gta4l_test.speed_recover_calls == 1); \
		CHECK(gta4l_test.speed_recover_budget_ms == \
		    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS); \
		CHECK(g_self->host.bus_width == \
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1); \
		CHECK(!g_self->host.deadline_active); \
		CHECK(strstr(gta4l_test.panic, \
		    "revert=1 rverified=1") != 0); \
		CHECK(strstr(gta4l_test.panic, \
		    "hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000") != 0); \
		CHECK(strstr(gta4l_test.panic, "rstatus=0") != 0); \
		CHECK(gta4l_test.message_count == 1); \
		CHECK(g_self->offlineNotified); \
		CHECK(!g_self->online); \
		CHECK(g_self->widthholdClient == 0); \
		CHECK(g_self->widthholdBoot == 0); \
		CHECK(g_self->widthholdWhole == 0); \
	} while (0)

static int
scenario_speed_match_4bit(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[512];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fallback_report_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	speed_window_expected_digest(digest);
	partread_digest_hex(digest, digestText);
	CHECK(strstr(gta4l_test.panic,
	    "wid=4 verified=1 hb=00 ha=02 cmd6=1 base=1 bytes1=4096") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "wfall=1 wroll=1") != 0);
	snprintf(expected, sizeof(expected),
	    " dig2=%s req=8 fails=0 cancel=0 budget=0 partial=0 pub=1 "
	    "partdiag=0 revert=1 rverified=1 rhb=02 rha=00 rcmd6=2 "
	    "rstatus=0 hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000 "
	    "limit=64 seg=1 cap=61071360", digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(gta4l_test.speed_switch8_calls == 1);
	CHECK(gta4l_test.speed_switch4_calls == 1);
	CHECK(g_self->report.speed_wid == 4u);
	CHECK(g_self->report.speed_wfall == 1u);
	CHECK(g_self->report.speed_wroll == 1u);
	CHECK(g_self->report.speed_req == 8u);
	CHECK(gta4l_test.message_count == 0);
	CHECK(gta4l_test.speed_recover_calls == 1);
	CHECK(speed_counter_positive("ticks1="));
	speed_print_line("SPEEDMATCH4");
	return 0;
}

static int
scenario_speed_read_crc(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_multi_hook = speed_crc_hook;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_READ);
	CHECK(g_self->report.speed_req == 4u);
	CHECK(g_self->report.speed_fails == 1u);
	CHECK(g_self->report.speed_bytes1 == 3072u);
	CHECK(g_self->report.speed_dig1[0] == 0u);
	CHECK(strstr(gta4l_test.panic,
	    "dig1=00000000000000000000000000000000") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "dig2=00000000000000000000000000000000") != 0);
	return 0;
}

static int
scenario_speed_read_timeout(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_multi_hook = speed_timeout_hook;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_READ);
	CHECK(g_self->report.speed_req == 7u);
	CHECK(g_self->report.speed_fails == 1u);
	CHECK(g_self->report.speed_bytes1 == 4096u);
	CHECK(g_self->report.speed_bytes2 == 2048u);
	speed_window_expected_digest(digest);
	partread_digest_hex(digest, digestText);
	snprintf(expected, sizeof(expected), "dig1=%s", digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic,
	    "dig2=00000000000000000000000000000000") != 0);
	return 0;
}

static int
scenario_speed_short(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_media_read_hook = speed_short_read_hook;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_SHORT);
	CHECK(g_self->report.speed_req == 1u);
	CHECK(g_self->report.speed_fails == 1u);
	CHECK(g_self->report.speed_partial == 1u);
	CHECK(g_self->report.speed_bytes1 ==
	    (GTA4L_EMMC_BLOCK_SPEED_CASE_BYTES - 1u));
	CHECK(strstr(gta4l_test.panic, "partial=1") != 0);
	CHECK(strstr(gta4l_test.panic, "bytes1=1023") != 0);
	return 0;
}

static int
scenario_speed_budget(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 0;
	g_widthhold_advance_ms = GTA4L_EMMC_BLOCK_SPEED_READ_BUDGET_MS;
	gta4l_test_multi_hook = widthhold_multi_hook;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_BUDGET);
	CHECK(g_self->report.speed_budget == 1u);
	CHECK(g_self->report.speed_req == 1u);
	CHECK(gta4l_test.multi_calls == 1u);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	return 0;
}

static int
scenario_speed_budget_boundary(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 0;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_SPEED_READ_BUDGET_MS - 1u;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_NONE);
	CHECK(g_self->report.speed_budget == 0u);
	CHECK(g_self->report.speed_req == 8u);
	CHECK(gta4l_test.multi_calls == 8u);
	CHECK(strstr(gta4l_test.panic, "budget=0") != 0);
	return 0;
}

static int
scenario_speed_cancel_before(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_self->reportCancel = true;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_CANCEL);
	CHECK(g_self->report.speed_cancel == 1u);
	CHECK(g_self->report.speed_req == 0u);
	CHECK(g_self->report.speed_pub == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	CHECK(strstr(gta4l_test.panic, "pub=0") != 0);
	return 0;
}

static int
scenario_speed_cancel_during(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_cancel_call = 1;
	gta4l_test_multi_hook = widthhold_multi_hook;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_CANCEL);
	CHECK(g_self->report.speed_cancel == 1u);
	CHECK(g_self->report.speed_req == 2u);
	CHECK(g_self->report.speed_bytes1 == 2048u);
	CHECK(gta4l_test.multi_calls == 2u);
	return 0;
}

static int
scenario_speed_media_absent(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(false, false);
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_MEDIA);
	CHECK(g_self->report.speed_pub == 0u);
	CHECK(g_self->report.speed_partdiag == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS);
	CHECK(strstr(gta4l_test.panic, "pub=0") != 0);
	return 0;
}

static int
scenario_speed_part_absent(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_PART);
	CHECK(g_self->report.speed_pub == 0u);
	CHECK(g_self->report.speed_partdiag ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_DEADLINE_MS);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_speed_part_duplicate(void)
{
	IOMedia *boot;

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_PART);
	CHECK(g_self->report.speed_partdiag ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_DUPLICATE);
	CHECK(g_widthhold_boot_media->open_calls == 0);
	CHECK(boot->open_calls == 0);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_speed_part_wrong_base(void)
{
	IOMedia *boot;

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	boot = add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    false, "Apple_Boot", true);
	boot->property_faults = GTA4L_TEST_PROP_BASE_MISSING_LATE;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_PART);
	CHECK(g_self->report.speed_partdiag ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_BASE);
	CHECK(boot->open_calls == 0);
	return 0;
}

static int
scenario_speed_part_writable(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	(void)add_media(false,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
	    true, "Apple_Boot", true);
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_PART);
	CHECK(g_self->report.speed_partdiag ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_speed_desc(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.descriptor_alloc_fail = 1;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_DESC);
	CHECK(g_self->report.speed_req == 0u);
	CHECK(g_self->report.speed_fails == 0u);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_speed_open_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_boot_media->open_refuse = true;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_OPEN);
	CHECK(g_self->report.speed_req == 0u);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(g_widthhold_boot_media->open_calls == 1);
	CHECK(g_widthhold_boot_media->close_calls == 0);
	CHECK(g_self->widthholdClient == 0);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_speed_foreign_tag(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_tag_delta = 1;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_FOREIGN);
	CHECK(g_self->report.speed_req == 1u);
	CHECK(g_self->report.speed_fails == 1u);
	CHECK(g_self->report.speed_pub == 1u);
	return 0;
}

static int
scenario_speed_order(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_extra = 1;
	SPEED_FAILURE_COMMON(GTA4L_EMMC_BLOCK_SPEED_STAGE_ORDER);
	CHECK(g_self->report.speed_req == 1u);
	CHECK(g_self->report.speed_fails == 1u);
	return 0;
}

static int
scenario_speed_digest_mismatch(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.multi_corrupt_at = 4;
	gta4l_test.multi_corrupt_count = 1;
	gta4l_test.multi_corrupt_block =
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_NONE);
	CHECK(g_self->report.speed_req == 8u);
	CHECK(g_self->report.speed_fails == 0u);
	speed_window_expected_digest(digest);
	partread_digest_hex(digest, digestText);
	snprintf(expected, sizeof(expected), "dig1=%s", digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	snprintf(expected, sizeof(expected), "dig2=%s", digestText);
	CHECK(strstr(gta4l_test.panic, expected) == 0);
	CHECK(strstr(gta4l_test.panic,
	    "dig2=00000000000000000000000000000000") == 0);
	CHECK(g_self->report.speed_dig1[0] != 0u);
	CHECK(memcmp(g_self->report.speed_dig1,
	    g_self->report.speed_dig2, 16u) != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 0);
	speed_print_line("SPEEDMISMATCH");
	return 0;
}

static int
scenario_speed_gate_timeout(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_gate_call = 0;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_GATE);
	CHECK(g_self->report.speed_revert == 0u);
	CHECK(gta4l_test.speed_recover_calls == 0u);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8);
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SNAP: locked=0") != 0);
	CHECK(gta4l_test.now_ms >= GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_MS);
	CHECK(gta4l_test.message_count == 1);
	g_self->gate->depth = 0;
	return 0;
}

static int
scenario_speed_revert_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_rc = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_revert == 1u);
	CHECK(g_self->report.speed_rverified == 0u);
	CHECK(g_self->report.speed_hsrevert == 0u);
	CHECK(g_self->report.speed_hsverified == 0u);
	CHECK(g_self->report.speed_rhs == 0u);
	CHECK(g_self->report.speed_rclk == GTA4L_EMMC_BLOCK_CLOCK_HZ);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic, "rstatus=-23") != 0);
	CHECK(!g_self->online);
	CHECK(gta4l_test.message_count == 1);
	CHECK(gta4l_test.speed_recover_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);
	return 0;
}

static int
scenario_speed_revert_unverified(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_verified = 0;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_rverified == 0u);
	CHECK(g_self->report.speed_hsrevert == 1u);
	CHECK(g_self->report.speed_hsverified == 0u);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic, "hsrevert=1 hsverified=0") != 0);
	return 0;
}

static int
scenario_speed_revert_budget(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_advance_ms =
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS;
	gta4l_test.speed_recover_rc = GTA4L_EMMC_ERR_DEADLINE;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_rverified == 0u);
	CHECK(g_self->report.speed_rstatus == GTA4L_EMMC_ERR_DEADLINE);
	CHECK(strstr(gta4l_test.panic, "rstatus=-25") != 0);
	CHECK(gta4l_test.speed_recover_budget_ms ==
	    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static int
scenario_speed_revert_hs_stuck(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_hs_after = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_rverified == 1u);
	CHECK(g_self->report.speed_hsrevert == 0u);
	CHECK(g_self->report.speed_rhs == 1u);
	CHECK(strstr(gta4l_test.panic, "hsrevert=0 hsverified=1") != 0);
	CHECK(strstr(gta4l_test.panic, "rhs=1") != 0);
	return 0;
}

static int
scenario_speed_revert_clock(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_clock_rc = GTA4L_EMMC_ERR_CLOCK;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_rclk == 0u);
	CHECK(strstr(gta4l_test.panic, "rclk=0") != 0);
	return 0;
}

static int
scenario_speed_revert_host_wide(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_host_clear = 0;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_rverified == 1u);
	CHECK((g_self->report.speed_rha &
	    (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) != 0u);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8);
	CHECK(strstr(gta4l_test.panic, "rha=20") != 0);
	return 0;
}

static int
scenario_speed_first_failure_latch(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.fail_multi_at = 0;
	gta4l_test.speed_recover_rc = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_READ);
	CHECK(g_self->report.speed_rverified == 0u);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=0") != 0);
	CHECK(!g_self->online);
	CHECK(gta4l_test.message_count == 1);
	return 0;
}

static int
scenario_speed_watchdog(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-speed=1",
	    speed_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_watchdog_call = 0;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SPEED: ") != 0);
	/* The watchdog snapshot is taken before the report thread's
	 * revert, so the line documents the no-revert watchdog policy. */
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHHOLDREAD") == 0);
	return 0;
}

static int
scenario_speed_thread_start(void)
{
	static const int fail_at[] = { 0, 1 };
	unsigned int index;

	for (index = 0; index < sizeof(fail_at) / sizeof(fail_at[0]);
	    index++) {
		gta4l_test_reset();
		gta4l_test.boot_args =
		    "gta4l-emmc-block=3 gta4l-emmc-speed=1";
		speed_fixture();
		gta4l_test.thread_start_fail_at = fail_at[index];
		gta4l_test.thread_start_fail_result = -1;
		g_self = new Gta4lEmmcBlock();
		g_started = false;
		g_panicked = 0;
		if (setjmp(gta4l_test.panic_jump) == 0) {
			g_started = g_self->start(0);
		} else {
			g_panicked = 1;
		}
		CHECK(!g_started);
		CHECK(g_panicked);
		CHECK(g_self->report.speed_fail_stage ==
		    GTA4L_EMMC_BLOCK_SPEED_STAGE_THREAD);
		CHECK(gta4l_test.speed_recover_calls == 1);
		CHECK(g_self->host.bus_width ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
		CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
		CHECK(g_self->report.speed_fail_status == 16);
	}
	return 0;
}

static int
scenario_speed_alloc(void)
{
	gta4l_test_reset();
	gta4l_test.boot_args = "gta4l-emmc-block=3 gta4l-emmc-speed=1";
	speed_fixture();
	gta4l_test.consume_alloc_fail = 1;
	g_self = new Gta4lEmmcBlock();
	g_started = false;
	g_panicked = 0;
	if (setjmp(gta4l_test.panic_jump) == 0) {
		g_started = g_self->start(0);
	} else {
		g_panicked = 1;
	}
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(g_self->speedWindow == 0);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_ALLOC);
	CHECK(gta4l_test.speed_hs_calls == 0u);
	CHECK(gta4l_test.speed_recover_calls == 0u);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SPEED: hs=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=0") != 0);
	return 0;
}

static int
scenario_speed_format_max(void)
{
	struct gta4l_emmc_block_report report;
	char buffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	const char *line;
	const char *end;
	uint32_t length;

	bzero(&report, sizeof(report));
	report.speed = 1u;
	report.speed_hs = 0xffffffffu;
	report.speed_hsrb = 0xffffffffu;
	report.speed_hsa = 0xffffffffu;
	report.speed_rcg = 0xffffffffu;
	report.speed_hispd = 0xffffffffu;
	report.speed_wtry = 0xffffffffu;
	report.speed_wfall = 0xffffffffu;
	report.speed_wroll = 0xffffffffu;
	report.speed_wid = 0xffffffffu;
	report.speed_verified = 0xffffffffu;
	report.speed_hb = 0xffffffffu;
	report.speed_ha = 0xffffffffu;
	report.speed_cmd6 = 0xffffffffu;
	report.speed_base = 0xffffffffu;
	report.speed_bytes1 = 0xffffffffu;
	report.speed_ticks1 = UINT64_MAX;
	report.speed_hz = 0xffffffffu;
	memset(report.speed_dig1, 0xff, sizeof(report.speed_dig1));
	report.speed_bytes2 = 0xffffffffu;
	report.speed_ticks2 = UINT64_MAX;
	memset(report.speed_dig2, 0xff, sizeof(report.speed_dig2));
	report.speed_req = 0xffffffffu;
	report.speed_fails = 0xffffffffu;
	report.speed_cancel = 0xffffffffu;
	report.speed_budget = 0xffffffffu;
	report.speed_partial = 0xffffffffu;
	report.speed_pub = 0xffffffffu;
	report.speed_partdiag = 0xffffffffu;
	report.speed_revert = 0xffffffffu;
	report.speed_rverified = 0xffffffffu;
	report.speed_rhb = 0xffffffffu;
	report.speed_rha = 0xffffffffu;
	report.speed_rcmd6 = 0xffffffffu;
	report.speed_rstatus = INT32_MIN;
	report.speed_hsrevert = 0xffffffffu;
	report.speed_hsverified = 0xffffffffu;
	report.speed_rhsb = 0xffffffffu;
	report.speed_rhs = 0xffffffffu;
	report.speed_rclk = 0xffffffffu;
	report.speed_limit = 0xffffffffu;
	report.speed_segments = 0xffffffffu;
	report.speed_capacity = 0xffffffffu;
	report.speedio_req = 0xffffffffu;
	report.speedio_blk = 0xffffffffu;
	report.speedio_inh = UINT64_MAX;
	report.speedio_cmd = UINT64_MAX;
	report.speedio_dat = UINT64_MAX;
	report.speedio_stop = UINT64_MAX;
	report.speedio_core = UINT64_MAX;
	report.speedio_bus = UINT64_MAX;
	report.speedio_io = UINT64_MAX;
	report.speedio_win = UINT64_MAX;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length != 0u);
	CHECK(length < sizeof(buffer));
	CHECK(strstr(buffer, "GTA4L-EMMC P5 SPEED: ") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 SPEED: ");
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) ==
	    GTA4L_EMMC_BLOCK_SPEED_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 SPEEDIO: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) ==
	    GTA4L_EMMC_BLOCK_SPEEDIO_LINE_BYTES);
	CHECK(GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_BYTES ==
	    (GTA4L_EMMC_BLOCK_SPEED_LINE_BYTES + 838u + 2u));
	CHECK(GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_BYTES +
	    GTA4L_EMMC_BLOCK_SPEEDIO_LINE_BYTES + 1u <=
	    GTA4L_EMMC_BLOCK_REPORT_BYTES);
	return 0;
}

static void
bulk_expected_digest(uint32_t bytes, uint8_t digest[32])
{
	struct gta4l_emmc_block_sha256_ctx ctx;
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t offset = 0;
	uint32_t length;

	gta4l_emmc_block_sha256_init(&ctx);
	while (offset < bytes) {
		length = bytes - offset;
		if (length > GTA4L_EMMC_BLOCK_BYTES) {
			length = GTA4L_EMMC_BLOCK_BYTES;
		}
		gta4l_test_fill_sector(
		    GTA4L_EMMC_BLOCK_WIDTHHOLD_LBA +
		    (offset / GTA4L_EMMC_BLOCK_BYTES), sector);
		gta4l_emmc_block_sha256_update(&ctx, sector, length);
		offset += length;
	}
	gta4l_emmc_block_sha256_final(&ctx, digest);
}

static void
bulk_full_digest(uint8_t digest[32])
{
	static uint8_t cached[32];
	static int have;

	if (!have) {
		bulk_expected_digest(
		    GTA4L_EMMC_BLOCK_BULK_REGION_BYTES, cached);
		have = 1;
	}
	memcpy(digest, cached, sizeof(cached));
}

static uint64_t
bulk_expected_window(uint32_t index)
{
	uint64_t region_blocks = GTA4L_EMMC_BLOCK_BULK_REGION_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES;
	uint32_t state = GTA4L_EMMC_BLOCK_BULK_SEED;
	uint32_t step;
	uint32_t round;
	uint64_t block;

	if (index < GTA4L_EMMC_BLOCK_BULK_ALIGNED_WINDOWS) {
		return ((uint64_t)index * GTA4L_EMMC_BLOCK_BULK_CHUNKS /
		    GTA4L_EMMC_BLOCK_BULK_ALIGNED_WINDOWS) *
		    GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS;
	}
	step = index - GTA4L_EMMC_BLOCK_BULK_ALIGNED_WINDOWS + 1u;
	for (round = 0; round < step; round++) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
	}
	block = state % (region_blocks -
	    GTA4L_EMMC_BLOCK_BULK_WINDOW_BLOCKS);
	if ((block % GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS) == 0u) {
		block++;
	}
	return block;
}

static uint32_t g_bulk_advance_ms = 1;
static int g_bulk_cancel_call = -1;

static void
bulk_hook_reset(void)
{
	g_bulk_advance_ms = 1;
	g_bulk_cancel_call = -1;
}

static void
bulk_multi_hook(uint32_t call)
{
	gta4l_test.now_ms += g_bulk_advance_ms;
	if ((g_bulk_cancel_call >= 0) &&
	    (call == (uint32_t)g_bulk_cancel_call)) {
		g_self->reportCancel = true;
	}
}

static void
bulk_fixture(void)
{
	speed_fixture();
	bulk_hook_reset();
	gta4l_test_multi_hook = bulk_multi_hook;
}

static void
bulk_smmu_fault_fixture(void)
{
	bulk_fixture();
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_S2CR(0) / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT;
}

#define SMMU_TEST_GR1 0x1000u
#define SMMU_TEST_CB0 0x40000u
#define SMMU_TEST_LAST 0x5f000u
#define SMMU_TEST_REG(offset) ((offset) / 4u)

static void
bulk_smmu_cb_fixture(uint32_t cbar, uint32_t sctlr, uint32_t fsr)
{
	bulk_fixture();
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_S2CR(0) / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_TRANSLATE <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT;
	/* 4 KiB pages, 64 global pages, 32 context banks. */
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_ID1 / 4u] =
	    (5u << GTA4L_EMMC_BLOCK_SMMU_ID1_NUMPAGENDXB_SHIFT) | 32u;
	gta4l_test.smmu_regs[SMMU_TEST_REG(SMMU_TEST_GR1 + 0u)] = cbar;
	gta4l_test.smmu_regs[SMMU_TEST_REG(SMMU_TEST_CB0 +
	    GTA4L_EMMC_BLOCK_SMMU_CB_SCTLR)] = sctlr;
	gta4l_test.smmu_regs[SMMU_TEST_REG(SMMU_TEST_CB0 +
	    GTA4L_EMMC_BLOCK_SMMU_CB_FSR)] = fsr;
	gta4l_test.smmu_regs[SMMU_TEST_REG(SMMU_TEST_LAST +
	    GTA4L_EMMC_BLOCK_SMMU_CB_SCTLR)] = 0u;
	gta4l_test.smmu_regs[SMMU_TEST_REG(SMMU_TEST_GR1 + (31u << 2))] =
	    0u;
}

static void
bulk_smmu_translate_live_fixture(void)
{
	bulk_smmu_cb_fixture(
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT,
	    GTA4L_EMMC_BLOCK_SMMU_SCTLR_M, 0u);
}

static void
bulk_smmu_cb_off_fixture(void)
{
	bulk_smmu_cb_fixture(
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT, 0u, 0u);
}

static void
bulk_smmu_cb_type_fixture(void)
{
	bulk_smmu_cb_fixture(0u, 0u, 0u);
}

static void
bulk_smmu_cb_fault_fixture(void)
{
	bulk_smmu_cb_fixture(
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT, 0u, 1u);
}

static void
bulk_smmu_conflict_fixture(void)
{
	bulk_smmu_cb_fixture(
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT, 0u, 0u);
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_ID0 / 4u] = 2u;
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_SMR(1) / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID;
}

static void
bulk_smmu_layout_fixture(void)
{
	bulk_fixture();
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_S2CR(0) / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_TRANSLATE <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT;
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_ID1 / 4u] = 0u;
}

static void
bulk_smmu_translate_fixture(void)
{
	bulk_smmu_translate_live_fixture();
}

static void
bulk_smmu_post_fault_fixture(void)
{
	bulk_smmu_cb_off_fixture();
	/* The gate's first FSR read is clean; the post-DMA read faults. */
	gta4l_test.smmu_fault_after = 2u;
}

static void
bulk_smmu_nomatch_fixture(void)
{
	bulk_fixture();
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_SMR(0) / 4u] = 0u;
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_SCR0 / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG;
}

static void
bulk_smmu_clientpd_fixture(void)
{
	bulk_fixture();
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_SCR0 / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_CLIENTPD;
	gta4l_test.smmu_regs[GTA4L_EMMC_BLOCK_SMMU_REG_S2CR(0) / 4u] =
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT;
}

static void
bulk_smmu_map_fail_fixture(void)
{
	bulk_fixture();
	gta4l_test.smmu_map_result = -1;
}

#define BULK_FAILURE_COMMON(stage) \
	do { \
		CHECK(invoke_thread(1) == 1); \
		CHECK(g_self->report.bulk == 1u); \
		CHECK(g_self->report.bulk_fail_stage == (stage)); \
		CHECK(gta4l_test.speed_recover_calls == 1); \
		CHECK(gta4l_test.speed_recover_budget_ms == \
		    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS); \
		CHECK(g_self->host.bus_width == \
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1); \
		CHECK(!g_self->host.deadline_active); \
		CHECK(strstr(gta4l_test.panic, \
		    "revert=1 rverified=1") != 0); \
		CHECK(strstr(gta4l_test.panic, \
		    "hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000") != 0); \
		CHECK(strstr(gta4l_test.panic, "rstatus=0") != 0); \
		CHECK(strstr(gta4l_test.panic, \
		    "GTA4L-EMMC P5 BULK: ") != 0); \
		CHECK(strstr(gta4l_test.panic, \
		    "GTA4L-EMMC P5 BULKFAIL: ") != 0); \
		CHECK(gta4l_test.message_count == 1); \
		CHECK(g_self->offlineNotified); \
		CHECK(!g_self->online); \
		CHECK(g_self->widthholdClient == 0); \
		CHECK(g_self->widthholdBoot == 0); \
		CHECK(g_self->widthholdWhole == 0); \
		CHECK(g_self->bulkClient == 0); \
		CHECK(g_self->bulkReference == 0); \
	} while (0)

static int
bulk_smmu_case(void (*fixture)(void), uint32_t decision, const char *token,
    uint32_t reads)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(gta4l_test.panic[0] != 0);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.smmu == 1u);
	CHECK(g_self->report.smmu_stage ==
	    GTA4L_EMMC_BLOCK_SMMU_STAGE_DONE);
	CHECK(g_self->report.smmu_decision == decision);
	CHECK(gta4l_test.smmu_maps == 1u);
	CHECK(gta4l_test.smmu_reads >= reads);
	CHECK(strstr(gta4l_test.panic, token) != 0);
	return 0;
}

static int
scenario_bulk_smmu_fault(void)
{
	return bulk_smmu_case(bulk_smmu_fault_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT, "dec=fault", 8u);
}

static int
scenario_bulk_smmu_translate(void)
{
	return bulk_smmu_case(bulk_smmu_translate_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_TRANSLATE_LIVE,
	    "dec=translate-live", 20u);
}

static int
scenario_bulk_smmu_cb_off(void)
{
	return bulk_smmu_case(bulk_smmu_cb_off_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_OFF,
	    "dec=cb-off", 20u);
}

static int
scenario_bulk_smmu_cb_type(void)
{
	return bulk_smmu_case(bulk_smmu_cb_type_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_TYPE,
	    "dec=cb-type", 20u);
}

static int
scenario_bulk_smmu_cb_fault(void)
{
	return bulk_smmu_case(bulk_smmu_cb_fault_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_FAULT,
	    "dec=cb-fault", 20u);
}

static int
scenario_bulk_smmu_conflict(void)
{
	return bulk_smmu_case(bulk_smmu_conflict_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CONFLICT,
	    "dec=conflict", 20u);
}

static int
scenario_bulk_smmu_layout(void)
{
	return bulk_smmu_case(bulk_smmu_layout_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_LAYOUT,
	    "dec=layout", 8u);
}

static int
scenario_bulk_smmu_nomatch(void)
{
	return bulk_smmu_case(bulk_smmu_nomatch_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT,
	    "dec=fault", 7u);
}

static int
scenario_bulk_smmu_clientpd(void)
{
	return bulk_smmu_case(bulk_smmu_clientpd_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS,
	    "dec=bypass", 8u);
}

static int
scenario_bulk_smmu_map_fail(void)
{
	return bulk_smmu_case(bulk_smmu_map_fail_fixture,
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_MAP_FAIL,
	    "dec=map-fail", 0u);
}

static int
scenario_dma_success(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	CHECK(g_self->bulkMode);
	CHECK(g_self->dmaMode);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.dma == 1u);
	CHECK(g_self->report.dma_mode ==
	    GTA4L_EMMC_BLOCK_DMA_MODE_ADMA2_32);
	CHECK(g_self->report.dma_smmu ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(g_self->report.dma_cmp_pass == 4u);
	CHECK(g_self->report.dma_cmp_total == 4u);
	CHECK(g_self->report.dma_canary == 1u);
	CHECK(g_self->report.dma_poison == 1u);
	CHECK(g_self->report.dma_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.dma_bytes ==
	    GTA4L_EMMC_BLOCK_BULK_REGION_BYTES);
	CHECK(g_self->report.dma_fails == 0u);
	CHECK(g_self->report.dma_stage == 0u);
	CHECK(g_self->report.dma_budget == 0u);
	CHECK(g_self->report.dma_skip == 0u);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 DMA: mode=adma2-32 smmu=bypass "
	    "cmp=4/4 canary=1 poison=1") != 0);
	CHECK(strstr(gta4l_test.panic, "skip=0") != 0);
	CHECK(gta4l_test.adma_calls ==
	    (uint32_t)(4u + GTA4L_EMMC_BLOCK_BULK_CHUNKS +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS));
	CHECK(gta4l_test.speed_recover_calls == 1u);
	CHECK(g_self->online);
	return 0;
}

static int
scenario_dma_cb_off(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_smmu_cb_off_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.dma == 1u);
	CHECK(g_self->report.dma_smmu ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_OFF);
	CHECK(g_self->report.dma_skip == 0u);
	CHECK(g_self->report.dma_cmp_pass == 4u);
	CHECK(g_self->report.dma_cmp_total == 4u);
	CHECK(g_self->report.dma_fails == 0u);
	CHECK(g_self->report.dma_stage == 0u);
	CHECK(strstr(gta4l_test.panic,
	    "mode=adma2-32 smmu=cb-off cmp=4/4") != 0);
	CHECK(strstr(gta4l_test.panic, "skip=0") != 0);
	CHECK(gta4l_test.adma_calls ==
	    (uint32_t)(4u + GTA4L_EMMC_BLOCK_BULK_CHUNKS +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS));
	return 0;
}

static int
scenario_dma_post_fault(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_smmu_post_fault_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.dma == 1u);
	CHECK(g_self->report.dma_smmu ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_OFF);
	CHECK(g_self->report.dma_skip == 0u);
	CHECK(g_self->report.dma_cmp_pass == 4u);
	CHECK(g_self->report.dma_cmp_total == 4u);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_SMMU);
	CHECK(g_self->report.dma_chunks == 0u);
	CHECK(g_self->report.smmu_pfsr == 1u);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_SMMU);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BULKFAIL: stage=13") != 0);
	CHECK(strstr(gta4l_test.panic, "stage=10") != 0);
	CHECK(strstr(gta4l_test.panic, "pfsr=00000001") != 0);
	return 0;
}

static int
scenario_dma_smmu_skip(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_smmu_fault_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.dma == 1u);
	CHECK(g_self->report.dma_skip == 1u);
	CHECK(g_self->report.smmu_decision ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT);
	CHECK(g_self->report.dma_cmp_total == 0u);
	CHECK(g_self->report.dma_chunks == 0u);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(gta4l_test.adma_calls == 0u);
	CHECK(strstr(gta4l_test.panic, "smmu=fault") != 0);
	CHECK(strstr(gta4l_test.panic, "skip=1") != 0);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_compare_mismatch(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_corrupt_at = 0;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_COMPARE);
	CHECK(g_self->report.dma_cmp_pass == 0u);
	CHECK(g_self->report.dma_cmp_total == 4u);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_chunks == 0u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_adma_error(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_fail_at = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_READ);
	CHECK(g_self->report.dma_ast == 3u);
	CHECK(g_self->report.dma_desc == 1u);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_cmp_pass == 1u);
	CHECK(strstr(gta4l_test.panic, "ast=00000003") != 0);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_short(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_short_blocks = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_SHORT);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_cmp_pass == 0u);
	return 0;
}

static int
scenario_dma_canary(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_canary_byte = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_COMPARE);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_cmp_pass == 0u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_poison_untouched(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_partial_blocks = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_COMPARE);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_data_timeout(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.adma_status_override = GTA4L_EMMC_ERR_DATA_TIMEOUT;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_READ);
	CHECK(g_self->report.dma_fails == 1u);
	CHECK(g_self->report.dma_ast == 0u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_physical_high(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1", bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.descriptor_physical_high = 1;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_DMA);
	CHECK(g_self->report.dma_stage ==
	    GTA4L_EMMC_BLOCK_DMA_STAGE_ALLOC);
	CHECK(g_self->report.dma_cmp_total == 0u);
	CHECK(gta4l_test.adma_calls == 0u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	return 0;
}

static int
scenario_dma_refused_orphan(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-dma=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-dma requires gta4l-emmc-bulk=1") != 0);
	return 0;
}

static int
scenario_dma_refused_value(void)
{
	static const int values[] = { 0, 2 };
	char arguments[96];
	unsigned int index;

	for (index = 0;
	    index < sizeof(values) / sizeof(values[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
		    "gta4l-emmc-dma=%d", values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-dma value must be 1") != 0);
	}
	return 0;
}

static int
scenario_dma_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-dma=1 gta4l-emmc-dma=1");
	CHECK(!g_started);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-dma token") != 0);
	return 0;
}


static int
scenario_bulk_success(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	CHECK(g_self->bulkMode);
	CHECK(!g_self->speedMode);
	CHECK(g_self->report.bulk == 1u);
	widthhold_setup(true, true);
	CHECK(invoke_thread(1) == 1);
	CHECK(gta4l_test.panic[0] != 0);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.bulk_bytes ==
	    GTA4L_EMMC_BLOCK_BULK_REGION_BYTES);
	CHECK(g_self->report.bulk_windows ==
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS);
	CHECK(g_self->report.bulk_wpass ==
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS);
	CHECK(g_self->report.bulk_wfail == 0u);
	CHECK(g_self->report.bulk_woffset == 0u);
	CHECK(g_self->report.bulk_cancel == 0u);
	CHECK(g_self->report.bulk_budget == 0u);
	CHECK(g_self->report.bulk_ticks != 0u);
	CHECK(g_self->report.bulk_hz == 1000000u);
	CHECK(g_self->report.bulk_seed == GTA4L_EMMC_BLOCK_BULK_SEED);
	CHECK(g_self->report.bulkio_brr != 0u);
	CHECK(g_self->report.bulkio_drain != 0u);
	CHECK(g_self->report.bulkio_cal != 0u);
	CHECK(g_self->report.bulkio_polls != 0u);
	CHECK(g_self->report.bulkio_delays != 0u);
	CHECK(g_self->report.bulkio_host_control == 0x20u);
	CHECK(g_self->report.bulkio_clock_control == 0x0005u);
	CHECK(g_self->report.bulkio_vendor_spec ==
	    GTA4L_EMMC_VENDOR_SPEC_POR);
	CHECK(g_self->report.bulkio_rcg_cfg == GTA4L_EMMC_RCG_50M_CFG);
	CHECK(g_self->report.smmu == 1u);
	CHECK(g_self->report.smmu_stage ==
	    GTA4L_EMMC_BLOCK_SMMU_STAGE_DONE);
	CHECK(g_self->report.smmu_scr0 == 0u);
	CHECK(g_self->report.smmu_id0 == 1u);
	CHECK(g_self->report.smmu_nsmrg == 1u);
	CHECK(g_self->report.smmu_index == 0u);
	CHECK(g_self->report.smmu_smr ==
	    (GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID));
	CHECK(g_self->report.smmu_s2cr ==
	    (GTA4L_EMMC_BLOCK_SMMU_TYPE_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT));
	CHECK(g_self->report.smmu_cb == 0u);
	CHECK(g_self->report.smmu_decision ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_test.smmu_maps == 1u);
	CHECK(gta4l_test.smmu_reads >= 8u);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SMMU: st=3 scr0=00000000 id0=00000001 "
	    "n=1 smr=0/800000c0 s2cr=00010000 cb=0 dec=bypass") != 0);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(g_self->report.speed_partdiag == 0u);
	CHECK(g_self->report.speedio_req ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_BULK_CHUNKS +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS));
	CHECK(g_self->report.speedio_blk ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_BULK_REGION_BYTES /
	    GTA4L_EMMC_BLOCK_BYTES +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS *
	    GTA4L_EMMC_BLOCK_BULK_WINDOW_BLOCKS));
	CHECK(g_self->report.speedio_dat != 0u);
	CHECK(g_self->report.speedio_core != 0u);
	CHECK(g_self->report.speedio_bus != 0u);
	CHECK(g_self->report.speedio_io != 0u);
	CHECK(g_self->report.speedio_win != 0u);
	bulk_full_digest(digest);
	partread_digest_hex(digest, digestText);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 BULK: region=85688320 chunks=2615 "
	    "bytes=85688320 ticks=") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 BULKIO: brr=") != 0);
	CHECK(strstr(gta4l_test.panic, " hc=20 cc=0005 ") != 0);
	snprintf(expected, sizeof(expected),
	    " windows=80 wpass=80 wfail=0 woffset=0 digest=%s",
	    digestText);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "BULKFAIL") == 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	CHECK(gta4l_test.multi_calls ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_BULK_CHUNKS +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS));
	CHECK(g_widthhold_boot_media->open_calls == 1u);
	CHECK(g_widthhold_boot_media->close_calls == 1u);
	CHECK(g_self->online);
	CHECK(!g_self->offlineNotified);
	CHECK(gta4l_test.message_count == 0u);
	CHECK(!g_self->host.deadline_active);
	return 0;
}

static int
scenario_bulk_digest(void)
{
	uint8_t digest[32];
	char digestText[33];
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.multi_corrupt_at = 3;
	gta4l_test.multi_corrupt_count = 1;
	gta4l_test.multi_corrupt_block = GTA4L_EMMC_BLOCK_WIDTHHOLD_LBA +
	    (3u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.bulk_bytes ==
	    GTA4L_EMMC_BLOCK_BULK_REGION_BYTES);
	CHECK(g_self->report.bulk_wpass ==
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS);
	CHECK(g_self->report.bulk_wfail == 0u);
	bulk_full_digest(digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) != 0);
	partread_digest_hex(digest, digestText);
	snprintf(expected, sizeof(expected), " digest=%s", digestText);
	CHECK(strstr(gta4l_test.panic, expected) == 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	CHECK(gta4l_test.message_count == 0u);
	return 0;
}

static int
scenario_bulk_window_mismatch(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.multi_corrupt_at =
	    (int32_t)GTA4L_EMMC_BLOCK_BULK_CHUNKS;
	gta4l_test.multi_corrupt_count = 1;
	gta4l_test.multi_corrupt_block = GTA4L_EMMC_BLOCK_WIDTHHOLD_LBA;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_WINDOW);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.bulk_bytes ==
	    GTA4L_EMMC_BLOCK_BULK_REGION_BYTES);
	CHECK(g_self->report.bulk_wpass == 0u);
	CHECK(g_self->report.bulk_wfail == 1u);
	CHECK(g_self->report.bulk_woffset == 0u);
	CHECK(gta4l_test.multi_calls ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_BULK_CHUNKS + 1u));
	bulk_full_digest(digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	return 0;
}

static int
scenario_bulk_read_crc(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_multi_hook = speed_crc_hook;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_READ);
	CHECK(g_self->report.bulk_chunks == 3u);
	CHECK(g_self->report.bulk_bytes ==
	    (3u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES));
	CHECK(gta4l_test.multi_calls == 4u);
	CHECK(g_self->report.bulk_fail_status != 0);
	bulk_expected_digest(3u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES,
	    digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	return 0;
}

static int
scenario_bulk_read_timeout(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_multi_hook = speed_timeout_hook;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_READ);
	CHECK(g_self->report.bulk_chunks == 6u);
	CHECK(g_self->report.bulk_bytes ==
	    (6u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES));
	CHECK(gta4l_test.multi_calls == 7u);
	bulk_expected_digest(6u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES,
	    digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	return 0;
}

static int
scenario_bulk_short(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test_media_read_hook = speed_short_read_hook;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_SHORT);
	CHECK(g_self->report.bulk_chunks == 0u);
	CHECK(g_self->report.bulk_bytes ==
	    (GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES - 1u));
	CHECK(g_self->report.bulk_wpass == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	bulk_expected_digest(GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES - 1u,
	    digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	return 0;
}

static int
scenario_bulk_budget(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_bulk_advance_ms = 1000u;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_BUDGET);
	CHECK(g_self->report.bulk_budget == 1u);
	CHECK(g_self->report.bulk_chunks == 4u);
	CHECK(g_self->report.bulk_bytes ==
	    (4u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES));
	CHECK(g_self->report.bulk_wpass == 0u);
	CHECK(g_self->report.bulk_wfail == 0u);
	CHECK(gta4l_test.multi_calls == 4u);
	CHECK(g_self->report.speedio_req == 4u);
	CHECK(g_self->report.speedio_blk ==
	    (4u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS));
	CHECK(g_self->report.speedio_dat != 0u);
	CHECK(g_self->report.speedio_core != 0u);
	CHECK(g_self->report.speedio_bus != 0u);
	CHECK(g_self->report.speedio_io != 0u);
	bulk_expected_digest(4u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES,
	    digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	return 0;
}

static int
scenario_bulk_budget_boundary(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_advance_call = 0;
	g_widthhold_advance_ms =
	    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS - 1u;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.bulk_budget == 0u);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.bulk_wpass ==
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS);
	CHECK(g_self->report.speedio_req ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_BULK_CHUNKS +
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS));
	CHECK(g_self->report.bulk_ticks ==
	    ((uint64_t)GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS - 1u) *
	    1000u);
	CHECK(strstr(gta4l_test.panic, "budget=0") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=1") != 0);
	return 0;
}

static int
scenario_bulk_cancel_before(void)
{
	uint8_t digest[16];
	uint8_t zero[16];

	memset(zero, 0, sizeof(zero));
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_self->reportCancel = true;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_CANCEL);
	CHECK(g_self->report.bulk_cancel == 1u);
	CHECK(g_self->report.bulk_chunks == 0u);
	CHECK(g_self->report.bulk_bytes == 0u);
	CHECK(g_self->report.bulk_ticks == 0u);
	CHECK(g_self->report.bulk_wpass == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	memcpy(digest, g_self->report.bulk_digest, sizeof(digest));
	CHECK(memcmp(digest, zero, sizeof(zero)) == 0);
	CHECK(strstr(gta4l_test.panic, "cancel=1") != 0);
	return 0;
}

static int
scenario_bulk_cancel_during(void)
{
	uint8_t digest[32];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_bulk_cancel_call = 1;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_CANCEL);
	CHECK(g_self->report.bulk_cancel == 1u);
	CHECK(g_self->report.bulk_chunks == 2u);
	CHECK(g_self->report.bulk_bytes ==
	    (2u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES));
	CHECK(gta4l_test.multi_calls == 2u);
	bulk_expected_digest(2u * GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES,
	    digest);
	CHECK(memcmp(g_self->report.bulk_digest, digest, 16u) == 0);
	return 0;
}

static int
scenario_bulk_media_absent(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(false, false);
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_MEDIA);
	CHECK(g_self->report.speed_pub == 0u);
	CHECK(g_self->report.speed_partdiag == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS);
	return 0;
}

static int
scenario_bulk_part_absent(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, false);
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_PART);
	CHECK(g_self->report.speed_pub == 0u);
	CHECK(g_self->report.speed_partdiag ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND);
	CHECK(gta4l_test.multi_calls == 0u);
	CHECK(gta4l_test.now_ms >=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_DEADLINE_MS);
	return 0;
}

static int
scenario_bulk_open_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	g_widthhold_boot_media->open_refuse = true;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_OPEN);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(g_widthhold_boot_media->open_calls == 1u);
	CHECK(g_widthhold_boot_media->close_calls == 0u);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_bulk_alloc(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.consume_alloc_fail = 1;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_ALLOC);
	CHECK(g_self->report.speed_pub == 1u);
	CHECK(gta4l_test.multi_calls == 0u);
	return 0;
}

static int
scenario_bulk_shape(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.widthhold_media_tag_delta = 1;
	BULK_FAILURE_COMMON(GTA4L_EMMC_BLOCK_BULK_STAGE_SHAPE);
	CHECK(g_self->report.bulk_chunks == 0u);
	CHECK(g_self->report.bulk_bytes ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES);
	CHECK(g_self->report.bulk_wpass == 0u);
	CHECK(gta4l_test.multi_calls == 1u);
	return 0;
}

static int
scenario_bulk_revert_fail(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	gta4l_test.speed_recover_rc = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.bulk_chunks ==
	    GTA4L_EMMC_BLOCK_BULK_CHUNKS);
	CHECK(g_self->report.bulk_wpass ==
	    GTA4L_EMMC_BLOCK_BULK_WINDOWS);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_REVERT);
	CHECK(g_self->report.speed_revert == 1u);
	CHECK(g_self->report.speed_rverified == 0u);
	CHECK(gta4l_test.speed_recover_calls == 1u);
	CHECK(strstr(gta4l_test.panic, "revert=1 rverified=0") != 0);
	CHECK(gta4l_test.message_count == 1u);
	CHECK(g_self->offlineNotified);
	CHECK(!g_self->online);
	CHECK(g_self->bulkClient == 0);
	CHECK(g_self->bulkReference == 0);
	return 0;
}

static int
scenario_bulk_gate_timeout(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_gate_call = 0;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->report.bulk_fail_stage ==
	    GTA4L_EMMC_BLOCK_BULK_STAGE_NONE);
	CHECK(g_self->report.speed_fail_stage ==
	    GTA4L_EMMC_BLOCK_SPEED_STAGE_GATE);
	CHECK(g_self->report.speed_revert == 0u);
	CHECK(gta4l_test.speed_recover_calls == 0u);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8);
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SNAP: locked=0") != 0);
	CHECK(gta4l_test.now_ms >= GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_MS);
	CHECK(gta4l_test.message_count == 1u);
	g_self->gate->depth = 0;
	return 0;
}

static int
scenario_bulk_watchdog(void)
{
	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-bulk=1",
	    bulk_fixture) == 0);
	CHECK(g_started);
	widthhold_setup(true, true);
	widthhold_hook_reset();
	g_widthhold_watchdog_call = 0;
	gta4l_test_multi_hook = widthhold_multi_hook;
	CHECK(invoke_thread(1) == 0);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 REPORT: rc=16 stop=16") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 BULK: ") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 SPEED: ") != 0);
	CHECK(strstr(gta4l_test.panic, "revert=0 rverified=0") != 0);
	return 0;
}

static int
scenario_bulk_format_max(void)
{
	struct gta4l_emmc_block_report report;
	char buffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	uint32_t length;

	bzero(&report, sizeof(report));
	report.bulk = 1u;
	report.bulk_region = 0xffffffffu;
	report.bulk_chunks = 0xffffffffu;
	report.bulk_bytes = 0xffffffffu;
	report.bulk_ticks = UINT64_MAX;
	report.bulk_hz = UINT64_MAX;
	report.bulk_seed = 0xffffffffu;
	report.bulk_windows = 0xffffffffu;
	report.bulk_wpass = 0xffffffffu;
	report.bulk_wfail = 0xffffffffu;
	report.bulk_woffset = 0xffffffffu;
	memset(report.bulk_digest, 0xff, sizeof(report.bulk_digest));
	report.bulk_fail_stage = 0xffffffffu;
	report.bulk_fail_status = INT32_MIN;
	report.bulk_cancel = 0xffffffffu;
	report.bulk_budget = 0xffffffffu;
	report.bulkio_brr = 0xffffffffu;
	report.bulkio_drain = 0xffffffffu;
	report.bulkio_cal = 0xffffffffu;
	report.bulkio_polls = 0xffffffffu;
	report.bulkio_delays = 0xffffffffu;
	report.bulkio_sha = 0xffffffffu;
	report.bulkio_sha_provider = 1u;
	report.bulkio_host_control = 0xffffffffu;
	report.bulkio_clock_control = 0xffffffffu;
	report.bulkio_present = 0xffffffffu;
	report.bulkio_caps0 = 0xffffffffu;
	report.bulkio_caps1 = 0xffffffffu;
	report.bulkio_dll_config = 0xffffffffu;
	report.bulkio_dll_status = 0xffffffffu;
	report.bulkio_vendor_spec = 0xffffffffu;
	report.bulkio_vendor_spec3 = 0xffffffffu;
	report.bulkio_rcg_cfg = 0xffffffffu;
	report.bulkio_rcg_m = 0xffffffffu;
	report.bulkio_rcg_n = 0xffffffffu;
	report.bulkio_rcg_d = 0xffffffffu;
	report.smmu = 1u;
	report.smmu_stage = 0xffffffffu;
	report.smmu_scr0 = 0xffffffffu;
	report.smmu_id0 = 0xffffffffu;
	report.smmu_nsmrg = 0xffffffffu;
	report.smmu_index = 0xffffffffu;
	report.smmu_smr = 0xffffffffu;
	report.smmu_s2cr = 0xffffffffu;
	report.smmu_cb = 0xffffffffu;
	report.smmu_decision = GTA4L_EMMC_BLOCK_SMMU_DECISION_TRANSLATE;
	report.smmu_cb_line = 1u;
	report.smmu_id1 = 0xffffffffu;
	report.smmu_id2 = 0xffffffffu;
	report.smmu_gfsr = 0xffffffffu;
	report.smmu_gfsynr0 = 0xffffffffu;
	report.smmu_valid = 0xffffffffu;
	report.smmu_matches = 0xffffffffu;
	report.smmu_share = 0xffffffffu;
	report.smmu_cbar = 0xffffffffu;
	report.smmu_sctlr = 0xffffffffu;
	report.smmu_tcr = 0xffffffffu;
	report.smmu_ttbr0 = UINT64_MAX;
	report.smmu_fsr = 0xffffffffu;
	report.smmu_last_index = 0xffffffffu;
	report.smmu_last_sctlr = 0xffffffffu;
	report.smmu_last_cbar = 0xffffffffu;
	report.smmu_pfsr = 0xffffffffu;
	report.smmu_pgfsr = 0xffffffffu;
	report.dma = 1u;
	report.dma_mode = GTA4L_EMMC_BLOCK_DMA_MODE_ADMA2_32;
	report.dma_smmu = GTA4L_EMMC_BLOCK_SMMU_DECISION_TRANSLATE;
	report.dma_cmp_pass = 0xffffffffu;
	report.dma_cmp_total = 0xffffffffu;
	report.dma_canary = 0xffffffffu;
	report.dma_poison = 0xffffffffu;
	report.dma_chunks = 0xffffffffu;
	report.dma_bytes = 0xffffffffu;
	report.dma_ticks = UINT64_MAX;
	report.dma_dat = UINT64_MAX;
	report.dma_fails = 0xffffffffu;
	report.dma_stage = 0xffffffffu;
	report.dma_ast = 0xffffffffu;
	report.dma_aaddr = 0xffffffffu;
	report.dma_desc = 0xffffffffu;
	report.dma_budget = 0xffffffffu;
	report.dma_skip = 0xffffffffu;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length != 0u);
	CHECK(length < sizeof(buffer));
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 BULK: ") ==
	    GTA4L_EMMC_BLOCK_BULK_LINE_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 BULKIO: ") ==
	    GTA4L_EMMC_BLOCK_BULKIO_LINE_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 SMMU: ") ==
	    GTA4L_EMMC_BLOCK_SMMU_LINE_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 SMMUCB: ") ==
	    GTA4L_EMMC_BLOCK_SMMUCB_LINE_BYTES);
	CHECK(strstr(buffer,
	    "GTA4L-EMMC P5 SMMUCB: id1=ffffffff id2=ffffffff") != 0);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 DMA: ") ==
	    GTA4L_EMMC_BLOCK_DMA_LINE_BYTES);
	CHECK(strstr(buffer, "dec=translate") != 0);
	CHECK(strstr(buffer, "mode=adma2-32 smmu=translate") != 0);
	CHECK(strstr(buffer,
	    "smr=4294967295/ffffffff s2cr=ffffffff cb=4294967295") != 0);
	CHECK(strstr(buffer, " cal=4294967295 ") != 0);
	CHECK(strstr(buffer, " shap=lk ") != 0);
	CHECK(strstr(buffer, "cap=ffffffffffffffff") != 0);
	CHECK(strstr(buffer, "dll=ffffffffffffffff") != 0);
	CHECK(strstr(buffer, "ven=ffffffffffffffff") != 0);
	CHECK(strstr(buffer, "rcg=ffffffffffffffff") != 0);
	CHECK(strstr(buffer, "rcg2=ffffffffffffffff") != 0);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 BULKFAIL: ") ==
	    GTA4L_EMMC_BLOCK_BULKFAIL_LINE_BYTES);
	CHECK(GTA4L_EMMC_BLOCK_BULK_REPORT_WORST_BYTES ==
	    (GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_BYTES +
	    GTA4L_EMMC_BLOCK_SPEEDIO_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_BULK_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_BULKIO_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_SMMU_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_SMMUCB_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_DMA_LINE_BYTES + 1u +
	    GTA4L_EMMC_BLOCK_BULKFAIL_LINE_BYTES + 1u));
	CHECK(GTA4L_EMMC_BLOCK_BULK_REPORT_WORST_BYTES <=
	    GTA4L_EMMC_BLOCK_REPORT_BYTES);
	return 0;
}

static int
scenario_bulk_absent(void)
{
	begin("gta4l-emmc-block=3");
	CHECK(g_started);
	CHECK(!g_self->bulkMode);
	CHECK(invoke_thread(1) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT:") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 BULK") == 0);
	return 0;
}

static int
scenario_bulk_refused_orphan(void)
{
	begin("gta4l-emmc-bulk=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-bulk requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_bulk_refused_block(void)
{
	static const char *arguments[] = {
		"gta4l-emmc-bulk=1",
		"gta4l-emmc-block=1 gta4l-emmc-bulk=1",
		"gta4l-emmc-block=2 gta4l-emmc-bulk=1",
		"gta4l-emmc-block=4 gta4l-emmc-bulk=1",
	};
	unsigned int index;

	for (index = 0;
	    index < sizeof(arguments) / sizeof(arguments[0]); index++) {
		begin(arguments[index]);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK((strstr(gta4l_test.log,
		    "requires gta4l-emmc-block=3") != 0) ||
		    (strstr(gta4l_test.log,
		    "malformed gta4l-emmc-block value") != 0) ||
		    (strstr(gta4l_test.log,
		    "publish-only mode 2 is deferred") != 0));
	}
	return 0;
}

static int
scenario_bulk_refused_value(void)
{
	static const int values[] = { 0, 2 };
	char arguments[64];
	unsigned int index;

	for (index = 0;
	    index < sizeof(values) / sizeof(values[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-bulk=%d",
		    values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-bulk value must be 1") != 0);
	}
	return 0;
}

static int
scenario_bulk_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-bulk=1 "
	    "gta4l-emmc-bulk=1");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-bulk token") != 0);
	return 0;
}

static int
scenario_bulk_refused_conflicts(void)
{
	static const struct {
		const char *token;
		const char *name;
	} conflicts[] = {
		{ "gta4l-emmc-partobs=1", "partobs" },
		{ "gta4l-emmc-partcmp=1", "partcmp" },
		{ "gta4l-emmc-partread=1", "partread" },
		{ "gta4l-emmc-deblock=1", "deblock" },
		{ "gta4l-emmc-breakup=1", "breakup" },
		{ "gta4l-emmc-sweep=1", "sweep" },
		{ "gta4l-emmc-consume=1", "consume" },
		{ "gta4l-emmc-width=4", "width" },
		{ "gta4l-emmc-widthhold=1", "widthhold" },
		{ "gta4l-emmc-widthpub=1", "widthpub" },
		{ "gta4l-emmc-speed=1", "speed" },
	};
	char arguments[128];
	char expected[128];
	unsigned int index;

	for (index = 0;
	    index < sizeof(conflicts) / sizeof(conflicts[0]); index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 %s gta4l-emmc-bulk=1",
		    conflicts[index].token);
		snprintf(expected, sizeof(expected),
		    "gta4l-emmc-%s and gta4l-emmc-bulk are mutually "
		    "exclusive", conflicts[index].name);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log, expected) != 0);
	}
	return 0;
}

static int
scenario_width_absent(void)
{
	begin("gta4l-emmc-block=1");
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 REPORT:") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTH") == 0);
	CHECK(gta4l_test.width_ext_csd_calls == 0);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	return 0;
}

static int
scenario_width_refused_orphan(void)
{
	begin("gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-width requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_width_refused_block1(void)
{
	begin("gta4l-emmc-block=1 gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-width requires gta4l-emmc-block=3") != 0);
	return 0;
}

static int
scenario_width_refused_value(void)
{
	static const int values[] = { 0, 1, 2, 3, 5, 8 };
	char arguments[64];
	unsigned int index;

	for (index = 0; index < sizeof(values) / sizeof(values[0]);
	    index++) {
		snprintf(arguments, sizeof(arguments),
		    "gta4l-emmc-block=3 gta4l-emmc-width=%d",
		    values[index]);
		begin(arguments);
		CHECK(!g_started);
		CHECK(gta4l_test.platform_maps == 0);
		CHECK(gta4l_test.thread_count == 0);
		CHECK(strstr(gta4l_test.log,
		    "gta4l-emmc-width value must be 4") != 0);
	}
	return 0;
}

static int
scenario_width_refused_duplicate(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-width=4 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "duplicate gta4l-emmc-width token") != 0);
	return 0;
}

static int
scenario_width_refused_partobs(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partobs=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-partobs and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_partcmp(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partcmp=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-partcmp and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_partread(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-partread=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-partread and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_deblock(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-deblock=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-deblock and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_breakup(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-breakup=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-breakup and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_sweep(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-sweep=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-sweep and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_refused_consume(void)
{
	begin("gta4l-emmc-block=3 gta4l-emmc-consume=1 "
	    "gta4l-emmc-width=4");
	CHECK(!g_started);
	CHECK(gta4l_test.platform_maps == 0);
	CHECK(gta4l_test.thread_count == 0);
	CHECK(strstr(gta4l_test.log,
	    "gta4l-emmc-consume and gta4l-emmc-width are mutually "
	    "exclusive") != 0);
	return 0;
}

static int
scenario_width_match(void)
{
	uint8_t expected_digest[32];
	char digest[33];
	char expected[512];
	OSNumber *property;

	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	CHECK(!g_panicked);
	CHECK(gta4l_test.registered == 1);
	CHECK(gta4l_test.thread_count == 2);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	CHECK(gta4l_test.width_host_control ==
	    GTA4L_EMMC_HOST_CONTROL_4BIT);
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumBlockCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 64));
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumByteCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 32768));
	property = OSDynamicCast(OSNumber,
	    g_self->getProperty(kIOMaximumSegmentCountReadKey));
	CHECK((property != 0) &&
	    (property->unsigned64BitValue() == 1));
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1 switch=4 verified=1 hb=0 ha=2 rb=1 cmd6=1 "
	    "revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 reads=2 bytes=2048 "
	    "blocks=4 subrequests=2 max=2 match=1 mismatches=0 fails=0 "
	    "cancel=0 budget=0 limit=64 seg=1 cap=61071360") != 0);
	width_expected_digest(expected_digest);
	partread_digest_hex(expected_digest, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 WIDTHREAD: index=0 lba=2255360 start=0 "
	    "length=1024 span=1024 actual=1024 status=0 subrequests=1 "
	    "blocks=2 max=2 digest=%s predigest=%s match=1",
	    digest, digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHFAIL") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 QUAL") == 0);
	CHECK(strstr(gta4l_test.panic, "wid=0") != 0);
	CHECK(g_self->report.width_complete == 1);
	CHECK(g_self->report.width_match == 1);
	CHECK(g_self->report.width_mismatch == 0);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(gta4l_test.width_host_control == 0);
	CHECK(g_self->block.counters.cmd17_reads == 0);
	CHECK(g_self->block.counters.cmd18_chunks == 2);
	CHECK(g_self->block.counters.stop_failures == 0);
	return 0;
}

static void
width_baseline_fixture(void)
{
	gta4l_test.width_ext_csd_fail = -5;
}

static int
scenario_width_baseline_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_baseline_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 0);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=1 "
	    "complete=0 switch=0 verified=0 hb=0 ha=0 rb=0 cmd6=0 "
	    "revert=0 rverified=0 rhb=0 rha=0 rcmd6=0 reads=0 bytes=0 "
	    "blocks=0 subrequests=0 max=0 match=0 mismatches=0 fails=0 "
	    "cancel=0 budget=0 limit=64 seg=1 cap=61071360") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_BASELINE, -5, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHREAD") == 0);
	return 0;
}

static void
width_preread_fixture(void)
{
	gta4l_test.fail_multi_at = 0;
}

static int
scenario_width_preread_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_preread_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_ext_csd_calls == 1);
	CHECK(gta4l_test.width_switch_calls == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=2 "
	    "complete=0") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_PREREAD, -5, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_switch_fixture(void)
{
	gta4l_test.width_switch_fail = GTA4L_EMMC_ERR_BUS_WIDTH;
}

static int
scenario_width_switch_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_switch_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=3 "
	    "complete=0 switch=4 verified=0 hb=0 ha=0 rb=0 cmd6=1") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH,
	    GTA4L_EMMC_ERR_BUS_WIDTH, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_switch_unverified_fixture(void)
{
	gta4l_test.width_switch_verified = 0;
}

static int
scenario_width_switch_unverified(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_switch_unverified_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=3 "
	    "complete=0 switch=4 verified=0 hb=0 ha=2 rb=1 cmd6=1") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_switch_host_fixture(void)
{
	gta4l_test.width_switch_host_bit = 0;
}

static int
scenario_width_switch_host(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_switch_host_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=3 "
	    "complete=0 switch=4 verified=1 hb=0 ha=0 rb=1 cmd6=1") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_switch_restore_fixture(void)
{
	gta4l_test.width_switch_fail = GTA4L_EMMC_ERR_WIDTH_RESTORE;
}

static int
scenario_width_switch_restore(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_switch_restore_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH,
	    GTA4L_EMMC_ERR_WIDTH_RESTORE, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_baseline_read_fixture(void)
{
	gta4l_test.fail_read_at = 0;
}

static int
scenario_width_baseline_read_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_baseline_read_fixture) == 1);
	CHECK(!g_started);
	CHECK(g_panicked);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	CHECK(gta4l_test.read_calls == 0);
	CHECK(strstr(gta4l_test.panic, "stop=11") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=4 "
	    "complete=0 switch=4 verified=1 hb=0 ha=2 rb=1 cmd6=1") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_READ, -5, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_read_fixture(void)
{
	gta4l_test.fail_multi_at = 1;
}

static int
scenario_width_read_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_read_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(gta4l_test.multi_calls == 2);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=4 "
	    "complete=0 switch=4 verified=1 hb=0 ha=2 rb=1 cmd6=1 "
	    "revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 reads=2 bytes=1024 "
	    "blocks=2 subrequests=2 max=2 match=0 mismatches=0 fails=1 "
	    "cancel=0 budget=0 limit=64 seg=1 cap=61071360") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_READ, -5, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHREAD") == 0);
	return 0;
}

static void
width_short_fixture(void)
{
	gta4l_test.multi_short_at = 1;
	gta4l_test.multi_short_blocks = 1;
}

static int
scenario_width_short_read(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_short_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(gta4l_test.multi_calls == 2);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_READ, -1, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_stop_fixture(void)
{
	gta4l_test.multi_stop_at = 1;
	gta4l_test.multi_stop_result = -5;
}

static int
scenario_width_stop_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_stop_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(gta4l_test.multi_calls == 2);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->block.counters.stop_failures == 1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "reason=4 complete=0") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_READ, -5, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_mismatch_fixture(void)
{
	gta4l_test.multi_corrupt_at = 1;
	gta4l_test.multi_corrupt_block = GTA4L_EMMC_BLOCK_WIDTH_LBA;
}

static int
scenario_width_match_mismatch(void)
{
	uint8_t observed[32];
	uint8_t scratch[GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES];
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_mismatch_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	gta4l_test_fill_sector(GTA4L_EMMC_BLOCK_WIDTH_LBA, scratch);
	gta4l_test_fill_sector(GTA4L_EMMC_BLOCK_WIDTH_LBA + 1u,
	    scratch + GTA4L_EMMC_BLOCK_BYTES);
	scratch[0] ^= 0x5a;
	gta4l_emmc_block_sha256(scratch,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, observed);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=5 "
	    "complete=0 switch=4 verified=1 hb=0 ha=2 rb=1 cmd6=1 "
	    "revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 reads=2 bytes=2048 "
	    "blocks=4 subrequests=2 max=2 match=0 mismatches=1 fails=0 "
	    "cancel=0 budget=0 limit=64 seg=1 cap=61071360") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_MATCH, 0,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, observed);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHREAD") == 0);
	return 0;
}

static void
width_corrupted_digest(uint8_t *digest)
{
	static uint8_t scratch[GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES];

	gta4l_test_fill_sector(GTA4L_EMMC_BLOCK_WIDTH_LBA, scratch);
	gta4l_test_fill_sector(GTA4L_EMMC_BLOCK_WIDTH_LBA + 1u,
	    scratch + GTA4L_EMMC_BLOCK_BYTES);
	scratch[0] ^= 0x5a;
	gta4l_emmc_block_sha256(scratch,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, digest);
}

static void
width_host_digest_fixture(void)
{
	gta4l_test.multi_corrupt_at = 0;
	gta4l_test.multi_corrupt_count = 2;
	gta4l_test.multi_corrupt_block = GTA4L_EMMC_BLOCK_WIDTH_LBA;
}

static int
scenario_width_host_digest_mismatch(void)
{
	uint8_t expected_digest[32];
	uint8_t observed_digest[32];
	char expected[512];
	char digest[33];
	char packaged[33];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_host_digest_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	/* Both on-device reads see the same corrupted window, so the
	 * driver's byte compare passes and it reports complete=1. The
	 * host comparison against the packaged image still rejects it. */
	width_expected_digest(expected_digest);
	width_corrupted_digest(observed_digest);
	CHECK(memcmp(observed_digest, expected_digest,
	    sizeof(observed_digest)) != 0);
	partread_digest_hex(observed_digest, digest);
	snprintf(expected, sizeof(expected),
	    "GTA4L-EMMC P5 WIDTHREAD: index=0 lba=2255360 start=0 "
	    "length=1024 span=1024 actual=1024 status=0 subrequests=1 "
	    "blocks=2 max=2 digest=%s predigest=%s match=1",
	    digest, digest);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1") != 0);
	CHECK(g_self->report.width_complete == 1);
	CHECK(g_self->report.width_match == 1);
	CHECK(strstr(gta4l_test.panic, "WIDTHFAIL") == 0);
	partread_digest_hex(expected_digest, packaged);
	CHECK(strstr(gta4l_test.panic, packaged) == 0);
	return 0;
}

static void
width_revert_fixture(void)
{
	gta4l_test.width_recover_fail = GTA4L_EMMC_ERR_WIDTH_RESTORE;
}

static int
scenario_width_revert_failure(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_revert_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=6 "
	    "complete=0 switch=4 verified=1 hb=0 ha=2 rb=1 cmd6=1 "
	    "revert=1 rverified=0 rhb=2 rha=2 rcmd6=2 reads=2 bytes=2048 "
	    "blocks=4 subrequests=2 max=2 match=1 mismatches=0 fails=0 "
	    "cancel=0 budget=0 limit=64 seg=1 cap=61071360") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_REVERT,
	    GTA4L_EMMC_ERR_WIDTH_RESTORE, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static void
width_revert_unverified_fixture(void)
{
	gta4l_test.width_recover_verified = 0;
}

static int
scenario_width_revert_unverified(void)
{
	char expected[256];

	CHECK(begin_with("gta4l-emmc-block=3 gta4l-emmc-width=4",
	    width_revert_unverified_fixture) == 0);
	CHECK(g_started);
	CHECK(invoke_thread(0) == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	CHECK(!g_self->online);
	CHECK(strstr(gta4l_test.panic,
	    "reason=6 complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "rverified=0 rhb=2 rha=0") != 0);
	width_fail_line(expected, sizeof(expected),
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_REVERT, 0, 0, 0);
	CHECK(strstr(gta4l_test.panic, expected) != 0);
	return 0;
}

static int
scenario_width_budget(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	gta4l_test.width_recover_advance_ms =
	    (int)GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 "
	    "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "budget=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHREAD:") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHFAIL") == 0);
	CHECK(g_self->report.width_complete == 0);
	return 0;
}

static int
scenario_width_cancel(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	g_self->reportCancel = true;
	CHECK(invoke_thread(0) == 0);
	CHECK(!gta4l_test.panicked);
	CHECK(gta4l_test.panic[0] == 0);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.width_cancel == 1);
	CHECK(g_self->report.width_reason ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL);
	CHECK(g_self->report.width_fail_stage ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL);
	CHECK(g_self->report.width_complete == 0);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(gta4l_test.multi_calls == 1);
	return 0;
}

static int
scenario_width_watchdog(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	CHECK(invoke_thread(1) == 1);
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=0 reason=0 "
	    "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "wid=1") != 0);
	return 0;
}

static void
width_cancel_wait_hook(uint32_t phase)
{
	(void)phase;
	/* Cancellation requested while the bounded gate acquisition sleeps. */
	g_self->reportCancel = true;
}

static void
width_cancel_after_read_hook(uint32_t phase)
{
	if (phase == GTA4L_TEST_WIDTH_PHASE_AFTER_READ) {
		g_self->reportCancel = true;
	}
}

static int
scenario_width_cancel_gate_wait(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	/* The gate is released only after the cancellation hook has fired, so
	 * the run continues through the read and the verified revert and then
	 * latches cancellation without a completion claim. */
	g_self->gate->depth = 1;
	gta4l_test.width_gate = g_self->gate;
	gta4l_test.width_gate_release_ms = 50;
	gta4l_test.width_wait_active = 1;
	gta4l_test_width_hook = width_cancel_wait_hook;
	CHECK(invoke_thread(0) == 0);
	gta4l_test_width_hook = 0;
	gta4l_test.width_wait_active = 0;
	gta4l_test.width_gate = 0;
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.width_cancel == 1);
	CHECK(g_self->report.width_complete == 0);
	CHECK(g_self->report.width_reason ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL);
	CHECK(g_self->report.width_fail_stage ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL);
	CHECK(g_self->report.width_reads == 2);
	CHECK(g_self->report.width_match == 1);
	CHECK(g_self->report.width_revert == 1);
	CHECK(g_self->report.width_rverified == 1);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static int
scenario_width_cancel_after_read(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	/* Cancellation is requested exactly after the 4-bit read and revert
	 * released the gate, before the terminal report is claimed. */
	gta4l_test_width_hook = width_cancel_after_read_hook;
	CHECK(invoke_thread(0) == 0);
	gta4l_test_width_hook = 0;
	CHECK(!gta4l_test.panicked);
	CHECK(g_self->reportDone);
	CHECK(!g_self->terminalClaimed);
	CHECK(g_self->report.width_cancel == 1);
	CHECK(g_self->report.width_complete == 0);
	CHECK(g_self->report.width_reason ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL);
	CHECK(g_self->report.width_reads == 2);
	CHECK(g_self->report.width_match == 1);
	CHECK(g_self->report.width_revert == 1);
	CHECK(g_self->report.width_rverified == 1);
	CHECK(gta4l_test.width_switch_calls == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	return 0;
}

static int g_width_terminal_hook_fired;

static void
width_terminal_watchdog_hook(uint32_t phase)
{
	if (phase != GTA4L_TEST_WIDTH_PHASE_BEFORE_TERMINAL) {
		return;
	}
	if (g_width_terminal_hook_fired != 0) {
		return;
	}
	g_width_terminal_hook_fired = 1;
	/* Simulate an intervening request that still owns the controller gate
	 * when the width path reaches its terminal report. The watchdog then
	 * takes the only terminal claim before the report thread can. */
	g_self->gate->depth = 1;
	(void)invoke_thread(1);
}

static int
scenario_width_terminal_watchdog(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	g_width_terminal_hook_fired = 0;
	gta4l_test_width_hook = width_terminal_watchdog_hook;
	CHECK(invoke_thread(0) == 0);
	gta4l_test_width_hook = 0;
	CHECK(g_self->terminalClaimed);
	CHECK(g_self->reportDone);
	CHECK(strstr(gta4l_test.panic, "stop=16") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 SNAP: locked=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=0 reason=0 "
	    "complete=0") != 0);
	CHECK(gta4l_test.width_recover_calls == 1);
	g_self->gate->depth = 0;
	return 0;
}

static int
scenario_width_gate_release(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	g_self->gate->depth = 1;
	gta4l_test.width_gate = g_self->gate;
	gta4l_test.width_gate_release_ms = 50;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHREAD:") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHFAIL") == 0);
	CHECK(g_self->report.width_complete == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	gta4l_test.width_gate = 0;
	return 0;
}

static int
scenario_width_gate_timeout(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	/* Simulate an in-flight request holding the controller gate for the
	 * whole bounded acquisition window. */
	g_self->gate->depth = 1;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=8 "
	    "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTHFAIL: stage=8 status=0 actual=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHREAD:") == 0);
	CHECK(g_self->report.width_complete == 0);
	CHECK(g_self->report.width_reason ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_GATE);
	CHECK(g_self->report.width_fail_stage ==
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_GATE);
	CHECK(g_self->online == false);
	CHECK(g_self->report.width_reads == 0);
	CHECK(g_self->report.width_bytes == 0);
	CHECK(g_self->report.width_revert == 0);
	CHECK(g_self->report.width_last_actual == 0);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4);
	/* The report owns the terminal claim; a later watchdog pass must
	 * return without a second panic or report. */
	CHECK(invoke_thread(1) == 0);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=8 "
	    "complete=0") != 0);
	g_self->gate->depth = 0;
	return 0;
}

static int
scenario_width_gate_deadline_release(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	g_self->gate->depth = 1;
	gta4l_test.width_gate = g_self->gate;
	gta4l_test.width_gate_release_ms =
	    (int)GTA4L_EMMC_BLOCK_WIDTH_GATE_MS;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 "
	    "complete=1") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHREAD:") != 0);
	CHECK(strstr(gta4l_test.panic, "WIDTHFAIL") == 0);
	CHECK(g_self->report.width_complete == 1);
	CHECK(gta4l_test.width_recover_calls == 1);
	CHECK(g_self->host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1);
	gta4l_test.width_gate = 0;
	return 0;
}

static int
scenario_width_gate_deadline_miss(void)
{
	CHECK(begin("gta4l-emmc-block=3 gta4l-emmc-width=4") == 0);
	CHECK(g_started);
	g_self->gate->depth = 1;
	gta4l_test.width_gate = g_self->gate;
	gta4l_test.width_gate_release_ms =
	    (int)GTA4L_EMMC_BLOCK_WIDTH_GATE_MS + 10;
	CHECK(invoke_thread(0) == 1);
	CHECK(strstr(gta4l_test.panic,
	    "GTA4L-EMMC P5 WIDTH: cases=1 passed=0 failed=1 reason=8 "
	    "complete=0") != 0);
	CHECK(strstr(gta4l_test.panic, "GTA4L-EMMC P5 WIDTHREAD:") == 0);
	CHECK(g_self->report.width_reads == 0);
	CHECK(g_self->report.width_revert == 0);
	CHECK(g_self->online == false);
	CHECK(g_self->block.counters.cmd18_chunks == 1);
	CHECK(gta4l_test.width_recover_calls == 0);
	gta4l_test.width_gate = 0;
	g_self->gate->depth = 0;
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
	CHECK(gta4l_emmc_block_partobs_select(0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_OFF);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_ON);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 1, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partobs_select(1, 2, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partobs_select(1, 1, 0, 1, 1) ==
	    GTA4L_EMMC_BLOCK_PARTOBS_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_OFF);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_ON);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 1, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 2, 0, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 1, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partcmp_select(1, 1, 0, 1, 0, 1) ==
	    GTA4L_EMMC_BLOCK_PARTCMP_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_OFF);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 0, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_ON);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 1, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(1, 2, 0, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 0, 1, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 0, 1, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_select(1, 1, 0, 1, 0, 0, 1) ==
	    GTA4L_EMMC_BLOCK_PARTREAD_REFUSED);
	CHECK(gta4l_emmc_block_partread_case_count() == 14);
	CHECK(gta4l_emmc_block_partread_case_at(14) == 0);
	CHECK(gta4l_emmc_block_consume_select(0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_OFF);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_ON);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 0, 3,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 1,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 1, 1, 3,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 2, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    1, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 1, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 1, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 0, 1, 0, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 1, 0, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 1, 0) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_consume_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 1) == GTA4L_EMMC_BLOCK_CONSUME_REFUSED);
	CHECK(gta4l_emmc_block_width_select(0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_OFF);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_ON);
	CHECK(gta4l_emmc_block_width_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 1, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 0, 3,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 1,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 2,
	    0, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    1, 0, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 1, 0, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 1, 0, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 1, 0, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 0, 1, 0, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 0, 0, 1, 0, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 1, 0) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_width_select(1, 4, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 1) == GTA4L_EMMC_BLOCK_WIDTH_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_OFF);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_ON);
	CHECK(gta4l_emmc_block_speed_select(1, 0, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 2, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 1, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 0, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    1, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 1, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 1, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 1, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 1, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 1, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_speed_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 1) ==
	    GTA4L_EMMC_BLOCK_SPEED_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(0, 0, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_OFF);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_ON);
	CHECK(gta4l_emmc_block_bulk_select(1, 0, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 2, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 1, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 0, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	CHECK(gta4l_emmc_block_bulk_select(1, 1, 0, 1, 3,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) ==
	    GTA4L_EMMC_BLOCK_BULK_REFUSED);
	{
		uint64_t windowBlock;
		uint64_t previous = 0;
		uint32_t windowIndex;

		CHECK(gta4l_emmc_block_bulk_window(0, &windowBlock) == 0);
		CHECK(windowBlock == 0u);
		CHECK(gta4l_emmc_block_bulk_window(
		    GTA4L_EMMC_BLOCK_BULK_WINDOWS, &windowBlock) == -1);
		CHECK(gta4l_emmc_block_bulk_window(0, 0) == -1);
		for (windowIndex = 0;
		    windowIndex < GTA4L_EMMC_BLOCK_BULK_WINDOWS;
		    windowIndex++) {
			CHECK(gta4l_emmc_block_bulk_window(windowIndex,
			    &windowBlock) == 0);
			CHECK(windowBlock ==
			    bulk_expected_window(windowIndex));
			CHECK((windowBlock +
			    GTA4L_EMMC_BLOCK_BULK_WINDOW_BLOCKS) <=
			    (GTA4L_EMMC_BLOCK_BULK_REGION_BYTES /
			    GTA4L_EMMC_BLOCK_BYTES));
			if (windowIndex <
			    GTA4L_EMMC_BLOCK_BULK_ALIGNED_WINDOWS) {
				CHECK((windowBlock %
				    GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS) ==
				    0u);
				if (windowIndex != 0) {
					CHECK(windowBlock > previous);
				}
				previous = windowBlock;
			} else {
				CHECK((windowBlock %
				    GTA4L_EMMC_BLOCK_BULK_CHUNK_BLOCKS) !=
				    0u);
			}
		}
	}
	CHECK(gta4l_emmc_block_smmu_match(0x00c0u,
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID) == 1);
	CHECK(gta4l_emmc_block_smmu_match(0x00c1u,
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID) == 0);
	CHECK(gta4l_emmc_block_smmu_match(0x00c0u,
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID) == 0);
	CHECK(gta4l_emmc_block_smmu_match(0x00c1u,
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID | (1u << 16)) == 1);
	CHECK(gta4l_emmc_block_smmu_match(0x01c0u,
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID | (1u << 24)) == 1);
	CHECK(gta4l_emmc_block_smmu_match(0x01c1u,
	    GTA4L_EMMC_BLOCK_SMMU_SMR_VALID |
	    GTA4L_EMMC_BLOCK_SMMU_STREAM_ID | (1u << 24)) == 0);
	CHECK(gta4l_emmc_block_smmu_decide(
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_CLIENTPD, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_emmc_block_smmu_decide(0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_emmc_block_smmu_decide(
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG, 0, 0) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT);
	CHECK(gta4l_emmc_block_smmu_decide(0,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 1) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_emmc_block_smmu_decide(0,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 1) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT);
	{
		struct gta4l_emmc_block_smmu_cb_state state;

		memset(&state, 0, sizeof(state));
		state.cbar =
		    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
		    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT;
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 1, 0,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_OFF);
		state.sctlr = GTA4L_EMMC_BLOCK_SMMU_SCTLR_M;
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 1, 0,
		    &state) ==
		    GTA4L_EMMC_BLOCK_SMMU_DECISION_TRANSLATE_LIVE);
		state.sctlr = 0;
		state.fsr = 1;
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 1, 0,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_FAULT);
		state.fsr = 0;
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 1, 1,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_FAULT);
		state.cbar = 0;
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 1, 0,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_CB_TYPE);
		state.cbar =
		    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS <<
		    GTA4L_EMMC_BLOCK_SMMU_CBAR_TYPE_SHIFT;
		CHECK(gta4l_emmc_block_smmu_decide_cb(2, 1, 0,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_CONFLICT);
		CHECK(gta4l_emmc_block_smmu_decide_cb(1, 0, 0,
		    &state) == GTA4L_EMMC_BLOCK_SMMU_DECISION_LAYOUT);
	}
	CHECK(gta4l_emmc_block_smmu_decide(0,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_TRANSLATE <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 1) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_TRANSLATE);
	CHECK(gta4l_emmc_block_smmu_decide(
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_CLIENTPD |
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 1) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_emmc_block_smmu_decide(0,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 0) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS);
	CHECK(gta4l_emmc_block_smmu_decide(
	    GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG,
	    GTA4L_EMMC_BLOCK_SMMU_TYPE_BYPASS <<
	    GTA4L_EMMC_BLOCK_SMMU_S2CR_TYPE_SHIFT, 0) ==
	    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT);
	CHECK(gta4l_emmc_block_dma_select(0, 0, 0, 1, 3, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_OFF);
	CHECK(gta4l_emmc_block_dma_select(1, 1, 0, 1, 3, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_ON);
	CHECK(gta4l_emmc_block_dma_select(1, 1, 0, 1, 3, 0,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_REFUSED);
	CHECK(gta4l_emmc_block_dma_select(1, 1, 0, 1, 1, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_REFUSED);
	CHECK(gta4l_emmc_block_dma_select(1, 0, 0, 1, 3, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_REFUSED);
	CHECK(gta4l_emmc_block_dma_select(1, 1, 1, 1, 3, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_REFUSED);
	CHECK(gta4l_emmc_block_dma_select(1, 1, 0, 1, 3, 1,
	    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0) ==
	    GTA4L_EMMC_BLOCK_DMA_REFUSED);
	{
		uint8_t table[64];

		memset(table, 0, sizeof(table));
		CHECK(gta4l_emmc_block_adma_build(table, sizeof(table),
		    0x1000u, 2048u, 1024u) == 2u);
		CHECK((table[0] & GTA4L_EMMC_BLOCK_ADMA_DESC_VALID) != 0u);
		CHECK((table[0] & GTA4L_EMMC_BLOCK_ADMA_DESC_ACT_TRAN) != 0u);
		CHECK((table[0] & GTA4L_EMMC_BLOCK_ADMA_DESC_END) == 0u);
		CHECK((table[8] & GTA4L_EMMC_BLOCK_ADMA_DESC_END) != 0u);
		CHECK(table[2] == 0x00u && table[3] == 0x04u);
		CHECK(table[4] == 0x00u && table[5] == 0x10u);
		CHECK(table[12] == 0x00u && table[13] == 0x14u);
		CHECK(gta4l_emmc_block_adma_build(table, sizeof(table),
		    0xfffff000u, 0x2000u, 1024u) == 0u);
		CHECK(gta4l_emmc_block_adma_build(table, 8u,
		    0x1000u, 2048u, 1024u) == 0u);
		CHECK(gta4l_emmc_block_adma_build(table, sizeof(table),
		    0x1000u, 0u, 1024u) == 0u);
		CHECK(gta4l_emmc_block_adma_build(table, sizeof(table),
		    0x1000u, 1024u, 0u) == 0u);
	}
	CHECK(gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) == 0);
	CHECK(gta4l_emmc_block_consume_validate(1,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) != 0);
	CHECK(gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES / 2u,
	    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) != 0);
	CHECK(gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES, 1024) == 0);
	CHECK(gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES, 1025) != 0);
	CHECK(gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES, 512) != 0);
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
	CHECK(strstr(buffer, "GTA4L-EMMC P5 READDETAIL: state=0 lba=0 "
	    "blocks=0 bytes=0 status=0 actual=0 desc_len=0 "
	    "desc_dir=0") != 0);
	report.partmap_read_state = 2u;
	report.partmap_read_lba = 0xffffffffffffffffull;
	report.partmap_read_blocks = 0xffffffffu;
	report.partmap_read_bytes = 0xffffffffu;
	report.partmap_read_status = (int32_t)0xe00002cau;
	report.partmap_read_actual = 0xffffffffffffffffull;
	report.partmap_read_desc_len = 0xffffffffu;
	report.partmap_read_desc_dir = 0xffffffffu;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 READDETAIL: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_PARTMAP_DETAIL_LINE_BYTES);
	CHECK(strstr(line, "state=2 lba=18446744073709551615 "
	    "blocks=4294967295 bytes=4294967295 status=3758097098 "
	    "actual=18446744073709551615 desc_len=4294967295 "
	    "desc_dir=4294967295") != 0);

	memset(&report, 0, sizeof(report));
	report.partread = 1;
	report.partread_cases = 14;
	report.partread_passed = 14;
	report.partread_complete = 1;
	report.partread_reads = 14;
	report.partread_blocks = 334;
	report.partread_bytes = 171008;
	report.partread_capacity = 61071360;
	report.partread_last_index = 13;
	report.partread_last_target = 1;
	report.partread_last_blocks = 64;
	report.partread_last_offset = 1048576u;
	report.partread_last_ok = 1;
	report.partread_last_actual = 32768;
	memset(report.partread_last_digest, 0xab,
	    sizeof(report.partread_last_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 PARTREAD: cases=14 passed=14 "
	    "failed=0 reason=0 complete=1 reads=14 blocks=334 bytes=171008 "
	    "fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 READTEST: index=13 target=1 "
	    "blocks=64 off=1048576 ok=1 status=0 actual=32768 "
	    "digest=abababababababababababababababab") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 READFAIL: index=0 reason=0 "
	    "status=0 actual=0 "
	    "digest=00000000000000000000000000000000") != 0);
	report.partread = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "PARTREAD") == 0);
	CHECK(strstr(buffer, "READTEST") == 0);
	CHECK(strstr(buffer, "READFAIL") == 0);

	memset(&report, 0, sizeof(report));
	report.deblock = 1;
	report.deblock_cases = 11;
	report.deblock_passed = 11;
	report.deblock_complete = 1;
	report.deblock_reads = 11;
	report.deblock_bytes = 54364;
	report.deblock_span = 56832;
	report.deblock_blocks = 111;
	report.deblock_segments = 3;
	report.deblock_capacity = 61071360;
	report.deblock_last_index = 10;
	report.deblock_last_target = 1;
	report.deblock_last_start = 4096;
	report.deblock_last_length = 1024;
	report.deblock_last_span = 1024;
	report.deblock_last_actual = 1024;
	memset(report.deblock_last_digest, 0xab,
	    sizeof(report.deblock_last_digest));
	memset(report.deblock_fail_digest, 0xcd,
	    sizeof(report.deblock_fail_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 "
	    "failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832 "
	    "blocks=111 fails=0 cancel=0 budget=0 media=0 part=0 seg=3 "
	    "cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 DEBLOCKREAD: index=10 "
	    "target=1 start=4096 length=1024 span=1024 actual=1024 "
	    "status=0 digest=abababababababababababababababab") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 DEBLOCK: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_DEBLOCK_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 DEBLOCKREAD: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_DEBLOCK_DETAIL_LINE_BYTES);
	report.deblock = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "DEBLOCK") == 0);

	memset(&report, 0, sizeof(report));
	report.breakup = 1;
	report.breakup_cases = 4;
	report.breakup_passed = 4;
	report.breakup_complete = 1;
	report.breakup_reads = 4;
	report.breakup_bytes = 199680;
	report.breakup_span = 200192;
	report.breakup_blocks = 391;
	report.breakup_subrequests = 49;
	report.breakup_max = 8;
	report.breakup_limit = 8;
	report.breakup_segments = 3;
	report.breakup_capacity = 61071360;
	report.breakup_last_index = 3;
	report.breakup_last_target = 1;
	report.breakup_last_start = 4196;
	report.breakup_last_length = 65024;
	report.breakup_last_span = 65536;
	report.breakup_last_actual = 65024;
	report.breakup_last_subrequests = 16;
	report.breakup_last_blocks = 128;
	report.breakup_last_max = 8;
	memset(report.breakup_last_digest, 0xab,
	    sizeof(report.breakup_last_digest));
	memset(report.breakup_fail_digest, 0xcd,
	    sizeof(report.breakup_fail_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 "
	    "failed=0 reason=0 complete=1 reads=4 bytes=199680 "
	    "span=200192 blocks=391 subrequests=49 max=8 foreign=0 "
	    "fails=0 cancel=0 budget=0 media=0 part=0 limit=8 seg=3 "
	    "cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 BREAKUPREAD: index=3 "
	    "target=1 start=4196 length=65024 span=65536 actual=65024 "
	    "status=0 subrequests=16 blocks=128 max=8 "
	    "digest=abababababababababababababababab") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 BREAKUP: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_BREAKUP_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 BREAKUPREAD: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_BREAKUP_DETAIL_LINE_BYTES);
	report.breakup = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "BREAKUP") == 0);
	report.breakup = 1;
	CHECK(gta4l_emmc_block_report_format(small, sizeof(small),
	    &report) == 0);
	CHECK(small[0] == 0);

	memset(&report, 0, sizeof(report));
	report.sweep = 1;
	report.sweep_cases = 6;
	report.sweep_passed = 6;
	report.sweep_complete = 1;
	report.sweep_reads = 6;
	report.sweep_bytes = 53248;
	report.sweep_span = 53248;
	report.sweep_blocks = 104;
	report.sweep_subrequests = 13;
	report.sweep_max = 8;
	report.sweep_foreign = 0;
	report.sweep_fails = 0;
	report.sweep_cancel = 0;
	report.sweep_budget = 0;
	report.sweep_media = 0;
	report.sweep_part = 0;
	report.sweep_refs = 6;
	report.sweep_refbytes = 53248;
	report.sweep_anchor = 1;
	report.sweep_mismatches = 0;
	report.sweep_limit = 8;
	report.sweep_segments = 1;
	report.sweep_capacity = 61071360;
	report.sweep_last_index = 5;
	report.sweep_last_target = 1;
	report.sweep_last_start = 4673536;
	report.sweep_last_length = 4096;
	report.sweep_last_span = 4096;
	report.sweep_last_actual = 4096;
	report.sweep_last_subrequests = 1;
	report.sweep_last_blocks = 8;
	report.sweep_last_max = 8;
	report.sweep_last_ref = 1;
	report.sweep_last_refbytes = 4096;
	memset(report.sweep_last_digest, 0xab,
	    sizeof(report.sweep_last_digest));
	memset(report.sweep_last_refdigest, 0xab,
	    sizeof(report.sweep_last_refdigest));
	memset(report.sweep_fail_digest, 0xcd,
	    sizeof(report.sweep_fail_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 SWEEP: cases=6 passed=6 "
	    "failed=0 reason=0 complete=1 reads=6 bytes=53248 "
	    "span=53248 blocks=104 subrequests=13 max=8 foreign=0 "
	    "fails=0 cancel=0 budget=0 media=0 part=0 refs=6 "
	    "refbytes=53248 anchor=1 mismatches=0 limit=8 seg=1 "
	    "cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 SWEEPREAD: index=5 "
	    "target=1 start=4673536 length=4096 span=4096 actual=4096 "
	    "status=0 subrequests=1 blocks=8 max=8 ref=1 refbytes=4096 "
	    "refstatus=0 mvalid=0 mismatch=0 "
	    "digest=abababababababababababababababab "
	    "refdigest=abababababababababababababababab") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 SWEEP: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_SWEEP_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 SWEEPREAD: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_SWEEPREAD_LINE_BYTES);
	report.sweep = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "SWEEP") == 0);
	report.sweep = 1;
	CHECK(gta4l_emmc_block_report_format(small, sizeof(small),
	    &report) == 0);
	CHECK(small[0] == 0);

	memset(&report, 0, sizeof(report));
	report.consume = 1;
	report.consume_cases = 1;
	report.consume_passed = 1;
	report.consume_failed = 0;
	report.consume_reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE;
	report.consume_complete = 1;
	report.consume_reads = 1;
	report.consume_bytes = 1024;
	report.consume_span = 1024;
	report.consume_blocks = 2;
	report.consume_subrequests = 1;
	report.consume_max = 2;
	report.consume_foreign = 0;
	report.consume_fails = 0;
	report.consume_cancel = 0;
	report.consume_budget = 0;
	report.consume_media = 0;
	report.consume_part = 0;
	report.consume_open = 1;
	report.consume_close = 1;
	report.consume_alloc = 0;
	report.consume_client = 1;
	report.consume_mismatch = 0;
	report.consume_limit = GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS;
	report.consume_segments = GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS;
	report.consume_capacity = 61071360;
	report.consume_last_index = 0;
	report.consume_last_target = GTA4L_EMMC_BLOCK_CONSUME_TARGET_BOOT;
	report.consume_last_start = 0;
	report.consume_last_length =
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_last_span = GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_last_actual = GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_last_subrequests = 1;
	report.consume_last_blocks = GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
	report.consume_last_max = GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
	memset(report.consume_last_digest, 0xab,
	    sizeof(report.consume_last_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 CONSUME: cases=1 passed=1 "
	    "failed=0 reason=0 complete=1 reads=1 bytes=1024 span=1024 "
	    "blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 "
	    "budget=0 media=0 part=0 open=1 close=1 alloc=0 client=1 "
	    "mismatches=0 limit=64 seg=1 cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 CONSUMEREAD: index=0 target=1 "
	    "start=0 length=1024 span=1024 actual=1024 status=0 "
	    "subrequests=1 blocks=2 max=2 "
	    "digest=abababababababababababababababab "
	    "expected=00000000000000000000000000000000 mismatch=0") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 CONSUME: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_CONSUME_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 CONSUMEREAD: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_CONSUMEREAD_LINE_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 QUAL") == 0);
	report.consume_failed = 1;
	report.consume_passed = 0;
	report.consume_complete = 0;
	report.consume_reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_READ;
	report.consume_fail_reason =
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_READ;
	report.consume_fail_index = 0;
	report.consume_fail_target = GTA4L_EMMC_BLOCK_CONSUME_TARGET_BOOT;
	report.consume_fail_length =
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_fail_span = GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_fail_requested =
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
	memset(report.consume_fail_digest, 0xcd,
	    sizeof(report.consume_fail_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 CONSUMEFAIL: reason=6 "
	    "case=0 target=1 start=0 length=1024 span=1024 "
	    "requested=2 completed=0 actual=0 status=0 "
	    "digest=cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd "
	    "expected=00000000000000000000000000000000 mismatch=0") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 CONSUMEFAIL: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_CONSUMEFAIL_LINE_BYTES);
	report.consume = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "CONSUME") == 0);

	memset(&report, 0, sizeof(report));
	report.width = 1;
	report.width_cases = 1;
	report.width_passed = 1;
	report.width_failed = 0;
	report.width_reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
	report.width_complete = 1;
	report.width_switch = GTA4L_EMMC_BLOCK_WIDTH_BITS_4;
	report.width_verified = 1;
	report.width_hb = 0;
	report.width_ha = GTA4L_EMMC_HOST_CONTROL_4BIT;
	report.width_rb = 1;
	report.width_cmd6 = 1;
	report.width_revert = 1;
	report.width_rverified = 1;
	report.width_rhb = GTA4L_EMMC_HOST_CONTROL_4BIT;
	report.width_rha = 0;
	report.width_rcmd6 = 2;
	report.width_reads = 2;
	report.width_bytes = 2048;
	report.width_blocks = 4;
	report.width_subrequests = 2;
	report.width_max = 2;
	report.width_match = 1;
	report.width_mismatch = 0;
	report.width_fails = 0;
	report.width_cancel = 0;
	report.width_budget = 0;
	report.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	report.width_segments = 1;
	report.width_capacity = 61071360;
	report.width_last_index = 0;
	report.width_last_lba = GTA4L_EMMC_BLOCK_WIDTH_LBA;
	report.width_last_start = 0;
	report.width_last_length = GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES;
	report.width_last_span = GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES;
	report.width_last_actual = GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES;
	report.width_last_status = 0;
	report.width_last_subrequests = 1;
	report.width_last_blocks = GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS;
	report.width_last_max = GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS;
	memset(report.width_last_digest, 0xab,
	    sizeof(report.width_last_digest));
	memset(report.width_last_predigest, 0xcd,
	    sizeof(report.width_last_predigest));
	report.width_last_match = 1;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTH: cases=1 passed=1 "
	    "failed=0 reason=0 complete=1 switch=4 verified=1 hb=0 ha=2 "
	    "rb=1 cmd6=1 revert=1 rverified=1 rhb=2 rha=0 rcmd6=2 "
	    "reads=2 bytes=2048 blocks=4 subrequests=2 max=2 match=1 "
	    "mismatches=0 fails=0 cancel=0 budget=0 limit=64 seg=1 "
	    "cap=61071360") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHREAD: index=0 "
	    "lba=2255360 start=0 length=1024 span=1024 actual=1024 "
	    "status=0 subrequests=1 blocks=2 max=2 "
	    "digest=abababababababababababababababab "
	    "predigest=cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd match=1") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 WIDTH: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_WIDTH_LINE_BYTES);
	line = strstr(buffer, "GTA4L-EMMC P5 WIDTHREAD: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_WIDTHREAD_LINE_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 READ:") == 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 QUAL") == 0);
	report.width_passed = 0;
	report.width_failed = 1;
	report.width_complete = 0;
	report.width_reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_READ;
	report.width_fail_stage = GTA4L_EMMC_BLOCK_WIDTH_REASON_READ;
	report.width_fail_status = -5;
	report.width_fail_actual = 0;
	memset(report.width_fail_digest, 0,
	    sizeof(report.width_fail_digest));
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHFAIL: stage=4 "
	    "status=4294967291 actual=0 "
	    "digest=00000000000000000000000000000000") != 0);
	line = strstr(buffer, "GTA4L-EMMC P5 WIDTHFAIL: ");
	CHECK(line != 0);
	end = strchr(line, '\n');
	CHECK(end != 0);
	CHECK((uint32_t)(end - line) <=
	    GTA4L_EMMC_BLOCK_WIDTHFAIL_LINE_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHREAD:") == 0);
	report.width = 0;
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "WIDTH") == 0);
	return 0;
}

static uint32_t
report_line_bytes(const char *buffer, const char *label)
{
	const char *line = strstr(buffer, label);
	const char *end;

	if (line == 0) {
		return 0;
	}
	end = strchr(line, '\n');
	return (uint32_t)((end != 0) ? (end - line) : strlen(line));
}

static void
widthhold_report_realistic(struct gta4l_emmc_block_report *report,
    uint32_t failed)
{
	memset(report, 0, sizeof(*report));
	report->widthhold = 1;
	report->widthhold_cases = GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES;
	report->widthhold_passed =
	    (failed == 0u) ? GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES : 0u;
	report->widthhold_failed = failed;
	report->widthhold_reason = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE :
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PART;
	report->widthhold_complete = (failed == 0u) ? 1u : 0u;
	report->widthhold_switch = GTA4L_EMMC_BLOCK_WIDTHHOLD_BITS_4;
	report->widthhold_verified = 1;
	report->widthhold_hb = 0;
	report->widthhold_ha = GTA4L_EMMC_HOST_CONTROL_4BIT;
	report->widthhold_rb = 0;
	report->widthhold_cmd6 = 1;
	report->widthhold_refs = 1;
	report->widthhold_refbytes =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES;
	report->widthhold_refblocks =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS;
	report->widthhold_refchunks = 4;
	report->widthhold_refstops = 4;
	memset(report->widthhold_refdigest, 0xa5,
	    sizeof(report->widthhold_refdigest));
	report->widthhold_reads = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES : 0u;
	report->widthhold_bytes = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES : 0u;
	report->widthhold_blocks = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS : 0u;
	report->widthhold_subrequests = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES : 0u;
	report->widthhold_max = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS : 0u;
	report->widthhold_c18 = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES : 0u;
	report->widthhold_stops = (failed == 0u) ?
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES : 0u;
	report->widthhold_match = (failed == 0u) ? 1u : 0u;
	memset(report->widthhold_obsdigest, 0xa5,
	    sizeof(report->widthhold_obsdigest));
	report->widthhold_partbase =
	    (failed == 0u) ? GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES : 0u;
	report->widthhold_partsize =
	    (failed == 0u) ? GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES : 0u;
	report->widthhold_partro = (failed == 0u) ? 1u : 0u;
	report->widthhold_partdiag = (failed == 0u) ? 0u :
	    (GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND |
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_DUPLICATE |
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_SIZE |
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_BASE |
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE |
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT);
	report->widthhold_fail_index =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES - 1u;
	report->widthhold_revert = 1;
	report->widthhold_rverified = 1;
	report->widthhold_rhb = GTA4L_EMMC_HOST_CONTROL_4BIT;
	report->widthhold_rcmd6 = 1;
	report->widthhold_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	report->widthhold_segments = 1;
	report->widthhold_capacity = 61071360;
	report->widthhold_last_index = GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES - 1u;
	report->widthhold_last_lba = (uint32_t)GTA4L_EMMC_BLOCK_WIDTHHOLD_LBA +
	    (GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES - 1u) *
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS;
	report->widthhold_last_start = (uint32_t)
	    ((GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES - 1u) *
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES);
	report->widthhold_last_length =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES;
	report->widthhold_last_span =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES;
	report->widthhold_last_actual =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES;
	report->widthhold_last_subrequests = 1;
	report->widthhold_last_blocks =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS;
	report->widthhold_last_max =
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS;
	report->widthhold_last_match = 1;
	memset(report->widthhold_last_digest, 0x5a,
	    sizeof(report->widthhold_last_digest));
	memset(report->widthhold_last_predigest, 0x5a,
	    sizeof(report->widthhold_last_predigest));
}

static void
widthhold_report_worst(struct gta4l_emmc_block_report *report,
    uint32_t failed)
{
	memset(report, 0, sizeof(*report));
	report->widthhold = 0xffffffffu;
	report->widthhold_cases = 0xffffffffu;
	report->widthhold_passed = 0xffffffffu;
	report->widthhold_failed = failed;
	report->widthhold_reason = 0xffffffffu;
	report->widthhold_complete = 0xffffffffu;
	report->widthhold_switch = 0xffffffffu;
	report->widthhold_verified = 0xffffffffu;
	report->widthhold_hb = 0xffffffffu;
	report->widthhold_ha = 0xffffffffu;
	report->widthhold_rb = 0xffffffffu;
	report->widthhold_cmd6 = 0xffffffffu;
	report->widthhold_refs = 0xffffffffu;
	report->widthhold_refbytes = 0xffffffffu;
	report->widthhold_refblocks = 0xffffffffu;
	report->widthhold_refstatus = -1;
	report->widthhold_refchunks = 0xffffffffu;
	report->widthhold_refstops = 0xffffffffu;
	report->widthhold_refstopfail = 0xffffffffu;
	memset(report->widthhold_refdigest, 0xff,
	    sizeof(report->widthhold_refdigest));
	report->widthhold_reads = 0xffffffffu;
	report->widthhold_bytes = 0xffffffffu;
	report->widthhold_blocks = 0xffffffffu;
	report->widthhold_subrequests = 0xffffffffu;
	report->widthhold_max = 0xffffffffu;
	report->widthhold_c18 = 0xffffffffu;
	report->widthhold_stops = 0xffffffffu;
	report->widthhold_stopfail = 0xffffffffu;
	report->widthhold_match = 0xffffffffu;
	report->widthhold_mismatch = 0xffffffffu;
	report->widthhold_fails = 0xffffffffu;
	report->widthhold_foreign = 0xffffffffu;
	report->widthhold_cancel = 0xffffffffu;
	report->widthhold_budget = 0xffffffffu;
	memset(report->widthhold_obsdigest, 0xff,
	    sizeof(report->widthhold_obsdigest));
	report->widthhold_partbase = 0xffffffffu;
	report->widthhold_partsize = 0xffffffffu;
	report->widthhold_partro = 0xffffffffu;
	report->widthhold_partdiag = 0xffffffffu;
	report->widthhold_revert = 0xffffffffu;
	report->widthhold_rverified = 0xffffffffu;
	report->widthhold_rhb = 0xffffffffu;
	report->widthhold_rha = 0xffffffffu;
	report->widthhold_rcmd6 = 0xffffffffu;
	report->widthhold_rstatus = -1;
	report->widthhold_limit = 0xffffffffu;
	report->widthhold_segments = 0xffffffffu;
	report->widthhold_capacity = 0xffffffffu;
	report->widthhold_last_index = 0xffffffffu;
	report->widthhold_last_lba = 0xffffffffu;
	report->widthhold_last_start = 0xffffffffu;
	report->widthhold_last_length = 0xffffffffu;
	report->widthhold_last_span = 0xffffffffu;
	report->widthhold_last_actual = 0xffffffffu;
	report->widthhold_last_status = -1;
	report->widthhold_last_subrequests = 0xffffffffu;
	report->widthhold_last_blocks = 0xffffffffu;
	report->widthhold_last_max = 0xffffffffu;
	report->widthhold_last_match = 0xffffffffu;
	memset(report->widthhold_last_digest, 0xff,
	    sizeof(report->widthhold_last_digest));
	memset(report->widthhold_last_predigest, 0xff,
	    sizeof(report->widthhold_last_predigest));
}

static int
scenario_widthhold_report(void)
{
	char buffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	struct gta4l_emmc_block_report report;
	uint32_t length;

	widthhold_report_realistic(&report, 0u);
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=4 "
	    "failed=0 reason=0 complete=1") != 0);
	CHECK(strstr(buffer, "partro=1 partdiag=0 revert=1 rverified=1") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHHOLDREAD: index=3 "
	    "lba=2255366 start=3072 length=1024 span=1024 actual=1024 "
	    "status=0 subrequests=1 blocks=2 max=2 "
	    "digest=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a "
	    "predigest=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a match=1") != 0);
	CHECK(strstr(buffer, "WIDTHHOLDFAIL") == 0);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLD: ") <=
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLDREAD: ") <=
	    GTA4L_EMMC_BLOCK_WIDTHHOLDREAD_LINE_BYTES);

	widthhold_report_realistic(&report, 1u);
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(strstr(buffer, "reason=5 complete=0") != 0);
	CHECK(strstr(buffer, "partdiag=63 revert=") != 0);
	CHECK(strstr(buffer, "partbase=0 partsize=0 partro=0") != 0);
	CHECK(strstr(buffer, "GTA4L-EMMC P5 WIDTHHOLDFAIL: index=3") != 0);
	CHECK(strstr(buffer, "WIDTHHOLDREAD") == 0);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLDFAIL: ") <=
	    GTA4L_EMMC_BLOCK_WIDTHHOLDFAIL_LINE_BYTES);

	widthhold_report_worst(&report, 0u);
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	/* Success shape: failed=0 is one character shorter than the cap. */
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLD: ") == 913u);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLDREAD: ") ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLDREAD_LINE_BYTES);

	widthhold_report_worst(&report, 0xffffffffu);
	length = gta4l_emmc_block_report_format(buffer, sizeof(buffer),
	    &report);
	CHECK(length > 0);
	CHECK(length < GTA4L_EMMC_BLOCK_REPORT_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLD: ") ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES);
	CHECK(report_line_bytes(buffer, "GTA4L-EMMC P5 WIDTHHOLDFAIL: ") ==
	    GTA4L_EMMC_BLOCK_WIDTHHOLDFAIL_LINE_BYTES);
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
	{ "chunked-three", scenario_chunked_three },
	{ "chunked-max", scenario_chunked_max },
	{ "chunked-21", scenario_chunked_21 },
	{ "chunked-failure", scenario_chunked_failure },
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
	{ "partcmp-descriptor-failure", scenario_partcmp_descriptor_failure },
	{ "partcmp-gpt-invalid", scenario_partcmp_gpt_invalid },
	{ "partcmp-entry-variants", scenario_partcmp_entry_variants },
	{ "partcmp-geometry", scenario_partcmp_geometry },
	{ "partcmp-media-invalid", scenario_partcmp_media_invalid },
	{ "partcmp-read-failure", scenario_partcmp_read_failure },
	{ "partcmp-read-short", scenario_partcmp_read_short },
	{ "partcmp-read-inflight", scenario_partcmp_read_inflight },
	{ "partcmp-page-aligned", scenario_partcmp_page_aligned },
	{ "partcmp-page-split", scenario_partcmp_page_split },
	{ "partcmp-budget", scenario_partcmp_budget },
	{ "partcmp-cancel", scenario_partcmp_cancel },
	{ "partcmp-watchdog", scenario_partcmp_watchdog },
	{ "partread-refused-orphan", scenario_partread_refused_orphan },
	{ "partread-refused-value", scenario_partread_refused_value },
	{ "partread-refused-duplicate", scenario_partread_refused_duplicate },
	{ "partread-refused-partobs", scenario_partread_refused_partobs },
	{ "partread-refused-partcmp", scenario_partread_refused_partcmp },
	{ "partread-match", scenario_partread_match },
	{ "partread-hash-mismatch", scenario_partread_hash_mismatch },
	{ "partread-read-failure", scenario_partread_read_failure },
	{ "partread-short-read", scenario_partread_short_read },
	{ "partread-media-absent", scenario_partread_media_absent },
	{ "partread-part-absent", scenario_partread_part_absent },
	{ "partread-open-refused", scenario_partread_open_refused },
	{ "partread-descriptor-failure", scenario_partread_descriptor_failure },
	{ "partread-budget", scenario_partread_budget },
	{ "partread-cancel", scenario_partread_cancel },
	{ "deblock-refused-orphan", scenario_deblock_refused_orphan },
	{ "deblock-refused-value", scenario_deblock_refused_value },
	{ "deblock-refused-duplicate", scenario_deblock_refused_duplicate },
	{ "deblock-refused-partobs", scenario_deblock_refused_partobs },
	{ "deblock-refused-partcmp", scenario_deblock_refused_partcmp },
	{ "deblock-refused-partread", scenario_deblock_refused_partread },
	{ "deblock-segment-property", scenario_deblock_segment_property },
	{ "deblock-match", scenario_deblock_match },
	{ "deblock-range-invalid", scenario_deblock_range_invalid },
	{ "deblock-seg1", scenario_deblock_seg1 },
	{ "deblock-unaligned-bounce", scenario_deblock_unaligned_bounce },
	{ "deblock-hash-mismatch", scenario_deblock_hash_mismatch },
	{ "deblock-read-failure", scenario_deblock_read_failure },
	{ "deblock-read-nomedia", scenario_deblock_read_nomedia },
	{ "deblock-short-read", scenario_deblock_short_read },
	{ "deblock-short-zero", scenario_deblock_short_zero },
	{ "deblock-short-over", scenario_deblock_short_over },
	{ "deblock-media-absent", scenario_deblock_media_absent },
	{ "deblock-two-whole", scenario_deblock_two_whole },
	{ "deblock-wrong-profile", scenario_deblock_wrong_profile },
	{ "deblock-whole-no-base", scenario_deblock_whole_no_base },
	{ "deblock-whole-base-invalid", scenario_deblock_whole_base_invalid },
	{ "deblock-part-absent", scenario_deblock_part_absent },
	{ "deblock-part-ambiguous", scenario_deblock_part_ambiguous },
	{ "deblock-open-refused", scenario_deblock_open_refused },
	{ "deblock-descriptor-failure",
	    scenario_deblock_descriptor_failure },
	{ "deblock-budget", scenario_deblock_budget },
	{ "deblock-cancel", scenario_deblock_cancel },
	{ "deblock-watchdog", scenario_deblock_watchdog },
	{ "breakup-refused-orphan", scenario_breakup_refused_orphan },
	{ "breakup-refused-value", scenario_breakup_refused_value },
	{ "breakup-refused-duplicate", scenario_breakup_refused_duplicate },
	{ "breakup-refused-partobs", scenario_breakup_refused_partobs },
	{ "breakup-refused-partcmp", scenario_breakup_refused_partcmp },
	{ "breakup-refused-partread", scenario_breakup_refused_partread },
	{ "breakup-refused-deblock", scenario_breakup_refused_deblock },
	{ "breakup-properties", scenario_breakup_properties },
	{ "breakup-pinned", scenario_breakup_pinned },
	{ "breakup-match", scenario_breakup_match },
	{ "breakup-block3", scenario_breakup_block3 },
	{ "breakup-production-limit", scenario_breakup_production_limit },
	{ "breakup-zero-length", scenario_breakup_zero_length },
	{ "breakup-range-invalid", scenario_breakup_range_invalid },
	{ "breakup-seg1", scenario_breakup_seg1 },
	{ "breakup-unaligned-bounce", scenario_breakup_unaligned_bounce },
	{ "breakup-hash-mismatch", scenario_breakup_hash_mismatch },
	{ "breakup-read-failure", scenario_breakup_read_failure },
	{ "breakup-read-nomedia", scenario_breakup_read_nomedia },
	{ "breakup-short-read", scenario_breakup_short },
	{ "breakup-media-absent", scenario_breakup_media },
	{ "breakup-part-absent", scenario_breakup_part },
	{ "breakup-open-refused", scenario_breakup_open },
	{ "breakup-descriptor-failure",
	    scenario_breakup_descriptor_failure },
	{ "breakup-budget", scenario_breakup_budget },
	{ "breakup-cancel", scenario_breakup_cancel },
	{ "breakup-watchdog", scenario_breakup_watchdog },
	{ "breakup-split-missing", scenario_breakup_split_missing },
	{ "breakup-split-extra", scenario_breakup_split_extra },
	{ "breakup-split-reorder", scenario_breakup_split_reorder },
	{ "breakup-split-overlap", scenario_breakup_split_overlap },
	{ "breakup-split-oversize", scenario_breakup_split_oversize },
	{ "breakup-split-single", scenario_breakup_split_single },
	{ "breakup-order-inspan", scenario_breakup_order_inspan },
	{ "breakup-foreign", scenario_breakup_foreign },
	{ "sweep-refused-orphan", scenario_sweep_refused_orphan },
	{ "sweep-refused-value", scenario_sweep_refused_value },
	{ "sweep-refused-duplicate", scenario_sweep_refused_duplicate },
	{ "sweep-refused-partobs", scenario_sweep_refused_partobs },
	{ "sweep-refused-partcmp", scenario_sweep_refused_partcmp },
	{ "sweep-refused-partread", scenario_sweep_refused_partread },
	{ "sweep-refused-deblock", scenario_sweep_refused_deblock },
	{ "sweep-refused-breakup", scenario_sweep_refused_breakup },
	{ "sweep-properties", scenario_sweep_properties },
	{ "sweep-pinned", scenario_sweep_pinned },
	{ "sweep-match", scenario_sweep_match },
	{ "sweep-block3", scenario_sweep_block3 },
	{ "sweep-production-limit", scenario_sweep_production_limit },
	{ "sweep-zero-length", scenario_sweep_zero_length },
	{ "sweep-range-invalid", scenario_sweep_range_invalid },
	{ "sweep-wrong-target", scenario_sweep_wrong_target },
	{ "sweep-unaligned-start", scenario_sweep_unaligned_start },
	{ "sweep-length-invalid", scenario_sweep_length_invalid },
	{ "sweep-hash-mismatch", scenario_sweep_hash_mismatch },
	{ "sweep-read-failure", scenario_sweep_read_failure },
	{ "sweep-short-read", scenario_sweep_short_read },
	{ "sweep-short-reference", scenario_sweep_short_reference },
	{ "sweep-reference-failure", scenario_sweep_reference_failure },
	{ "sweep-reference-wrong-lba", scenario_sweep_reference_wrong_lba },
	{ "sweep-reference-shifted", scenario_sweep_reference_shifted },
	{ "sweep-reference-stale", scenario_sweep_reference_stale },
	{ "sweep-mismatch", scenario_sweep_mismatch },
	{ "sweep-split-missing", scenario_sweep_split_missing },
	{ "sweep-split-overlap", scenario_sweep_split_overlap },
	{ "sweep-split-oversize", scenario_sweep_split_oversize },
	{ "sweep-order", scenario_sweep_order },
	{ "sweep-foreign", scenario_sweep_foreign },
	{ "sweep-descriptor-failure", scenario_sweep_descriptor_failure },
	{ "sweep-budget", scenario_sweep_budget },
	{ "sweep-cancel", scenario_sweep_cancel },
	{ "sweep-watchdog", scenario_sweep_watchdog },
	{ "sweep-media-absent", scenario_sweep_media_absent },
	{ "sweep-part-absent", scenario_sweep_part_absent },
	{ "sweep-open-refused", scenario_sweep_open_refused },
	{ "sweep-two-whole", scenario_sweep_two_whole },
	{ "consume-refused-orphan", scenario_consume_refused_orphan },
	{ "consume-refused-block1", scenario_consume_refused_block1 },
	{ "consume-refused-value", scenario_consume_refused_value },
	{ "consume-refused-duplicate", scenario_consume_refused_duplicate },
	{ "consume-refused-partobs", scenario_consume_refused_partobs },
	{ "consume-refused-partcmp", scenario_consume_refused_partcmp },
	{ "consume-refused-partread", scenario_consume_refused_partread },
	{ "consume-refused-deblock", scenario_consume_refused_deblock },
	{ "consume-refused-breakup", scenario_consume_refused_breakup },
	{ "consume-refused-sweep", scenario_consume_refused_sweep },
	{ "consume-match", scenario_consume_match },
	{ "consume-media-absent", scenario_consume_media_absent },
	{ "consume-two-whole", scenario_consume_two_whole },
	{ "consume-part-absent", scenario_consume_part_absent },
	{ "consume-part-duplicate", scenario_consume_part_duplicate },
	{ "consume-part-wrong-base", scenario_consume_part_wrong_base },
	{ "consume-part-wrong-size", scenario_consume_part_wrong_size },
	{ "consume-part-writable", scenario_consume_part_writable },
	{ "consume-open-refused", scenario_consume_open_refused },
	{ "consume-client-failure", scenario_consume_client_failure },
	{ "consume-alloc-failure", scenario_consume_alloc_failure },
	{ "consume-descriptor-failure", scenario_consume_descriptor_failure },
	{ "consume-read-failure", scenario_consume_read_failure },
	{ "consume-short-read", scenario_consume_short_read },
	{ "consume-digest-mismatch", scenario_consume_digest_mismatch },
	{ "consume-foreign", scenario_consume_foreign },
	{ "consume-order", scenario_consume_order },
	{ "consume-budget", scenario_consume_budget },
	{ "consume-cancel", scenario_consume_cancel },
	{ "consume-watchdog", scenario_consume_watchdog },
	{ "width-absent", scenario_width_absent },
	{ "width-refused-orphan", scenario_width_refused_orphan },
	{ "width-refused-block1", scenario_width_refused_block1 },
	{ "width-refused-value", scenario_width_refused_value },
	{ "width-refused-duplicate", scenario_width_refused_duplicate },
	{ "width-refused-partobs", scenario_width_refused_partobs },
	{ "width-refused-partcmp", scenario_width_refused_partcmp },
	{ "width-refused-partread", scenario_width_refused_partread },
	{ "width-refused-deblock", scenario_width_refused_deblock },
	{ "width-refused-breakup", scenario_width_refused_breakup },
	{ "width-refused-sweep", scenario_width_refused_sweep },
	{ "width-refused-consume", scenario_width_refused_consume },
	{ "width-match", scenario_width_match },
	{ "width-baseline-failure", scenario_width_baseline_failure },
	{ "width-preread-failure", scenario_width_preread_failure },
	{ "width-switch-failure", scenario_width_switch_failure },
	{ "width-switch-unverified", scenario_width_switch_unverified },
	{ "width-switch-host", scenario_width_switch_host },
	{ "width-switch-restore", scenario_width_switch_restore },
	{ "width-baseline-read-failure",
	  scenario_width_baseline_read_failure },
	{ "width-read-failure", scenario_width_read_failure },
	{ "width-short-read", scenario_width_short_read },
	{ "width-stop-failure", scenario_width_stop_failure },
	{ "width-match-mismatch", scenario_width_match_mismatch },
	{ "width-host-digest-mismatch", scenario_width_host_digest_mismatch },
	{ "width-revert-failure", scenario_width_revert_failure },
	{ "width-revert-unverified", scenario_width_revert_unverified },
	{ "width-budget", scenario_width_budget },
	{ "width-cancel", scenario_width_cancel },
	{ "width-gate-release", scenario_width_gate_release },
	{ "width-gate-timeout", scenario_width_gate_timeout },
	{ "width-gate-deadline-release", scenario_width_gate_deadline_release },
	{ "width-gate-deadline-miss", scenario_width_gate_deadline_miss },
	{ "width-watchdog", scenario_width_watchdog },
	{ "width-cancel-gate-wait", scenario_width_cancel_gate_wait },
	{ "width-cancel-after-read", scenario_width_cancel_after_read },
	{ "width-terminal-watchdog", scenario_width_terminal_watchdog },
	{ "widthhold-match", scenario_widthhold_match },
	{ "widthhold-report", scenario_widthhold_report },
	{ "widthhold-absent", scenario_widthhold_absent },
	{ "widthhold-refused-orphan", scenario_widthhold_refused_orphan },
	{ "widthhold-refused-block", scenario_widthhold_refused_block },
	{ "widthhold-refused-value", scenario_widthhold_refused_value },
	{ "widthhold-refused-duplicate", scenario_widthhold_refused_duplicate },
	{ "widthhold-refused-conflicts", scenario_widthhold_refused_conflicts },
	{ "widthhold-baseline-failure", scenario_widthhold_baseline_failure },
	{ "widthhold-preread-failure", scenario_widthhold_preread_failure },
	{ "widthhold-preread-short", scenario_widthhold_preread_short },
	{ "widthhold-preread-stop-missing", scenario_widthhold_preread_stop_missing },
	{ "widthhold-preread-stop-failure", scenario_widthhold_preread_stop_failure },
	{ "widthhold-switch-failure", scenario_widthhold_switch_failure },
	{ "widthhold-switch-unverified", scenario_widthhold_switch_unverified },
	{ "widthhold-switch-host", scenario_widthhold_switch_host },
	{ "widthhold-switch-restore", scenario_widthhold_switch_restore },
	{ "widthhold-base4", scenario_widthhold_base4 },
	{ "widthhold-advert", scenario_widthhold_advert },
	{ "widthhold-thread-watchdog", scenario_widthhold_thread_watchdog },
	{ "widthhold-thread-reporter", scenario_widthhold_thread_reporter },
	{ "widthhold-alloc-start", scenario_widthhold_alloc_start },
	{ "widthhold-cancel-pre-switch", scenario_widthhold_cancel_pre_switch },
	{ "widthhold-media-absent", scenario_widthhold_media_absent },
	{ "widthhold-media-writable", scenario_widthhold_media_writable },
	{ "widthhold-media-writable-missing", scenario_widthhold_media_writable_missing },
	{ "widthhold-media-writable-wrong", scenario_widthhold_media_writable_wrong },
	{ "widthhold-media-size-missing", scenario_widthhold_media_size_missing },
	{ "widthhold-media-two-whole", scenario_widthhold_media_two_whole },
	{ "widthhold-part-absent", scenario_widthhold_part_absent },
	{ "widthhold-part-duplicate", scenario_widthhold_part_duplicate },
	{ "widthhold-part-wrong-base", scenario_widthhold_part_wrong_base },
	{ "widthhold-part-wrong-size", scenario_widthhold_part_wrong_size },
	{ "widthhold-part-base-late", scenario_widthhold_part_base_late },
	{ "widthhold-part-size-late", scenario_widthhold_part_size_late },
	{ "widthhold-part-writable", scenario_widthhold_part_writable },
	{ "widthhold-part-writable-missing",
	  scenario_widthhold_part_writable_missing },
	{ "widthhold-part-writable-wrong",
	  scenario_widthhold_part_writable_wrong },
	{ "widthhold-part-content-missing",
	  scenario_widthhold_part_content_missing },
	{ "widthhold-part-content-wrong",
	  scenario_widthhold_part_content_wrong },
	{ "widthhold-part-content-name",
	  scenario_widthhold_part_content_name },
	{ "widthhold-part-content-guid",
	  scenario_widthhold_part_content_guid },
	{ "widthhold-open-refused", scenario_widthhold_open_refused },
	{ "widthhold-alloc-case", scenario_widthhold_alloc_case },
	{ "widthhold-descriptor", scenario_widthhold_descriptor },
	{ "widthhold-read-error", scenario_widthhold_read_error },
	{ "widthhold-short", scenario_widthhold_short },
	{ "widthhold-match-mismatch", scenario_widthhold_match_mismatch },
	{ "widthhold-foreign-tag", scenario_widthhold_foreign_tag },
	{ "widthhold-foreign-block", scenario_widthhold_foreign_block },
	{ "widthhold-order-extra", scenario_widthhold_order_extra },
	{ "widthhold-foreign-extra", scenario_widthhold_foreign_extra },
	{ "widthhold-budget", scenario_widthhold_budget },
	{ "widthhold-budget-boundary", scenario_widthhold_budget_boundary },
	{ "widthhold-cancel-post-switch", scenario_widthhold_cancel_post_switch },
	{ "widthhold-cancel-case", scenario_widthhold_cancel_case },
	{ "widthhold-cancel-before-revert", scenario_widthhold_cancel_before_revert },
	{ "widthhold-cancel-after-revert", scenario_widthhold_cancel_after_revert },
	{ "widthhold-cancel-first-failure", scenario_widthhold_cancel_first_failure },
	{ "widthhold-gate-timeout", scenario_widthhold_gate_timeout },
	{ "widthhold-revert-failure", scenario_widthhold_revert_failure },
	{ "widthhold-revert-unverified", scenario_widthhold_revert_unverified },
	{ "widthhold-revert-card", scenario_widthhold_revert_card },
	{ "widthhold-watchdog-before", scenario_widthhold_watchdog_before },
	{ "widthhold-watchdog-case", scenario_widthhold_watchdog_case },
	{ "widthhold-watchdog-after-revert", scenario_widthhold_watchdog_after_revert },
	{ "widthpub-match", scenario_widthpub_match },
	{ "widthpub-absent", scenario_widthpub_absent },
	{ "widthpub-media-absent", scenario_widthpub_media_absent },
	{ "widthpub-refused-orphan", scenario_widthpub_refused_orphan },
	{ "widthpub-refused-block", scenario_widthpub_refused_block },
	{ "widthpub-refused-value", scenario_widthpub_refused_value },
	{ "widthpub-refused-duplicate", scenario_widthpub_refused_duplicate },
	{ "widthpub-refused-conflicts", scenario_widthpub_refused_conflicts },
	{ "widthpub-budget", scenario_widthpub_budget },
	{ "widthpub-budget-boundary", scenario_widthpub_budget_boundary },
	{ "widthpub-read-error", scenario_widthpub_read_error },
	{ "widthpub-short", scenario_widthpub_short },
	{ "widthpub-match-mismatch", scenario_widthpub_match_mismatch },
	{ "widthpub-foreign-tag", scenario_widthpub_foreign_tag },
	{ "widthpub-cancel-case", scenario_widthpub_cancel_case },
	{ "widthpub-gate-timeout", scenario_widthpub_gate_timeout },
	{ "widthpub-revert-failure", scenario_widthpub_revert_failure },
	{ "widthpub-baseline-failure", scenario_widthpub_baseline_failure },
	{ "widthpub-switch-failure", scenario_widthpub_switch_failure },
	{ "speed-match-8bit", scenario_speed_match_8bit },
	{ "speed-match-4bit", scenario_speed_match_4bit },
	{ "speed-read-crc", scenario_speed_read_crc },
	{ "speed-read-timeout", scenario_speed_read_timeout },
	{ "speed-short", scenario_speed_short },
	{ "speed-budget", scenario_speed_budget },
	{ "speed-budget-boundary", scenario_speed_budget_boundary },
	{ "speed-cancel-before", scenario_speed_cancel_before },
	{ "speed-cancel-during", scenario_speed_cancel_during },
	{ "speed-media-absent", scenario_speed_media_absent },
	{ "speed-part-absent", scenario_speed_part_absent },
	{ "speed-part-duplicate", scenario_speed_part_duplicate },
	{ "speed-part-wrong-base", scenario_speed_part_wrong_base },
	{ "speed-part-writable", scenario_speed_part_writable },
	{ "speed-desc", scenario_speed_desc },
	{ "speed-open-fail", scenario_speed_open_fail },
	{ "speed-foreign-tag", scenario_speed_foreign_tag },
	{ "speed-order", scenario_speed_order },
	{ "speed-digest-mismatch", scenario_speed_digest_mismatch },
	{ "speed-gate-timeout", scenario_speed_gate_timeout },
	{ "speed-revert-fail", scenario_speed_revert_fail },
	{ "speed-revert-unverified", scenario_speed_revert_unverified },
	{ "speed-revert-budget", scenario_speed_revert_budget },
	{ "speed-revert-hs-stuck", scenario_speed_revert_hs_stuck },
	{ "speed-revert-clock", scenario_speed_revert_clock },
	{ "speed-revert-host-wide", scenario_speed_revert_host_wide },
	{ "speed-first-failure-latch", scenario_speed_first_failure_latch },
	{ "speed-watchdog", scenario_speed_watchdog },
	{ "speed-thread-start", scenario_speed_thread_start },
	{ "speed-alloc", scenario_speed_alloc },
	{ "speed-format-max", scenario_speed_format_max },
	{ "speed-absent", scenario_speed_absent },
	{ "speed-refused-orphan", scenario_speed_refused_orphan },
	{ "speed-refused-block", scenario_speed_refused_block },
	{ "speed-refused-value", scenario_speed_refused_value },
	{ "speed-refused-duplicate", scenario_speed_refused_duplicate },
	{ "speed-refused-conflicts", scenario_speed_refused_conflicts },
	{ "speed-start-8bit", scenario_speed_start_8bit },
	{ "speed-start-hispd-record", scenario_speed_start_hispd_record },
	{ "speed-start-fallback", scenario_speed_start_fallback },
	{ "speed-start-width-restore", scenario_speed_start_width_restore },
	{ "speed-start-fallback-fail", scenario_speed_start_fallback_fail },
	{ "speed-start-hs-fail", scenario_speed_start_hs_fail },
	{ "speed-start-hs-readback", scenario_speed_start_hs_readback },
	{ "speed-start-hs-clock", scenario_speed_start_hs_clock },
	{ "speed-start-baseline-fail", scenario_speed_start_baseline_fail },
	{ "speed-start-base-fail", scenario_speed_start_base_fail },
	{ "speed-start-advert", scenario_speed_start_advert },
	{ "speed-start-gate-timeout", scenario_speed_start_gate_timeout },
	{ "bulk-success", scenario_bulk_success },
	{ "bulk-digest", scenario_bulk_digest },
	{ "bulk-window-mismatch", scenario_bulk_window_mismatch },
	{ "bulk-read-crc", scenario_bulk_read_crc },
	{ "bulk-read-timeout", scenario_bulk_read_timeout },
	{ "bulk-short", scenario_bulk_short },
	{ "bulk-budget", scenario_bulk_budget },
	{ "bulk-budget-boundary", scenario_bulk_budget_boundary },
	{ "bulk-cancel-before", scenario_bulk_cancel_before },
	{ "bulk-cancel-during", scenario_bulk_cancel_during },
	{ "bulk-media-absent", scenario_bulk_media_absent },
	{ "bulk-part-absent", scenario_bulk_part_absent },
	{ "bulk-open-fail", scenario_bulk_open_fail },
	{ "bulk-alloc", scenario_bulk_alloc },
	{ "bulk-shape", scenario_bulk_shape },
	{ "bulk-revert-fail", scenario_bulk_revert_fail },
	{ "bulk-gate-timeout", scenario_bulk_gate_timeout },
	{ "bulk-watchdog", scenario_bulk_watchdog },
	{ "bulk-format-max", scenario_bulk_format_max },
	{ "bulk-smmu-fault", scenario_bulk_smmu_fault },
	{ "bulk-smmu-translate", scenario_bulk_smmu_translate },
	{ "bulk-smmu-cb-off", scenario_bulk_smmu_cb_off },
	{ "bulk-smmu-cb-type", scenario_bulk_smmu_cb_type },
	{ "bulk-smmu-cb-fault", scenario_bulk_smmu_cb_fault },
	{ "bulk-smmu-conflict", scenario_bulk_smmu_conflict },
	{ "bulk-smmu-layout", scenario_bulk_smmu_layout },
	{ "bulk-smmu-nomatch", scenario_bulk_smmu_nomatch },
	{ "bulk-smmu-clientpd", scenario_bulk_smmu_clientpd },
	{ "bulk-smmu-map-fail", scenario_bulk_smmu_map_fail },
	{ "dma-success", scenario_dma_success },
	{ "dma-cb-off", scenario_dma_cb_off },
	{ "dma-post-fault", scenario_dma_post_fault },
	{ "dma-smmu-skip", scenario_dma_smmu_skip },
	{ "dma-compare-mismatch", scenario_dma_compare_mismatch },
	{ "dma-adma-error", scenario_dma_adma_error },
	{ "dma-short", scenario_dma_short },
	{ "dma-canary", scenario_dma_canary },
	{ "dma-poison-untouched", scenario_dma_poison_untouched },
	{ "dma-data-timeout", scenario_dma_data_timeout },
	{ "dma-physical-high", scenario_dma_physical_high },
	{ "dma-refused-orphan", scenario_dma_refused_orphan },
	{ "dma-refused-value", scenario_dma_refused_value },
	{ "dma-refused-duplicate", scenario_dma_refused_duplicate },
	{ "bulk-absent", scenario_bulk_absent },
	{ "bulk-refused-orphan", scenario_bulk_refused_orphan },
	{ "bulk-refused-block", scenario_bulk_refused_block },
	{ "bulk-refused-value", scenario_bulk_refused_value },
	{ "bulk-refused-duplicate", scenario_bulk_refused_duplicate },
	{ "bulk-refused-conflicts", scenario_bulk_refused_conflicts },
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
