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
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "BREAKUP worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES ==
    GTA4L_EMMC_BLOCK_BYTES,
    "BREAKUP sector must match the media block size");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_MAX_BLOCKS == 128u,
    "BREAKUP bounce must cover 128 blocks");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES == 65536u,
    "BREAKUP bounce must be 65,536 bytes");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS == 8u,
    "BREAKUP must advertise the eight-block read limit");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_MAX_SUBREQUESTS == 16u,
    "BREAKUP case must not exceed sixteen subrequests");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS > 1u,
    "BREAKUP must advertise the multi-segment path");
static_assert(GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES >=
    GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES,
    "BREAKUP bounce must cover the largest aligned span");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "SWEEP worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES ==
    GTA4L_EMMC_BLOCK_BYTES,
    "SWEEP sector must match the media block size");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_MAX_BLOCKS == 32u,
    "SWEEP must bound a case at 32 blocks");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES == 16384u,
    "SWEEP must bound a case at 16,384 bytes");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS == 8u,
    "SWEEP must advertise the eight-block read limit");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_READ_MAX_BYTES == 4096u,
    "SWEEP must advertise the 4,096-byte read limit");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS == 1u,
    "SWEEP must advertise the single-segment path");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_CASES ==
    (GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES +
    GTA4L_EMMC_BLOCK_SWEEP_RUNTIME_CASES),
    "SWEEP table must hold both anchors and all runtime windows");
static_assert(GTA4L_EMMC_BLOCK_SWEEP_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "SWEEP summary line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_SWEEPREAD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "SWEEP detail line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "CONSUME conditional worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES ==
    GTA4L_EMMC_BLOCK_BYTES,
    "CONSUME sector must match the media block size");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS == 2u,
    "CONSUME window must be two blocks");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES == 1024u,
    "CONSUME window must be 1,024 bytes");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES == 32768u,
    "CONSUME buffer must stay the pinned 32 KiB allocation");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS ==
    GTA4L_EMMC_BLOCK_MAX_BLOCKS,
    "CONSUME must use the production block advertisement");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_MAX_BYTES ==
    GTA4L_EMMC_BLOCK_MAX_BYTES,
    "CONSUME must use the production byte advertisement");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS == 1u,
    "CONSUME must use the production single-segment advertisement");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_CASE_TAG != 0u,
    "CONSUME case tag must be nonzero");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "CONSUME summary line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_CONSUMEREAD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "CONSUMEREAD detail line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_CONSUMEFAIL_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "CONSUMEFAIL detail line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_CONSUME_REPORT_WORST_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "CONSUME worst report must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_WORST_MS <
    GTA4L_EMMC_BLOCK_WATCHDOG_MS,
    "WIDTH worst case must stay below the watchdog");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS <
    GTA4L_EMMC_BLOCK_WIDTH_WORST_MS,
    "WIDTH report budget must be inside the worst-case bound");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS + 13010u ==
    GTA4L_EMMC_BLOCK_WIDTH_WORST_MS,
    "WIDTH report budget plus the pre-watchdog path must equal the bound");
static_assert(GTA4L_EMMC_BLOCK_WIDTHFAIL_LINE_BYTES <
    GTA4L_EMMC_BLOCK_WIDTHREAD_LINE_BYTES,
    "WIDTHFAIL must stay smaller than the WIDTHREAD worst-case line");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES <=
    GTA4L_EMMC_BLOCK_MAX_BYTES,
    "WIDTH case must stay inside the 64-block request limit");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS ==
    GTA4L_EMMC_BLOCK_CHUNK_BLOCKS,
    "WIDTH case must use the production two-block chunk");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTH summary line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_WIDTHREAD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHREAD detail line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_WIDTHFAIL_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHFAIL detail line must fit the report buffer");
