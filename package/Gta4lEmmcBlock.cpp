// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l read-only IOBlockStorageDevice adapter.
//
// The adapter is intentionally thin: the portable request helper owns
// validation, the per-block loop, the bounce buffer and the counters; the
// policy header owns boot-argument selection, geometry checks and the bounded
// report format. This file owns the IOKit lifecycle, the controller lock, the
// descriptor copy and the single report thread.

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
#include "Gta4lEmmcBlockTestSupport.h"
#else
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOGUIDPartitionScheme.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOPartitionScheme.h>
#include <IOKit/storage/IOStorage.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>
#endif

#include <stdint.h>
#include <string.h>

#include "Gta4lEmmc.h"
#include "Gta4lEmmcPlatform.h"
#include "Gta4lEmmcBlock.h"
#include "Gta4lEmmcBlockCore.h"

#ifndef GTA4L_EMMC_BLOCK_HOST_TEST
extern "C" kern_return_t kernel_thread_start(void (*)(void *, wait_result_t),
    void *, thread_t *);
extern "C" void thread_deallocate(thread_t);
extern "C" thread_t current_thread(void);
extern "C" kern_return_t thread_terminate(thread_t);
#endif

#define GTA4L_EMMC_BLOCK_CONTENT_BYTES 48u
#define GTA4L_EMMC_BLOCK_ANCESTRY_DEPTH 4u
#define GTA4L_EMMC_BLOCK_CLOCK_HZ 400000u
#define GTA4L_EMMC_BLOCK_WATCHDOG_MS 30000u

static_assert(GTA4L_EMMC_BLOCK_PARTMAP_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "PARTMAP worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS ==
    GTA4L_EMMC_BLOCK_CHUNK_BLOCKS,
    "PARTMAP chunk must match the core chunk");
static_assert(GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES >=
    GTA4L_EMMC_BLOCK_BYTES,
    "PARTMAP bounce must hold at least one block");
static_assert(GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES <=
    GTA4L_EMMC_BLOCK_PARTMAP_ARRAY_BYTES,
    "PARTMAP bounce must not exceed the GPT array");
static_assert(GTA4L_EMMC_BLOCK_PARTREAD_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "PARTREAD worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES <=
    GTA4L_EMMC_BLOCK_MAX_BYTES,
    "PARTREAD case must stay inside the 64-block request limit");
static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "DEBLOCK worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES ==
    GTA4L_EMMC_BLOCK_BYTES,
    "DEBLOCK sector must match the media block size");
static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES >=
    GTA4L_EMMC_BLOCK_MAX_BYTES,
    "DEBLOCK bounce must cover the 64-block aligned span");
static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS > 1u,
    "DEBLOCK must advertise the multi-segment path");

/* The exact comparator needs a 16 KiB GPT array plus both validated copies
 * and two media snapshots. Keeping that state in the driver object would
 * exceed the kalloc safe allocation size, so the report thread owns one
 * bounded scratch allocation instead. */
struct gta4l_emmc_block_partmap_scratch {
	uint8_t array[GTA4L_EMMC_BLOCK_PARTMAP_ARRAY_BYTES];
	struct gta4l_emmc_block_gpt gpt;
	struct gta4l_emmc_block_gpt backup;
	struct gta4l_emmc_block_partmap_media
	    media[GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA];
	struct gta4l_emmc_block_partmap_media
	    previous[GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA];
};

class Gta4lEmmcBlock : public IOBlockStorageDevice {
	OSDeclareDefaultStructors(Gta4lEmmcBlock)

public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	void free() override;

	IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block,
	    UInt64 nblks, IOStorageAttributes *attributes,
	    IOStorageCompletion *completion) override;
	IOReturn doEjectMedia() override;
	IOReturn doFormatMedia(UInt64 byteCapacity) override;
	UInt32 doGetFormatCapacities(UInt64 *capacities,
	    UInt32 capacitiesMaxCount) const override;
	char *getVendorString() override;
	char *getProductString() override;
	char *getRevisionString() override;
	char *getAdditionalDeviceInfoString() override;
	IOReturn reportBlockSize(UInt64 *blockSize) override;
	IOReturn reportEjectability(bool *isEjectable) override;
	IOReturn reportMaxValidBlock(UInt64 *maxBlock) override;
	IOReturn reportMediaState(bool *mediaPresent,
	    bool *changedState) override;
	IOReturn reportRemovability(bool *isRemovable) override;
	IOReturn reportWriteProtection(bool *isWriteProtected) override;
	IOReturn doSynchronizeCache() override;
	IOReturn getWriteCacheState(bool *enabled) override;
	IOReturn setWriteCacheState(bool enabled) override;
	IOReturn doUnmap(IOBlockStorageDeviceExtent *extents,
	    UInt32 extentsCount, IOStorageUnmapOptions options) override;
	IOReturn doSynchronize(UInt64 block, UInt64 nblks,
	    IOStorageSynchronizeOptions options) override;
	IOReturn doGetProvisionStatus(UInt64 block, UInt64 nblks,
	    UInt32 *extentsCount,
	    IOBlockStorageProvisionDeviceExtent *extents,
	    IOStorageGetProvisionStatusOptions options) override;

private:
	struct qual_capture {
		bool pending;
		bool active;
		bool captured;
		bool ambiguous;
		uint64_t block;
		uint32_t nblks;
		uint32_t bytes;
		uint32_t copied;
		uint32_t matches;
		uint32_t mismatch_valid;
		uint32_t mismatch_offset;
		uint32_t completed;
		int result;
		uint32_t cmd17;
		uint32_t cmd18;
		uint32_t cmd18_blocks;
		uint32_t stops;
		uint32_t stop_failures;
		int32_t last_stop_result;
	};

	struct qual_result {
		uint32_t attempted;
		uint32_t ok;
		uint32_t reason;
		uint32_t bytes;
		uint32_t mismatch_valid;
		uint32_t mismatch_offset;
		uint32_t matches;
		uint32_t cmd18;
		uint32_t cmd17;
		uint32_t stops;
		uint32_t stop_failures;
	};

	static void reportEntry(void *context, wait_result_t waitResult);
	static void watchdogEntry(void *context, wait_result_t waitResult);
	static int readBlockCallback(void *context, uint64_t block,
	    void *buffer);
	static int readMultiCallback(void *context, uint64_t block,
	    uint32_t count, void *buffer,
	    struct gta4l_emmc_block_multi_result *result);
	static uint32_t copyOutCallback(void *context, uint32_t offset,
	    const void *source, uint32_t length);

	bool initializeHardware(void);
	bool claimTerminal(void);
	void panicWithReport(void);
	void fillDriverReport(void);
	void fillWatchdogReport(void);
	bool snapshotReport(struct gta4l_emmc_block_report *target, bool wait);
	bool readQualificationReference(void);
	void qualifyMediaRead(IOMedia *media, uint32_t blocks,
	    struct qual_result *out);
	void runReport(void);
	void runPartitionObservation(void);
	void runPartitionComparison(void);
	void partmapReportFailure(uint32_t reason);
	void partmapReportCancel(void);
	void partmapHandleRead(uint32_t status);
	void partmapPublishGpt(void);
	void releasePartmapMedia(void);
	void closePartmapWhole(void);
	uint64_t partmapElapsedMs(void);
	uint32_t readPartmapSectors(IOMedia *media, uint64_t lba,
	    uint32_t blocks, uint8_t *buffer);
	IOMedia *findExactWholeMedia(uint32_t *count);
	IOService *findGuidScheme(IOMedia *whole);
	uint32_t snapshotPartitionMedia(IOService *scheme, IOMedia *whole,
	    uint32_t *propErrors, uint32_t *nested);
	IOMedia *findPartitionMedia(uint64_t base, uint64_t size,
	    uint32_t *count);
	void runPartitionRead(void);
	void releasePartreadMedia(void);
	void partreadReportFailure(uint32_t reason);
	void partreadReportCancel(void);
	uint64_t partreadElapsedMs(void);
	void runDeblock(void);
	void releaseDeblockMedia(void);
	void deblockReportFailure(uint32_t reason);
	void deblockReportCancel(void);
	uint64_t deblockElapsedMs(void);
	IOMedia *findWholeMedia(void);
	uint32_t countPartitions(void);
	bool ancestryContains(IOService *service) const;
	void copyContent(char *destination, const char *source) const;

	IOLock *gate = nullptr;
	IOLock *reportLock = nullptr;
	struct gta4l_emmc_platform platform;
	struct gta4l_emmc_host host;
	struct gta4l_emmc_block block;
	struct gta4l_emmc_block_io io;
	IOMemoryDescriptor *requestBuffer = nullptr;
	uint8_t chunk[GTA4L_EMMC_BLOCK_CHUNK_BYTES];
	uint32_t capacityBlocks = 0;
	bool chunkedMode = false;
	volatile bool online = false;
	volatile bool reportCancel = false;
	volatile bool reportDone = false;
	bool terminalClaimed = false;
	bool reportMode = false;
	bool partObsMode = false;
	bool partCmpMode = false;
	bool partReadMode = false;
	bool partmapWholeOpen = false;
	IOMedia *partmapWhole = nullptr;
	IOService *partmapScheme = nullptr;
	uint64_t partmapReadStart = 0;
	uint8_t partmapPmbr[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t partmapHeader[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_block_partmap_scratch *partmapScratch = nullptr;
	uint8_t *partmapDma = nullptr;
	uint64_t partreadReadStart = 0;
	uint8_t *partreadBuffer = nullptr;
	IOMedia *partreadWhole = nullptr;
	IOMedia *partreadBoot = nullptr;
	bool partreadWholeOpen = false;
	bool partreadBootOpen = false;
	bool deblockMode = false;
	uint64_t deblockReadStart = 0;
	uint8_t *deblockBuffer = nullptr;
	IOMedia *deblockWhole = nullptr;
	IOMedia *deblockBoot = nullptr;
	bool deblockWholeOpen = false;
	bool deblockBootOpen = false;
	bool registered = false;
	bool reportStarted = false;
	thread_t reportThread = nullptr;
	thread_t watchdogThread = nullptr;
	uint8_t baseline[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t baselineSum = 0;
	bool baselineValid = false;
	uint8_t qualReference[GTA4L_EMMC_BLOCK_QUAL_BYTES];
	bool qualReferenceValid = false;
	uint8_t qualRead[GTA4L_EMMC_BLOCK_QUAL_BYTES];
	struct qual_capture qualCapture;
	struct gta4l_emmc_block_report report;
	struct gta4l_emmc_block_report watchdogReport;
	char reportBuffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	char watchdogBuffer[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	char mediaContent[GTA4L_EMMC_BLOCK_CONTENT_BYTES];
	char partContent[GTA4L_EMMC_BLOCK_PART_REPORT]
	    [GTA4L_EMMC_BLOCK_CONTENT_BYTES];
};

static_assert(sizeof(Gta4lEmmcBlock) <= (16u * 1024u),
    "Gta4lEmmcBlock must fit the kalloc safe allocation size");

OSDefineMetaClassAndStructors(Gta4lEmmcBlock, IOBlockStorageDevice)

static uint8_t
gta4l_emmc_block_read8(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	return gta4l_emmc_platform_read8(platform, space, offset);
}

static uint16_t
gta4l_emmc_block_read16(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	return gta4l_emmc_platform_read16(platform, space, offset);
}

static uint32_t
gta4l_emmc_block_read32(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	return gta4l_emmc_platform_read32(platform, space, offset);
}

static void
gta4l_emmc_block_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	gta4l_emmc_platform_write8(platform, space, offset, value);
}

static void
gta4l_emmc_block_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	gta4l_emmc_platform_write16(platform, space, offset, value);
}

static void
gta4l_emmc_block_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	gta4l_emmc_platform_write32(platform, space, offset, value);
}

static void
gta4l_emmc_block_barrier(void *context)
{
	(void)context;
	gta4l_emmc_platform_barrier();
}

static void
gta4l_emmc_block_delay(void *context, uint32_t microseconds)
{
	struct gta4l_emmc_platform *platform =
	    static_cast<struct gta4l_emmc_platform *>(context);

	gta4l_emmc_platform_delay_us(platform, microseconds);
}

static void
gta4l_emmc_block_log(void *context, uint32_t event, uint32_t a, uint32_t b)
{
	(void)context;
	(void)event;
	(void)a;
	(void)b;
}

static uint64_t
gta4l_emmc_block_counter(void *context)
{
	(void)context;
	return gta4l_emmc_platform_counter();
}

static const struct gta4l_emmc_ops gta4l_emmc_block_platform_ops = {
	.read8 = gta4l_emmc_block_read8,
	.read16 = gta4l_emmc_block_read16,
	.read32 = gta4l_emmc_block_read32,
	.write8 = gta4l_emmc_block_write8,
	.write16 = gta4l_emmc_block_write16,
	.write32 = gta4l_emmc_block_write32,
	.barrier = gta4l_emmc_block_barrier,
	.delay_us = gta4l_emmc_block_delay,
	.log = gta4l_emmc_block_log,
	.counter = gta4l_emmc_block_counter,
};

int
Gta4lEmmcBlock::readBlockCallback(void *context, uint64_t blockNumber,
    void *buffer)
{
	Gta4lEmmcBlock *self = static_cast<Gta4lEmmcBlock *>(context);
	int result;

	result = gta4l_emmc_read_block(&self->host, blockNumber, buffer);
	if (self->qualCapture.active) {
		self->qualCapture.cmd17++;
	}
	return result;
}

int
Gta4lEmmcBlock::readMultiCallback(void *context, uint64_t blockNumber,
    uint32_t count, void *buffer, struct gta4l_emmc_block_multi_result *result)
{
	Gta4lEmmcBlock *self = static_cast<Gta4lEmmcBlock *>(context);
	struct gta4l_emmc_multi_result multi;
	int rc;

	rc = gta4l_emmc_read_multi_result(&self->host, blockNumber, count,
	    buffer, &multi);
	result->transport_result = multi.transport_result;
	result->requested_blocks = multi.requested_blocks;
	result->completed_blocks = multi.completed_blocks;
	result->stop_attempted = (multi.stop_attempted != 0) ? 1u : 0u;
	result->stop_result = multi.stop_result;
	if (self->qualCapture.active) {
		self->qualCapture.cmd18++;
		self->qualCapture.cmd18_blocks += count;
		if (result->stop_attempted != 0) {
			self->qualCapture.stops++;
			if (result->stop_result != 0) {
				self->qualCapture.stop_failures++;
			}
		}
		self->qualCapture.last_stop_result = result->stop_result;
	}
	/* The portable callback contract distinguishes execution from
	 * refusal: zero means result is a valid outcome record even when
	 * the data phase or stop failed. Only the pre-command argument
	 * refusal keeps a nonzero return. */
	if (rc == GTA4L_EMMC_ERR_ARG) {
		return rc;
	}
	return 0;
}

