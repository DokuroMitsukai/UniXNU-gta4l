// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Compile-only ABI probe for the N10.15 read-only consumer lifecycle.
//
// This mirrors the corrected run sequence: allocate the 32 KiB contiguous
// buffer, create the one-case input descriptor, validate the first physical
// segment, tag the descriptor, open the BOOT media read-only, issue one
// synchronous read, close, release the descriptor, free the buffer and
// release the client.  It is compiled with the adapter's pinned
// IOPlatformExpert.cpo.json record and the qualified storage headers; it is
// not linked or run.
//
// The import surface is pinned by scripts/build-gta4l-emmc-block.py: 187
// undefined symbols, all defined by the candidate kernel, split as 185
// __TEXT_EXEC,__text, one __DATA_CONST,__const (PAGE_SHIFT_CONST) and one
// __DATA,__common (IOService::gMetaClass).

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/storage/IOMedia.h>

#define PROBE_BUFFER_BYTES 32768u
#define PROBE_CASE_BYTES 1024u
#define PROBE_CASE_TAG 1u

class Gta4lEmmcProbeClient : public IOService {
	OSDeclareDefaultStructors(Gta4lEmmcProbeClient)

public:
	bool run(IOMedia *boot, uint64_t start, uint32_t length, uint32_t tag);
};

OSDefineMetaClassAndStructors(Gta4lEmmcProbeClient, IOService)

bool
Gta4lEmmcProbeClient::run(IOMedia *boot, uint64_t start, uint32_t length,
    uint32_t tag)
{
	IOMemoryDescriptor *descriptor;
	IOPhysicalAddress address;
	IOByteCount segmentLength = 0;
	UInt64 actual = 0;
	IOReturn status;
	uint8_t *buffer;

	if (boot == nullptr) {
		return false;
	}
	buffer = (uint8_t *)IOMallocContiguous(PROBE_BUFFER_BYTES, PAGE_SIZE,
	    nullptr);
	if (buffer == nullptr) {
		return false;
	}
	descriptor = IOMemoryDescriptor::withAddress(buffer, length,
	    kIODirectionIn);
	if (descriptor == nullptr) {
		IOFreeContiguous(buffer, PROBE_BUFFER_BYTES);
		return false;
	}
	address = descriptor->getPhysicalSegment(0, &segmentLength,
	    kIOMemoryMapperNone);
	if ((address == 0) || (segmentLength < (IOByteCount)length)) {
		descriptor->release();
		IOFreeContiguous(buffer, PROBE_BUFFER_BYTES);
		return false;
	}
	descriptor->setTag(tag);
	if (!boot->open(this, 0, kIOStorageAccessReader)) {
		descriptor->release();
		IOFreeContiguous(buffer, PROBE_BUFFER_BYTES);
		return false;
	}
	status = boot->read(this, start, descriptor, nullptr, &actual);
	boot->close(this, 0);
	descriptor->release();
	IOFreeContiguous(buffer, PROBE_BUFFER_BYTES);
	return (status == kIOReturnSuccess) &&
	    (actual == (UInt64)length);
}

bool
gta4l_emmc_consumer_probe(IOMedia *boot, uint64_t start, uint32_t length,
    uint32_t tag)
{
	Gta4lEmmcProbeClient *client;

	client = new Gta4lEmmcProbeClient();
	if (client == nullptr) {
		return false;
	}
	if (!client->init()) {
		client->release();
		return false;
	}
	(void)client->run(boot, start, length, tag);
	client->release();
	return true;
}