static_assert(GTA4L_EMMC_BLOCK_WIDTH_REPORT_WORST_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTH worst report must fit the report buffer");

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

struct gta4l_emmc_consume_result {
	uint32_t reason;
	uint32_t attempted;
	int32_t status;
	uint64_t actual;
	uint8_t digest[32];
};

/* The consumer is a distinct IOService identity for IOMedia::open() and
 * IOMedia::read(). It owns the contiguous buffer and the descriptor for the
 * single case; run() frees both on every path. The adapter owns the media
 * references and the report. */
class Gta4lEmmcConsumer : public IOService {
	OSDeclareDefaultStructors(Gta4lEmmcConsumer)

public:
	bool run(IOMedia *boot, uint64_t start, uint32_t length,
	    uint32_t tag, struct gta4l_emmc_consume_result *result);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
	void release() override;
#endif
};

OSDefineMetaClassAndStructors(Gta4lEmmcConsumer, IOService)

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
void
Gta4lEmmcConsumer::release()
{
	gta4l_test.consumer_release_calls++;
}
#endif

bool
Gta4lEmmcConsumer::run(IOMedia *boot, uint64_t start, uint32_t length,
    uint32_t tag, struct gta4l_emmc_consume_result *result)
{
	IOMemoryDescriptor *descriptor;
	IOPhysicalAddress address;
	IOByteCount segmentLength = 0;
	UInt64 actual = 0;
	IOReturn status;
	uint8_t *buffer;

	if ((boot == nullptr) || (result == nullptr)) {
		return false;
	}
	result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE;
	result->attempted = 0;
	result->status = 0;
	result->actual = 0;
	bzero(result->digest, sizeof(result->digest));

	buffer = (uint8_t *)IOMallocContiguous(
	    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES, PAGE_SIZE, nullptr);
	if (buffer == nullptr) {
		result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC;
		return false;
	}
	descriptor = IOMemoryDescriptor::withAddress(buffer, length,
	    kIODirectionIn);
	if (descriptor == nullptr) {
		IOFreeContiguous(buffer,
		    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);
		result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;
		return false;
	}
	address = descriptor->getPhysicalSegment(0, &segmentLength,
	    kIOMemoryMapperNone);
	if ((address == 0) || (segmentLength < (IOByteCount)length)) {
		descriptor->release();
		IOFreeContiguous(buffer,
		    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);
		result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;
		return false;
	}
	descriptor->setTag(tag);
	result->attempted = 1;
	status = boot->read(this, start, descriptor, nullptr, &actual);
	descriptor->release();
	result->status = (int32_t)status;
	result->actual = actual;
	if (status != kIOReturnSuccess) {
		result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_READ;
	} else if (actual != (UInt64)length) {
		result->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_SHORT;
	} else {
		gta4l_emmc_block_sha256(buffer, length, result->digest);
	}
	IOFreeContiguous(buffer, GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);
	return true;
}

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
	void panicWithReportNoGate(void);
	void finishReport(bool wait);
	bool acquireWidthGate(uint32_t timeoutMs);
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
	void runBreakup(void);
	void releaseBreakupMedia(void);
	void breakupReportFailure(uint32_t reason);
	void breakupReportCancel(void);
	uint64_t breakupElapsedMs(void);
	void breakupObserve(IOMemoryDescriptor *buffer, UInt64 block,
	    UInt64 nblks);
	void runSweep(void);
	void releaseSweepMedia(void);
	void sweepReportFailure(uint32_t reason);
	void sweepReportCancel(void);
	uint64_t sweepElapsedMs(void);
	void sweepObserve(IOMemoryDescriptor *buffer, UInt64 block,
	    UInt64 nblks);
	int readSweepReference(uint64_t firstBlock, uint32_t blocks,
	    uint32_t *readBytes, int32_t *failStatus);
	void runConsume(void);
	void releaseConsumeMedia(void);
	void consumeReportFailure(uint32_t reason);
	void consumeReportCancel(void);
	bool consumeObserve(IOMemoryDescriptor *buffer, UInt64 block,
	    UInt64 nblks);
	uint64_t consumeElapsedMs(void);
	void runWidth(void);
	bool readWidthWindow(uint8_t *target, uint32_t *status,
	    uint64_t *actual, uint32_t *subrequests, uint32_t *blocks,
	    uint32_t *max, bool *stopOk);
	void widthRecordSwitch(const struct gta4l_emmc_bus_result *bus);
	void widthRecordRevert(const struct gta4l_emmc_bus_result *bus);
	void widthLatchFailure(uint32_t stage, int32_t status,
	    uint64_t actual, const uint8_t *digest);
	void widthReportCancel(void);
	bool widthAttemptRevert(void);
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
	bool breakupMode = false;
	uint64_t breakupReadStart = 0;
	uint8_t *breakupBuffer = nullptr;
	IOMedia *breakupWhole = nullptr;
	IOMedia *breakupBoot = nullptr;
	bool breakupWholeOpen = false;
	bool breakupBootOpen = false;
	bool breakupActive = false;
	uint32_t breakupActiveTag = 0;
	uint64_t breakupActiveFirst = 0;
	uint64_t breakupActiveNext = 0;
	uint64_t breakupActiveEnd = 0;
	uint32_t breakupActiveSubrequests = 0;
	uint32_t breakupActiveMax = 0;
	uint32_t breakupObservedSubrequests = 0;
	uint32_t breakupObservedBlocks = 0;
	uint32_t breakupObservedMax = 0;
	uint32_t breakupObservedOrder = 0;
	uint32_t breakupObservedForeign = 0;
	bool sweepMode = false;
	uint64_t sweepReadStart = 0;
	uint8_t *sweepBuffer = nullptr;
	uint8_t *sweepReference = nullptr;
	IOMedia *sweepWhole = nullptr;
	IOMedia *sweepBoot = nullptr;
	bool sweepWholeOpen = false;
	bool sweepBootOpen = false;
	bool sweepActive = false;
	uint32_t sweepActiveTag = 0;
	uint64_t sweepActiveFirst = 0;
	uint64_t sweepActiveNext = 0;
	uint64_t sweepActiveEnd = 0;
	uint32_t sweepActiveSubrequests = 0;
	uint32_t sweepActiveMax = 0;
	uint32_t sweepObservedSubrequests = 0;
	uint32_t sweepObservedBlocks = 0;
	uint32_t sweepObservedMax = 0;
	uint32_t sweepObservedOrder = 0;
	uint32_t sweepObservedForeign = 0;
	bool consumeMode = false;
	Gta4lEmmcConsumer *consumeClient = nullptr;
	IOMedia *consumeWhole = nullptr;
	IOMedia *consumeBoot = nullptr;
	bool consumeWholeOpen = false;
	bool consumeBootOpen = false;
	bool consumeActive = false;
	bool consumeInFlight = false;
	uint32_t consumeActiveTag = 0;
	uint64_t consumeActiveFirst = 0;
	uint64_t consumeActiveBlocks = 0;
	uint64_t consumeActiveEnd = 0;
	uint32_t consumeObservedSubrequests = 0;
	uint32_t consumeObservedBlocks = 0;
	uint32_t consumeObservedMax = 0;
	uint32_t consumeObservedOrder = 0;
	uint32_t consumeObservedForeign = 0;
	uint32_t consumeCreditedC18 = 0;
	uint32_t consumeCreditedC18b = 0;
	uint32_t consumeCreditedStops = 0;
	uint64_t consumeReadStart = 0;
	bool widthMode = false;
	bool widthCaptureActive = false;
	uint8_t *widthCaptureBuffer = nullptr;
	uint32_t widthCaptureCopied = 0;
	uint8_t widthBaseline[GTA4L_EMMC_EXT_CSD_BYTES];
	uint8_t *widthReference = nullptr;
	uint8_t *widthObserved = nullptr;
	uint8_t widthReferenceDigest[32];
	uint8_t widthObservedDigest[32];
	struct gta4l_emmc_bus_result widthSwitch;
	struct gta4l_emmc_bus_result widthRevert;
	int32_t widthRevertStatus = 0;
	uint32_t widthRefStatus = 0;
	uint64_t widthRefActual = 0;
	uint32_t widthRefSubrequests = 0;
	uint32_t widthRefBlocks = 0;
	uint32_t widthRefMax = 0;
	bool widthRefStopOk = false;
	uint32_t widthFailStage =
	    GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
	int32_t widthFailStatus = 0;
	uint64_t widthFailActual = 0;
	uint8_t widthFailDigest[16];
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
	char *reportBuffer = nullptr;
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
	uint32_t widthCopied;

	if (self->widthCaptureActive) {
		widthCopied = 0;
		if ((source != nullptr) &&
		    (offset < GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES)) {
			widthCopied = length;
			if (widthCopied >
			    (GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES - offset)) {
				widthCopied =
				    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES - offset;
			}
			if (self->widthCaptureBuffer != nullptr) {
				memcpy(self->widthCaptureBuffer + offset,
				    source, widthCopied);
			}
		}
		self->widthCaptureCopied += widthCopied;
		return widthCopied;
	}

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
	uint32_t breakupTokens;
	uint32_t sweepTokens;
	uint32_t consumeTokens;
	uint32_t widthTokens;
	int value = 0;
	int partValue = 0;
	int partcmpValue = 0;
	int partreadValue = 0;
	int deblockValue = 0;
	int breakupValue = 0;
	int sweepValue = 0;
	int consumeValue = 0;
	int widthValue = 0;
	int parsed;
	int partobsParsed;
	int partcmpParsed;
	int partreadParsed;
	int deblockParsed;
	int breakupParsed;
	int sweepParsed;
	int consumeParsed;
	int widthParsed;
	int mode;
	int partobs;
	int partcmp;
	int partread;
	int deblock;
	int breakup;
	int sweep;
	int consume;
	int width;
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
	breakupTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_BREAKUP_ARG);
	sweepTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_SWEEP_ARG);
	consumeTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_CONSUME_ARG);
	widthTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_WIDTH_ARG);
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
	breakupParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_BREAKUP_ARG,
	    &breakupValue, sizeof(breakupValue));
	if ((breakupParsed == 0) && (breakupTokens != 0)) {
		breakupValue = -1;
	}
	sweepParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_SWEEP_ARG,
	    &sweepValue, sizeof(sweepValue));
	if ((sweepParsed == 0) && (sweepTokens != 0)) {
		sweepValue = -1;
	}
	consumeParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_CONSUME_ARG,
	    &consumeValue, sizeof(consumeValue));
	if ((consumeParsed == 0) && (consumeTokens != 0)) {
		consumeValue = -1;
	}
	widthParsed = PE_parse_boot_argn(GTA4L_EMMC_BLOCK_WIDTH_ARG,
	    &widthValue, sizeof(widthValue));
	if ((widthParsed == 0) && (widthTokens != 0)) {
		widthValue = -1;
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
		if (breakupTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-breakup requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		if (sweepTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-sweep requires "
			    "gta4l-emmc-block=1 or 3\n");
		}
		if (consumeTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-consume requires "
			    "gta4l-emmc-block=3\n");
		}
		if (widthTokens != 0) {
			IOLog("GTA4L-EMMC P5 REFUSED: "
			    "gta4l-emmc-width requires "
			    "gta4l-emmc-block=3\n");
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
	breakup = gta4l_emmc_block_breakup_select(breakupTokens != 0,
	    breakupValue, breakupTokens > 1u, 1,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON,
	    partread == GTA4L_EMMC_BLOCK_PARTREAD_ON,
	    deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON);
	if (breakup == GTA4L_EMMC_BLOCK_BREAKUP_REFUSED) {
		if (breakupTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-breakup token\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-breakup are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-breakup are mutually "
			    "exclusive\n");
		} else if (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partread "
			    "and gta4l-emmc-breakup are mutually "
			    "exclusive\n");
		} else if (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-deblock "
			    "and gta4l-emmc-breakup are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-breakup value\n");
		}
		return false;
	}
	breakupMode = (breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON);
	sweep = gta4l_emmc_block_sweep_select(sweepTokens != 0, sweepValue,
	    sweepTokens > 1u, 1,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON,
	    partread == GTA4L_EMMC_BLOCK_PARTREAD_ON,
	    deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON,
	    breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON);
	if (sweep == GTA4L_EMMC_BLOCK_SWEEP_REFUSED) {
		if (sweepTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-sweep token\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-sweep are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-sweep are mutually "
			    "exclusive\n");
		} else if (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partread "
			    "and gta4l-emmc-sweep are mutually "
			    "exclusive\n");
		} else if (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-deblock "
			    "and gta4l-emmc-sweep are mutually "
			    "exclusive\n");
		} else if (breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-breakup "
			    "and gta4l-emmc-sweep are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-sweep value\n");
		}
		return false;
	}
	sweepMode = (sweep == GTA4L_EMMC_BLOCK_SWEEP_ON);
	consume = gta4l_emmc_block_consume_select(consumeTokens != 0,
	    consumeValue, consumeTokens > 1u, 1, value,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON,
	    partread == GTA4L_EMMC_BLOCK_PARTREAD_ON,
	    deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON,
	    breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON,
	    sweep == GTA4L_EMMC_BLOCK_SWEEP_ON);
	if (consume == GTA4L_EMMC_BLOCK_CONSUME_REFUSED) {
		if (consumeTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-consume token\n");
		} else if (value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-consume "
			    "requires gta4l-emmc-block=3\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else if (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partread "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else if (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-deblock "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else if (breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-breakup "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else if (sweep == GTA4L_EMMC_BLOCK_SWEEP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-sweep "
			    "and gta4l-emmc-consume are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: malformed "
			    "gta4l-emmc-consume value\n");
		}
		return false;
	}
	consumeMode = (consume == GTA4L_EMMC_BLOCK_CONSUME_ON);
	width = gta4l_emmc_block_width_select(widthTokens != 0, widthValue,
	    widthTokens > 1u, 1, value,
	    partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON,
	    partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON,
	    partread == GTA4L_EMMC_BLOCK_PARTREAD_ON,
	    deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON,
	    breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON,
	    sweep == GTA4L_EMMC_BLOCK_SWEEP_ON,
	    consume == GTA4L_EMMC_BLOCK_CONSUME_ON);
	if (width == GTA4L_EMMC_BLOCK_WIDTH_REFUSED) {
		if (widthTokens > 1u) {
			IOLog("GTA4L-EMMC P5 REFUSED: duplicate "
			    "gta4l-emmc-width token\n");
		} else if (value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-width "
			    "requires gta4l-emmc-block=3\n");
		} else if (partobs == GTA4L_EMMC_BLOCK_PARTOBS_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partobs "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (partcmp == GTA4L_EMMC_BLOCK_PARTCMP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partcmp "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (partread == GTA4L_EMMC_BLOCK_PARTREAD_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-partread "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-deblock "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (breakup == GTA4L_EMMC_BLOCK_BREAKUP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-breakup "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (sweep == GTA4L_EMMC_BLOCK_SWEEP_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-sweep "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else if (consume == GTA4L_EMMC_BLOCK_CONSUME_ON) {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-consume "
			    "and gta4l-emmc-width are mutually "
			    "exclusive\n");
		} else {
			IOLog("GTA4L-EMMC P5 REFUSED: gta4l-emmc-width "
			    "value must be 4; 1-bit is the absence of the "
			    "token\n");
		}
		return false;
	}
	widthMode = (width == GTA4L_EMMC_BLOCK_WIDTH_ON);
	if (widthMode) {
		/* The reference and observed copies never alias: the reference
		 * is filled only by the pre-switch 1-bit read, the observed
		 * copy only by the post-switch 4-bit read. Both are copied out
		 * of the block core, so no descriptor is built from them. */
		widthReference = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, PAGE_SIZE);
		if (widthReference == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: width reference "
			    "buffer allocation\n");
			return false;
		}
		widthObserved = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES, PAGE_SIZE);
		if (widthObserved == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: width observed "
			    "buffer allocation\n");
			return false;
		}
		bzero(widthReference, GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES);
		bzero(widthObserved, GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES);
	}
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
	if (breakupMode) {
		/* Every case is copied through one page-aligned buffer so the
		 * breaker sees the same deterministic page-aligned span on
		 * every stage; it covers the largest requested length and the
		 * largest aligned span. */
		breakupBuffer = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES, PAGE_SIZE);
		if (breakupBuffer == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: breakup buffer "
			    "allocation\n");
			return false;
		}
	}
	if (sweepMode) {
		/* The reference and media buffers never alias: the reference
		 * is filled only by the direct CMD17 helper under the
		 * controller gate, the media buffer only by the media-path
		 * read. Both cover the largest case and start page-aligned. */
		sweepReference = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES, PAGE_SIZE);
		if (sweepReference == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: sweep reference "
			    "buffer allocation\n");
			return false;
		}
		sweepBuffer = (uint8_t *)IOMallocAligned(
		    GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES, PAGE_SIZE);
		if (sweepBuffer == nullptr) {
			IOLog("GTA4L-EMMC P5 FAILED: sweep media "
			    "buffer allocation\n");
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
	reportBuffer = (char *)IOMalloc(GTA4L_EMMC_BLOCK_REPORT_BYTES);
	if (reportBuffer == nullptr) {
		IOLog("GTA4L-EMMC P5 FAILED: report buffer allocation\n");
		IOLockFree(reportLock);
		reportLock = nullptr;
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

	if (widthMode) {
		int widthRc;

		/* N10.16 pre-watchdog sequence: 1-bit EXT_CSD baseline,
		 * 1-bit reference read of the pinned BOOT window, then the
		 * verified 4-bit switch. Any failure stops before
		 * publication. ERR_WIDTH_RESTORE is terminal unknown
		 * framing: no further read and no extra recovery. */
		IOLockLock(reportLock);
		report.width = 1;
		report.width_cases = GTA4L_EMMC_BLOCK_WIDTH_CASES;
		report.width_complete = 0;
		report.width_reason =
		    GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
		report.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
		report.width_segments = 1u;
		report.width_capacity = capacityBlocks;
		IOLockUnlock(reportLock);
		IOLockLock(gate);
		widthRc = gta4l_emmc_read_ext_csd(&host, widthBaseline);
		if (widthRc != GTA4L_EMMC_OK) {
			IOLockUnlock(gate);
			widthLatchFailure(
			    GTA4L_EMMC_BLOCK_WIDTH_REASON_BASELINE,
			    widthRc, 0, nullptr);
			panicWithReport();
			return false;
		}
		if (!readWidthWindow(widthReference, &widthRefStatus,
		    &widthRefActual, &widthRefSubrequests, &widthRefBlocks,
		    &widthRefMax, &widthRefStopOk)) {
			IOLockUnlock(gate);
			widthLatchFailure(
			    GTA4L_EMMC_BLOCK_WIDTH_REASON_PREREAD,
			    (int32_t)widthRefStatus, widthRefActual,
			    nullptr);
			panicWithReport();
			return false;
		}
		gta4l_emmc_block_sha256(widthReference,
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES,
		    widthReferenceDigest);
		bzero(&widthSwitch, sizeof(widthSwitch));
		widthRc = gta4l_emmc_set_bus_width_verified(&host,
		    GTA4L_EMMC_BLOCK_WIDTH_BITS_4, &widthSwitch);
		widthRecordSwitch(&widthSwitch);
		IOLockUnlock(gate);
		if ((widthRc != GTA4L_EMMC_OK) ||
		    (widthSwitch.verified != 1u) ||
		    (widthSwitch.switch_result != GTA4L_EMMC_OK) ||
		    ((widthSwitch.host_after &
		    GTA4L_EMMC_HOST_CONTROL_4BIT) !=
		    GTA4L_EMMC_HOST_CONTROL_4BIT) ||
		    (host.bus_width !=
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4)) {
			widthLatchFailure(
			    GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH,
			    (widthRc != GTA4L_EMMC_OK) ? widthRc :
			    widthSwitch.switch_result, 0, nullptr);
			panicWithReport();
			return false;
		}
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
		if (widthMode) {
			/* The existing baseline read runs at the new width. A
			 * failure here is a 4-bit read failure: fail closed
			 * and preserve the first width-stage failure. */
			widthLatchFailure(
			    GTA4L_EMMC_BLOCK_WIDTH_REASON_READ,
			    baselineResult, 0, nullptr);
		}
		report.rc = (uint32_t)baselineResult;
		report.stop = 11;
		report.fail_valid = 1;
		report.fail_stage = host.last_step;
		report.fail_code = baselineResult;
		panicWithReport();
		return false;
	}

	if (!setProperty(kIOMaximumBlockCountReadKey,
	    (unsigned long long)(sweepMode ?
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS : breakupMode ?
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS :
	    GTA4L_EMMC_BLOCK_MAX_BLOCKS), 32)) {
		IOLog("GTA4L-EMMC P5 FAILED: block limit property\n");
		return false;
	}
	if (!setProperty(kIOMaximumByteCountReadKey,
	    (unsigned long long)(sweepMode ?
	    GTA4L_EMMC_BLOCK_SWEEP_READ_MAX_BYTES : breakupMode ?
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_MAX_BYTES :
	    GTA4L_EMMC_BLOCK_MAX_BYTES), 32)) {
		IOLog("GTA4L-EMMC P5 FAILED: byte limit property\n");
		return false;
	}
	if (!setProperty(kIOMaximumSegmentCountReadKey,
	    (unsigned long long)(sweepMode ?
	    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS : breakupMode ?
	    GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS : deblockMode ?
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
	if (breakupBuffer != nullptr) {
		IOFreeAligned(breakupBuffer,
		    GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES);
		breakupBuffer = nullptr;
	}
	if (sweepBuffer != nullptr) {
		IOFreeAligned(sweepBuffer,
		    GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES);
		sweepBuffer = nullptr;
	}
	if (sweepReference != nullptr) {
		IOFreeAligned(sweepReference,
		    GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES);
		sweepReference = nullptr;
	}
	if (widthObserved != nullptr) {
		IOFreeAligned(widthObserved,
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES);
		widthObserved = nullptr;
	}
	if (widthReference != nullptr) {
		IOFreeAligned(widthReference,
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES);
		widthReference = nullptr;
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
	if (reportBuffer != nullptr) {
		IOFree(reportBuffer, GTA4L_EMMC_BLOCK_REPORT_BYTES);
		reportBuffer = nullptr;
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
	struct gta4l_emmc_block_counters countersBefore;
	struct gta4l_emmc_block_counters countersAfter;
	uint64_t completed = 0;
	int result;
	IOReturn status;
	bool wentOffline = false;
	bool consumeAttributed = false;

	(void)attributes;
	if ((buffer == nullptr) || (completion == nullptr) ||
	    (completion->action == nullptr)) {
		return kIOReturnBadArgument;
	}
	if (nblks == 0) {
		return kIOReturnBadArgument;
	}
	breakupObserve(buffer, blockNumber, nblks);
	sweepObserve(buffer, blockNumber, nblks);
	consumeAttributed = consumeObserve(buffer, blockNumber, nblks);
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
	countersBefore = block.counters;
	result = gta4l_emmc_block_read(&block, blockNumber, (uint32_t)nblks,
	    (uint64_t)buffer->getLength(), direction, &completed);
	countersAfter = block.counters;
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

	if (consumeAttributed &&
	    (result == GTA4L_EMMC_BLOCK_OK)) {
		IOLockLock(reportLock);
		if ((countersAfter.cmd18_chunks ==
		    countersBefore.cmd18_chunks + 1u) &&
		    (countersAfter.cmd18_blocks ==
		    countersBefore.cmd18_blocks +
		    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS) &&
		    (countersAfter.stop_attempts ==
		    countersBefore.stop_attempts + 1u) &&
		    (countersAfter.stop_failures ==
		    countersBefore.stop_failures)) {
			consumeCreditedC18++;
			consumeCreditedC18b +=
			    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
			consumeCreditedStops++;
		} else {
			consumeObservedOrder = 1;
		}
		IOLockUnlock(reportLock);
	}

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
		target->breakup = report.breakup;
		target->breakup_cases = report.breakup_cases;
		target->breakup_passed = report.breakup_passed;
		target->breakup_failed = report.breakup_failed;
		target->breakup_reason = report.breakup_reason;
		target->breakup_complete = report.breakup_complete;
		target->breakup_reads = report.breakup_reads;
		target->breakup_bytes = report.breakup_bytes;
		target->breakup_span = report.breakup_span;
		target->breakup_blocks = report.breakup_blocks;
		target->breakup_subrequests = report.breakup_subrequests;
		target->breakup_max = report.breakup_max;
		target->breakup_foreign = report.breakup_foreign;
		target->breakup_fails = report.breakup_fails;
		target->breakup_cancel = report.breakup_cancel;
		target->breakup_budget = report.breakup_budget;
		target->breakup_media = report.breakup_media;
		target->breakup_part = report.breakup_part;
		target->breakup_limit = report.breakup_limit;
		target->breakup_segments = report.breakup_segments;
		target->breakup_capacity = report.breakup_capacity;
		target->breakup_last_index = report.breakup_last_index;
		target->breakup_last_target = report.breakup_last_target;
		target->breakup_last_start = report.breakup_last_start;
		target->breakup_last_length = report.breakup_last_length;
		target->breakup_last_span = report.breakup_last_span;
		target->breakup_last_status = report.breakup_last_status;
		target->breakup_last_actual = report.breakup_last_actual;
		target->breakup_last_subrequests =
		    report.breakup_last_subrequests;
		target->breakup_last_blocks = report.breakup_last_blocks;
		target->breakup_last_max = report.breakup_last_max;
		memcpy(target->breakup_last_digest,
		    report.breakup_last_digest,
		    sizeof(target->breakup_last_digest));
		target->breakup_fail_index = report.breakup_fail_index;
		target->breakup_fail_reason = report.breakup_fail_reason;
		target->breakup_fail_status = report.breakup_fail_status;
		target->breakup_fail_actual = report.breakup_fail_actual;
		target->breakup_fail_target = report.breakup_fail_target;
		target->breakup_fail_start = report.breakup_fail_start;
		target->breakup_fail_length = report.breakup_fail_length;
		target->breakup_fail_span = report.breakup_fail_span;
		target->breakup_fail_subrequests =
		    report.breakup_fail_subrequests;
		target->breakup_fail_blocks = report.breakup_fail_blocks;
		target->breakup_fail_max = report.breakup_fail_max;
		memcpy(target->breakup_fail_digest,
		    report.breakup_fail_digest,
		    sizeof(target->breakup_fail_digest));
		target->sweep = report.sweep;
		target->sweep_cases = report.sweep_cases;
		target->sweep_passed = report.sweep_passed;
		target->sweep_failed = report.sweep_failed;
		target->sweep_reason = report.sweep_reason;
		target->sweep_complete = report.sweep_complete;
		target->sweep_reads = report.sweep_reads;
		target->sweep_bytes = report.sweep_bytes;
		target->sweep_span = report.sweep_span;
		target->sweep_blocks = report.sweep_blocks;
		target->sweep_subrequests = report.sweep_subrequests;
		target->sweep_max = report.sweep_max;
		target->sweep_foreign = report.sweep_foreign;
		target->sweep_fails = report.sweep_fails;
		target->sweep_cancel = report.sweep_cancel;
		target->sweep_budget = report.sweep_budget;
		target->sweep_media = report.sweep_media;
		target->sweep_part = report.sweep_part;
		target->sweep_refs = report.sweep_refs;
		target->sweep_refbytes = report.sweep_refbytes;
		target->sweep_anchor = report.sweep_anchor;
		target->sweep_mismatches = report.sweep_mismatches;
		target->sweep_limit = report.sweep_limit;
		target->sweep_segments = report.sweep_segments;
		target->sweep_capacity = report.sweep_capacity;
		target->sweep_last_index = report.sweep_last_index;
		target->sweep_last_target = report.sweep_last_target;
		target->sweep_last_start = report.sweep_last_start;
		target->sweep_last_length = report.sweep_last_length;
		target->sweep_last_span = report.sweep_last_span;
		target->sweep_last_status = report.sweep_last_status;
		target->sweep_last_actual = report.sweep_last_actual;
		target->sweep_last_subrequests =
		    report.sweep_last_subrequests;
		target->sweep_last_blocks = report.sweep_last_blocks;
		target->sweep_last_max = report.sweep_last_max;
		target->sweep_last_ref = report.sweep_last_ref;
		target->sweep_last_refbytes =
		    report.sweep_last_refbytes;
		target->sweep_last_refstatus =
		    report.sweep_last_refstatus;
		target->sweep_last_mvalid = report.sweep_last_mvalid;
		target->sweep_last_mismatch =
		    report.sweep_last_mismatch;
		memcpy(target->sweep_last_digest,
		    report.sweep_last_digest,
		    sizeof(target->sweep_last_digest));
		memcpy(target->sweep_last_refdigest,
		    report.sweep_last_refdigest,
		    sizeof(target->sweep_last_refdigest));
		target->sweep_fail_index = report.sweep_fail_index;
		target->sweep_fail_reason = report.sweep_fail_reason;
		target->sweep_fail_status = report.sweep_fail_status;
		target->sweep_fail_actual = report.sweep_fail_actual;
		target->sweep_fail_target = report.sweep_fail_target;
		target->sweep_fail_start = report.sweep_fail_start;
		target->sweep_fail_length = report.sweep_fail_length;
		target->sweep_fail_span = report.sweep_fail_span;
		target->sweep_fail_subrequests =
		    report.sweep_fail_subrequests;
		target->sweep_fail_blocks = report.sweep_fail_blocks;
		target->sweep_fail_max = report.sweep_fail_max;
		target->sweep_fail_ref = report.sweep_fail_ref;
		target->sweep_fail_refbytes =
		    report.sweep_fail_refbytes;
		target->sweep_fail_refstatus =
		    report.sweep_fail_refstatus;
		target->sweep_fail_mvalid = report.sweep_fail_mvalid;
		target->sweep_fail_mismatch =
		    report.sweep_fail_mismatch;
		memcpy(target->sweep_fail_digest,
		    report.sweep_fail_digest,
		    sizeof(target->sweep_fail_digest));
		memcpy(target->sweep_fail_refdigest,
		    report.sweep_fail_refdigest,
		    sizeof(target->sweep_fail_refdigest));
		target->consume = report.consume;
		target->consume_cases = report.consume_cases;
		target->consume_passed = report.consume_passed;
		target->consume_failed = report.consume_failed;
		target->consume_reason = report.consume_reason;
		target->consume_complete = report.consume_complete;
		target->consume_reads = report.consume_reads;
		target->consume_bytes = report.consume_bytes;
		target->consume_span = report.consume_span;
		target->consume_blocks = report.consume_blocks;
		target->consume_subrequests = report.consume_subrequests;
		target->consume_max = report.consume_max;
		target->consume_foreign = report.consume_foreign;
		target->consume_fails = report.consume_fails;
		target->consume_cancel = report.consume_cancel;
		target->consume_budget = report.consume_budget;
		target->consume_media = report.consume_media;
		target->consume_part = report.consume_part;
		target->consume_limit = report.consume_limit;
		target->consume_segments = report.consume_segments;
		target->consume_capacity = report.consume_capacity;
		target->consume_open = report.consume_open;
		target->consume_close = report.consume_close;
		target->consume_alloc = report.consume_alloc;
		target->consume_client = report.consume_client;
		target->consume_mismatch = report.consume_mismatch;
		target->consume_last_index = report.consume_last_index;
		target->consume_last_target = report.consume_last_target;
		target->consume_last_start = report.consume_last_start;
		target->consume_last_length = report.consume_last_length;
		target->consume_last_span = report.consume_last_span;
		target->consume_last_status = report.consume_last_status;
		target->consume_last_actual = report.consume_last_actual;
		target->consume_last_subrequests =
		    report.consume_last_subrequests;
		target->consume_last_blocks = report.consume_last_blocks;
		target->consume_last_max = report.consume_last_max;
		memcpy(target->consume_last_digest,
		    report.consume_last_digest,
		    sizeof(target->consume_last_digest));
		target->consume_fail_index = report.consume_fail_index;
		target->consume_fail_reason = report.consume_fail_reason;
		target->consume_fail_status = report.consume_fail_status;
		target->consume_fail_actual = report.consume_fail_actual;
		target->consume_fail_target = report.consume_fail_target;
		target->consume_fail_start = report.consume_fail_start;
		target->consume_fail_length = report.consume_fail_length;
		target->consume_fail_span = report.consume_fail_span;
		target->consume_fail_requested =
		    report.consume_fail_requested;
		target->consume_fail_completed =
		    report.consume_fail_completed;
		memcpy(target->consume_fail_digest,
		    report.consume_fail_digest,
		    sizeof(target->consume_fail_digest));
		target->width = report.width;
		target->width_cases = report.width_cases;
		target->width_passed = report.width_passed;
		target->width_failed = report.width_failed;
		target->width_reason = report.width_reason;
		target->width_complete = report.width_complete;
		target->width_switch = report.width_switch;
		target->width_verified = report.width_verified;
		target->width_hb = report.width_hb;
		target->width_ha = report.width_ha;
		target->width_rb = report.width_rb;
		target->width_cmd6 = report.width_cmd6;
		target->width_revert = report.width_revert;
		target->width_rverified = report.width_rverified;
		target->width_rhb = report.width_rhb;
		target->width_rha = report.width_rha;
		target->width_rcmd6 = report.width_rcmd6;
		target->width_reads = report.width_reads;
		target->width_bytes = report.width_bytes;
		target->width_blocks = report.width_blocks;
		target->width_subrequests = report.width_subrequests;
		target->width_max = report.width_max;
		target->width_match = report.width_match;
		target->width_mismatch = report.width_mismatch;
		target->width_fails = report.width_fails;
		target->width_cancel = report.width_cancel;
		target->width_budget = report.width_budget;
		target->width_limit = report.width_limit;
		target->width_segments = report.width_segments;
		target->width_capacity = report.width_capacity;
		target->width_last_index = report.width_last_index;
		target->width_last_lba = report.width_last_lba;
		target->width_last_start = report.width_last_start;
		target->width_last_length = report.width_last_length;
		target->width_last_span = report.width_last_span;
		target->width_last_status = report.width_last_status;
		target->width_last_actual = report.width_last_actual;
		target->width_last_subrequests =
		    report.width_last_subrequests;
		target->width_last_blocks = report.width_last_blocks;
		target->width_last_max = report.width_last_max;
		memcpy(target->width_last_digest,
		    report.width_last_digest,
		    sizeof(target->width_last_digest));
		memcpy(target->width_last_predigest,
		    report.width_last_predigest,
		    sizeof(target->width_last_predigest));
		target->width_last_match = report.width_last_match;
		target->width_fail_stage = report.width_fail_stage;
		target->width_fail_status = report.width_fail_status;
		target->width_fail_actual = report.width_fail_actual;
		memcpy(target->width_fail_digest,
		    report.width_fail_digest,
		    sizeof(target->width_fail_digest));
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

uint64_t
Gta4lEmmcBlock::breakupElapsedMs()
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
	return (now - breakupReadStart) / ticks_per_ms;
}

void
Gta4lEmmcBlock::releaseBreakupMedia()
{
	if (breakupBootOpen && (breakupBoot != nullptr)) {
		breakupBoot->close(this, 0);
		breakupBootOpen = false;
	}
	if (breakupBoot != nullptr) {
		breakupBoot->release();
		breakupBoot = nullptr;
	}
	if (breakupWholeOpen && (breakupWhole != nullptr)) {
		breakupWhole->close(this, 0);
		breakupWholeOpen = false;
	}
	if (breakupWhole != nullptr) {
		breakupWhole->release();
		breakupWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::breakupReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.breakup_reason = reason;
	report.breakup_complete = 0;
	breakupActive = false;
	if (reason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA) {
		report.breakup_media = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_PART) {
		report.breakup_part = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_BUDGET) {
		report.breakup_budget = 1;
	}
	IOLockUnlock(reportLock);
	releaseBreakupMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::breakupReportCancel()
{
	IOLockLock(reportLock);
	report.breakup_cancel = 1;
	report.breakup_reason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_CANCEL;
	report.breakup_complete = 0;
	breakupActive = false;
	IOLockUnlock(reportLock);
	releaseBreakupMedia();
	reportDone = true;
}

void
Gta4lEmmcBlock::breakupObserve(IOMemoryDescriptor *buffer, UInt64 block,
    UInt64 nblks)
{
	uint32_t tag;
	uint32_t order = 0;
	uint64_t end;

	if (!breakupMode || (buffer == nullptr)) {
		return;
	}
	tag = (uint32_t)buffer->getTag();
	IOLockLock(reportLock);
	if (!breakupActive) {
		IOLockUnlock(reportLock);
		return;
	}
	if ((tag == 0) &&
	    ((block < breakupActiveFirst) || (block >= breakupActiveEnd))) {
		if (breakupObservedForeign != 0xffffffffu) {
			breakupObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return;
	}
	if ((tag != 0) && (tag != breakupActiveTag)) {
		if (breakupObservedForeign != 0xffffffffu) {
			breakupObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return;
	}
	if (nblks > GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS) {
		order = 1;
	}
	if ((breakupActiveMax != 0) && (nblks > breakupActiveMax)) {
		order = 1;
	}
	if ((breakupActiveSubrequests != 0) &&
	    (breakupObservedSubrequests >= breakupActiveSubrequests)) {
		order = 1;
	}
	if (block != breakupActiveNext) {
		order = 1;
	}
	end = (nblks > (~0ull - block)) ? ~0ull : (block + nblks);
	if (end > breakupActiveEnd) {
		order = 1;
	}
	if (breakupObservedSubrequests != 0xffffffffu) {
		breakupObservedSubrequests++;
	}
	if (nblks > 0xffffffffull) {
		breakupObservedBlocks = 0xffffffffu;
	} else if (breakupObservedBlocks <=
	    (0xffffffffu - (uint32_t)nblks)) {
		breakupObservedBlocks += (uint32_t)nblks;
	} else {
		breakupObservedBlocks = 0xffffffffu;
	}
	if (nblks > 0xffffffffull) {
		breakupObservedMax = 0xffffffffu;
	} else if ((uint32_t)nblks > breakupObservedMax) {
		breakupObservedMax = (uint32_t)nblks;
	}
	if (order != 0) {
		breakupObservedOrder = 1;
	} else {
		breakupActiveNext = end;
	}
	IOLockUnlock(reportLock);
}

uint64_t
Gta4lEmmcBlock::sweepElapsedMs()
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
	return (now - sweepReadStart) / ticks_per_ms;
}

void
Gta4lEmmcBlock::releaseSweepMedia()
{
	if (sweepBootOpen && (sweepBoot != nullptr)) {
		sweepBoot->close(this, 0);
		sweepBootOpen = false;
	}
	if (sweepBoot != nullptr) {
		sweepBoot->release();
		sweepBoot = nullptr;
	}
	if (sweepWholeOpen && (sweepWhole != nullptr)) {
		sweepWhole->close(this, 0);
		sweepWholeOpen = false;
	}
	if (sweepWhole != nullptr) {
		sweepWhole->release();
		sweepWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::sweepReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.sweep_reason = reason;
	report.sweep_complete = 0;
	sweepActive = false;
	if (reason == GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA) {
		report.sweep_media = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_SWEEP_REASON_PART) {
		report.sweep_part = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_SWEEP_REASON_BUDGET) {
		report.sweep_budget = 1;
	}
	IOLockUnlock(reportLock);
	releaseSweepMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::sweepReportCancel()
{
	IOLockLock(reportLock);
	report.sweep_cancel = 1;
	report.sweep_reason = GTA4L_EMMC_BLOCK_SWEEP_REASON_CANCEL;
	report.sweep_complete = 0;
	sweepActive = false;
	IOLockUnlock(reportLock);
	releaseSweepMedia();
	reportDone = true;
}

void
Gta4lEmmcBlock::sweepObserve(IOMemoryDescriptor *buffer, UInt64 block,
    UInt64 nblks)
{
	uint32_t tag;
	uint32_t order = 0;
	uint64_t end;

	if (!sweepMode || (buffer == nullptr)) {
		return;
	}
	tag = (uint32_t)buffer->getTag();
	IOLockLock(reportLock);
	if (!sweepActive) {
		IOLockUnlock(reportLock);
		return;
	}
	if ((tag == 0) &&
	    ((block < sweepActiveFirst) || (block >= sweepActiveEnd))) {
		if (sweepObservedForeign != 0xffffffffu) {
			sweepObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return;
	}
	if ((tag != 0) && (tag != sweepActiveTag)) {
		if (sweepObservedForeign != 0xffffffffu) {
			sweepObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return;
	}
	if (nblks > GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS) {
		order = 1;
	}
	if ((sweepActiveMax != 0) && (nblks > sweepActiveMax)) {
		order = 1;
	}
	if ((sweepActiveSubrequests != 0) &&
	    (sweepObservedSubrequests >= sweepActiveSubrequests)) {
		order = 1;
	}
	if (block != sweepActiveNext) {
		order = 1;
	}
	end = (nblks > (~0ull - block)) ? ~0ull : (block + nblks);
	if (end > sweepActiveEnd) {
		order = 1;
	}
	if (sweepObservedSubrequests != 0xffffffffu) {
		sweepObservedSubrequests++;
	}
	if (nblks > 0xffffffffull) {
		sweepObservedBlocks = 0xffffffffu;
	} else if (sweepObservedBlocks <=
	    (0xffffffffu - (uint32_t)nblks)) {
		sweepObservedBlocks += (uint32_t)nblks;
	} else {
		sweepObservedBlocks = 0xffffffffu;
	}
	if (nblks > 0xffffffffull) {
		sweepObservedMax = 0xffffffffu;
	} else if ((uint32_t)nblks > sweepObservedMax) {
		sweepObservedMax = (uint32_t)nblks;
	}
	if (order != 0) {
		sweepObservedOrder = 1;
	} else {
		sweepActiveNext = end;
	}
	IOLockUnlock(reportLock);
}

int
Gta4lEmmcBlock::readSweepReference(uint64_t firstBlock, uint32_t blocks,
    uint32_t *readBytes, int32_t *failStatus)
{
	uint32_t index;
	int result;

	if ((readBytes == nullptr) || (failStatus == nullptr) ||
	    (gate == nullptr)) {
		return -1;
	}
	*readBytes = 0;
	*failStatus = 0;
	IOLockLock(gate);
	for (index = 0; index < blocks; index++) {
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
		if ((gta4l_test.sweep_reference_short_at >= 0) &&
		    ((uint32_t)gta4l_test.sweep_reference_short_at ==
		    gta4l_test.read_calls)) {
			*readBytes += (index *
			    GTA4L_EMMC_BLOCK_BYTES) + 1u;
			IOLockUnlock(gate);
			return 0;
		}
#endif
		result = gta4l_emmc_read_block(&host, firstBlock + index,
		    sweepReference +
		    ((uint64_t)index * GTA4L_EMMC_BLOCK_BYTES));
		if (result != 0) {
			*failStatus = (int32_t)result;
			IOLockUnlock(gate);
			return -1;
		}
		*readBytes += GTA4L_EMMC_BLOCK_BYTES;
	}
	IOLockUnlock(gate);
	return 0;
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
Gta4lEmmcBlock::runBreakup()
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
	uint32_t subrequests = 0;
	uint32_t maxObserved = 0;
	uint32_t foreign = 0;
	uint32_t fails = 0;
	uint32_t reason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE;
	uint32_t complete;
	uint32_t budget = 0;
	uint32_t writable;
	uint64_t wholeSize;
	OSBoolean *writableProperty;
	OSString *contentProperty;
	int check;
	uint8_t digest[32];

	bzero(digest, sizeof(digest));
	breakupWhole = nullptr;
	breakupBoot = nullptr;
	breakupWholeOpen = false;
	breakupBootOpen = false;
	IOLockLock(reportLock);
	breakupActive = false;
	breakupObservedSubrequests = 0;
	breakupObservedBlocks = 0;
	breakupObservedMax = 0;
	breakupObservedOrder = 0;
	breakupObservedForeign = 0;
	report.breakup_cases = gta4l_emmc_block_breakup_case_count();
	report.breakup_reason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA;
	report.breakup_capacity = capacityBlocks;
	report.breakup_segments = GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS;
	report.breakup_limit = GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS;
	IOLockUnlock(reportLock);

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			breakupReportCancel();
			return;
		}
		wholeCount = 0;
		breakupWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			breakupReportFailure(
			    GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA);
			return;
		}
		if ((breakupWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (breakupWhole != nullptr) {
			breakupWhole->release();
			breakupWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (breakupWhole == nullptr) {
		breakupReportFailure(GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    breakupWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    breakupWhole->getProperty(kIOMediaContentKey));
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
	report.breakup_capacity = capacityBlocks;
	IOLockUnlock(reportLock);
	check = gta4l_emmc_block_media_check(0, wholeSize, capacityBlocks, 1,
	    (int)writable);
	if (check != 0) {
		breakupReportFailure(GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA);
		return;
	}
	if (!breakupWhole->open(this, 0, kIOStorageAccessReader)) {
		breakupReportFailure(GTA4L_EMMC_BLOCK_BREAKUP_REASON_OPEN);
		return;
	}
	breakupWholeOpen = true;
	breakupReadStart = gta4l_emmc_platform_counter();

	cases = gta4l_emmc_block_breakup_case_count();
	for (index = 0; index < cases; index++) {
		const struct gta4l_emmc_block_breakup_case *item =
		    gta4l_emmc_block_breakup_case_at(index);
		IOMedia *media = nullptr;
		IOMemoryDescriptor *descriptor;
		UInt64 actual = 0;
		uint64_t start = 0;
		uint64_t limit = 0;
		uint64_t mediaBase = 0;
		uint64_t firstBlock = 0;
		uint64_t endBlock = 0;
		uint32_t length = 0;
		uint32_t caseSpan = 0;
		uint32_t caseBlocks = 0;
		uint32_t caseSubrequests = 0;
		uint32_t caseMax = 0;
		uint32_t caseTag = index + 1u;
		uint32_t caseReason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE;
		uint32_t observedSubrequests = 0;
		uint32_t observedBlocks = 0;
		uint32_t observedMax = 0;
		uint32_t observedOrder = 0;
		uint32_t ok = 0;
		int32_t caseStatus = 0;
		uint64_t caseActual = 0;
		IOReturn status;

		bzero(digest, sizeof(digest));
		if (reportCancel) {
			breakupReportCancel();
			return;
		}
		if (breakupElapsedMs() >=
		    GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS) {
			budget = 1;
			break;
		}
		if (item == nullptr) {
			caseReason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
		} else {
			start = item->start;
			length = item->length;
			if ((length == 0u) ||
			    (length > GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
			}
		}
		if ((item != nullptr) &&
		    (caseReason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE)) {
			if (item->target ==
			    GTA4L_EMMC_BLOCK_BREAKUP_TARGET_WHOLE) {
				media = breakupWhole;
				limit = wholeSize;
			} else if (item->target ==
			    GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT) {
				if (breakupBoot == nullptr) {
					uint32_t waitPart = 0;

					while (waitPart <
					    GTA4L_EMMC_BLOCK_BREAKUP_DEADLINE_MS) {
						if (reportCancel) {
							breakupReportCancel();
							return;
						}
						partCount = 0;
						breakupBoot = findPartitionMedia(
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
						    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES,
						    &partCount);
						if (partCount > 1u) {
							breakupReportFailure(
							    GTA4L_EMMC_BLOCK_BREAKUP_REASON_PART);
							return;
						}
						if ((breakupBoot != nullptr) &&
						    (partCount == 1u)) {
							break;
						}
						if (breakupBoot != nullptr) {
							breakupBoot->release();
							breakupBoot = nullptr;
						}
						IOSleep(
						    GTA4L_EMMC_BLOCK_BREAKUP_STEP_MS);
						waitPart +=
						    GTA4L_EMMC_BLOCK_BREAKUP_STEP_MS;
					}
					if (breakupBoot == nullptr) {
						breakupReportFailure(
						    GTA4L_EMMC_BLOCK_BREAKUP_REASON_PART);
						return;
					}
					if (!breakupBoot->open(this, 0,
					    kIOStorageAccessReader)) {
						breakupReportFailure(
						    GTA4L_EMMC_BLOCK_BREAKUP_REASON_OPEN);
						return;
					}
					breakupBootOpen = true;
				}
				media = breakupBoot;
				limit =
				    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES;
				mediaBase =
				    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES;
			} else {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
			}
		}
		if (caseReason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) {
			if ((start > limit) ||
			    ((uint64_t)length > limit - start)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
			} else if (gta4l_emmc_block_breakup_geometry(start,
			    length, &caseSpan, &caseBlocks, &caseSubrequests,
			    &caseMax) != 0) {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
			} else if ((caseSpan != item->span) ||
			    (caseBlocks != item->blocks) ||
			    (caseSubrequests != item->subrequests) ||
			    (caseMax != item->max_blocks)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG;
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) {
			descriptor = IOMemoryDescriptor::withAddress(
			    breakupBuffer, length, kIODirectionIn);
			if (descriptor == nullptr) {
				caseReason =
				    GTA4L_EMMC_BLOCK_BREAKUP_REASON_DESC;
			} else {
				descriptor->setTag(caseTag);
				firstBlock = (mediaBase + start) /
				    GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
				endBlock = firstBlock + caseBlocks;
				IOLockLock(reportLock);
				breakupActive = true;
				breakupActiveTag = caseTag;
				breakupActiveFirst = firstBlock;
				breakupActiveNext = firstBlock;
				breakupActiveEnd = endBlock;
				breakupActiveSubrequests = caseSubrequests;
				breakupActiveMax = caseMax;
				breakupObservedSubrequests = 0;
				breakupObservedBlocks = 0;
				breakupObservedMax = 0;
				breakupObservedOrder = 0;
				IOLockUnlock(reportLock);
				status = media->read(this, start,
				    descriptor, nullptr, &actual);
				IOLockLock(reportLock);
				breakupActive = false;
				observedSubrequests =
				    breakupObservedSubrequests;
				observedBlocks = breakupObservedBlocks;
				observedMax = breakupObservedMax;
				observedOrder = breakupObservedOrder;
				IOLockUnlock(reportLock);
				descriptor->release();
				reads++;
				span += caseSpan;
				blocks += caseSpan /
				    GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
				bytes += length;
				subrequests += observedSubrequests;
				if (observedMax > maxObserved) {
					maxObserved = observedMax;
				}
				caseStatus = (int32_t)status;
				caseActual = actual;
				if (status != kIOReturnSuccess) {
					caseReason =
					    GTA4L_EMMC_BLOCK_BREAKUP_REASON_READ;
					fails++;
				} else if (actual != length) {
					caseReason =
					    GTA4L_EMMC_BLOCK_BREAKUP_REASON_SHORT;
					fails++;
				} else {
					gta4l_emmc_block_sha256(
					    breakupBuffer, length, digest);
					if (memcmp(digest, item->expected,
					    sizeof(digest)) != 0) {
						caseReason =
						    GTA4L_EMMC_BLOCK_BREAKUP_REASON_HASH;
					} else if (observedOrder != 0) {
						caseReason =
						    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER;
					} else if ((observedSubrequests !=
					    caseSubrequests) ||
					    (observedBlocks != caseBlocks) ||
					    (observedMax != caseMax)) {
						caseReason =
						    GTA4L_EMMC_BLOCK_BREAKUP_REASON_SPLIT;
					} else {
						ok = 1;
					}
				}
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) {
			passed++;
		} else {
			failed++;
			if (reason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) {
				reason = caseReason;
				IOLockLock(reportLock);
				report.breakup_fail_index = index;
				report.breakup_fail_reason = caseReason;
				report.breakup_fail_status = caseStatus;
				report.breakup_fail_actual = caseActual;
				report.breakup_fail_target =
				    (item != nullptr) ? item->target : 0u;
				report.breakup_fail_start = start;
				report.breakup_fail_length = length;
				report.breakup_fail_span = caseSpan;
				report.breakup_fail_subrequests =
				    observedSubrequests;
				report.breakup_fail_blocks = observedBlocks;
				report.breakup_fail_max = observedMax;
				memcpy(report.breakup_fail_digest, digest,
				    sizeof(report.breakup_fail_digest));
				IOLockUnlock(reportLock);
			}
		}
		IOLockLock(reportLock);
		report.breakup_passed = passed;
		report.breakup_failed = failed;
		report.breakup_reads = reads;
		report.breakup_blocks = blocks;
		report.breakup_bytes = bytes;
		report.breakup_span = span;
		report.breakup_subrequests = subrequests;
		report.breakup_max = maxObserved;
		report.breakup_fails = fails;
		report.breakup_last_index = index;
		report.breakup_last_target =
		    (item != nullptr) ? item->target : 0u;
		report.breakup_last_start = start;
		report.breakup_last_length = length;
		report.breakup_last_span = caseSpan;
		report.breakup_last_status = caseStatus;
		report.breakup_last_actual = caseActual;
		report.breakup_last_subrequests = observedSubrequests;
		report.breakup_last_blocks = observedBlocks;
		report.breakup_last_max = observedMax;
		if (ok != 0) {
			memcpy(report.breakup_last_digest, digest,
			    sizeof(report.breakup_last_digest));
		} else {
			bzero(report.breakup_last_digest,
			    sizeof(report.breakup_last_digest));
		}
		IOLockUnlock(reportLock);
	}

	if (reportCancel) {
		breakupReportCancel();
		return;
	}

	IOLockLock(reportLock);
	foreign = breakupObservedForeign;
	report.breakup_cases = cases;
	report.breakup_passed = passed;
	report.breakup_failed = failed;
	report.breakup_reason = (reason !=
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) ? reason :
	    ((foreign != 0) ? GTA4L_EMMC_BLOCK_BREAKUP_REASON_FOREIGN :
	    ((budget != 0) ? GTA4L_EMMC_BLOCK_BREAKUP_REASON_BUDGET :
	    GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE));
	report.breakup_reads = reads;
	report.breakup_blocks = blocks;
	report.breakup_bytes = bytes;
	report.breakup_span = span;
	report.breakup_subrequests = subrequests;
	report.breakup_max = maxObserved;
	report.breakup_foreign = foreign;
	report.breakup_fails = fails;
	if (budget != 0) {
		report.breakup_budget = 1;
	}
	complete = ((report.breakup_cases != 0u) &&
	    (report.breakup_passed == report.breakup_cases) &&
	    (report.breakup_failed == 0u) &&
	    (report.breakup_reason == GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE) &&
	    (report.breakup_cancel == 0u) &&
	    (report.breakup_budget == 0u) &&
	    (report.breakup_media == 0u) &&
	    (report.breakup_part == 0u) &&
	    (report.breakup_foreign == 0u) &&
	    (report.breakup_limit ==
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS) &&
	    (report.breakup_segments ==
	    GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS)) ? 1u : 0u;
	report.breakup_complete = complete;
	IOLockUnlock(reportLock);
	releaseBreakupMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::runSweep()
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
	uint32_t subrequests = 0;
	uint32_t maxObserved = 0;
	uint32_t foreign = 0;
	uint32_t fails = 0;
	uint32_t refs = 0;
	uint32_t refbytes = 0;
	uint32_t anchorMatched = 0;
	uint32_t mismatches = 0;
	uint32_t reason = GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE;
	uint32_t complete;
	uint32_t budget = 0;
	uint32_t tableValid = 1;
	uint32_t writable;
	uint32_t bootBlocks;
	uint64_t wholeSize;
	uint64_t bootSize;
	uint64_t tableStart;
	OSBoolean *writableProperty;
	OSString *contentProperty;
	OSNumber *bootSizeProperty;
	int check;
	uint8_t digest[32];
	uint8_t refdigest[32];

	bzero(digest, sizeof(digest));
	bzero(refdigest, sizeof(refdigest));
	sweepWhole = nullptr;
	sweepBoot = nullptr;
	sweepWholeOpen = false;
	sweepBootOpen = false;
	IOLockLock(reportLock);
	sweepActive = false;
	sweepObservedSubrequests = 0;
	sweepObservedBlocks = 0;
	sweepObservedMax = 0;
	sweepObservedOrder = 0;
	sweepObservedForeign = 0;
	report.sweep = 1;
	report.sweep_cases = gta4l_emmc_block_sweep_case_count();
	report.sweep_reason = GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA;
	report.sweep_capacity = capacityBlocks;
	report.sweep_segments = GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS;
	report.sweep_limit = GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS;
	IOLockUnlock(reportLock);

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			sweepReportCancel();
			return;
		}
		wholeCount = 0;
		sweepWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			sweepReportFailure(
			    GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA);
			return;
		}
		if ((sweepWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (sweepWhole != nullptr) {
			sweepWhole->release();
			sweepWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (sweepWhole == nullptr) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    sweepWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    sweepWhole->getProperty(kIOMediaContentKey));
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
	report.sweep_capacity = capacityBlocks;
	IOLockUnlock(reportLock);
	check = gta4l_emmc_block_media_check(0, wholeSize, capacityBlocks, 1,
	    (int)writable);
	if (check != 0) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA);
		return;
	}
	if (!sweepWhole->open(this, 0, kIOStorageAccessReader)) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_OPEN);
		return;
	}
	sweepWholeOpen = true;

	waited = 0;
	while (waited < GTA4L_EMMC_BLOCK_SWEEP_DEADLINE_MS) {
		if (reportCancel) {
			sweepReportCancel();
			return;
		}
		partCount = 0;
		sweepBoot = findPartitionMedia(
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES, &partCount);
		if (partCount > 1u) {
			sweepReportFailure(
			    GTA4L_EMMC_BLOCK_SWEEP_REASON_PART);
			return;
		}
		if ((sweepBoot != nullptr) && (partCount == 1u)) {
			break;
		}
		if (sweepBoot != nullptr) {
			sweepBoot->release();
			sweepBoot = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_SWEEP_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_SWEEP_STEP_MS;
	}
	if (sweepBoot == nullptr) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_PART);
		return;
	}
	bootSizeProperty = OSDynamicCast(OSNumber,
	    sweepBoot->getProperty(kIOMediaSizeKey));
	if (bootSizeProperty == nullptr) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_PART);
		return;
	}
	bootSize = bootSizeProperty->unsigned64BitValue();
	if ((bootSize != GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) ||
	    ((bootSize % GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES) != 0u) ||
	    ((bootSize / GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES) >
	    (uint64_t)0xffffffffu)) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_PART);
		return;
	}
	bootBlocks = (uint32_t)(bootSize /
	    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES);
	if (bootBlocks != (uint32_t)
	    (GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES /
	    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES)) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_PART);
		return;
	}
	if (!sweepBoot->open(this, 0, kIOStorageAccessReader)) {
		sweepReportFailure(GTA4L_EMMC_BLOCK_SWEEP_REASON_OPEN);
		return;
	}
	sweepBootOpen = true;

	cases = gta4l_emmc_block_sweep_case_count();
	for (index = 0; index < cases; index++) {
		const struct gta4l_emmc_block_sweep_case *item =
		    gta4l_emmc_block_sweep_case_at(index);
		uint32_t caseReason = GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE;

		tableStart = 0;
		if (item == nullptr) {
			caseReason = GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
		} else if (item->runtime != 0u) {
			uint64_t startBlock = 0;

			if ((index <
			    GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) ||
			    (gta4l_emmc_block_sweep_derive(
			    index - GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES,
			    item->blocks, bootBlocks, &startBlock) != 0)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
			} else {
				tableStart = startBlock *
				    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
			}
		} else {
			tableStart = item->start;
		}
		if ((caseReason ==
		    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) &&
		    (gta4l_emmc_block_sweep_validate(item, tableStart,
		    bootBlocks, GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES)
		    != 0)) {
			caseReason = GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
		}
		if (caseReason != GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
			tableValid = 0;
			failed = 1;
			reason = GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
			IOLockLock(reportLock);
			report.sweep_fail_index = index;
			report.sweep_fail_reason =
			    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
			report.sweep_fail_target =
			    (item != nullptr) ? item->target : 0u;
			report.sweep_fail_start = tableStart;
			report.sweep_fail_length =
			    (item != nullptr) ? item->length : 0u;
			report.sweep_fail_span =
			    (item != nullptr) ? item->span : 0u;
			IOLockUnlock(reportLock);
			break;
		}
	}
	if (tableValid != 0u) {
		sweepReadStart = gta4l_emmc_platform_counter();
	}

	for (index = 0; (tableValid != 0u) && (index < cases); index++) {
		const struct gta4l_emmc_block_sweep_case *item =
		    gta4l_emmc_block_sweep_case_at(index);
		IOMemoryDescriptor *descriptor;
		UInt64 actual = 0;
		uint64_t start = 0;
		uint64_t firstBlock = 0;
		uint64_t endBlock = 0;
		uint32_t length = 0;
		uint32_t caseTag = index + 1u;
		uint32_t caseReason = GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE;
		uint32_t observedSubrequests = 0;
		uint32_t observedBlocks = 0;
		uint32_t observedMax = 0;
		uint32_t observedOrder = 0;
		uint32_t ok = 0;
		uint32_t caseHashed = 0;
		uint32_t caseRef = 0;
		uint32_t caseRefbytes = 0;
		int32_t caseRefstatus = 0;
		uint32_t caseMvalid = 0;
		uint32_t caseMismatch = 0;
		uint32_t refBytes = 0;
		int32_t refStatus = 0;
		int32_t caseStatus = 0;
		uint64_t caseActual = 0;
		uint32_t mismatchOffset;
		IOReturn status;

		bzero(digest, sizeof(digest));
		bzero(refdigest, sizeof(refdigest));
		if (reportCancel) {
			sweepReportCancel();
			return;
		}
		if (sweepElapsedMs() >=
		    GTA4L_EMMC_BLOCK_SWEEP_READ_BUDGET_MS) {
			budget = 1;
			break;
		}
		if (item == nullptr) {
			caseReason = GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
		} else {
			start = item->start;
			length = item->length;
			if (item->runtime != 0u) {
				uint64_t startBlock = 0;

				if ((index <
				    GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) ||
				    (gta4l_emmc_block_sweep_derive(
				    index -
				    GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES,
				    item->blocks, bootBlocks,
				    &startBlock) != 0)) {
					caseReason =
					    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
				} else {
					start = startBlock *
					    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
				}
			}
			if ((caseReason ==
			    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) &&
			    (gta4l_emmc_block_sweep_validate(item, start,
			    bootBlocks,
			    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES)
			    != 0)) {
				caseReason =
				    GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG;
			}
		}
		if (caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
			firstBlock = (GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES +
			    start) / GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
			if (readSweepReference(firstBlock, item->blocks,
			    &refBytes, &refStatus) != 0) {
				caseReason =
				    GTA4L_EMMC_BLOCK_SWEEP_REASON_REF;
				caseRefbytes = refBytes;
				caseRefstatus = refStatus;
				refbytes += refBytes;
			} else if (refBytes !=
			    item->blocks * GTA4L_EMMC_BLOCK_BYTES) {
				caseReason =
				    GTA4L_EMMC_BLOCK_SWEEP_REASON_REF;
				caseRefbytes = refBytes;
				caseRefstatus = 0;
				refbytes += refBytes;
			} else {
				refs++;
				refbytes += refBytes;
				caseRef = 1;
				caseRefbytes = refBytes;
				caseRefstatus = 0;
			}
		}
		if (caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
			descriptor = IOMemoryDescriptor::withAddress(
			    sweepBuffer, length, kIODirectionIn);
			if (descriptor == nullptr) {
				caseReason =
				    GTA4L_EMMC_BLOCK_SWEEP_REASON_DESC;
			} else {
				descriptor->setTag(caseTag);
				endBlock = firstBlock + item->blocks;
				IOLockLock(reportLock);
				sweepActive = true;
				sweepActiveTag = caseTag;
				sweepActiveFirst = firstBlock;
				sweepActiveNext = firstBlock;
				sweepActiveEnd = endBlock;
				sweepActiveSubrequests = item->subrequests;
				sweepActiveMax = item->max_blocks;
				sweepObservedSubrequests = 0;
				sweepObservedBlocks = 0;
				sweepObservedMax = 0;
				sweepObservedOrder = 0;
				IOLockUnlock(reportLock);
				status = sweepBoot->read(this, start,
				    descriptor, nullptr, &actual);
				IOLockLock(reportLock);
				sweepActive = false;
				observedSubrequests =
				    sweepObservedSubrequests;
				observedBlocks = sweepObservedBlocks;
				observedMax = sweepObservedMax;
				observedOrder = sweepObservedOrder;
				IOLockUnlock(reportLock);
				descriptor->release();
				reads++;
				span += item->span;
				blocks += item->blocks;
				bytes += length;
				subrequests += observedSubrequests;
				if (observedMax > maxObserved) {
					maxObserved = observedMax;
				}
				caseStatus = (int32_t)status;
				caseActual = actual;
				if (status != kIOReturnSuccess) {
					caseReason =
					    GTA4L_EMMC_BLOCK_SWEEP_REASON_READ;
					fails++;
				} else if (actual != length) {
					caseReason =
					    GTA4L_EMMC_BLOCK_SWEEP_REASON_SHORT;
					fails++;
				} else {
					gta4l_emmc_block_sha256(sweepBuffer,
					    length, digest);
					gta4l_emmc_block_sha256(
					    sweepReference, length, refdigest);
					caseHashed = 1;
					if (memcmp(sweepBuffer,
					    sweepReference, length) != 0) {
						mismatchOffset = 0;
						while ((mismatchOffset < length) &&
						    (sweepBuffer[mismatchOffset] ==
						    sweepReference[mismatchOffset])) {
							mismatchOffset++;
						}
						caseMvalid = 1;
						caseMismatch = mismatchOffset;
						caseReason =
						    GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH;
						mismatches++;
					} else if (item->runtime == 0u) {
						if (memcmp(digest,
						    item->expected,
						    sizeof(digest)) != 0) {
							caseReason =
							    GTA4L_EMMC_BLOCK_SWEEP_REASON_HASH;
						} else {
							anchorMatched++;
						}
					}
					if (caseReason ==
					    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
						if (observedOrder != 0) {
							caseReason =
							    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER;
						} else if (
						    (observedSubrequests !=
						    item->subrequests) ||
						    (observedBlocks !=
						    item->blocks) ||
						    (observedMax !=
						    item->max_blocks)) {
							caseReason =
							    GTA4L_EMMC_BLOCK_SWEEP_REASON_SPLIT;
						} else {
							ok = 1;
						}
					}
				}
			}
		}

		if (caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
			passed++;
		} else {
			failed++;
			if (reason ==
			    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) {
				reason = caseReason;
				IOLockLock(reportLock);
				report.sweep_fail_index = index;
				report.sweep_fail_reason = caseReason;
				report.sweep_fail_status = caseStatus;
				report.sweep_fail_actual = caseActual;
				report.sweep_fail_target =
				    (item != nullptr) ? item->target : 0u;
				report.sweep_fail_start = start;
				report.sweep_fail_length = length;
				report.sweep_fail_span =
				    (item != nullptr) ? item->span : 0u;
				report.sweep_fail_subrequests =
				    observedSubrequests;
				report.sweep_fail_blocks = observedBlocks;
				report.sweep_fail_max = observedMax;
				report.sweep_fail_ref = caseRef;
				report.sweep_fail_refbytes = caseRefbytes;
				report.sweep_fail_refstatus = caseRefstatus;
				report.sweep_fail_mvalid = caseMvalid;
				report.sweep_fail_mismatch = caseMismatch;
				if (caseHashed != 0) {
					memcpy(report.sweep_fail_digest,
					    digest,
					    sizeof(report.sweep_fail_digest));
					memcpy(report.sweep_fail_refdigest,
					    refdigest,
					    sizeof(report.sweep_fail_refdigest));
				}
				IOLockUnlock(reportLock);
			}
		}
		IOLockLock(reportLock);
		report.sweep_passed = passed;
		report.sweep_failed = failed;
		report.sweep_reads = reads;
		report.sweep_blocks = blocks;
		report.sweep_bytes = bytes;
		report.sweep_span = span;
		report.sweep_subrequests = subrequests;
		report.sweep_max = maxObserved;
		report.sweep_fails = fails;
		report.sweep_refs = refs;
		report.sweep_refbytes = refbytes;
		report.sweep_anchor = (anchorMatched ==
		    GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) ? 1u : 0u;
		report.sweep_mismatches = mismatches;
		report.sweep_last_index = index;
		report.sweep_last_target =
		    (item != nullptr) ? item->target : 0u;
		report.sweep_last_start = start;
		report.sweep_last_length = length;
		report.sweep_last_span =
		    (item != nullptr) ? item->span : 0u;
		report.sweep_last_status = caseStatus;
		report.sweep_last_actual = caseActual;
		report.sweep_last_subrequests = observedSubrequests;
		report.sweep_last_blocks = observedBlocks;
		report.sweep_last_max = observedMax;
		report.sweep_last_ref = caseRef;
		report.sweep_last_refbytes = caseRefbytes;
		report.sweep_last_refstatus = caseRefstatus;
		report.sweep_last_mvalid = caseMvalid;
		report.sweep_last_mismatch = caseMismatch;
		if (ok != 0) {
			memcpy(report.sweep_last_digest, digest,
			    sizeof(report.sweep_last_digest));
			memcpy(report.sweep_last_refdigest, refdigest,
			    sizeof(report.sweep_last_refdigest));
		} else {
			bzero(report.sweep_last_digest,
			    sizeof(report.sweep_last_digest));
			bzero(report.sweep_last_refdigest,
			    sizeof(report.sweep_last_refdigest));
		}
		IOLockUnlock(reportLock);
		if ((caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG) ||
		    (caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_REF) ||
		    (caseReason == GTA4L_EMMC_BLOCK_SWEEP_REASON_HASH)) {
			break;
		}
	}

	if (reportCancel) {
		sweepReportCancel();
		return;
	}

	IOLockLock(reportLock);
	foreign = sweepObservedForeign;
	report.sweep_cases = cases;
	report.sweep_passed = passed;
	report.sweep_failed = failed;
	report.sweep_reason = (reason !=
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) ? reason :
	    ((foreign != 0) ? GTA4L_EMMC_BLOCK_SWEEP_REASON_FOREIGN :
	    ((budget != 0) ? GTA4L_EMMC_BLOCK_SWEEP_REASON_BUDGET :
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE));
	report.sweep_reads = reads;
	report.sweep_blocks = blocks;
	report.sweep_bytes = bytes;
	report.sweep_span = span;
	report.sweep_subrequests = subrequests;
	report.sweep_max = maxObserved;
	report.sweep_foreign = foreign;
	report.sweep_fails = fails;
	report.sweep_refs = refs;
	report.sweep_refbytes = refbytes;
	report.sweep_anchor = (anchorMatched ==
	    GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES) ? 1u : 0u;
	report.sweep_mismatches = mismatches;
	if (budget != 0) {
		report.sweep_budget = 1;
	}
	complete = ((report.sweep_cases ==
	    GTA4L_EMMC_BLOCK_SWEEP_CASES) &&
	    (report.sweep_passed == report.sweep_cases) &&
	    (report.sweep_failed == 0u) &&
	    (report.sweep_reason ==
	    GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE) &&
	    (report.sweep_reads == report.sweep_cases) &&
	    (report.sweep_refs == report.sweep_cases) &&
	    (report.sweep_refbytes == report.sweep_bytes) &&
	    (report.sweep_span == report.sweep_bytes) &&
	    (report.sweep_blocks ==
	    (report.sweep_bytes / GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES)) &&
	    (report.sweep_subrequests == 13u) &&
	    (report.sweep_max == 8u) &&
	    (report.sweep_foreign == 0u) &&
	    (report.sweep_fails == 0u) &&
	    (report.sweep_cancel == 0u) &&
	    (report.sweep_budget == 0u) &&
	    (report.sweep_media == 0u) &&
	    (report.sweep_part == 0u) &&
	    (report.sweep_anchor == 1u) &&
	    (report.sweep_mismatches == 0u) &&
	    (report.sweep_limit ==
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS) &&
	    (report.sweep_segments ==
	    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS)) ? 1u : 0u;
	report.sweep_complete = complete;
	IOLockUnlock(reportLock);
	releaseSweepMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::runConsume()
{
	uint32_t wholeCount = 0;
	uint32_t partCount = 0;
	uint32_t waited = 0;
	uint32_t reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE;
	uint32_t complete;
	uint32_t writable;
	uint32_t attempted;
	uint32_t observedSubrequests = 0;
	uint32_t observedBlocks = 0;
	uint32_t observedMax = 0;
	uint32_t observedOrder = 0;
	uint32_t observedForeign = 0;
	uint32_t creditedC18 = 0;
	uint32_t creditedC18b = 0;
	uint32_t creditedStops = 0;
	uint64_t wholeSize;
	uint64_t bootSize;
	uint64_t firstBlock;
	uint64_t caseActual = 0;
	int32_t caseStatus = 0;
	OSBoolean *writableProperty;
	OSBoolean *bootWritableProperty;
	OSString *contentProperty;
	OSNumber *bootSizeProperty;
	struct gta4l_emmc_consume_result result;
	int check;

	consumeWhole = nullptr;
	consumeBoot = nullptr;
	consumeClient = nullptr;
	consumeWholeOpen = false;
	consumeBootOpen = false;
	bzero(&result, sizeof(result));
	IOLockLock(reportLock);
	consumeActive = false;
	consumeInFlight = false;
	consumeObservedSubrequests = 0;
	consumeObservedBlocks = 0;
	consumeObservedMax = 0;
	consumeObservedOrder = 0;
	consumeObservedForeign = 0;
	consumeCreditedC18 = 0;
	consumeCreditedC18b = 0;
	consumeCreditedStops = 0;
	report.consume = 1;
	report.consume_cases = GTA4L_EMMC_BLOCK_CONSUME_CASES;
	report.consume_complete = 0;
	report.consume_reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA;
	report.consume_limit = GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS;
	report.consume_segments = GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS;
	report.consume_capacity = capacityBlocks;
	IOLockUnlock(reportLock);

	while (waited < GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS) {
		if (reportCancel) {
			consumeReportCancel();
			return;
		}
		wholeCount = 0;
		consumeWhole = findExactWholeMedia(&wholeCount);
		if (wholeCount > 1u) {
			consumeReportFailure(
			    GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA);
			return;
		}
		if ((consumeWhole != nullptr) && (wholeCount == 1u)) {
			break;
		}
		if (consumeWhole != nullptr) {
			consumeWhole->release();
			consumeWhole = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_MEDIA_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_MEDIA_STEP_MS;
	}
	if (consumeWhole == nullptr) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA);
		return;
	}

	wholeSize = (uint64_t)capacityBlocks * GTA4L_EMMC_BLOCK_BYTES;
	writableProperty = OSDynamicCast(OSBoolean,
	    consumeWhole->getProperty(kIOMediaWritableKey));
	contentProperty = OSDynamicCast(OSString,
	    consumeWhole->getProperty(kIOMediaContentKey));
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
	IOLockUnlock(reportLock);
	check = gta4l_emmc_block_media_check(0, wholeSize, capacityBlocks, 1,
	    (int)writable);
	if (check != 0) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA);
		return;
	}
	if (!consumeWhole->open(this, 0, kIOStorageAccessReader)) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_OPEN);
		return;
	}
	consumeWholeOpen = true;

	waited = 0;
	while (waited < GTA4L_EMMC_BLOCK_CONSUME_DEADLINE_MS) {
		if (reportCancel) {
			consumeReportCancel();
			return;
		}
		partCount = 0;
		consumeBoot = findPartitionMedia(
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES,
		    GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES, &partCount);
		if (partCount > 1u) {
			consumeReportFailure(
			    GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);
			return;
		}
		if ((consumeBoot != nullptr) && (partCount == 1u)) {
			break;
		}
		if (consumeBoot != nullptr) {
			consumeBoot->release();
			consumeBoot = nullptr;
		}
		IOSleep(GTA4L_EMMC_BLOCK_CONSUME_STEP_MS);
		waited += GTA4L_EMMC_BLOCK_CONSUME_STEP_MS;
	}
	if (consumeBoot == nullptr) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);
		return;
	}
	bootSizeProperty = OSDynamicCast(OSNumber,
	    consumeBoot->getProperty(kIOMediaSizeKey));
	if (bootSizeProperty == nullptr) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);
		return;
	}
	bootSize = bootSizeProperty->unsigned64BitValue();
	if (bootSize != GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);
		return;
	}
	bootWritableProperty = OSDynamicCast(OSBoolean,
	    consumeBoot->getProperty(kIOMediaWritableKey));
	if ((bootWritableProperty == nullptr) ||
	    bootWritableProperty->getValue()) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);
		return;
	}
	if (gta4l_emmc_block_consume_validate(0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES, bootSize) != 0) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_ARG);
		return;
	}
	if (reportCancel) {
		consumeReportCancel();
		return;
	}
	consumeReadStart = gta4l_emmc_platform_counter();

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
	if (gta4l_test.consume_client_alloc_fail != 0) {
		consumeClient = nullptr;
	} else {
		consumeClient = new Gta4lEmmcConsumer();
	}