uint32_t
Gta4lEmmcBlock::copyOutCallback(void *context, uint32_t offset,
    const void *source, uint32_t length)
{
	Gta4lEmmcBlock *self = static_cast<Gta4lEmmcBlock *>(context);
	IOByteCount copied;

	if (self->qualCapture.active && (source != nullptr)) {
		const uint8_t *bytes = static_cast<const uint8_t *>(source);
		uint32_t index;

		for (index = 0; index < length; index++) {
			if (offset + index >=
			    sizeof(self->qualReference)) {
				if (self->qualCapture.mismatch_valid == 0) {
					self->qualCapture.mismatch_valid = 1;
					self->qualCapture.mismatch_offset =
					    offset + index;
				}
				break;
			}
			if (bytes[index] ==
			    self->qualReference[offset + index]) {
				self->qualCapture.matches++;
			} else if (self->qualCapture.mismatch_valid == 0) {
				self->qualCapture.mismatch_valid = 1;
				self->qualCapture.mismatch_offset =
				    offset + index;
			}
		}
		self->qualCapture.copied += length;
	}
	if (self->requestBuffer == nullptr) {
		return 0;
	}
	copied = self->requestBuffer->writeBytes(offset, source, length);
	if (copied > 0xffffffffu) {
		return 0xffffffffu;
	}
	return (uint32_t)copied;
}

void
Gta4lEmmcBlock::copyContent(char *destination, const char *source) const
{
	uint32_t index = 0;

	if (destination == nullptr) {
		return;
	}
	if (source != nullptr) {
		while ((source[index] != 0) &&
		    (index + 1u < GTA4L_EMMC_BLOCK_CONTENT_BYTES)) {
			destination[index] = source[index];
			index++;
		}
	}
	destination[index] = 0;
}

bool
Gta4lEmmcBlock::ancestryContains(IOService *service) const
{
	IOService *provider;
	uint32_t depth;

	if (service == nullptr) {
		return false;
	}
	provider = service->getProvider();
	for (depth = 0; (depth < GTA4L_EMMC_BLOCK_ANCESTRY_DEPTH) &&
	    (provider != nullptr); depth++) {
		if (provider == this) {
			return true;
		}
		provider = provider->getProvider();
	}
	return false;
}

bool
Gta4lEmmcBlock::initializeHardware(void)
{
	int result;

	bzero(&platform, sizeof(platform));
	bzero(&host, sizeof(host));
	host.ops = &gta4l_emmc_block_platform_ops;
	host.context = &platform;
	host.fixed_1v8 = 1;
	host.pad_pwr_switch = 1;
	if (gta4l_emmc_platform_map(&platform) != 0) {
		return false;
	}
	result = gta4l_emmc_init(&host);
	if (result != GTA4L_EMMC_OK) {
		report.rc = (uint32_t)result;
		report.stop = 11;
		report.fail_valid = 1;
		report.fail_stage = host.last_step;
		report.fail_code = result;
		return false;
	}
	if (host.capacity_blocks == 0) {
		report.rc = (uint32_t)GTA4L_EMMC_ERR_ARG;
		report.stop = 11;
		report.fail_valid = 1;
		report.fail_stage = host.last_step;
		report.fail_code = GTA4L_EMMC_ERR_ARG;
		return false;
	}
	capacityBlocks = host.capacity_blocks;
	io.context = this;
	io.read_block = readBlockCallback;
	io.read_multi = (chunkedMode) ? readMultiCallback : nullptr;
	io.chunk_buffer = (chunkedMode) ? chunk : nullptr;
	io.chunk_buffer_bytes = (chunkedMode) ?
	    GTA4L_EMMC_BLOCK_CHUNK_BYTES : 0u;
	io.copy_out = copyOutCallback;
	gta4l_emmc_block_init(&block, &io, capacityBlocks);
	return true;
}

bool
Gta4lEmmcBlock::start(IOService *provider)
{
	const char *arguments = nullptr;
	uint32_t blockTokens;
	uint32_t probeTokens;
	uint32_t conflictTokens;
	uint32_t partobsTokens;
	uint32_t partcmpTokens;
	uint32_t partreadTokens;
	uint32_t deblockTokens;
	int value = 0;
	int partValue = 0;
	int partcmpValue = 0;
	int partreadValue = 0;
	int deblockValue = 0;
	int parsed;
	int partobsParsed;
	int partcmpParsed;
	int partreadParsed;
	int deblockParsed;
	int mode;
	int partobs;
	int partcmp;
	int partread;
	int deblock;
	int baselineResult;

	arguments = PE_boot_args();
	blockTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_BOOT_ARG);
	probeTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_PROBE_ARG);
	partobsTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_PARTOBS_ARG);
	partcmpTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_PARTCMP_ARG);
	partreadTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_PARTREAD_ARG);
	deblockTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_DEBLOCK_ARG);
	conflictTokens = gta4l_emmc_block_token_count(arguments,
	    "gta4l-sdhci-audit") +
	    gta4l_emmc_block_token_count(arguments, "gta4l-sdhci-snapshot") +
	    gta4l_emmc_block_token_count(arguments, "gta4l-sdhci-host-init") +
	    gta4l_emmc_block_token_count(arguments, "gta4l-emmc-mb");
	parsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_BOOT_ARG, &value,
	    sizeof(value));
	if ((parsed == 0) && (blockTokens != 0)) {
		value = -1;
	}
	partobsParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_PARTOBS_ARG,
	    &partValue, sizeof(partValue));
	if ((partobsParsed == 0) && (partobsTokens != 0)) {
		partValue = -1;
	}
	partcmpParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_PARTCMP_ARG,
	    &partcmpValue, sizeof(partcmpValue));
	if ((partcmpParsed == 0) && (partcmpTokens != 0)) {
		partcmpValue = -1;
	}
	partreadParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_PARTREAD_ARG,
	    &partreadValue, sizeof(partreadValue));
	if ((partreadParsed == 0) && (partreadTokens != 0)) {
		partreadValue = -1;
	}
	deblockParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_DEBLOCK_ARG,
	    &deblockValue, sizeof(deblockValue));
	if ((deblockParsed == 0) && (deblockTokens != 0)) {
		deblockValue = -1;
	}
	mode = gta4l_emmc_block_mode_select(blockTokens != 0, value,
	    probeTokens != 0, conflictTokens != 0, blockTokens > 1u);
	if (mode == GTA4L_EMMC_BLOCK_SELECT_INERT) {
		if (partobsTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-partobs requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		if (partcmpTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-partcmp requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		if (partreadTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-partread requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		if (deblockTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-deblock requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		return false;
	}
	if ((mode != GTA4L_EMMC_BLOCK_SELECT_REPORT) &&
	    (mode != GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED)) {
		if (blockTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-block token\n");
		} else if (probeTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-probe "
			    "selected\n");
		} else if (conflictTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: another SDHCI "
			    "diagnostic selected\n");
		} else if (value == GTA4L_EMMC_BLOCK_MODE_PUBLISH) {
			IOLog("GTA4L-EMMC P5 REFUSED: publish-only mode 2 "
			    "is deferred\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-block value\n");
		}
		return false;
	}
	partobs = gta4l_emmc_block_partobs_select(partobsTokens != 0,
	    partValue, partobsTokens > 1u, 1);
	if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_REFUSED) {
		if (partobsTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-partobs token\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-partobs value\n");
		}
		return false;
	}
	partObsMode = (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON);
	partcmp = gta4l_emmc_block_partcmp_select(partcmpTokens != 0,
	    partcmpValue, partcmpTokens > 1u, 1,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON);
	if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_REFUSED) {
		if (partcmpTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-partcmp token\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-partcmp are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-partcmp value\n");
		}
		return false;
	}
	partCmpMode = (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON);
	partread = gta4l_emmc_block_partread_select(partreadTokens != 0,
	    partreadValue, partreadTokens > 1u, 1,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON);
	if (partread == GTA4L_EMMC_BLOCK_PARTREAD_REFUSED) {
		if (partreadTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-partread token\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-partread are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-partread are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-partread value\n");
		}
		return false;
	}
	partReadMode = (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON);
	deblock = gta4l_emmc_block_deblock_select(deblockTokens != 0,
	    deblockValue, deblockTokens > 1u, 1,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON,
	    partread == GTA4L_EMMC_BLOCK_PARTREAD_ON);
	if (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED) {
		if (deblockTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-deblock token\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-deblock are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-deblock are mutually "
			    "exclusive\n");
		} else if (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partread "
			    "and gta4l-emmc-deblock are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-deblock value\n");
		}
		return false;
	}
	deblockMode = (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON);
	if (partReadMode) {
		/* Every case is copied through one page-aligned buffer so the
		 * descriptor never straddles a page boundary and every
		 * request is a whole number of media blocks. */
		partreadBuffer = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES, PAGE_SIZE);
		if (partreadBuffer == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: partread buffer "
			    "allocation\n");
			return false;
		}
	}
	if (deblockMode) {
		/* Every case is copied through one page-aligned buffer so the
		 * descriptor never straddles a page boundary; the deblocker
		 * writes only the requested bytes into it and discards the
		 * aligned excess into its own scratch. */
		deblockBuffer = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES, PAGE_SIZE);
		if (deblockBuffer == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: deblock buffer "
			    "allocation\n");
			return false;
		}
	}
	if (partCmpMode) {
		/* The exact comparator's array, GPT copies and media
		 * snapshots are too large for the driver object. The
		 * report thread owns one bounded scratch allocation. */
		partmapScratch = (struct gta4l_emmc_block_partmap_scratch *)
		    IOMalloc(sizeof(*partmapScratch));
		if (partmapScratch == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: partmap scratch "
			    "allocation\n");
			return false;
		}
		/* The comparator's reads are copied through this buffer so
		 * every descriptor starts on a kernel page boundary. The
		 * class members are not page-contained, and a sub-block
		 * first segment is truncated to zero by the storage
		 * driver before the adapter is called. */
		partmapDma = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES, PAGE_SIZE);
		if (partmapDma == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: partmap DMA "
			    "allocation\n");
			return false;
		}
	}

	if (!IOBlockStorageDevice::start(provider)) {
		return false;
	}
	gate = IOLockAlloc();
	if (gate == nullptr) {
		IOLog("GTA4L-EMMC P5 FAILED: lock allocation\n");
		return false;
	}
	reportLock = IOLockAlloc();
	if (reportLock == nullptr) {
		IOLog("GTA4L-EMMC P5 FAILED: report lock allocation\n");
		IOLockFree(gate);
		gate = nullptr;
		return false;
	}
	reportMode = true;
	chunkedMode = (mode == GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED);
	if (!initializeHardware()) {
		panicWithReport();
		return false;
	}

	IOLockLock(gate);
	baselineResult = gta4l_emmc_read_block(&host,
	    GTA4L_EMMC_BLOCK_REPORT_LBA, baseline);
	if (baselineResult == 0) {
		baselineSum = gta4l_emmc_block_sum(baseline,
		    GTA4L_EMMC_BLOCK_BYTES);
		baselineValid = true;
	}
	IOLockUnlock(gate);
	if (baselineResult != 0) {
		report.rc = (uint32_t)baselineResult;
		report.stop = 11;
		report.fail_valid = 1;
		report.fail_stage = host.last_step;
		report.fail_code = baselineResult;
		panicWithReport();
		return false;
	}

	if (!setProperty(kIOMaximumBlockCountReadKey,
	    (unsigned long long)GTA4L_EMMC_BLOCK_MAX_BLOCKS, 32)) {
		IOLog("GTA4L-EMMC P5 FAILED: block limit property\n");
		return false;
	}
	if (!setProperty(kIOMaximumByteCountReadKey,
	    (unsigned long long)GTA4L_EMMC_BLOCK_MAX_BYTES, 32)) {
		IOLog("GTA4L-EMMC P5 FAILED: byte limit property\n");
		return false;
	}
	if (!setProperty(kIOMaximumSegmentCountReadKey,
	    (unsigned long long)(deblockMode ?
	    GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS : 1u), 32)) {
		IOLog("GTA4L-EMMC P5 FAILED: segment limit property\n");
		return false;
	}

	online = true;
	registerService();
	registered = true;

	retain();
	reportCancel = false;
	if (kernel_thread_start(&Gta4lEmmcBlock::reportEntry, this,
	    &reportThread) != KERN_SUCCESS) {
		release();
		report.rc = 16;
		report.stop = 16;
		report.fail_valid = 1;
		report.fail_stage = 1;
		report.fail_code = 16;
		panicWithReport();
		return false;
	}
	thread_deallocate(reportThread);
	reportThread = nullptr;
	reportStarted = true;

	retain();
	if (kernel_thread_start(&Gta4lEmmcBlock::watchdogEntry, this,
	    &watchdogThread) != KERN_SUCCESS) {
		release();
		watchdogThread = nullptr;
		IOLog("GTA4L-EMMC P5 WATCHDOG: thread start failed\n");
		report.rc = 16;
		report.stop = 16;
		report.fail_valid = 1;
		report.fail_stage = 17;
		report.fail_code = 16;
		panicWithReport();
		return false;
	}
	thread_deallocate(watchdogThread);
	watchdogThread = nullptr;
	return true;
}

void
Gta4lEmmcBlock::stop(IOService *provider)
{
	if (gate != nullptr) {
		IOLockLock(gate);
		online = false;
		reportCancel = true;
		IOLockUnlock(gate);
	} else {
		reportCancel = true;
	}
	IOBlockStorageDevice::stop(provider);
}

