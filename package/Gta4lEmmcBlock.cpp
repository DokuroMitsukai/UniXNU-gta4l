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
	int value = 0;
	int parsed;
	int mode;
	int baselineResult;

	arguments = PE_boot_args();
	blockTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_BOOT_ARG);
	probeTokens = gta4l_emmc_block_token_count(arguments,
	    GTA4L_EMMC_BLOCK_PROBE_ARG);
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
	mode = gta4l_emmc_block_mode_select(blockTokens != 0, value,
	    probeTokens != 0, conflictTokens != 0, blockTokens > 1u);
	if (mode == GTA4L_EMMC_BLOCK_SELECT_INERT) {
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
	if (!setProperty(kIOMaximumSegmentCountReadKey, 1ULL, 32)) {
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