#else
	consumeClient = new Gta4lEmmcConsumer();
#endif
	if (consumeClient == nullptr) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_CLIENT);
		return;
	}
	if (!consumeClient->init()) {
		consumeClient->release();
		consumeClient = nullptr;
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_CLIENT);
		return;
	}
	IOLockLock(reportLock);
	report.consume_client = 1;
	IOLockUnlock(reportLock);
	if (!consumeBoot->open(consumeClient, 0,
	    kIOStorageAccessReader)) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_OPEN);
		return;
	}
	consumeBootOpen = true;
	IOLockLock(reportLock);
	report.consume_open = 1;
	IOLockUnlock(reportLock);
	if (reportCancel) {
		consumeReportCancel();
		return;
	}
	if (consumeElapsedMs() >=
	    GTA4L_EMMC_BLOCK_CONSUME_READ_MS) {
		consumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_BUDGET);
		return;
	}

	firstBlock = GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES /
	    GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES;
	IOLockLock(reportLock);
	consumeActive = true;
	consumeInFlight = false;
	consumeActiveTag = GTA4L_EMMC_BLOCK_CONSUME_CASE_TAG;
	consumeActiveFirst = firstBlock;
	consumeActiveBlocks = GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
	consumeActiveEnd = firstBlock +
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS;
	consumeObservedSubrequests = 0;
	consumeObservedBlocks = 0;
	consumeObservedMax = 0;
	consumeObservedOrder = 0;
	consumeObservedForeign = 0;
	IOLockUnlock(reportLock);

	(void)consumeClient->run(consumeBoot, 0,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES,
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_TAG, &result);

	IOLockLock(reportLock);
	consumeActive = false;
	consumeInFlight = false;
	observedSubrequests = consumeObservedSubrequests;
	observedBlocks = consumeObservedBlocks;
	observedMax = consumeObservedMax;
	observedOrder = consumeObservedOrder;
	observedForeign = consumeObservedForeign;
	creditedC18 = consumeCreditedC18;
	creditedC18b = consumeCreditedC18b;
	creditedStops = consumeCreditedStops;
	IOLockUnlock(reportLock);

	attempted = result.attempted;
	caseStatus = result.status;
	caseActual = result.actual;
	reason = result.reason;
	if ((reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&
	    (observedForeign != 0u)) {
		reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_FOREIGN;
	}
	if ((reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&
	    ((observedOrder != 0u) || (observedSubrequests != 1u))) {
		reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ORDER;
	}

	if (reportCancel) {
		consumeReportCancel();
		return;
	}

	if (consumeBootOpen && (consumeBoot != nullptr)) {
		consumeBoot->close(consumeClient, 0);
		consumeBootOpen = false;
		IOLockLock(reportLock);
		report.consume_close = 1;
		IOLockUnlock(reportLock);
	}
	if (consumeClient != nullptr) {
		consumeClient->release();
		consumeClient = nullptr;
	}
	releaseConsumeMedia();

	IOLockLock(reportLock);
	report.consume_reason = reason;
	report.consume_passed = (reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) ? 1u : 0u;
	report.consume_failed = (reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) ? 0u : 1u;
	report.consume_reads = attempted;
	report.consume_bytes = (attempted != 0u) ?
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES : 0u;
	report.consume_span = (attempted != 0u) ?
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES : 0u;
	report.consume_blocks = (attempted != 0u) ?
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS : 0u;
	report.consume_subrequests = observedSubrequests;
	report.consume_max = observedMax;
	report.consume_foreign = observedForeign;
	report.consume_fails = ((result.reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_READ) ||
	    (result.reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_SHORT)) ?
	    1u : 0u;
	report.consume_mismatch = 0;
	if (result.reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC) {
		report.consume_alloc = 1;
	}
	report.consume_last_index = 0;
	report.consume_last_target =
	    GTA4L_EMMC_BLOCK_CONSUME_TARGET_BOOT;
	report.consume_last_start = 0;
	report.consume_last_length = GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_last_span = GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
	report.consume_last_status = caseStatus;
	report.consume_last_actual = caseActual;
	report.consume_last_subrequests = observedSubrequests;
	report.consume_last_blocks = observedBlocks;
	report.consume_last_max = observedMax;
	memcpy(report.consume_last_digest, result.digest,
	    sizeof(report.consume_last_digest));
	if (reason != GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) {
		report.consume_fail_index = 0;
		report.consume_fail_reason = reason;
		report.consume_fail_status = caseStatus;
		report.consume_fail_actual = caseActual;
		report.consume_fail_target =
		    GTA4L_EMMC_BLOCK_CONSUME_TARGET_BOOT;
		report.consume_fail_start = 0;
		report.consume_fail_length =
		    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
		report.consume_fail_span =
		    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES;
		report.consume_fail_requested = (attempted != 0u) ?
		    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS : 0u;
		report.consume_fail_completed =
		    (caseActual > (uint64_t)GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES) ?
		    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS :
		    (uint32_t)(caseActual /
		    GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES);
		memcpy(report.consume_fail_digest, result.digest,
		    sizeof(report.consume_fail_digest));
	}
	complete = ((report.consume_cases ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASES) &&
	    (report.consume_passed == report.consume_cases) &&
	    (report.consume_failed == 0u) &&
	    (report.consume_reason ==
	    GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&
	    (report.consume_reads == 1u) &&
	    (report.consume_bytes ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES) &&
	    (report.consume_span ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES) &&
	    (report.consume_blocks ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS) &&
	    (report.consume_subrequests == 1u) &&
	    (report.consume_max ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS) &&
	    (report.consume_foreign == 0u) &&
	    (report.consume_fails == 0u) &&
	    (report.consume_cancel == 0u) &&
	    (report.consume_budget == 0u) &&
	    (report.consume_media == 0u) &&
	    (report.consume_part == 0u) &&
	    (report.consume_open == 1u) &&
	    (report.consume_close == 1u) &&
	    (report.consume_alloc == 0u) &&
	    (report.consume_client == 1u) &&
	    (report.consume_mismatch == 0u) &&
	    (report.consume_limit ==
	    GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS) &&
	    (report.consume_segments ==
	    GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS) &&
	    (creditedC18 == 1u) &&
	    (creditedC18b ==
	    GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS) &&
	    (creditedStops == 1u)) ? 1u : 0u;
	report.consume_complete = complete;
	IOLockUnlock(reportLock);
	panicWithReport();
}

void
Gta4lEmmcBlock::releaseConsumeMedia()
{
	if (consumeBootOpen && (consumeBoot != nullptr)) {
		consumeBoot->close(consumeClient, 0);
		consumeBootOpen = false;
		IOLockLock(reportLock);
		report.consume_close = 1;
		IOLockUnlock(reportLock);
	}
	if (consumeClient != nullptr) {
		consumeClient->release();
		consumeClient = nullptr;
	}
	if (consumeBoot != nullptr) {
		consumeBoot->release();
		consumeBoot = nullptr;
	}
	if (consumeWholeOpen && (consumeWhole != nullptr)) {
		consumeWhole->close(this, 0);
		consumeWholeOpen = false;
	}
	if (consumeWhole != nullptr) {
		consumeWhole->release();
		consumeWhole = nullptr;
	}
}

void
Gta4lEmmcBlock::consumeReportFailure(uint32_t reason)
{
	IOLockLock(reportLock);
	report.consume_reason = reason;
	report.consume_failed = 1;
	report.consume_complete = 0;
	report.consume_fail_reason = reason;
	consumeActive = false;
	consumeInFlight = false;
	if (reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA) {
		report.consume_media = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_PART) {
		report.consume_part = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC) {
		report.consume_alloc = 1;
	}
	if (reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_BUDGET) {
		report.consume_budget = 1;
	}
	IOLockUnlock(reportLock);
	releaseConsumeMedia();
	panicWithReport();
}

void
Gta4lEmmcBlock::consumeReportCancel()
{
	IOLockLock(reportLock);
	report.consume_cancel = 1;
	report.consume_reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_CANCEL;
	report.consume_complete = 0;
	consumeActive = false;
	consumeInFlight = false;
	IOLockUnlock(reportLock);
	releaseConsumeMedia();
	reportDone = true;
}

uint64_t
Gta4lEmmcBlock::consumeElapsedMs()
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
	return (now - consumeReadStart) / ticks_per_ms;
}

bool
Gta4lEmmcBlock::consumeObserve(IOMemoryDescriptor *buffer, UInt64 block,
    UInt64 nblks)
{
	uint32_t tag;
	uint64_t end;

	if (!consumeMode || (buffer == nullptr)) {
		return false;
	}
	tag = (uint32_t)buffer->getTag();
	IOLockLock(reportLock);
	if (!consumeActive) {
		IOLockUnlock(reportLock);
		return false;
	}
	if ((tag == 0u) || (tag != consumeActiveTag) ||
	    (block != consumeActiveFirst) ||
	    (nblks != consumeActiveBlocks)) {
		if (consumeObservedForeign != 0xffffffffu) {
			consumeObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return false;
	}
	end = (nblks > (~0ull - block)) ? ~0ull : (block + nblks);
	if (end > consumeActiveEnd) {
		if (consumeObservedForeign != 0xffffffffu) {
			consumeObservedForeign++;
		}
		IOLockUnlock(reportLock);
		return false;
	}
	if (consumeInFlight) {
		consumeObservedOrder = 1;
		IOLockUnlock(reportLock);
		return false;
	}
	consumeInFlight = true;
	if (consumeObservedSubrequests != 0xffffffffu) {
		consumeObservedSubrequests++;
	}
	if (nblks > 0xffffffffull) {
		consumeObservedBlocks = 0xffffffffu;
	} else if (consumeObservedBlocks <=
	    (0xffffffffu - (uint32_t)nblks)) {
		consumeObservedBlocks += (uint32_t)nblks;
	} else {
		consumeObservedBlocks = 0xffffffffu;
	}
	if (nblks > 0xffffffffull) {
		consumeObservedMax = 0xffffffffu;
	} else if ((uint32_t)nblks > consumeObservedMax) {
		consumeObservedMax = (uint32_t)nblks;
	}
	IOLockUnlock(reportLock);
	return true;
}

bool
Gta4lEmmcBlock::readWidthWindow(uint8_t *target, uint32_t *status,
    uint64_t *actual, uint32_t *subrequests, uint32_t *blocks,
    uint32_t *max, bool *stopOk)
{
	struct gta4l_emmc_block_counters before;
	struct gta4l_emmc_block_counters after;
	uint64_t completed = 0;
	uint32_t chunkDelta;
	uint32_t blockDelta;
	uint32_t stopDelta;
	uint32_t stopFailDelta;
	int32_t rawStatus;
	int result;

	if ((target == nullptr) || (status == nullptr) || (actual == nullptr) ||
	    (subrequests == nullptr) || (blocks == nullptr) ||
	    (max == nullptr) || (stopOk == nullptr)) {
		return false;
	}
	*status = 0;
	*actual = 0;
	*subrequests = 0;
	*blocks = 0;
	*max = 0;
	*stopOk = false;
	before = block.counters;
	widthCaptureBuffer = target;
	widthCaptureCopied = 0;
	widthCaptureActive = true;
	result = gta4l_emmc_block_read(&block,
	    GTA4L_EMMC_BLOCK_WIDTH_LBA,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES,
	    GTA4L_EMMC_BLOCK_READ, &completed);
	widthCaptureActive = false;
	after = block.counters;
	chunkDelta = (after.cmd18_chunks >= before.cmd18_chunks) ?
	    (uint32_t)(after.cmd18_chunks - before.cmd18_chunks) : 0u;
	blockDelta = (after.blocks >= before.blocks) ?
	    (uint32_t)(after.blocks - before.blocks) : 0u;
	stopDelta = (after.stop_attempts >= before.stop_attempts) ?
	    (uint32_t)(after.stop_attempts - before.stop_attempts) : 0u;
	stopFailDelta = (after.stop_failures >= before.stop_failures) ?
	    (uint32_t)(after.stop_failures - before.stop_failures) : 0u;
	rawStatus = result;
	if ((result != GTA4L_EMMC_BLOCK_OK) &&
	    (block.counters.first_fail_valid != 0) &&
	    (block.counters.first_fail_block >= GTA4L_EMMC_BLOCK_WIDTH_LBA) &&
	    (block.counters.first_fail_block <
	    GTA4L_EMMC_BLOCK_WIDTH_LBA +
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS)) {
		rawStatus = block.counters.first_fail_error;
	}
	*status = (uint32_t)rawStatus;
	*actual = completed;
	*subrequests = (after.requests >= before.requests) ?
	    (uint32_t)(after.requests - before.requests) : 0u;
	*blocks = blockDelta;
	/* The production core issues the two-block case as exactly one
	 * CMD18 chunk, so the largest observed subrequest is the chunk. */
	*max = (chunkDelta == 1u) ?
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS : 0u;
	*stopOk = (result == GTA4L_EMMC_BLOCK_OK) && (chunkDelta == 1u) &&
	    (blockDelta == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) &&
	    (stopDelta == 1u) && (stopFailDelta == 0u) &&
	    (after.last_stop_result == 0);
	return (result == GTA4L_EMMC_BLOCK_OK) &&
	    (completed == GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES) &&
	    (*subrequests == 1u) &&
	    (*blocks == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) &&
	    (*max == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) && *stopOk;
}

void
Gta4lEmmcBlock::widthRecordSwitch(
    const struct gta4l_emmc_bus_result *bus)
{
	if (bus == nullptr) {
		return;
	}
	IOLockLock(reportLock);
	report.width_switch = bus->requested;
	report.width_verified = bus->verified;
	report.width_hb = bus->host_before;
	report.width_ha = bus->host_after;
	report.width_rb = bus->switched;
	report.width_cmd6 = bus->cmd6_attempts;
	IOLockUnlock(reportLock);
}

void
Gta4lEmmcBlock::widthRecordRevert(
    const struct gta4l_emmc_bus_result *bus)
{
	if (bus == nullptr) {
		return;
	}
	IOLockLock(reportLock);
	report.width_revert = bus->requested;
	report.width_rverified = bus->verified;
	report.width_rhb = bus->host_before;
	report.width_rha = bus->host_after;
	report.width_rcmd6 = bus->cmd6_attempts;
	IOLockUnlock(reportLock);
}

void
Gta4lEmmcBlock::widthLatchFailure(uint32_t stage, int32_t status,
    uint64_t actual, const uint8_t *digest)
{
	IOLockLock(reportLock);
	if (widthFailStage == GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE) {
		widthFailStage = stage;
		widthFailStatus = status;
		widthFailActual = actual;
		if (digest != nullptr) {
			memcpy(widthFailDigest, digest,
			    sizeof(widthFailDigest));
		} else {
			bzero(widthFailDigest, sizeof(widthFailDigest));
		}
	}
	report.width_passed = 0;
	report.width_failed = 1;
	report.width_reason = widthFailStage;
	report.width_fail_stage = widthFailStage;
	report.width_fail_status = widthFailStatus;
	report.width_fail_actual = widthFailActual;
	memcpy(report.width_fail_digest, widthFailDigest,
	    sizeof(report.width_fail_digest));
	report.width = 1;
	report.width_cases = GTA4L_EMMC_BLOCK_WIDTH_CASES;
	report.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	report.width_segments = 1u;
	report.width_capacity = capacityBlocks;
	report.width_complete = 0;
	IOLockUnlock(reportLock);
}

void
Gta4lEmmcBlock::widthReportCancel()
{
	widthLatchFailure(GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL, 0, 0,
	    nullptr);
	IOLockLock(reportLock);
	report.width_cancel = 1;
	report.width_reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL;
	report.width_complete = 0;
	IOLockUnlock(reportLock);
	reportDone = true;
}

bool
Gta4lEmmcBlock::widthAttemptRevert()
{
	int result;

	bzero(&widthRevert, sizeof(widthRevert));
	result = gta4l_emmc_set_bus_width_recover(&host, 1u,
	    widthBaseline, &widthRevert);
	widthRevertStatus = result;
	widthRecordRevert(&widthRevert);
	return (result == GTA4L_EMMC_OK) &&
	    (widthRevert.verified == 1u) &&
	    (host.bus_width == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1) &&
	    ((widthRevert.host_after &
	    (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) == 0u);
}

bool
Gta4lEmmcBlock::acquireWidthGate(uint32_t timeoutMs)
{
	uint64_t start;
	uint64_t hz;
	uint64_t deadline;

	if (gate == nullptr) {
		return false;
	}
	hz = gta4l_emmc_platform_counter_hz();
	if (hz == 0u) {
		return false;
	}
	start = gta4l_emmc_platform_counter();
	deadline = ((uint64_t)timeoutMs * hz) / 1000u;
	while (!IOLockTryLock(gate)) {
		if ((gta4l_emmc_platform_counter() - start) >= deadline) {
			return false;
		}
		IOSleep(GTA4L_EMMC_BLOCK_WIDTH_GATE_STEP_MS);
	}
	return true;
}

void
Gta4lEmmcBlock::runWidth()
{
	uint32_t status = 0;
	uint64_t actual = 0;
	uint32_t subrequests = 0;
	uint32_t blocks = 0;
	uint32_t max = 0;
	bool stopOk = false;
	bool readOk;
	bool revertOk;
	uint32_t reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
	uint32_t complete;
	uint32_t match = 0;
	uint64_t startTicks;
	uint64_t elapsedMs = 0;
	uint64_t hz;
	uint64_t ticksPerMs;

	IOLockLock(reportLock);
	report.width = 1;
	report.width_cases = GTA4L_EMMC_BLOCK_WIDTH_CASES;
	report.width_complete = 0;
	report.width_reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
	report.width_switch = widthSwitch.requested;
	report.width_verified = widthSwitch.verified;
	report.width_hb = widthSwitch.host_before;
	report.width_ha = widthSwitch.host_after;
	report.width_rb = widthSwitch.switched;
	report.width_cmd6 = widthSwitch.cmd6_attempts;
	report.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	report.width_segments = 1u;
	report.width_capacity = capacityBlocks;
	IOLockUnlock(reportLock);

	if (reportCancel) {
		widthReportCancel();
		return;
	}

	startTicks = gta4l_emmc_platform_counter();
	bzero(widthObserved, GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES);
	if (!acquireWidthGate(GTA4L_EMMC_BLOCK_WIDTH_GATE_MS)) {
		hz = gta4l_emmc_platform_counter_hz();
		ticksPerMs = hz / 1000u;
		if (ticksPerMs != 0) {
			elapsedMs =
			    (gta4l_emmc_platform_counter() - startTicks) /
			    ticksPerMs;
		}
		/* The in-flight request still owns the gate. Leave the card
		 * at the switched width, take media offline and report a
		 * terminal gate failure without claiming a revert. */
		online = false;
		widthLatchFailure(GTA4L_EMMC_BLOCK_WIDTH_REASON_GATE,
		    0, 0, nullptr);
		IOLockLock(reportLock);
		report.width_budget = (elapsedMs >=
		    GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS) ? 1u : 0u;
		IOLockUnlock(reportLock);
		panicWithReportNoGate();
		return;
	}
	readOk = readWidthWindow(widthObserved, &status, &actual,
	    &subrequests, &blocks, &max, &stopOk);
	if (actual != 0) {
		gta4l_emmc_block_sha256(widthObserved,
		    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES,
		    widthObservedDigest);
	} else {
		bzero(widthObservedDigest, sizeof(widthObservedDigest));
	}
	if (readOk && (memcmp(widthReference, widthObserved,
	    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES) == 0)) {
		match = 1;
	}
	if (!readOk) {
		reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_READ;
	} else if (match == 0u) {
		reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_MATCH;
	}
	/* The revert runs under the same gate as the 4-bit read, including
	 * after a failed or mismatched read. */
	revertOk = widthAttemptRevert();
	if (!revertOk && (reason == GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE)) {
		reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_REVERT;
	}
	/* Any failed 4-bit read, comparison or revert takes the media
	 * offline before the terminal report, matching the read callback
	 * failure contract. The success path stays online for publication. */
	if (reason != GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE) {
		online = false;
	}
	IOLockUnlock(gate);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
	if (gta4l_test_width_hook != nullptr) {
		gta4l_test_width_hook(GTA4L_TEST_WIDTH_PHASE_AFTER_READ);
	}
#endif
	hz = gta4l_emmc_platform_counter_hz();
	ticksPerMs = hz / 1000u;
	if (ticksPerMs != 0) {
		elapsedMs = (gta4l_emmc_platform_counter() - startTicks) /
		    ticksPerMs;
	}

	IOLockLock(reportLock);
	report.width_reads = 2;
	report.width_bytes = (uint32_t)(widthRefActual + actual);
	report.width_blocks = widthRefBlocks + blocks;
	report.width_subrequests = widthRefSubrequests + subrequests;
	report.width_max = (max > widthRefMax) ? max : widthRefMax;
	report.width_match = match;
	report.width_mismatch = (readOk && (match == 0u)) ? 1u : 0u;
	report.width_fails = (readOk) ? 0u : 1u;
	report.width_budget = (elapsedMs >=
	    GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS) ? 1u : 0u;
	report.width_last_index = 0;
	report.width_last_lba = GTA4L_EMMC_BLOCK_WIDTH_LBA;
	report.width_last_start = 0;
	report.width_last_length = GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES;
	report.width_last_span = GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES;
	report.width_last_actual = actual;
	report.width_last_status = (int32_t)status;
	report.width_last_subrequests = subrequests;
	report.width_last_blocks = blocks;
	report.width_last_max = max;
	memcpy(report.width_last_digest, widthObservedDigest,
	    sizeof(report.width_last_digest));
	memcpy(report.width_last_predigest, widthReferenceDigest,
	    sizeof(report.width_last_predigest));
	report.width_last_match = match;
	IOLockUnlock(reportLock);

	if (reportCancel) {
		widthReportCancel();
		return;
	}
	if (reason == GTA4L_EMMC_BLOCK_WIDTH_REASON_REVERT) {
		widthLatchFailure(reason, widthRevertStatus, 0, nullptr);
	} else if (reason != GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE) {
		widthLatchFailure(reason, (int32_t)status, actual,
		    (actual != 0) ? widthObservedDigest : nullptr);
	}

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
	/* Interleaving seam: the first-failure latch is already preserved, but
	 * the success/failure commit and complete computation have not run. */
	if (gta4l_test_width_hook != nullptr) {
		gta4l_test_width_hook(GTA4L_TEST_WIDTH_PHASE_BEFORE_TERMINAL);
	}
#endif

	IOLockLock(reportLock);
	if (reason == GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE) {
		report.width_passed = GTA4L_EMMC_BLOCK_WIDTH_CASES;
		report.width_failed = 0;
		report.width_reason = GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE;
	} else {
		report.width_passed = 0;
		report.width_failed = 1;
		report.width_reason = reason;
	}
	complete = ((report.width_cases ==
	    GTA4L_EMMC_BLOCK_WIDTH_CASES) &&
	    (report.width_passed == GTA4L_EMMC_BLOCK_WIDTH_CASES) &&
	    (report.width_failed == 0u) &&
	    (report.width_reason == GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE) &&
	    (report.width_switch == GTA4L_EMMC_BLOCK_WIDTH_BITS_4) &&
	    (report.width_verified == 1u) &&
	    ((report.width_hb & GTA4L_EMMC_HOST_CONTROL_4BIT) == 0u) &&
	    ((report.width_ha & GTA4L_EMMC_HOST_CONTROL_4BIT) ==
	    GTA4L_EMMC_HOST_CONTROL_4BIT) &&
	    (report.width_cmd6 == 1u) &&
	    (report.width_reads == 2u) &&
	    (report.width_bytes ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES * 2u)) &&
	    (report.width_blocks ==
	    (uint32_t)(GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS * 2u)) &&
	    (report.width_subrequests == 2u) &&
	    (report.width_max == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) &&
	    (report.width_match == 1u) &&
	    (report.width_mismatch == 0u) &&
	    (report.width_fails == 0u) &&
	    (report.width_cancel == 0u) &&
	    (report.width_budget == 0u) &&
	    (report.width_revert == 1u) &&
	    (report.width_rverified == 1u) &&
	    ((report.width_rhb & GTA4L_EMMC_HOST_CONTROL_4BIT) ==
	    GTA4L_EMMC_HOST_CONTROL_4BIT) &&
	    ((report.width_rha &
	    (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) == 0u) &&
	    (report.width_rcmd6 >= 1u) && (report.width_rcmd6 <= 2u) &&
	    (report.width_limit == GTA4L_EMMC_BLOCK_MAX_BLOCKS) &&
	    (report.width_segments == 1u) &&
	    (report.width_capacity == capacityBlocks) &&
	    (widthRefStatus == 0u) &&
	    (widthRefActual == GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES) &&
	    (widthRefSubrequests == 1u) &&
	    (widthRefBlocks == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) &&
	    (widthRefMax == GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS) &&
	    (widthRefStopOk) && stopOk) ? 1u : 0u;
	report.width_complete = complete;
	IOLockUnlock(reportLock);
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
Gta4lEmmcBlock::finishReport(bool wait)
{
	uint32_t length;

	if (!claimTerminal()) {
		return;
	}
	if (wait) {
		fillDriverReport();
	} else {
		/* The width gate timed out because another request still
		 * owns it. Never block the terminal report on that gate:
		 * take the bounded try-lock snapshot instead. */
		(void)snapshotReport(&report, false);
	}
	/* Reporting is complete only after the coherent snapshot has been
	 * taken. The watchdog defers while this terminal owner is active. */
	reportDone = true;
	if (reportBuffer == nullptr) {
		panic("GTA4L-EMMC P5 REPORT: no report buffer");
		return;
	}
	length = gta4l_emmc_block_report_format(reportBuffer,
	    GTA4L_EMMC_BLOCK_REPORT_BYTES, &report);
	if (length == 0) {
		panic("GTA4L-EMMC P5 REPORT: format failed");
		return;
	}
	panic("%s", reportBuffer);
}

void
Gta4lEmmcBlock::panicWithReport()
{
	finishReport(true);
}

void
Gta4lEmmcBlock::panicWithReportNoGate()
{
	finishReport(false);
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
	report.breakup = breakupMode ? 1u : 0u;
	report.breakup_cases = gta4l_emmc_block_breakup_case_count();
	report.breakup_complete = 0;
	report.breakup_reason = GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA;
	report.breakup_segments = GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS;
	report.breakup_limit = GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS;
	report.sweep = sweepMode ? 1u : 0u;
	report.sweep_cases = gta4l_emmc_block_sweep_case_count();
	report.sweep_complete = 0;
	report.sweep_reason = GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA;
	report.sweep_segments = GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS;
	report.sweep_limit = GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS;
	report.consume = consumeMode ? 1u : 0u;
	report.consume_cases = GTA4L_EMMC_BLOCK_CONSUME_CASES;
	report.consume_complete = 0;
	report.consume_reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA;
	report.consume_segments = GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS;
	report.consume_limit = GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS;
	report.consume_capacity = capacityBlocks;
	report.width = widthMode ? 1u : 0u;
	report.width_cases = GTA4L_EMMC_BLOCK_WIDTH_CASES;
	report.width_complete = 0;
	report.width_reason = widthFailStage;
	report.width_switch = widthSwitch.requested;
	report.width_verified = widthSwitch.verified;
	report.width_hb = widthSwitch.host_before;
	report.width_ha = widthSwitch.host_after;
	report.width_rb = widthSwitch.switched;
	report.width_cmd6 = widthSwitch.cmd6_attempts;
	report.width_revert = widthRevert.requested;
	report.width_rverified = widthRevert.verified;
	report.width_rhb = widthRevert.host_before;
	report.width_rha = widthRevert.host_after;
	report.width_rcmd6 = widthRevert.cmd6_attempts;
	report.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	report.width_segments = 1u;
	report.width_capacity = capacityBlocks;
	report.width_fail_stage = widthFailStage;
	report.width_fail_status = widthFailStatus;
	report.width_fail_actual = widthFailActual;
	memcpy(report.width_fail_digest, widthFailDigest,
	    sizeof(report.width_fail_digest));
	if (widthMode) {
		runWidth();
		return;
	}
	if (consumeMode) {
		runConsume();
		return;
	}
	if (sweepMode) {
		runSweep();
		return;
	}
	if (breakupMode) {
		runBreakup();
		return;
	}
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