void
Gta4lEmmcBlock::free()
{
	if (partreadBuffer != nullptr) {
		IOFreeAligned(partreadBuffer,
		    GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES);
		partreadBuffer = nullptr;
	}
	if (deblockBuffer != nullptr) {
		IOFreeAligned(deblockBuffer,
		    GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES);
		deblockBuffer = nullptr;
	}
	if (partmapDma != nullptr) {
		IOFreeAligned(partmapDma,
		    GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES);
		partmapDma = nullptr;
	}
	if (partmapScratch != nullptr) {
		IOFree(partmapScratch, sizeof(*partmapScratch));
		partmapScratch = nullptr;
	}
	if (reportLock != nullptr) {
		IOLockFree(reportLock);
		reportLock = nullptr;
	}
	if (gate != nullptr) {
		IOLockFree(gate);
		gate = nullptr;
	}
	IOBlockStorageDevice::free();
}

IOReturn
Gta4lEmmcBlock::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 blockNumber,
    UInt64 nblks, IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	enum gta4l_emmc_block_request_result check;
	enum gta4l_emmc_block_direction direction;
	uint64_t completed = 0;
	int result;
	IOReturn status;
	bool wentOffline = false;

	(void)attributes;
	if ((buffer == nullptr) || (completion == nullptr) ||
	    (completion->action == nullptr)) {
		return kIOReturnBadArgument;
	}
	if (nblks == 0) {
		return kIOReturnBadArgument;
	}
	if (blockNumber >= (UInt64)capacityBlocks) {
		return kIOReturnBadArgument;
	}
	if (nblks > (UInt64)capacityBlocks - blockNumber) {
		return kIOReturnBadArgument;
	}
	if (nblks > GTA4L_EMMC_BLOCK_MAX_BLOCKS) {
		return kIOReturnMessageTooLarge;
	}
	if ((UInt64)buffer->getLength() <
	    nblks * GTA4L_EMMC_BLOCK_BYTES) {
		return kIOReturnBadArgument;
	}
	if (buffer->getDirection() == kIODirectionOut) {
		return kIOReturnNotWritable;
	}
	if (buffer->getDirection() != kIODirectionIn) {
		return kIOReturnBadArgument;
	}
	if (gate == nullptr) {
		return kIOReturnNotReady;
	}

	check = gta4l_emmc_block_request_check(capacityBlocks, blockNumber,
	    nblks, (uint64_t)buffer->getLength(),
	    GTA4L_EMMC_BLOCK_DIRECTION_READ, online ? 1 : 0);
	if (check == GTA4L_EMMC_BLOCK_REQUEST_NOT_WRITABLE) {
		return kIOReturnNotWritable;
	}
	if (check == GTA4L_EMMC_BLOCK_REQUEST_TOO_LARGE) {
		return kIOReturnMessageTooLarge;
	}
	if (check == GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT) {
		return kIOReturnBadArgument;
	}

	IOLockLock(gate);
	if (!online) {
		IOLockUnlock(gate);
		completion->action(completion->target, completion->parameter,
		    kIOReturnNoMedia, 0);
		return kIOReturnSuccess;
	}
	requestBuffer = buffer;
	if (qualCapture.pending &&
	    (blockNumber == qualCapture.block) &&
	    ((uint32_t)nblks == qualCapture.nblks) &&
	    ((uint64_t)buffer->getLength() == qualCapture.bytes)) {
		if (qualCapture.active || qualCapture.captured) {
			/* A second request with the controlled identity
			 * arrived while the qualification window was
			 * open: attribution is ambiguous. */
			qualCapture.ambiguous = true;
		} else {
			qualCapture.active = true;
			qualCapture.copied = 0;
			qualCapture.matches = 0;
			qualCapture.mismatch_valid = 0;
			qualCapture.mismatch_offset = 0;
			qualCapture.cmd17 = 0;
			qualCapture.cmd18 = 0;
			qualCapture.cmd18_blocks = 0;
			qualCapture.stops = 0;
			qualCapture.stop_failures = 0;
			qualCapture.last_stop_result = 0;
		}
	}
	direction = GTA4L_EMMC_BLOCK_READ;
	result = gta4l_emmc_block_read(&block, blockNumber, (uint32_t)nblks,
	    (uint64_t)buffer->getLength(), direction, &completed);
	if (qualCapture.active) {
		qualCapture.result = result;
		qualCapture.completed = (completed > 0xffffffffu) ?
		    0xffffffffu : (uint32_t)completed;
		qualCapture.captured = true;
		qualCapture.active = false;
	}
	requestBuffer = nullptr;
	if (result != GTA4L_EMMC_BLOCK_OK) {
		online = false;
		wentOffline = true;
	}
	IOLockUnlock(gate);

	switch (result) {
	case GTA4L_EMMC_BLOCK_OK:
		status = kIOReturnSuccess;
		break;
	case GTA4L_EMMC_BLOCK_NOT_WRITABLE:
		status = kIOReturnNotWritable;
		break;
	case GTA4L_EMMC_BLOCK_NO_MEDIA:
		status = kIOReturnNoMedia;
		break;
	case GTA4L_EMMC_BLOCK_TOO_LARGE:
		status = kIOReturnMessageTooLarge;
		break;
	case GTA4L_EMMC_BLOCK_SHORT_COPY:
		status = kIOReturnNoMemory;
		break;
	case GTA4L_EMMC_BLOCK_IO_ERROR:
		status = kIOReturnIOError;
		break;
	default:
		status = kIOReturnBadArgument;
		break;
	}
	completion->action(completion->target, completion->parameter, status,
	    completed);
	if (wentOffline) {
		messageClients(kIOMessageMediaStateHasChanged,
		    (void *)(uintptr_t)kIOMediaStateOffline);
	}
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::doEjectMedia()
{
	return kIOReturnUnsupported;
}

IOReturn
Gta4lEmmcBlock::doFormatMedia(UInt64 byteCapacity)
{
	(void)byteCapacity;
	return kIOReturnUnsupported;
}

UInt32
Gta4lEmmcBlock::doGetFormatCapacities(UInt64 *capacities,
    UInt32 capacitiesMaxCount) const
{
	(void)capacities;
	(void)capacitiesMaxCount;
	return 0;
}

char *
Gta4lEmmcBlock::getVendorString()
{
	return const_cast<char *>("UniXNU");
}

char *
Gta4lEmmcBlock::getProductString()
{
	return const_cast<char *>("gta4l eMMC (read-only)");
}

char *
Gta4lEmmcBlock::getRevisionString()
{
	return const_cast<char *>("1.0");
}

char *
Gta4lEmmcBlock::getAdditionalDeviceInfoString()
{
	return const_cast<char *>("read-only bring-up; no DMA/CQE/ICE");
}

IOReturn
Gta4lEmmcBlock::reportBlockSize(UInt64 *blockSize)
{
	if (blockSize == nullptr) {
		return kIOReturnBadArgument;
	}
	*blockSize = GTA4L_EMMC_BLOCK_BYTES;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::reportEjectability(bool *isEjectable)
{
	if (isEjectable == nullptr) {
		return kIOReturnBadArgument;
	}
	*isEjectable = false;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::reportMaxValidBlock(UInt64 *maxBlock)
{
	if (maxBlock == nullptr) {
		return kIOReturnBadArgument;
	}
	*maxBlock = (capacityBlocks == 0) ? 0 : capacityBlocks - 1u;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::reportMediaState(bool *mediaPresent, bool *changedState)
{
	if (mediaPresent == nullptr) {
		return kIOReturnBadArgument;
	}
	*mediaPresent = online;
	if (changedState != nullptr) {
		*changedState = false;
	}
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::reportRemovability(bool *isRemovable)
{
	if (isRemovable == nullptr) {
		return kIOReturnBadArgument;
	}
	*isRemovable = false;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::reportWriteProtection(bool *isWriteProtected)
{
	if (isWriteProtected == nullptr) {
		return kIOReturnBadArgument;
	}
	*isWriteProtected = true;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::doSynchronizeCache()
{
	if (!online) {
		return kIOReturnNotReady;
	}
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::getWriteCacheState(bool *enabled)
{
	if (enabled == nullptr) {
		return kIOReturnBadArgument;
	}
	*enabled = false;
	return kIOReturnSuccess;
}

IOReturn
Gta4lEmmcBlock::setWriteCacheState(bool enabled)
{
	(void)enabled;
	return kIOReturnUnsupported;
}

IOReturn
Gta4lEmmcBlock::doUnmap(IOBlockStorageDeviceExtent *extents,
    UInt32 extentsCount, IOStorageUnmapOptions options)
{
	(void)extents;
	(void)extentsCount;
	(void)options;
	return kIOReturnUnsupported;
}

IOReturn
Gta4lEmmcBlock::doSynchronize(UInt64 blockNumber, UInt64 nblks,
    IOStorageSynchronizeOptions options)
{
	(void)blockNumber;
	(void)nblks;
	(void)options;
	return doSynchronizeCache();
}

IOReturn
Gta4lEmmcBlock::doGetProvisionStatus(UInt64 blockNumber, UInt64 nblks,
    UInt32 *extentsCount, IOBlockStorageProvisionDeviceExtent *extents,
    IOStorageGetProvisionStatusOptions options)
{
	(void)blockNumber;
	(void)nblks;
	(void)extentsCount;
	(void)extents;
	(void)options;
	return kIOReturnUnsupported;
}

bool
Gta4lEmmcBlock::claimTerminal()
{
	bool claimed = false;

	if (reportLock == nullptr) {
		return false;
	}
	IOLockLock(reportLock);
	if (!terminalClaimed) {
		terminalClaimed = true;
		claimed = true;
	}
	IOLockUnlock(reportLock);
	return claimed;
}

bool
Gta4lEmmcBlock::snapshotReport(struct gta4l_emmc_block_report *target,
    bool wait)
{
	bool locked;

	if (target == nullptr) {
		return false;
	}
	target->capacity_blocks = capacityBlocks;
	target->block_size = GTA4L_EMMC_BLOCK_BYTES;
	target->bytes = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	target->bus_width = host.bus_width;
	target->timing = 0;
	target->clock_hz = GTA4L_EMMC_BLOCK_CLOCK_HZ;
	target->online = online ? 1u : 0u;
	target->write_protected = 1;
	target->mode = (chunkedMode) ? GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED :
	    GTA4L_EMMC_BLOCK_MODE_REPORT;
	target->baseline_sum = baselineSum;
	if (wait) {
		IOLockLock(gate);
		locked = true;
	} else {
		locked = IOLockTryLock(gate);
	}
	if (locked) {
		target->requests = block.counters.requests;
		target->blocks = block.counters.blocks;
		target->failures = block.counters.failures;
		target->max_request_blocks =
		    block.counters.max_request_blocks;
		target->first_fail_valid =
		    block.counters.first_fail_valid;
		target->first_fail_block =
		    block.counters.first_fail_block;
		target->first_fail_error =
		    block.counters.first_fail_error;
		target->requested_blocks =
		    block.counters.requested_blocks;
		target->cmd17_reads = block.counters.cmd17_reads;
		target->cmd18_chunks = block.counters.cmd18_chunks;
		target->cmd18_blocks = block.counters.cmd18_blocks;
		target->chunk_failures = block.counters.chunk_failures;
		target->stop_attempts = block.counters.stop_attempts;
		target->stop_failures = block.counters.stop_failures;
		target->last_stop_result =
		    block.counters.last_stop_result;
		target->snapshot_locked = 1;
		IOLockUnlock(gate);
	} else {
		target->snapshot_locked = 0;
	}
	if (reportLock != nullptr) {
		/* Copy the observation state in one briefly held snapshot so
		 * the watchdog cannot observe a half-updated sample. */
		IOLockLock(reportLock);
		target->part_obs = report.part_obs;
		target->part_obs_samples = report.part_obs_samples;
		target->part_obs_first = report.part_obs_first;
		target->part_obs_first_ms = report.part_obs_first_ms;
		target->part_obs_first_nz = report.part_obs_first_nz;
		target->part_obs_first_nz_ms =
		    report.part_obs_first_nz_ms;
		target->part_obs_final = report.part_obs_final;
		target->part_obs_final_ms = report.part_obs_final_ms;
		target->part_obs_changes = report.part_obs_changes;
		target->part_obs_stable = report.part_obs_stable;
		target->part_obs_stable_ms = report.part_obs_stable_ms;
		target->part_obs_deadline_ms =
		    report.part_obs_deadline_ms;
		target->part_obs_timed_out = report.part_obs_timed_out;
		target->part_obs_none = report.part_obs_none;
		target->part_obs_error = report.part_obs_error;
		target->part_obs_slots = report.part_obs_slots;
		target->part_obs_reported = report.part_obs_reported;
		target->part_obs_complete = report.part_obs_complete;
		target->partmap = report.partmap;
		target->partmap_gpt = report.partmap_gpt;
		target->partmap_reason = report.partmap_reason;
		target->partmap_slots = report.partmap_slots;
		target->partmap_used = report.partmap_used;
		target->partmap_pmbr = report.partmap_pmbr;
		target->partmap_hdr = report.partmap_hdr;
		target->partmap_arr = report.partmap_arr;
		target->partmap_bak = report.partmap_bak;
		target->partmap_xcheck = report.partmap_xcheck;
		target->partmap_media = report.partmap_media;
		target->partmap_match = report.partmap_match;
		target->partmap_missing = report.partmap_missing;
		target->partmap_extra = report.partmap_extra;
		target->partmap_dup = report.partmap_dup;
		target->partmap_nested = report.partmap_nested;
		target->partmap_prop = report.partmap_prop;
		target->partmap_samples = report.partmap_samples;
		target->partmap_changes = report.partmap_changes;
		target->partmap_stable = report.partmap_stable;
		target->partmap_stable_ms = report.partmap_stable_ms;
		target->partmap_deadline_ms = report.partmap_deadline_ms;
		target->partmap_timed_out = report.partmap_timed_out;
		target->partmap_budget = report.partmap_budget;
		target->partmap_reads = report.partmap_reads;
		target->partmap_blocks = report.partmap_blocks;
		target->partmap_fails = report.partmap_fails;
		target->partmap_cancel = report.partmap_cancel;
		target->partmap_complete = report.partmap_complete;
		target->partmap_read_state = report.partmap_read_state;
		target->partmap_read_blocks = report.partmap_read_blocks;
		target->partmap_read_bytes = report.partmap_read_bytes;
		target->partmap_read_desc_len =
		    report.partmap_read_desc_len;
		target->partmap_read_desc_dir =
		    report.partmap_read_desc_dir;
		target->partmap_read_status = report.partmap_read_status;
		target->partmap_read_lba = report.partmap_read_lba;
		target->partmap_read_actual = report.partmap_read_actual;
		target->partread = report.partread;
		target->partread_cases = report.partread_cases;
		target->partread_passed = report.partread_passed;
		target->partread_failed = report.partread_failed;
		target->partread_reason = report.partread_reason;
		target->partread_complete = report.partread_complete;
		target->partread_reads = report.partread_reads;
		target->partread_blocks = report.partread_blocks;
		target->partread_bytes = report.partread_bytes;
		target->partread_fails = report.partread_fails;
		target->partread_cancel = report.partread_cancel;
		target->partread_budget = report.partread_budget;
		target->partread_media = report.partread_media;
		target->partread_part = report.partread_part;
		target->partread_capacity = report.partread_capacity;
		target->partread_last_index = report.partread_last_index;
		target->partread_last_target = report.partread_last_target;
		target->partread_last_blocks = report.partread_last_blocks;
		target->partread_last_offset = report.partread_last_offset;
		target->partread_last_ok = report.partread_last_ok;
		target->partread_last_status = report.partread_last_status;
		target->partread_last_actual = report.partread_last_actual;
		memcpy(target->partread_last_digest,
		    report.partread_last_digest,
		    sizeof(target->partread_last_digest));
		target->partread_fail_index = report.partread_fail_index;
		target->partread_fail_reason = report.partread_fail_reason;
		target->partread_fail_status = report.partread_fail_status;
		target->partread_fail_actual = report.partread_fail_actual;
		memcpy(target->partread_fail_digest,
		    report.partread_fail_digest,
		    sizeof(target->partread_fail_digest));
		target->deblock = report.deblock;
		target->deblock_cases = report.deblock_cases;
		target->deblock_passed = report.deblock_passed;
		target->deblock_failed = report.deblock_failed;
		target->deblock_reason = report.deblock_reason;
		target->deblock_complete = report.deblock_complete;
		target->deblock_reads = report.deblock_reads;
		target->deblock_bytes = report.deblock_bytes;
		target->deblock_span = report.deblock_span;
		target->deblock_blocks = report.deblock_blocks;
		target->deblock_fails = report.deblock_fails;
		target->deblock_cancel = report.deblock_cancel;
		target->deblock_budget = report.deblock_budget;
		target->deblock_media = report.deblock_media;
		target->deblock_part = report.deblock_part;
		target->deblock_segments = report.deblock_segments;
		target->deblock_capacity = report.deblock_capacity;
		target->deblock_last_index = report.deblock_last_index;
		target->deblock_last_target = report.deblock_last_target;
		target->deblock_last_start = report.deblock_last_start;
		target->deblock_last_length = report.deblock_last_length;
		target->deblock_last_span = report.deblock_last_span;
		target->deblock_last_status = report.deblock_last_status;
		target->deblock_last_actual = report.deblock_last_actual;
		memcpy(target->deblock_last_digest,
		    report.deblock_last_digest,
		    sizeof(target->deblock_last_digest));
		target->deblock_fail_index = report.deblock_fail_index;
		target->deblock_fail_reason = report.deblock_fail_reason;
		target->deblock_fail_status = report.deblock_fail_status;
		target->deblock_fail_actual = report.deblock_fail_actual;
		memcpy(target->deblock_fail_digest,
		    report.deblock_fail_digest,
		    sizeof(target->deblock_fail_digest));
		IOLockUnlock(reportLock);
	}
	return locked;
}

void
Gta4lEmmcBlock::fillDriverReport()
{
	(void)snapshotReport(&report, true);
}

IOMedia *
Gta4lEmmcBlock::findWholeMedia()
{
	OSDictionary *matching;
	OSIterator *iterator;
	IOMedia *found = nullptr;

	matching = IOService::serviceMatching("IOMedia");
	if (matching == nullptr) {
		return nullptr;
	}
	iterator = IOService::getMatchingServices(matching);
	matching->release();
	if (iterator == nullptr) {
		return nullptr;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *service = OSDynamicCast(IOService, object);
		IOMedia *media = (service != nullptr) ?
		    static_cast<IOMedia *>(service) : nullptr;
		OSBoolean *whole;

		if (media == nullptr) {
			continue;
		}
		if (!ancestryContains(media)) {
			continue;
		}
		whole = OSDynamicCast(OSBoolean,
		    media->getProperty(kIOMediaWholeKey));
		if ((whole == nullptr) || !whole->getValue()) {
			continue;
		}
		found = media;
		found->retain();
		break;
	}
	iterator->release();
	return found;
}

uint32_t
Gta4lEmmcBlock::countPartitions()
{
	OSDictionary *matching;
	OSIterator *iterator;
	uint32_t count = 0;
	uint32_t reported = 0;

	matching = IOService::serviceMatching("IOMedia");
	if (matching == nullptr) {
		report.part_error = 15;
		return 0;
	}
	iterator = IOService::getMatchingServices(matching);
	matching->release();
	if (iterator == nullptr) {
		report.part_error = 15;
		return 0;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *service = OSDynamicCast(IOService, object);
		IOMedia *media = (service != nullptr) ?
		    static_cast<IOMedia *>(service) : nullptr;
		OSBoolean *whole;
		OSNumber *base;
		OSNumber *size;
		OSString *content;

		if (media == nullptr) {
			continue;
		}
		if (!ancestryContains(media)) {
			continue;
		}
		whole = OSDynamicCast(OSBoolean,
		    media->getProperty(kIOMediaWholeKey));
		if ((whole != nullptr) && whole->getValue()) {
			continue;
		}
		if (reported < GTA4L_EMMC_BLOCK_PART_REPORT) {
			base = OSDynamicCast(OSNumber,
			    media->getProperty(kIOMediaBaseKey));
			size = OSDynamicCast(OSNumber,
			    media->getProperty(kIOMediaSizeKey));
			content = OSDynamicCast(OSString,
			    media->getProperty(kIOMediaContentKey));
			report.part[reported].base = (base != nullptr) ?
			    base->unsigned64BitValue() : 0;
			report.part[reported].size = (size != nullptr) ?
			    size->unsigned64BitValue() : 0;
			copyContent(partContent[reported],
			    (content != nullptr) ? content->getCStringNoCopy() : nullptr);
			report.part[reported].content =
			    partContent[reported];
			reported++;
		}
		count++;
	}
	iterator->release();
	report.part_reported = reported;
	return count;
}

static bool
gta4l_emmc_block_text_equal(const char *left, const char *right)
{
	uint32_t index = 0;

	if ((left == nullptr) || (right == nullptr)) {
		return false;
	}
	while ((left[index] != 0) && (left[index] == right[index])) {
		index++;
	}
	return (left[index] == 0) && (right[index] == 0);
}

uint64_t
Gta4lEmmcBlock::partmapElapsedMs()
{
	uint64_t now = gta4l_emmc_platform_counter();
	uint64_t hz = gta4l_emmc_platform_counter_hz();
	uint64_t ticks_per_ms;

	if (hz == 0) {
		return 0;
	}
	ticks_per_ms = hz / 1000u;
	if (ticks_per_ms == 0) {
		ticks_per_ms = 1;
	}
	return (now - partmapReadStart) / ticks_per_ms;
}

uint32_t
Gta4lEmmcBlock::readPartmapSectors(IOMedia *media, uint64_t lba,
    uint32_t blocks, uint8_t *buffer)
{
	IOMemoryDescriptor *descriptor;
	uint8_t *destination = buffer;
	UInt64 actual = 0;
	UInt64 requested = (UInt64)blocks * GTA4L_EMMC_BLOCK_BYTES;
	IOReturn status;
	uint32_t state;

	if (reportCancel) {
		return GTA4L_EMMC_BLOCK_PARTMAP_REASON_CANCELLED;
	}
	if (partmapElapsedMs() >= GTA4L_EMMC_BLOCK_PARTMAP_READ_BUDGET_MS) {
		return GTA4L_EMMC_BLOCK_PARTMAP_REASON_BUDGET;
	}
	if ((partmapDma != nullptr) &&
	    (requested <= GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES)) {
		destination = partmapDma;
	}
	descriptor = IOMemoryDescriptor::withAddress(destination,
	    requested, kIODirectionIn);
	if (descriptor == nullptr) {
		return GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA;
	}
	IOLockLock(reportLock);
	state = report.partmap_read_state;
	if ((state != GTA4L_EMMC_BLOCK_PARTMAP_READ_ERROR) &&
	    (state != GTA4L_EMMC_BLOCK_PARTMAP_READ_SHORT)) {
		report.partmap_read_state =
		    GTA4L_EMMC_BLOCK_PARTMAP_READ_FLIGHT;
		report.partmap_read_lba = lba;
		report.partmap_read_blocks = blocks;
		report.partmap_read_bytes = (uint32_t)requested;
		report.partmap_read_desc_len =
		    (uint32_t)descriptor->getLength();
		report.partmap_read_desc_dir =
		    (uint32_t)descriptor->getDirection();
		report.partmap_read_status = 0;
		report.partmap_read_actual = 0;
	}
	IOLockUnlock(reportLock);
	status = media->read(this,
	    lba * GTA4L_EMMC_BLOCK_BYTES, descriptor, nullptr, &actual);
	descriptor->release();
	IOLockLock(reportLock);
	report.partmap_reads++;
	report.partmap_blocks += blocks;
	state = report.partmap_read_state;
	if ((state == GTA4L_EMMC_BLOCK_PARTMAP_READ_FLIGHT) &&
	    (report.partmap_read_lba == lba) &&
	    (report.partmap_read_blocks == blocks)) {
		report.partmap_read_status = (int32_t)status;
		report.partmap_read_actual = (uint64_t)actual;
		if (status != kIOReturnSuccess) {
			report.partmap_read_state =
			    GTA4L_EMMC_BLOCK_PARTMAP_READ_ERROR;
		} else if (actual != requested) {
			report.partmap_read_state =
			    GTA4L_EMMC_BLOCK_PARTMAP_READ_SHORT;
		} else {
			report.partmap_read_state =
			    GTA4L_EMMC_BLOCK_PARTMAP_READ_FULL;
		}
	}
	IOLockUnlock(reportLock);
	if ((status == kIOReturnSuccess) && (actual == requested) &&
	    (destination != buffer)) {
		memcpy(buffer, destination, (size_t)requested);
	}
	if ((status != kIOReturnSuccess) || (actual != requested)) {
		return GTA4L_EMMC_BLOCK_PARTMAP_REASON_READ;
	}
	return 0;
}

void
Gta4lEmmcBlock::closePartmapWhole()
{
	if (partmapWholeOpen && (partmapWhole != nullptr)) {
		partmapWhole->close(this, 0);
		partmapWholeOpen = false;
	}
}

void
Gta4lEmmcBlock::releasePartmapMedia()
{
	closePartmapWhole();
	if (partmapScheme != nullptr) {
		partmapScheme->release();
		partmapScheme = nullptr;
	}
	if (partmapWhole != nullptr) {
		partmapWhole->release();
		partmapWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::partmapPublishGpt()
{
	struct gta4l_emmc_block_gpt &partmapGpt = partmapScratch->gpt;

	IOLockLock(reportLock);
	report.partmap_slots = partmapGpt.slots;
	report.partmap_used = partmapGpt.used;
	report.partmap_pmbr = partmapGpt.pmbr;
	report.partmap_hdr = partmapGpt.hdr;
	report.partmap_arr = partmapGpt.arr;
	report.partmap_bak = partmapGpt.bak;
	report.partmap_xcheck = partmapGpt.xcheck;
	IOLockUnlock(reportLock);
}

void
Gta4lEmmcBlock::partmapReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.partmap_reason = reason;
	report.partmap_complete = 0;
	if (reason == GTA4L_EMMC_BLOCK_PARTMAP_REASON_BUDGET) {
		report.partmap_budget = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_PARTMAP_REASON_READ) {
		report.partmap_fails = 1;
	}
	IOLockUnlock(reportLock);
	closePartmapWhole();
	releasePartmapMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::partmapReportCancel()
{
	IOLockLock(reportLock);
	report.partmap_cancel = 1;
	report.partmap_complete = 0;
	IOLockUnlock(reportLock);
	closePartmapWhole();
	releasePartmapMedia();
	reportDone = true;
}

void
Gta4lEmmcBlock::partmapHandleRead(uint32_t status)
{
	if (status == GTA4L_EMMC_BLOCK_PARTMAP_REASON_CANCELLED) {
		partmapReportCancel();
		return;
	}
	partmapReportFailure(status);
}

IOMedia *
Gta4lEmmcBlock::findExactWholeMedia(uint32_t *count)
{
	OSDictionary *matching;
	OSIterator *iterator;
	IOMedia *found = nullptr;

	if (count == nullptr) {
		return nullptr;
	}
	*count = 0;
	matching = IOService::serviceMatching("IOMedia");
	if (matching == nullptr) {
		return nullptr;
	}
	iterator = IOService::getMatchingServices(matching);
	matching->release();
	if (iterator == nullptr) {
		return nullptr;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *service = OSDynamicCast(IOService, object);
		IOMedia *media = (service != nullptr) ?
		    static_cast<IOMedia *>(service) : nullptr;
		OSBoolean *whole;
		OSObject *baseProperty;
		OSNumber *base;
		OSNumber *size;
		OSString *content;

		if ((media == nullptr) || !ancestryContains(media)) {
			continue;
		}
		whole = OSDynamicCast(OSBoolean,
		    media->getProperty(kIOMediaWholeKey));
		baseProperty = media->getProperty(kIOMediaBaseKey);
		base = OSDynamicCast(OSNumber, baseProperty);
		size = OSDynamicCast(OSNumber,
		    media->getProperty(kIOMediaSizeKey));
		content = OSDynamicCast(OSString,
		    media->getProperty(kIOMediaContentKey));
		if ((whole == nullptr) || !whole->getValue()) {
			continue;
		}
		/*
		 * The whole media created by IOBlockStorageDriver publishes no
		 * Base property; kIOMediaBaseKey is set only by partition
		 * schemes on partition media. An absent Base on a whole media
		 * is the implied zero. A present Base must still be a zero
		 * OSNumber, so a nonzero or wrong-typed property is rejected.
		 */
		if ((baseProperty != nullptr) &&
		    ((base == nullptr) || (base->unsigned64BitValue() != 0))) {
			continue;
		}
		if ((size == nullptr) || (size->unsigned64BitValue() !=
		    (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES)) {
			continue;
		}
		if ((content == nullptr) ||
		    !gta4l_emmc_block_text_equal(
		    content->getCStringNoCopy(), "GUID_partition_scheme")) {
			continue;
		}
		(*count)++;
		if (found == nullptr) {
			found = media;
			found->retain();
		}
	}
	iterator->release();
	return found;
}

IOMedia *
Gta4lEmmcBlock::findPartitionMedia(uint64_t base, uint64_t size,
    uint32_t *count)
{
	OSDictionary *matching;
	OSIterator *iterator;
	IOMedia *found = nullptr;

	if (count == nullptr) {
		return nullptr;
	}
	*count = 0;
	matching = IOService::serviceMatching("IOMedia");
	if (matching == nullptr) {
		return nullptr;
	}
	iterator = IOService::getMatchingServices(matching);
	matching->release();
	if (iterator == nullptr) {
		return nullptr;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *service = OSDynamicCast(IOService, object);
		IOMedia *media = (service != nullptr) ?
		    static_cast<IOMedia *>(service) : nullptr;
		OSBoolean *whole;
		OSNumber *baseProperty;
		OSNumber *sizeProperty;

		if ((media == nullptr) || !ancestryContains(media)) {
			continue;
		}
		whole = OSDynamicCast(OSBoolean,
		    media->getProperty(kIOMediaWholeKey));
		if ((whole == nullptr) || whole->getValue()) {
			continue;
		}
		baseProperty = OSDynamicCast(OSNumber,
		    media->getProperty(kIOMediaBaseKey));
		sizeProperty = OSDynamicCast(OSNumber,
		    media->getProperty(kIOMediaSizeKey));
		if ((baseProperty == nullptr) || (sizeProperty == nullptr)) {
			continue;
		}
		if (baseProperty->unsigned64BitValue() != base) {
			continue;
		}
		if (sizeProperty->unsigned64BitValue() != size) {
			continue;
		}
		(*count)++;
		if (found == nullptr) {
			found = media;
			found->retain();
		}
	}
	iterator->release();
	return found;
}

IOService *
Gta4lEmmcBlock::findGuidScheme(IOMedia *whole)
{
	OSIterator *iterator;
	IOService *found = nullptr;
	uint32_t count = 0;

	if (whole == nullptr) {
		return nullptr;
	}
	/*
	 * The partition scheme is attached to the whole media as a direct
	 * service-plane child before it is probed. The pinned scheme does not
	 * register itself, so a matched-state walk cannot see it. Enumerate
	 * the selected media's own children instead; the matched-state walk
	 * would only find schemes that registered, which is not required.
	 */
	iterator = whole->getChildIterator(gIOServicePlane);
	if (iterator == nullptr) {
		return nullptr;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *scheme;
		OSString *uuid;

		/*
		 * The pinned scheme class lives in the prelinked storage
		 * kext and its metaClass is not exported to the kernel
		 * link, so OSDynamicCast cannot reference it. Match the
		 * registered class by name; the provider, UUID and
		 * exactly-one guards still constrain the result.
		 */
		if (object->metaCast("IOGUIDPartitionScheme") == nullptr) {
			continue;
		}
		scheme = static_cast<IOService *>(object);
		if (scheme->getProvider() != whole) {
			continue;
		}
		uuid = OSDynamicCast(OSString,
		    scheme->getProperty(kIOGUIDPartitionSchemeUUIDKey));
		if (uuid == nullptr) {
			continue;
		}
		count++;
		if (found == nullptr) {
			found = scheme;
			found->retain();
		}
	}
	iterator->release();
	if (count != 1u) {
		if (found != nullptr) {
			found->release();
		}
		return nullptr;
	}
	return found;
}

uint32_t
Gta4lEmmcBlock::snapshotPartitionMedia(IOService *scheme, IOMedia *whole,
    uint32_t *propErrors, uint32_t *nested)
{
	OSDictionary *matching;
	OSIterator *iterator;
	uint32_t count = 0;
	uint64_t wholeSize =
	    (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	struct gta4l_emmc_block_partmap_media
	    (&partmapMedia)[GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA] =
	    partmapScratch->media;

	if ((propErrors == nullptr) || (nested == nullptr)) {
		return 0;
	}
	*propErrors = 0;
	*nested = 0;
	matching = IOService::serviceMatching("IOMedia");
	if (matching == nullptr) {
		*propErrors = 1;
		return 0;
	}
	iterator = IOService::getMatchingServices(matching);
	matching->release();
	if (iterator == nullptr) {
		*propErrors = 1;
		return 0;
	}
	while (OSObject *object = iterator->getNextObject()) {
		IOService *service = OSDynamicCast(IOService, object);
		IOMedia *media = (service != nullptr) ?
		    static_cast<IOMedia *>(service) : nullptr;
		OSBoolean *wholeProperty;
		OSNumber *baseProperty;
		OSNumber *sizeProperty;
		OSString *contentProperty;
		uint64_t baseValue;
		uint64_t sizeValue;

		if ((media == nullptr) || !ancestryContains(media)) {
			continue;
		}
		if (media == whole) {
			continue;
		}
		if (media->getProvider() != scheme) {
			(*nested)++;
			continue;
		}
		wholeProperty = OSDynamicCast(OSBoolean,
		    media->getProperty(kIOMediaWholeKey));
		baseProperty = OSDynamicCast(OSNumber,
		    media->getProperty(kIOMediaBaseKey));
		sizeProperty = OSDynamicCast(OSNumber,
		    media->getProperty(kIOMediaSizeKey));
		contentProperty = OSDynamicCast(OSString,
		    media->getProperty(kIOMediaContentKey));
		if ((wholeProperty == nullptr) || wholeProperty->getValue() ||
		    (baseProperty == nullptr) || (sizeProperty == nullptr) ||
		    (contentProperty == nullptr)) {
			if (*propErrors < 255u) {
				(*propErrors)++;
			}
			continue;
		}
		baseValue = baseProperty->unsigned64BitValue();
		sizeValue = sizeProperty->unsigned64BitValue();
		if (gta4l_emmc_block_partmap_media_check(baseValue, sizeValue,
		    wholeSize) != 0) {
			if (*propErrors < 255u) {
				(*propErrors)++;
			}
			continue;
		}
		if (count >= GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA) {
			if (*propErrors < 255u) {
				(*propErrors)++;
			}
			continue;
		}
		partmapMedia[count].base = baseValue;
		partmapMedia[count].size = sizeValue;
		count++;
	}
	iterator->release();
	return count;
}

uint64_t
Gta4lEmmcBlock::partreadElapsedMs()
{
	uint64_t now = gta4l_emmc_platform_counter();
	uint64_t hz = gta4l_emmc_platform_counter_hz();
	uint64_t ticks_per_ms;

	if (hz == 0) {
		return 0;
	}
	ticks_per_ms = hz / 1000u;
	if (ticks_per_ms == 0) {
		ticks_per_ms = 1;
	}
	return (now - partreadReadStart) / ticks_per_ms;
}

void
Gta4lEmmcBlock::releasePartreadMedia()
{
	if (partreadBootOpen && (partreadBoot != nullptr)) {
		partreadBoot->close(this, 0);
		partreadBootOpen = false;
	}
	if (partreadBoot != nullptr) {
		partreadBoot->release();
		partreadBoot = nullptr;
	}
	if (partreadWholeOpen && (partreadWhole != nullptr)) {
		partreadWhole->close(this, 0);
		partreadWholeOpen = false;
	}
	if (partreadWhole != nullptr) {
		partreadWhole->release();
		partreadWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::partreadReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.partread_reason = reason;
	report.partread_complete = 0;
	if (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA) {
		report.partread_media = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART) {
		report.partread_part = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_BUDGET) {
		report.partread_budget = 1;
	}
	IOLockUnlock(reportLock);
	releasePartreadMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::partreadReportCancel()
{
	IOLockLock(reportLock);
	report.partread_cancel = 1;
	report.partread_complete = 0;
	IOLockUnlock(reportLock);
	releasePartreadMedia();
	reportDone = true;
}

uint64_t
Gta4lEmmcBlock::deblockElapsedMs()
{
	uint64_t now = gta4l_emmc_platform_counter();
	uint64_t hz = gta4l_emmc_platform_counter_hz();
	uint64_t ticks_per_ms;

	if (hz == 0) {
		return 0;
	}
	ticks_per_ms = hz / 1000u;
	if (ticks_per_ms == 0) {
		ticks_per_ms = 1;
	}
	return (now - deblockReadStart) / ticks_per_ms;
}

void
Gta4lEmmcBlock::releaseDeblockMedia()
{
	if (deblockBootOpen && (deblockBoot != nullptr)) {
		deblockBoot->close(this, 0);
		deblockBootOpen = false;
	}
	if (deblockBoot != nullptr) {
		deblockBoot->release();
		deblockBoot = nullptr;
	}
	if (deblockWholeOpen && (deblockWhole != nullptr)) {
		deblockWhole->close(this, 0);
		deblockWholeOpen = false;
	}
	if (deblockWhole != nullptr) {
		deblockWhole->release();
		deblockWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::deblockReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.deblock_reason = reason;
	report.deblock_complete = 0;
	if (reason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA) {
		report.deblock_media = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_PART) {
		report.deblock_part = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_BUDGET) {
		report.deblock_budget = 1;
	}
	IOLockUnlock(reportLock);
	releaseDeblockMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::deblockReportCancel()
{
	IOLockLock(reportLock);
	report.deblock_cancel = 1;
	report.deblock_reason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_CANCEL;
	report.deblock_complete = 0;
	IOLockUnlock(reportLock);
	releaseDeblockMedia();
	reportDone = true;
}

void
Gta4lEmmcBlock::runPartitionRead()
{
	uint32_t wholeCount = 0;
	uint32_t partCount = 0;
	uint32_t waited = 0;
	uint32_t index;
	uint32_t cases;
	uint32_t passed = 0;
	uint32_t failed = 0;
	uint32_t reads = 0;
	uint32_t blocks = 0;
	uint32_t bytes = 0;
	uint32_t fails = 0;
	uint32_t reason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE;
	uint32_t complete;
	uint32_t budget = 0;
	uint32_t writable;
	uint64_t wholeSize;
	OSBoolean *writableProperty;
	OSString *contentProperty;
	int check;
	uint8_t digest[32];

	bzero(digest, sizeof(digest));
	partreadWhole = nullptr;
	partreadBoot = nullptr;
	partreadWholeOpen = false;
	partreadBootOpen = false;
	IOLockLock(reportLock);
	report.partread_cases = gta4l_emmc_block_partread_case_count();
	report.partread_reason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA;
	report.partread_capacity = capacityBlocks;
	IOLockUnlock(reportLock);

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			partreadReportCancel();
			return;
		}
		wholeCount = 0;
		partreadWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			partreadReportFailure(
			    GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA);
			return;
		}
		if ((partreadWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (partreadWhole != nullptr) {
			partreadWhole->release();
			partreadWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (partreadWhole == nullptr) {
		partreadReportFailure(GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    partreadWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    partreadWhole->getProperty(kIOMediaContentKey));
	writable = ((writableProperty != nullptr) &&
	    writableProperty->getValue()) ? 1u : 0u;
	copyContent(mediaContent, (contentProperty != nullptr) ?
	    contentProperty->getCStringNoCopy() : nullptr);
	IOLockLock(reportLock);
	report.media_valid = 1;
	report.whole = 1;
	report.base = 0;
	report.size = wholeSize;
	report.writable = writable;
	report.content = mediaContent;
	report.partread_capacity = capacityBlocks;
	IOLockUnlock(reportLock);
	check = gta4l_emmc_block_media_check(0, wholeSize, capacityBlocks, 1,
	    (int)writable);
	if (check != 0) {
		partreadReportFailure(GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA);
		return;
	}
	if (!partreadWhole->open(this, 0, kIOStorageAccessReader)) {
		partreadReportFailure(GTA4L_EMMC_BLOCK_PARTREAD_REASON_OPEN);
		return;
	}
	partreadWholeOpen = true;
	partreadReadStart = gta4l_emmc_platform_counter();

	cases = gta4l_emmc_block_partread_case_count();
	for (index = 0; index < cases; index++) {
		const struct gta4l_emmc_block_partread_case *item =
		    gta4l_emmc_block_partread_case_at(index);
		IOMedia *media = nullptr;
		IOMemoryDescriptor *descriptor;
		UInt64 actual = 0;
		uint64_t offsetBytes = 0;
		uint32_t blockCount = 0;
		uint32_t expectedBytes;
		uint32_t caseReason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE;
		uint32_t ok = 0;
		int32_t caseStatus = 0;
		uint64_t caseActual = 0;
		IOReturn status;

		bzero(digest, sizeof(digest));
		if (reportCancel) {
			partreadReportCancel();
			return;
		}
		if (partreadElapsedMs() >=
		    GTA4L_EMMC_BLOCK_PARTREAD_READ_BUDGET_MS) {
			budget = 1;
			break;
		}
		if (item == nullptr) {
			caseReason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
		} else {
			blockCount = item->blocks;
			if ((blockCount == 0) || (blockCount >
			    GTA4L_EMMC_BLOCK_PARTREAD_MAX_BLOCKS)) {
				caseReason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
			}
		}
		if ((item != nullptr) &&
		    (caseReason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE)) {
			if (item->target ==
			    GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE) {
				media = partreadWhole;
				if (item->from_end_blocks != 0u) {
					if ((uint64_t)item->from_end_blocks >
					    (uint64_t)capacityBlocks) {
						caseReason =
						    GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
					} else {
						offsetBytes =
						    ((uint64_t)capacityBlocks -
						    item->from_end_blocks) *
						    GTA4L_EMMC_BLOCK_BYTES;
					}
				} else {
					offsetBytes = item->offset;
				}
				if ((caseReason ==
				    GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) &&
				    (((offsetBytes %
				    GTA4L_EMMC_BLOCK_BYTES) != 0u) ||
				    (offsetBytes > wholeSize) ||
				    ((uint64_t)blockCount *
				    GTA4L_EMMC_BLOCK_BYTES >
				    wholeSize - offsetBytes))) {
					caseReason =
					    GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
				}
			} else if (item->target ==
			    GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT) {
				if (partreadBoot == nullptr) {
					uint32_t waitPart = 0;

					while (waitPart <
					    GTA4L_EMMC_BLOCK_PARTREAD_DEADLINE_MS) {
						if (reportCancel) {
							partreadReportCancel();
							return;
						}
						partCount = 0;
						partreadBoot = findPartitionMedia(
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
						    &partCount);
						if (partCount > 1u) {
							partreadReportFailure(
							    GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART);
							return;
						}
						if ((partreadBoot != nullptr) &&
						    (partCount == 1u)) {
							break;
						}
						if (partreadBoot != nullptr) {
							partreadBoot->release();
							partreadBoot = nullptr;
						}
						IOSleep(GTA4L_EMMC_BLOCK_PARTREAD_STEP_MS);
						waitPart +=
						    GTA4L_EMMC_BLOCK_PARTREAD_STEP_MS;
					}
					if (partreadBoot == nullptr) {
						partreadReportFailure(
						    GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART);
						return;
					}
					if (!partreadBoot->open(this, 0,
					    kIOStorageAccessReader)) {
						partreadReportFailure(
						    GTA4L_EMMC_BLOCK_PARTREAD_REASON_OPEN);
						return;
					}
					partreadBootOpen = true;
				}
				media = partreadBoot;
				offsetBytes = item->offset;
				if (((offsetBytes %
				    GTA4L_EMMC_BLOCK_BYTES) != 0u) ||
				    (offsetBytes >
				    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) ||
				    ((uint64_t)blockCount *
				    GTA4L_EMMC_BLOCK_BYTES >
				    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES -
				    offsetBytes)) {
					caseReason =
					    GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
				}
			} else {
				caseReason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG;
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) {
			expectedBytes = blockCount * GTA4L_EMMC_BLOCK_BYTES;
			descriptor = IOMemoryDescriptor::withAddress(
			    partreadBuffer, expectedBytes, kIODirectionIn);
			if (descriptor == nullptr) {
				caseReason =
				    GTA4L_EMMC_BLOCK_PARTREAD_REASON_DESC;
			} else {
				status = media->read(this, offsetBytes,
				    descriptor, nullptr, &actual);
				descriptor->release();
				reads++;
				blocks += blockCount;
				bytes += (actual > 0xffffffffu) ?
				    0xffffffffu : (uint32_t)actual;
				caseStatus = (int32_t)status;
				caseActual = actual;
				if (status != kIOReturnSuccess) {
					caseReason =
					    GTA4L_EMMC_BLOCK_PARTREAD_REASON_READ;
					fails++;
				} else if (actual != expectedBytes) {
					caseReason =
					    GTA4L_EMMC_BLOCK_PARTREAD_REASON_SHORT;
					fails++;
				} else {
					gta4l_emmc_block_sha256(
					    partreadBuffer, expectedBytes,
					    digest);
					if (memcmp(digest, item->expected,
					    sizeof(digest)) != 0) {
						caseReason =
						    GTA4L_EMMC_BLOCK_PARTREAD_REASON_HASH;
					} else {
						ok = 1;
					}
				}
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) {
			passed++;
		} else {
			failed++;
			if (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) {
				reason = caseReason;
				IOLockLock(reportLock);
				report.partread_fail_index = index;
				report.partread_fail_reason = caseReason;
				report.partread_fail_status = caseStatus;
				report.partread_fail_actual = caseActual;
				memcpy(report.partread_fail_digest, digest,
				    sizeof(report.partread_fail_digest));
				IOLockUnlock(reportLock);
			}
		}
		IOLockLock(reportLock);
		report.partread_passed = passed;
		report.partread_failed = failed;
		report.partread_reads = reads;
		report.partread_blocks = blocks;
		report.partread_bytes = bytes;
		report.partread_fails = fails;
		report.partread_last_index = index;
		report.partread_last_target =
		    (item != nullptr) ? item->target : 0u;
		report.partread_last_blocks = blockCount;
		report.partread_last_offset = offsetBytes;
		report.partread_last_ok = ok;
		report.partread_last_status = caseStatus;
		report.partread_last_actual = caseActual;
		if (ok != 0) {
			memcpy(report.partread_last_digest, digest,
			    sizeof(report.partread_last_digest));
		} else {
			bzero(report.partread_last_digest,
			    sizeof(report.partread_last_digest));
		}
		IOLockUnlock(reportLock);
	}

	complete = ((budget == 0) &&
	    (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) &&
	    (failed == 0) && (passed == cases) && (cases != 0u)) ? 1u : 0u;
	IOLockLock(reportLock);
	report.partread_cases = cases;
	report.partread_passed = passed;
	report.partread_failed = failed;
	report.partread_reason = (reason !=
	    GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) ? reason :
	    ((budget != 0) ? GTA4L_EMMC_BLOCK_PARTREAD_REASON_BUDGET :
	    GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE);
	report.partread_complete = complete;
	report.partread_reads = reads;
	report.partread_blocks = blocks;
	report.partread_bytes = bytes;
	report.partread_fails = fails;
	if (budget != 0) {
		report.partread_budget = 1;
	}
	IOLockUnlock(reportLock);
	releasePartreadMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::runDeblock()
{
	uint32_t wholeCount = 0;
	uint32_t partCount = 0;
	uint32_t waited = 0;
	uint32_t index;
	uint32_t cases;
	uint32_t passed = 0;
	uint32_t failed = 0;
	uint32_t reads = 0;
	uint32_t blocks = 0;
	uint32_t bytes = 0;
	uint32_t span = 0;
	uint32_t fails = 0;
	uint32_t reason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE;
	uint32_t complete;
	uint32_t budget = 0;
	uint32_t writable;
	uint64_t wholeSize;
	OSBoolean *writableProperty;
	OSString *contentProperty;
	int check;
	uint8_t digest[32];

	bzero(digest, sizeof(digest));
	deblockWhole = nullptr;
	deblockBoot = nullptr;
	deblockWholeOpen = false;
	deblockBootOpen = false;
	IOLockLock(reportLock);
	report.deblock_cases = gta4l_emmc_block_deblock_case_count();
	report.deblock_reason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA;
	report.deblock_capacity = capacityBlocks;
	report.deblock_segments = GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS;
	IOLockUnlock(reportLock);

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			deblockReportCancel();
			return;
		}
		wholeCount = 0;
		deblockWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			deblockReportFailure(
			    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA);
			return;
		}
		if ((deblockWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (deblockWhole != nullptr) {
			deblockWhole->release();
			deblockWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (deblockWhole == nullptr) {
		deblockReportFailure(GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    deblockWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    deblockWhole->getProperty(kIOMediaContentKey));
	writable = ((writableProperty != nullptr) &&
	    writableProperty->getValue()) ? 1u : 0u;
	copyContent(mediaContent, (contentProperty != nullptr) ?
	    contentProperty->getCStringNoCopy() : nullptr);
	IOLockLock(reportLock);
	report.media_valid = 1;
	report.whole = 1;
	report.base = 0;
	report.size = wholeSize;
	report.writable = writable;
	report.content = mediaContent;
	report.deblock_capacity = capacityBlocks;
	IOLockUnlock(reportLock);
	check = gta4l_emmc_block_media_check(0, wholeSize, capacityBlocks, 1,
	    (int)writable);
	if (check != 0) {
		deblockReportFailure(GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA);
		return;
	}
	if (!deblockWhole->open(this, 0, kIOStorageAccessReader)) {
		deblockReportFailure(GTA4L_EMMC_BLOCK_DEBLOCK_REASON_OPEN);
		return;
	}
	deblockWholeOpen = true;
	deblockReadStart = gta4l_emmc_platform_counter();

	cases = gta4l_emmc_block_deblock_case_count();
	for (index = 0; index < cases; index++) {
		const struct gta4l_emmc_block_deblock_case *item =
		    gta4l_emmc_block_deblock_case_at(index);
		IOMedia *media = nullptr;
		IOMemoryDescriptor *descriptor;
		UInt64 actual = 0;
		uint64_t start = 0;
		uint64_t end;
		uint64_t alignedSpan = 0;
		uint64_t limit = 0;
		uint32_t length = 0;
		uint32_t caseSpan = 0;
		uint32_t caseReason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE;
		uint32_t ok = 0;
		int32_t caseStatus = 0;
		uint64_t caseActual = 0;
		IOReturn status;

		bzero(digest, sizeof(digest));
		if (reportCancel) {
			deblockReportCancel();
			return;
		}
		if (deblockElapsedMs() >=
		    GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS) {
			budget = 1;
			break;
		}
		if (item == nullptr) {
			caseReason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;
		} else {
			start = item->start;
			length = item->length;
			if ((length == 0u) ||
			    (length > GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;
			}
		}
		if ((item != nullptr) &&
		    (caseReason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE)) {
			if (item->target ==
			    GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE) {
				media = deblockWhole;
				limit = wholeSize;
			} else if (item->target ==
			    GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT) {
				if (deblockBoot == nullptr) {
					uint32_t waitPart = 0;

					while (waitPart <
					    GTA4L_EMMC_BLOCK_DEBLOCK_DEADLINE_MS) {
						if (reportCancel) {
							deblockReportCancel();
							return;
						}
						partCount = 0;
						deblockBoot = findPartitionMedia(
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
						    &partCount);
						if (partCount > 1u) {
							deblockReportFailure(
							    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_PART);
							return;
						}
						if ((deblockBoot != nullptr) &&
						    (partCount == 1u)) {
							break;
						}
						if (deblockBoot != nullptr) {
							deblockBoot->release();
							deblockBoot = nullptr;
						}
						IOSleep(GTA4L_EMMC_BLOCK_DEBLOCK_STEP_MS);
						waitPart +=
						    GTA4L_EMMC_BLOCK_DEBLOCK_STEP_MS;
					}
					if (deblockBoot == nullptr) {
						deblockReportFailure(
						    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_PART);
						return;
					}
					if (!deblockBoot->open(this, 0,
					    kIOStorageAccessReader)) {
						deblockReportFailure(
						    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_OPEN);
						return;
					}
					deblockBootOpen = true;
				}
				media = deblockBoot;
				limit =
				    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES;
			} else {
				caseReason =
				    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;
			}
		}
		if (caseReason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) {
			if ((start > limit) ||
			    ((uint64_t)length > limit - start)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;
			} else {
				end = start + length;
				alignedSpan =
				    (((end +
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES - 1u) /
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES) *
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES) -
				    ((start /
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES) *
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES);
				if ((alignedSpan == 0u) ||
				    (alignedSpan >
				    GTA4L_EMMC_BLOCK_MAX_BYTES)) {
					caseReason =
					    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;
				} else {
					caseSpan = (uint32_t)alignedSpan;
				}
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) {
			descriptor = IOMemoryDescriptor::withAddress(
			    deblockBuffer, length, kIODirectionIn);
			if (descriptor == nullptr) {
				caseReason =
				    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_DESC;
			} else {
				status = media->read(this, start,
				    descriptor, nullptr, &actual);
				descriptor->release();
				reads++;
				span += caseSpan;
				blocks += caseSpan /
				    GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES;
				bytes += length;
				caseStatus = (int32_t)status;
				caseActual = actual;
				if (status != kIOReturnSuccess) {
					caseReason =
					    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_READ;
					fails++;
				} else if (actual != length) {
					caseReason =
					    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_SHORT;
					fails++;
				} else {
					gta4l_emmc_block_sha256(
					    deblockBuffer, length, digest);
					if (memcmp(digest, item->expected,
					    sizeof(digest)) != 0) {
						caseReason =
						    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_HASH;
					} else {
						ok = 1;
					}
				}
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) {
			passed++;
		} else {
			failed++;
			if (reason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) {
				reason = caseReason;
				IOLockLock(reportLock);
				report.deblock_fail_index = index;
				report.deblock_fail_reason = caseReason;
				report.deblock_fail_status = caseStatus;
				report.deblock_fail_actual = caseActual;
				memcpy(report.deblock_fail_digest, digest,
				    sizeof(report.deblock_fail_digest));
				IOLockUnlock(reportLock);
			}
		}
		IOLockLock(reportLock);
		report.deblock_passed = passed;
		report.deblock_failed = failed;
		report.deblock_reads = reads;
		report.deblock_blocks = blocks;
		report.deblock_bytes = bytes;
		report.deblock_span = span;
		report.deblock_fails = fails;
		report.deblock_last_index = index;
		report.deblock_last_target =
		    (item != nullptr) ? item->target : 0u;
		report.deblock_last_start = start;
		report.deblock_last_length = length;
		report.deblock_last_span = caseSpan;
		report.deblock_last_status = caseStatus;
		report.deblock_last_actual = caseActual;
		if (ok != 0) {
			memcpy(report.deblock_last_digest, digest,
			    sizeof(report.deblock_last_digest));
		} else {
			bzero(report.deblock_last_digest,
			    sizeof(report.deblock_last_digest));
		}
		IOLockUnlock(reportLock);
	}

	if (reportCancel) {
		deblockReportCancel();
		return;
	}

	IOLockLock(reportLock);
	report.deblock_cases = cases;
	report.deblock_passed = passed;
	report.deblock_failed = failed;
	report.deblock_reason = (reason !=
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) ? reason :
	    ((budget != 0) ? GTA4L_EMMC_BLOCK_DEBLOCK_REASON_BUDGET :
	    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE);
	report.deblock_reads = reads;
	report.deblock_blocks = blocks;
	report.deblock_bytes = bytes;
	report.deblock_span = span;
	report.deblock_fails = fails;
	if (budget != 0) {
		report.deblock_budget = 1;
	}
	complete = ((report.deblock_cases != 0u) &&
	    (report.deblock_passed == report.deblock_cases) &&
	    (report.deblock_failed == 0u) &&
	    (report.deblock_reason == GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE) &&
	    (report.deblock_cancel == 0u) &&
	    (report.deblock_budget == 0u) &&
	    (report.deblock_media == 0u) &&
	    (report.deblock_part == 0u) &&
	    (report.deblock_segments ==
	    GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS)) ? 1u : 0u;
	report.deblock_complete = complete;
	IOLockUnlock(reportLock);
	releaseDeblockMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::runPartitionComparison()
{
	uint32_t wholeCount = 0;
	uint32_t waited = 0;
	uint32_t status;
	uint64_t entryLba;
	uint32_t slots;
	uint32_t arrayBlocks;
	uint32_t offset;
	uint32_t chunk;
	uint32_t prop;
	uint32_t nested;
	uint32_t nestedSeen = 0;
	uint32_t propSeen = 0;
	uint32_t count = 0;
	uint32_t previousCount = 0;
	uint32_t samples = 0;
	uint32_t changes = 0;
	uint32_t stable = 0;
	uint32_t stableMs = 0;
	uint32_t lastChangeMs = 0;
	uint32_t elapsed = 0;
	uint32_t timedOut = 0;
	uint32_t match = 0;
	uint32_t missing = 0;
	uint32_t extra = 0;
	uint32_t dup = 0;
	uint32_t changed;
	uint32_t complete;
	uint32_t reason;
	uint64_t wholeSize;
	OSBoolean *writableProperty;
	OSString *contentProperty;
	uint8_t *const partmapArray = partmapScratch->array;
	struct gta4l_emmc_block_gpt &partmapGpt = partmapScratch->gpt;
	struct gta4l_emmc_block_gpt &partmapBackup = partmapScratch->backup;
	struct gta4l_emmc_block_partmap_media
	    (&partmapMedia)[GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA] =
	    partmapScratch->media;
	struct gta4l_emmc_block_partmap_media
	    (&partmapPrevious)[GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA] =
	    partmapScratch->previous;

	bzero(&partmapGpt, sizeof(partmapGpt));
	bzero(&partmapBackup, sizeof(partmapBackup));
	bzero(partmapMedia, sizeof(partmapMedia));
	bzero(partmapPrevious, sizeof(partmapPrevious));
	bzero(partmapPmbr, sizeof(partmapPmbr));
	bzero(partmapHeader, sizeof(partmapHeader));
	partmapWhole = nullptr;
	partmapScheme = nullptr;
	partmapWholeOpen = false;

	report.partmap = 1;
	report.partmap_deadline_ms = GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS;
	report.partmap_complete = 0;
	report.partmap_reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA;

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			partmapReportCancel();
			return;
		}
		partmapWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			partmapReportFailure(
			    GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA);
			return;
		}
		if ((partmapWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (partmapWhole != nullptr) {
			partmapWhole->release();
			partmapWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (partmapWhole == nullptr) {
		partmapReportFailure(GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    partmapWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    partmapWhole->getProperty(kIOMediaContentKey));
	report.media_valid = 1;
	report.whole = 1;
	report.base = 0;
	report.size = wholeSize;
	report.writable = ((writableProperty != nullptr) &&
	    writableProperty->getValue()) ? 1u : 0u;
	copyContent(mediaContent, (contentProperty != nullptr) ?
	    contentProperty->getCStringNoCopy() : nullptr);
	report.content = mediaContent;

	partmapScheme = findGuidScheme(partmapWhole);
	if (partmapScheme == nullptr) {
		partmapReportFailure(GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA);
		return;
	}
	if (!partmapWhole->open(this, 0, kIOStorageAccessReader)) {
		partmapReportFailure(GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA);
		return;
	}
	partmapWholeOpen = true;

	partmapReadStart = gta4l_emmc_platform_counter();
	status = readPartmapSectors(partmapWhole, 0, 1, partmapPmbr);
	if (status != 0) {
		partmapHandleRead(status);
		return;
	}
	status = readPartmapSectors(partmapWhole, 1, 1, partmapHeader);
	if (status != 0) {
		partmapHandleRead(status);
		return;
	}
	if (gta4l_emmc_block_gpt_geometry(partmapHeader, capacityBlocks, 1,
	    &entryLba, &slots) == 0) {
		partmapReportFailure(
		    GTA4L_EMMC_BLOCK_PARTMAP_REASON_HEADER);
		return;
	}
	arrayBlocks = ((slots * 128u) + 511u) / 512u;
	for (offset = 0; offset < arrayBlocks;
	    offset += GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS) {
		chunk = arrayBlocks - offset;
		if (chunk > GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS) {
			chunk = GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS;
		}
		status = readPartmapSectors(partmapWhole,
		    (uint64_t)entryLba + offset, chunk,
		    partmapArray + (offset * GTA4L_EMMC_BLOCK_BYTES));
		if (status != 0) {
			partmapHandleRead(status);
			return;
		}
	}
	if (gta4l_emmc_block_gpt_primary(partmapPmbr, partmapHeader,
	    partmapArray, capacityBlocks, &partmapGpt) == 0) {
		partmapPublishGpt();
		partmapReportFailure(partmapGpt.reason);
		return;
	}
	partmapPublishGpt();

	status = readPartmapSectors(partmapWhole,
	    (uint64_t)capacityBlocks - 1u, 1, partmapHeader);
	if (status != 0) {
		partmapHandleRead(status);
		return;
	}
	if (gta4l_emmc_block_gpt_geometry(partmapHeader, capacityBlocks, 0,
	    &entryLba, &slots) == 0) {
		partmapReportFailure(
		    GTA4L_EMMC_BLOCK_PARTMAP_REASON_BACKUP_HEADER);
		return;
	}
	arrayBlocks = ((slots * 128u) + 511u) / 512u;
	for (offset = 0; offset < arrayBlocks;
	    offset += GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS) {
		chunk = arrayBlocks - offset;
		if (chunk > GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS) {
			chunk = GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS;
		}
		status = readPartmapSectors(partmapWhole,
		    (uint64_t)entryLba + offset, chunk,
		    partmapArray + (offset * GTA4L_EMMC_BLOCK_BYTES));
		if (status != 0) {
			partmapHandleRead(status);
			return;
		}
	}
	if (gta4l_emmc_block_gpt_backup(partmapHeader, partmapArray,
	    capacityBlocks, &partmapGpt, &partmapBackup) == 0) {
		partmapPublishGpt();
		partmapReportFailure(partmapGpt.reason);
		return;
	}
	partmapPublishGpt();
	closePartmapWhole();
	IOLockLock(reportLock);
	report.partmap_gpt = 1;
	report.partmap_reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_NONE;
	IOLockUnlock(reportLock);

	for (;;) {
		if (reportCancel) {
			partmapReportCancel();
			return;
		}
		prop = 0;
		nested = 0;
		count = snapshotPartitionMedia(partmapScheme, partmapWhole,
		    &prop, &nested);
		samples++;
		if (nested != 0) {
			nestedSeen = 1;
		}
		if (prop != 0) {
			propSeen = prop;
			IOLockLock(reportLock);
			report.partmap_prop = propSeen;
			report.partmap_nested = nestedSeen;
			report.partmap_media = count;
			report.partmap_samples = samples;
			IOLockUnlock(reportLock);
			partmapReportFailure(
			    GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA);
			return;
		}
		match = gta4l_emmc_block_partmap_compare(&partmapGpt,
		    partmapMedia, count, &missing, &extra, &dup);
		changed = (samples == 1u) ||
		    (count != previousCount) ||
		    (memcmp(partmapMedia, partmapPrevious,
		    (size_t)count *
		    sizeof(struct gta4l_emmc_block_partmap_media)) != 0);
		if (changed != 0) {
			if (samples > 1u) {
				changes++;
			}
			lastChangeMs = elapsed;
			previousCount = count;
			if (count != 0) {
				memcpy(partmapPrevious, partmapMedia,
				    (size_t)count *
				    sizeof(struct
				    gta4l_emmc_block_partmap_media));
			}
		}
		stableMs = elapsed - lastChangeMs;
		stable = (stableMs >=
		    GTA4L_EMMC_BLOCK_PARTMAP_STABLE_MS) ? 1u : 0u;
		IOLockLock(reportLock);
		report.partmap_media = count;
		report.partmap_match = match;
		report.partmap_missing = missing;
		report.partmap_extra = extra;
		report.partmap_dup = dup;
		report.partmap_nested = nestedSeen;
		report.partmap_prop = propSeen;
		report.partmap_samples = samples;
		report.partmap_changes = changes;
		report.partmap_stable = stable;
		report.partmap_stable_ms = stableMs;
		report.partmap_timed_out = 0;
		IOLockUnlock(reportLock);
		if ((stable != 0) && (match != 0)) {
			break;
		}
		if (elapsed >= GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS) {
			if (stable == 0) {
				timedOut = 1;
				match = 0;
			}
			break;
		}
		IOSleep(GTA4L_EMMC_BLOCK_PARTMAP_STEP_MS);
		elapsed += GTA4L_EMMC_BLOCK_PARTMAP_STEP_MS;
		if (elapsed > GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS) {
			elapsed = GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS;
		}
	}

	if (timedOut != 0) {
		reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_CHANGING;
	} else if (match != 0) {
		reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_NONE;
	} else {
		reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_MISMATCH;
	}
	complete = ((report.partmap_gpt != 0) && (match != 0) &&
	    (stable != 0) && (timedOut == 0) &&
	    (report.partmap_budget == 0) && (report.partmap_cancel == 0) &&
	    (report.partmap_fails == 0) && (propSeen == 0) &&
	    (dup == 0) && (missing == 0) && (extra == 0) &&
	    (count == partmapGpt.used)) ? 1u : 0u;
	IOLockLock(reportLock);
	report.partmap_reason = reason;
	report.partmap_media = count;
	report.partmap_match = match;
	report.partmap_missing = missing;
	report.partmap_extra = extra;
	report.partmap_dup = dup;
	report.partmap_nested = nestedSeen;
	report.partmap_prop = propSeen;
	report.partmap_samples = samples;
	report.partmap_changes = changes;
	report.partmap_stable = stable;
	report.partmap_stable_ms = stableMs;
	report.partmap_timed_out = timedOut;
	report.partmap_complete = complete;
	IOLockUnlock(reportLock);
	releasePartmapMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::panicWithReport()
{
	uint32_t length;

	if (!claimTerminal()) {
		return;
	}
	fillDriverReport();
	/* Reporting is complete only after the coherent snapshot has been
	 * taken. The watchdog defers while this terminal owner is active. */
	reportDone = true;
	length = gta4l_emmc_block_report_format(reportBuffer,
	    sizeof(reportBuffer), &report);
	if (length == 0) {
		panic("GTA4L-EMMC P5 REPORT: format failed");
		return;
	}
	panic("%s", reportBuffer);
}

bool
Gta4lEmmcBlock::readQualificationReference()
{
	uint32_t index;

	qualReferenceValid = false;
	IOLockLock(gate);
	for (index = 0; index < GTA4L_EMMC_BLOCK_QUAL_REF_BLOCKS; index++) {
		if (gta4l_emmc_read_block(&host,
		    (uint64_t)GTA4L_EMMC_BLOCK_QUAL_LBA + index,
		    qualReference + (index * GTA4L_EMMC_BLOCK_BYTES)) != 0) {
			IOLockUnlock(gate);
			return false;
		}
	}
	IOLockUnlock(gate);
	qualReferenceValid = true;
	return true;
}

void
Gta4lEmmcBlock::qualifyMediaRead(IOMedia *media, uint32_t blocks,
    struct qual_result *out)
{
	IOMemoryDescriptor *descriptor;
	struct qual_capture capture;
	UInt64 actual = 0;
	IOReturn status;
	uint32_t expected_bytes = blocks * GTA4L_EMMC_BLOCK_BYTES;
	uint32_t expected_cmd17;
	bool shape_ok;

	bzero(out, sizeof(*out));
	if (!qualReferenceValid) {
		out->reason = 1;
		return;
	}
	descriptor = IOMemoryDescriptor::withAddress(qualRead, expected_bytes,
	    kIODirectionIn);
	if (descriptor == nullptr) {
		out->reason = 2;
		return;
	}
	IOLockLock(gate);
	bzero(&qualCapture, sizeof(qualCapture));
	qualCapture.pending = true;
	qualCapture.block = GTA4L_EMMC_BLOCK_QUAL_LBA;
	qualCapture.nblks = blocks;
	qualCapture.bytes = expected_bytes;
	IOLockUnlock(gate);

	out->attempted = 1;
	/* media->read() re-enters the adapter, which takes the controller
	 * gate itself. Never hold the gate across this call. */
	status = media->read(this,
	    (UInt64)GTA4L_EMMC_BLOCK_QUAL_LBA * GTA4L_EMMC_BLOCK_BYTES,
	    descriptor, nullptr, &actual);
	descriptor->release();

	IOLockLock(gate);
	capture = qualCapture;
	bzero(&qualCapture, sizeof(qualCapture));
	IOLockUnlock(gate);

	out->bytes = (actual > 0xffffffffu) ? 0xffffffffu : (uint32_t)actual;
	out->matches = capture.matches;
	if (capture.mismatch_valid != 0) {
		out->mismatch_valid = 1;
		out->mismatch_offset = capture.mismatch_offset;
	}
	out->cmd18 = capture.cmd18;
	out->cmd17 = capture.cmd17;
	out->stops = capture.stops;
	out->stop_failures = capture.stop_failures;

	if (capture.ambiguous) {
		out->reason = 5;
		return;
	}
	if (!capture.captured) {
		out->reason = 2;
		return;
	}
	if ((status != kIOReturnSuccess) || (actual != expected_bytes) ||
	    (capture.result != GTA4L_EMMC_BLOCK_OK) ||
	    (capture.completed != expected_bytes) ||
	    (capture.copied != expected_bytes)) {
		out->reason = 2;
		return;
	}
	if ((capture.mismatch_valid != 0) ||
	    (capture.matches != expected_bytes)) {
		out->reason = 3;
		return;
	}
	expected_cmd17 = (blocks > GTA4L_EMMC_BLOCK_CHUNK_BLOCKS) ? 1u : 0u;
	shape_ok = (capture.cmd18 == 1u) &&
	    (capture.cmd17 == expected_cmd17) &&
	    (capture.cmd18_blocks == GTA4L_EMMC_BLOCK_CHUNK_BLOCKS) &&
	    (capture.stops == 1u) &&
	    (capture.stop_failures == 0u) &&
	    (capture.last_stop_result == 0);
	if (!shape_ok) {
		out->reason = 4;
		return;
	}
	out->ok = 1;
	out->reason = 0;
}

void
Gta4lEmmcBlock::runPartitionObservation()
{
	uint32_t samples = 0;
	uint32_t first = 0;
	uint32_t firstMs = 0;
	uint32_t firstNz = 0;
	uint32_t firstNzMs = 0;
	uint32_t previous = 0;
	uint32_t final = 0;
	uint32_t finalMs = 0;
	uint32_t changes = 0;
	uint32_t lastChangeMs = 0;
	uint32_t stableMs = 0;
	uint32_t elapsed = 0;
	uint32_t count;
	uint32_t stable = 0;
	uint32_t timedOut = 0;
	uint32_t none = 0;
	uint32_t error = 0;

	/* Publish the observation state in one coherent snapshot. The
	 * watchdog copies these fields under the same lock. */
	IOLockLock(reportLock);
	report.part_obs = 1;
	report.part_obs_deadline_ms = GTA4L_EMMC_BLOCK_PART_OBSERVE_MS;
	report.part_obs_slots = report.part_expected;
	report.part_obs_complete = 0;
	report.part_error = 0;
	IOLockUnlock(reportLock);

	for (;;) {
		/* Observation cancellation is checked before each registry
		 * walk and after every report-thread sleep. */
		if (reportCancel) {
			reportDone = true;
			return;
		}
		count = countPartitions();
		samples++;
		if (report.part_error != 0) {
			/* Latch the first registry-walk failure and stop. */
			error = 1;
			report.stop = 15;
			break;
		}
		if (samples == 1u) {
			first = count;
			firstMs = elapsed;
		}
		if ((count != 0) && (firstNz == 0)) {
			firstNz = count;
			firstNzMs = elapsed;
		}
		if ((samples > 1u) && (count != previous)) {
			changes++;
			lastChangeMs = elapsed;
		}
		previous = count;
		final = count;
		finalMs = elapsed;
		stableMs = elapsed - lastChangeMs;
		stable = ((count != 0) &&
		    (stableMs >= GTA4L_EMMC_BLOCK_PART_STABLE_MS)) ?
		    1u : 0u;
		IOLockLock(reportLock);
		report.part_count = final;
		report.part_obs_samples = samples;
		report.part_obs_first = first;
		report.part_obs_first_ms = firstMs;
		report.part_obs_first_nz = firstNz;
		report.part_obs_first_nz_ms = firstNzMs;
		report.part_obs_final = final;
		report.part_obs_final_ms = finalMs;
		report.part_obs_changes = changes;
		report.part_obs_stable = stable;
		report.part_obs_stable_ms = stableMs;
		report.part_obs_timed_out = 0;
		report.part_obs_none = 0;
		report.part_obs_error = 0;
		report.part_obs_reported = report.part_reported;
		IOLockUnlock(reportLock);
		if (stable != 0) {
			break;
		}
		if (elapsed >= GTA4L_EMMC_BLOCK_PART_OBSERVE_MS) {
			timedOut = 1;
			none = (final == 0) ? 1u : 0u;
			break;
		}
		IOSleep(GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS);
		elapsed += GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS;
		if (elapsed > GTA4L_EMMC_BLOCK_PART_OBSERVE_MS) {
			elapsed = GTA4L_EMMC_BLOCK_PART_OBSERVE_MS;
		}
	}
	IOLockLock(reportLock);
	report.part_obs_samples = samples;
	report.part_obs_timed_out = timedOut;
	report.part_obs_none = none;
	report.part_obs_error = error;
	report.part_obs_reported = report.part_reported;
	IOLockUnlock(reportLock);
	panicWithReport();
}

void
Gta4lEmmcBlock::runReport()
{
	IOMedia *media;
	uint64_t base = 0;
	uint64_t size = 0;
	uint64_t actual = 0;
	int whole = 0;
	int writable = 0;
	int check;
	uint32_t waited = 0;
	uint32_t partWaited = 0;
	uint32_t partCount;
	OSBoolean *wholeProperty;
	OSBoolean *writableProperty;
	OSNumber *baseProperty;
	OSNumber *sizeProperty;
	OSString *contentProperty;
	IOMemoryDescriptor *descriptor;
	IOReturn status;
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	struct qual_result twoQual;
	struct qual_result threeQual;

	bzero(&report, sizeof(report));
	bzero(sector, sizeof(sector));
	bzero(&twoQual, sizeof(twoQual));
	bzero(&threeQual, sizeof(threeQual));
	report.rc = 0;
	report.stop = 0;
	/* The opt-in token selects the PARTOBS report form even if the report
	 * stops before the observation window starts. The fields stay zero
	 * until the observation publishes its first coherent sample. */
	report.part_obs = partObsMode ? 1u : 0u;
	report.part_obs_deadline_ms = GTA4L_EMMC_BLOCK_PART_OBSERVE_MS;
	report.part_obs_complete = 0;
	report.partmap = partCmpMode ? 1u : 0u;
	report.partmap_deadline_ms = GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS;
	report.partmap_complete = 0;
	report.partread = partReadMode ? 1u : 0u;
	report.partread_cases = gta4l_emmc_block_partread_case_count();
	report.partread_complete = 0;
	report.partread_reason = GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA;
	report.deblock = deblockMode ? 1u : 0u;
	report.deblock_cases = gta4l_emmc_block_deblock_case_count();
	report.deblock_complete = 0;
	report.deblock_reason = GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA;
	report.deblock_segments = GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS;
	if (deblockMode) {
		runDeblock();
		return;
	}
	if (partReadMode) {
		runPartitionRead();
		return;
	}
	if (partCmpMode) {
		runPartitionComparison();
		return;
	}
	report.qual_ref_lba = GTA4L_EMMC_BLOCK_QUAL_LBA;
	report.qual_ref_blocks = GTA4L_EMMC_BLOCK_QUAL_REF_BLOCKS;
	media = nullptr;
	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			reportDone = true;
			return;
		}
		media = findWholeMedia();
		if (media != nullptr) {
			break;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	report.media_valid = (media != nullptr) ? 1u : 0u;
	if (media == nullptr) {
		report.stop = 12;
		report.fail_valid = 1;
		report.fail_stage = 1;
		report.fail_code = 12;
		panicWithReport();
		return;
	}

	wholeProperty = OSDynamicCast(OSBoolean,
	    media->getProperty(kIOMediaWholeKey));
	writableProperty = OSDynamicCast(OSBoolean,
	    media->getProperty(kIOMediaWritableKey));
	baseProperty = OSDynamicCast(OSNumber,
	    media->getProperty(kIOMediaBaseKey));
	sizeProperty = OSDynamicCast(OSNumber,
	    media->getProperty(kIOMediaSizeKey));
	contentProperty = OSDynamicCast(OSString,
	    media->getProperty(kIOMediaContentKey));
	whole = ((wholeProperty != nullptr) && wholeProperty->getValue()) ? 1 : 0;
	writable = ((writableProperty != nullptr) &&
	    writableProperty->getValue()) ? 1 : 0;
	base = (baseProperty != nullptr) ? baseProperty->unsigned64BitValue() : 0;
	size = (sizeProperty != nullptr) ? sizeProperty->unsigned64BitValue() : 0;
	copyContent(mediaContent,
	    (contentProperty != nullptr) ? contentProperty->getCStringNoCopy() : nullptr);
	report.whole = (uint32_t)whole;
	report.base = base;
	report.size = size;
	report.writable = (uint32_t)writable;
	report.content = mediaContent;
	check = gta4l_emmc_block_media_check(base, size, capacityBlocks, whole,
	    writable);
	if (check != 0) {
		report.stop = 13;
		report.fail_valid = 1;
		report.fail_stage = 2;
		report.fail_code = check;
		media->release();
		panicWithReport();
		return;
	}

	if (chunkedMode) {
		report.qual_ref_valid =
		    readQualificationReference() ? 1u : 0u;
	}

	if (!media->open(this, 0, kIOStorageAccessReader)) {
		report.stop = 14;
		report.fail_valid = 1;
		report.fail_stage = 6;
		report.fail_code = kIOReturnNotOpen;
		media->release();
		panicWithReport();
		return;
	}

	if (chunkedMode) {
		qualifyMediaRead(media, GTA4L_EMMC_BLOCK_QUAL_TWO_BLOCKS,
		    &twoQual);
		report.qual_two_attempted = twoQual.attempted;
		report.qual_two_ok = twoQual.ok;
		report.qual_two_reason = twoQual.reason;
		report.qual_two_bytes = twoQual.bytes;
		report.qual_two_mismatch_valid = twoQual.mismatch_valid;
		report.qual_two_mismatch_offset = twoQual.mismatch_offset;
		report.qual_two_matches = twoQual.matches;
		report.qual_two_cmd18 = twoQual.cmd18;
		report.qual_two_cmd17 = twoQual.cmd17;
		report.qual_two_stops = twoQual.stops;
		report.qual_two_stop_failures = twoQual.stop_failures;
		if (reportCancel) {
			media->close(this, 0);
			media->release();
			reportDone = true;
			return;
		}
		qualifyMediaRead(media, GTA4L_EMMC_BLOCK_QUAL_THREE_BLOCKS,
		    &threeQual);
		report.qual_three_attempted = threeQual.attempted;
		report.qual_three_ok = threeQual.ok;
		report.qual_three_reason = threeQual.reason;
		report.qual_three_bytes = threeQual.bytes;
		report.qual_three_mismatch_valid =
		    threeQual.mismatch_valid;
		report.qual_three_mismatch_offset =
		    threeQual.mismatch_offset;
		report.qual_three_matches = threeQual.matches;
		report.qual_three_cmd18 = threeQual.cmd18;
		report.qual_three_cmd17 = threeQual.cmd17;
		report.qual_three_stops = threeQual.stops;
		report.qual_three_stop_failures =
		    threeQual.stop_failures;
		if (reportCancel) {
			media->close(this, 0);
			media->release();
			reportDone = true;
			return;
		}
	}

	descriptor = IOMemoryDescriptor::withAddress(sector,
	    GTA4L_EMMC_BLOCK_BYTES, kIODirectionIn);
	if (descriptor == nullptr) {
		report.stop = 14;
		report.fail_valid = 1;
		report.fail_stage = 3;
		report.fail_code = -1;
		media->close(this, 0);
		media->release();
		panicWithReport();
		return;
	}
	report.read_attempted = 1;
	status = media->read(this,
	    (UInt64)GTA4L_EMMC_BLOCK_REPORT_LBA * GTA4L_EMMC_BLOCK_BYTES,
	    descriptor, nullptr, &actual);
	descriptor->release();
	report.read_bytes = actual;
	if ((status != kIOReturnSuccess) ||
	    (actual != GTA4L_EMMC_BLOCK_BYTES)) {
		report.stop = 14;
		report.fail_valid = 1;
		report.fail_stage = 4;
		report.fail_code = (status != kIOReturnSuccess) ? status :
		    (int32_t)actual;
		media->close(this, 0);
		media->release();
		panicWithReport();
		return;
	}
	report.read_ok = 1;
	report.read_sum = gta4l_emmc_block_sum(sector,
	    GTA4L_EMMC_BLOCK_BYTES);
	report.same = (baselineValid && (report.read_sum == baselineSum)) ?
	    1u : 0u;
	if (report.same == 0) {
		report.stop = 14;
		report.fail_valid = 1;
		report.fail_stage = 5;
		report.fail_code = (int32_t)report.read_sum;
		media->close(this, 0);
		media->release();
		panicWithReport();
		return;
	}
	report.part_expected = gta4l_emmc_block_gpt_entries(sector,
	    GTA4L_EMMC_BLOCK_BYTES);
	media->close(this, 0);
	media->release();

	if (partObsMode) {
		runPartitionObservation();
		return;
	}

	report.part_error = 0;
	while (partWaited < GTA4L_EMMC_BLOCK_PART_WAIT_MS) {
		if (reportCancel) {
			reportDone = true;
			return;
		}
		partCount = countPartitions();
		if (report.part_error != 0) {
			report.stop = 15;
			break;
		}
		if (partCount != 0) {
			report.part_count = partCount;
			break;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		partWaited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	report.part_wait_ms = partWaited;
	if (report.part_error == 0) {
		report.part_count = countPartitions();
		if (report.part_error != 0) {
			report.stop = 15;
		}
	}
	panicWithReport();
}

void
Gta4lEmmcBlock::fillWatchdogReport(void)
{
	/* The watchdog must not stall on the controller gate: a bounded
	 * try-lock snapshot is reported as coherent (1) or not (0). */
	(void)snapshotReport(&watchdogReport, false);
	watchdogReport.rc = 16;
	watchdogReport.stop = 16;
	watchdogReport.fail_valid = 1;
	watchdogReport.fail_stage = 16;
	watchdogReport.fail_code = 16;
}

void
Gta4lEmmcBlock::watchdogEntry(void *context, wait_result_t waitResult)
{
	Gta4lEmmcBlock *self = static_cast<Gta4lEmmcBlock *>(context);
	uint32_t waited = 0;

	(void)waitResult;
	if (self == nullptr) {
		thread_terminate(current_thread());
		return;
	}
	while (waited < GTA4L_EMMC_BLOCK_WATCHDOG_MS) {
		if (self->reportDone) {
			self->release();
			thread_terminate(current_thread());
			return;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (!self->reportDone && self->claimTerminal()) {
		uint32_t length;

		bzero(&self->watchdogReport, sizeof(self->watchdogReport));
		self->fillWatchdogReport();
		self->reportDone = true;
		length = gta4l_emmc_block_report_format(self->watchdogBuffer,
		    sizeof(self->watchdogBuffer), &self->watchdogReport);
		if (length == 0) {
			panic("GTA4L-EMMC P5 REPORT: watchdog format failed");
		} else {
			panic("%s", self->watchdogBuffer);
		}
	}
	self->release();
	thread_terminate(current_thread());
}

void
Gta4lEmmcBlock::reportEntry(void *context, wait_result_t waitResult)
{
	Gta4lEmmcBlock *self = static_cast<Gta4lEmmcBlock *>(context);

	(void)waitResult;
	if (self != nullptr) {
		self->runReport();
		self->release();
	}
	thread_terminate(current_thread());
}
