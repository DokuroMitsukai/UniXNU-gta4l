// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host-only IOKit seam implementation for the gta4l block adapter tests.

#include "Gta4lEmmcBlockTestSupport.h"

gta4l_test_state gta4l_test;
gta4l_test_media_read_fn gta4l_test_media_read_hook;
IOMedia *gta4l_test_media[8];
int gta4l_test_media_count;

void
gta4l_test_reset(void)
{
	memset(&gta4l_test, 0, sizeof(gta4l_test));
	gta4l_test.boot_args = "";
	gta4l_test.init_result = 0;
	gta4l_test.capacity_blocks = 61071360u;
	gta4l_test.reporter_start_result = 0;
	gta4l_test.watchdog_start_result = 0;
	gta4l_test.lock_alloc_fail = 0;
	gta4l_test.fail_read_at = -1;
	gta4l_test.fail_multi_at = -1;
	gta4l_test.multi_stop_attempted = 1;
	gta4l_test.multi_corrupt_at = -1;
	gta4l_test.multi_corrupt_block = 0;
	gta4l_test.multi_corrupt_pair = 0;
	gta4l_test.read_corrupt_at = -1;
	gta4l_test.read_corrupt_block = 0;
	gta4l_test.media_read_replay = 0;
	gta4l_test.short_copy_at = -1;
	gta4l_test.descriptor_alloc_fail = 0;
	gta4l_test_media_read_hook = 0;
	gta4l_test_media_count = 0;
}

void
gta4l_test_media_reset(void)
{
	int index;

	for (index = 0; index < gta4l_test_media_count; index++) {
		delete gta4l_test_media[index];
		gta4l_test_media[index] = 0;
	}
	gta4l_test_media_count = 0;
}

void
gta4l_test_add_media(IOMedia *media)
{
	if (gta4l_test_media_count < (int)(sizeof(gta4l_test_media) /
	    sizeof(gta4l_test_media[0]))) {
		gta4l_test_media[gta4l_test_media_count++] = media;
	}
}

void
gta4l_test_media_configure(IOMedia *media, bool whole, UInt64 base,
    UInt64 size, bool writable, const char *content)
{
	if (media == 0) {
		return;
	}
	media->whole_value = whole;
	media->base_value = base;
	media->size_value = size;
	media->writable_value = writable;
	media->content_value = content;
	media->whole_object.setValue(whole);
	media->writable_object.setValue(writable);
	media->base_object.setValue(base);
	media->size_object.setValue(size);
	media->content_object.setValue(content);
}

void
gta4l_test_media_chain(IOMedia *media, IOService *driver, IOService *root)
{
	if (media == 0) {
		return;
	}
	media->provider_ = driver;
	if (driver != 0) {
		driver->provider_ = root;
	}
}

void
gta4l_test_fill_sector(uint64_t block, void *buffer)
{
	uint8_t *bytes = static_cast<uint8_t *>(buffer);
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

IOLock *
IOLockAlloc(void)
{
	if (gta4l_test.lock_alloc_fail != 0) {
		return 0;
	}
	return new IOLock();
}

void
IOLockFree(IOLock *lock)
{
	delete lock;
}

void
IOLockLock(IOLock *lock)
{
	if (lock == 0) {
		return;
	}
	lock->depth++;
	gta4l_test.lock_depth++;
	if (gta4l_test.lock_depth > gta4l_test.lock_depth_max) {
		gta4l_test.lock_depth_max = gta4l_test.lock_depth;
	}
}

bool
IOLockTryLock(IOLock *lock)
{
	if ((lock == 0) || (lock->depth != 0)) {
		return false;
	}
	IOLockLock(lock);
	return true;
}

void
IOLockUnlock(IOLock *lock)
{
	if (lock == 0) {
		return;
	}
	lock->depth--;
	gta4l_test.lock_depth--;
}

void
IOLog(const char *format, ...)
{
	va_list arguments;
	int written;

	if (gta4l_test.log_count >= sizeof(gta4l_test.log)) {
		return;
	}
	va_start(arguments, format);
	written = vsnprintf(gta4l_test.log + gta4l_test.log_count,
	    sizeof(gta4l_test.log) - gta4l_test.log_count, format, arguments);
	va_end(arguments);
	if (written > 0) {
		gta4l_test.log_count += (uint32_t)written;
	}
}

void
IOSleep(unsigned milliseconds)
{
	gta4l_test.sleeps++;
	gta4l_test.now_ms += milliseconds;
}

void
panic(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(gta4l_test.panic, sizeof(gta4l_test.panic), format, arguments);
	va_end(arguments);
	gta4l_test.panicked = 1;
	longjmp(gta4l_test.panic_jump, 1);
}

char *
PE_boot_args(void)
{
	return const_cast<char *>(gta4l_test.boot_args);
}

int
PE_parse_boot_argn(const char *name, void *value, int size)
{
	const char *cursor = gta4l_test.boot_args;
	size_t name_length;

	if ((name == 0) || (value == 0) || (size < (int)sizeof(int))) {
		return 0;
	}
	name_length = strlen(name);
	while ((cursor != 0) && (*cursor != 0)) {
		const char *token;
		const char *end;

		while ((*cursor == ' ') || (*cursor == '\t')) {
			cursor++;
		}
		token = cursor;
		while ((*cursor != 0) && (*cursor != ' ') &&
		    (*cursor != '\t')) {
			cursor++;
		}
		end = cursor;
		if (((size_t)(end - token) > name_length) &&
		    (strncmp(token, name, name_length) == 0) &&
		    (token[name_length] == '=')) {
			char *parsed_end = 0;
			long parsed = strtol(token + name_length + 1,
			    &parsed_end, 10);

			if (parsed_end == end) {
				*static_cast<int *>(value) = (int)parsed;
				return 1;
			}
		}
	}
	return 0;
}

kern_return_t
kernel_thread_start(void (*func)(void *, wait_result_t), void *context,
    thread_t *thread)
{
	int index = gta4l_test.thread_count;
	int result = (index == 0) ? gta4l_test.reporter_start_result :
	    gta4l_test.watchdog_start_result;

	if (result != KERN_SUCCESS) {
		return result;
	}
	if (index < (int)(sizeof(gta4l_test.threads) /
	    sizeof(gta4l_test.threads[0]))) {
		gta4l_test.threads[index].func = func;
		gta4l_test.threads[index].context = context;
		gta4l_test.thread_count++;
	}
	if (thread != 0) {
		*thread = (thread_t)(uintptr_t)(index + 1);
	}
	return KERN_SUCCESS;
}

void
thread_deallocate(thread_t thread)
{
	(void)thread;
}

thread_t
current_thread(void)
{
	return (thread_t)(uintptr_t)1;
}

kern_return_t
thread_terminate(thread_t thread)
{
	(void)thread;
	return KERN_SUCCESS;
}

int
gta4l_emmc_platform_map(struct gta4l_emmc_platform *platform)
{
	gta4l_test.platform_maps++;
	if (gta4l_test.map_result != 0) {
		return gta4l_test.map_result;
	}
	if (platform != 0) {
		platform->sdhci = 1;
		platform->gcc = 2;
		platform->tlmm = 3;
		platform->counter_hz = 1000000;
	}
	return 0;
}

uint64_t
gta4l_emmc_platform_counter(void)
{
	return (uint64_t)gta4l_test.now_ms * 1000u;
}

uint64_t
gta4l_emmc_platform_counter_hz(void)
{
	return 1000000;
}

uint8_t
gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	(void)platform;
	(void)space;
	(void)offset;
	return 0;
}

uint16_t
gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	(void)platform;
	(void)space;
	(void)offset;
	return 0;
}

uint32_t
gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	(void)platform;
	(void)space;
	(void)offset;
	return 0;
}

void
gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value)
{
	(void)platform;
	(void)space;
	(void)offset;
	(void)value;
}

void
gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value)
{
	(void)platform;
	(void)space;
	(void)offset;
	(void)value;
}

void
gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value)
{
	(void)platform;
	(void)space;
	(void)offset;
	(void)value;
}

void
gta4l_emmc_platform_barrier(void)
{
}

void
gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds)
{
	(void)platform;
	gta4l_test.now_ms += microseconds / 1000u;
}

int
gta4l_emmc_init(struct gta4l_emmc_host *host)
{
	gta4l_test.init_calls++;
	if (gta4l_test.init_result != 0) {
		return gta4l_test.init_result;
	}
	if (host != 0) {
		host->capacity_blocks = gta4l_test.capacity_blocks;
		host->bus_width = 1;
		host->initialized = 1;
		host->last_step = 0;
	}
	return 0;
}

int
gta4l_emmc_read_block(struct gta4l_emmc_host *host, uint64_t block,
    void *buffer)
{
	uint8_t *bytes = static_cast<uint8_t *>(buffer);

	(void)host;
	if ((gta4l_test.fail_read_at >= 0) &&
	    ((uint32_t)gta4l_test.fail_read_at == gta4l_test.read_calls)) {
		return -5;
	}
	gta4l_test_fill_sector(block, buffer);
	if ((gta4l_test.read_corrupt_at >= 0) &&
	    ((uint32_t)gta4l_test.read_corrupt_at == gta4l_test.read_calls) &&
	    (block == gta4l_test.read_corrupt_block)) {
		bytes[0] ^= 0x5au;
	}
	gta4l_test.read_calls++;
	return 0;
}

int
gta4l_emmc_read_multi_result(struct gta4l_emmc_host *host, uint64_t start_lba,
    uint32_t count, void *buffer, struct gta4l_emmc_multi_result *result)
{
	uint32_t index;
	uint32_t completed;

	if (result == 0) {
		return GTA4L_EMMC_ERR_ARG;
	}
	result->transport_result = GTA4L_EMMC_ERR_ARG;
	result->requested_blocks = count;
	result->completed_blocks = 0;
	result->stop_attempted = 0;
	result->stop_result = 0;
	if ((host == 0) || (buffer == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if ((gta4l_test.fail_multi_at >= 0) &&
	    ((uint32_t)gta4l_test.fail_multi_at == gta4l_test.multi_calls)) {
		gta4l_test.multi_calls++;
		result->transport_result = -5;
		return -5;
	}
	for (index = 0; index < count; index++) {
		gta4l_test_fill_sector(start_lba + index,
		    static_cast<uint8_t *>(buffer) +
		    (index * GTA4L_EMMC_BLOCK_BYTES));
		if ((gta4l_test.multi_corrupt_at >= 0) &&
		    ((uint32_t)gta4l_test.multi_corrupt_at ==
		    gta4l_test.multi_calls) &&
		    ((start_lba + index) ==
		    gta4l_test.multi_corrupt_block)) {
			uint8_t *bytes = static_cast<uint8_t *>(buffer) +
			    (index * GTA4L_EMMC_BLOCK_BYTES);

			if (gta4l_test.multi_corrupt_pair != 0) {
				/* Adjacent +1/-31 changes preserve the
				 * rolling 31-based sum while changing
				 * bytes, so only an exact byte check can
				 * detect them. */
				bytes[31] = (uint8_t)(bytes[31] + 1u);
				bytes[32] = (uint8_t)(bytes[32] - 31u);
			} else {
				bytes[0] ^= 0x5a;
			}
		}
	}
	completed = count;
	if (gta4l_test.multi_short_blocks > 0) {
		if ((uint32_t)gta4l_test.multi_short_blocks >= count) {
			completed = 0;
		} else {
			completed = count -
			    (uint32_t)gta4l_test.multi_short_blocks;
		}
	}
	gta4l_test.multi_calls++;
	result->requested_blocks = count;
	result->completed_blocks = completed;
	result->stop_attempted = (gta4l_test.multi_stop_attempted != 0) ?
	    1u : 0u;
	result->stop_result = gta4l_test.multi_stop_result;
	result->transport_result = gta4l_test.multi_data_result;
	return gta4l_test.multi_rc;
}

IOMemoryDescriptor *
IOMemoryDescriptor::withAddress(void *address, UInt64 length, int direction)
{
	IOMemoryDescriptor *descriptor;

	if (gta4l_test.descriptor_alloc_fail != 0) {
		return 0;
	}
	descriptor = new IOMemoryDescriptor();
	descriptor->address_ = address;
	descriptor->length_ = length;
	descriptor->direction_ = direction;
	return descriptor;
}

IOByteCount
IOMemoryDescriptor::writeBytes(IOByteCount offset, const void *source,
    IOByteCount length)
{
	uint32_t call = gta4l_test.copy_calls++;

	if ((gta4l_test.short_copy_at >= 0) &&
	    ((uint32_t)gta4l_test.short_copy_at == call)) {
		return (length == 0) ? 0 : length - 1;
	}
	if ((offset > length_) || (length > length_ - offset)) {
		return 0;
	}
	if ((address_ != 0) && (source != 0)) {
		memcpy(static_cast<uint8_t *>(address_) + offset, source,
		    (size_t)length);
	}
	return length;
}

void
IOService::registerService(void)
{
	registered_ = true;
	gta4l_test.registered++;
}

void
IOService::messageClients(UInt32 type, void *argument)
{
	gta4l_test.message_count++;
	gta4l_test.message_type = type;
	gta4l_test.message_argument = argument;
	gta4l_test.message_lock_depth = gta4l_test.lock_depth;
}

bool
IOService::setProperty(const char *key, const char *value)
{
	(void)key;
	(void)value;
	return true;
}

bool
IOService::setProperty(const char *key, bool value)
{
	(void)key;
	(void)value;
	return true;
}

bool
IOService::setProperty(const char *key, unsigned long long value,
    unsigned int bits)
{
	if (property_count_ < (int)(sizeof(properties_) /
	    sizeof(properties_[0]))) {
		properties_[property_count_].key = key;
		properties_[property_count_].value = value;
		properties_[property_count_].bits = bits;
		property_count_++;
	}
	return true;
}

OSObject *
IOService::getProperty(const char *key) const
{
	(void)key;
	return 0;
}

OSDictionary *
IOService::serviceMatching(const char *className)
{
	(void)className;
	return new OSDictionary();
}

OSIterator *
IOService::getMatchingServices(OSDictionary *matching)
{
	static OSObject *items[8];
	int index;

	(void)matching;
	for (index = 0; index < gta4l_test_media_count; index++) {
		items[index] = gta4l_test_media[index];
	}
	return new OSIterator(items, gta4l_test_media_count);
}

IOMedia::IOMedia()
    : whole_value(false),
      base_value(0),
      size_value(0),
      writable_value(false),
      content_value(0),
      open_value(false),
      open_refuse(false),
      open_calls(0),
      close_calls(0),
      open_client(0),
      open_access(kIOStorageAccessNone)
{
}

bool
IOMedia::open(IOService *client, IOOptionBits options, IOStorageAccess access)
{
	(void)options;
	open_calls++;
	if (open_refuse) {
		return false;
	}
	open_value = true;
	open_client = client;
	open_access = access;
	return true;
}

void
IOMedia::close(IOService *client, IOOptionBits options)
{
	(void)options;
	close_calls++;
	if (open_client == client) {
		open_value = false;
		open_client = 0;
	}
}

OSObject *
IOMedia::getProperty(const char *key) const
{
	if (key == 0) {
		return 0;
	}
	if (strcmp(key, kIOMediaWholeKey) == 0) {
		const_cast<IOMedia *>(this)->whole_object.setValue(whole_value);
		return const_cast<OSBoolean *>(&whole_object);
	}
	if (strcmp(key, kIOMediaBaseKey) == 0) {
		const_cast<IOMedia *>(this)->base_object.setValue(base_value);
		return const_cast<OSNumber *>(&base_object);
	}
	if (strcmp(key, kIOMediaSizeKey) == 0) {
		const_cast<IOMedia *>(this)->size_object.setValue(size_value);
		return const_cast<OSNumber *>(&size_object);
	}
	if (strcmp(key, kIOMediaContentKey) == 0) {
		const_cast<IOMedia *>(this)->content_object.setValue(
		    content_value);
		return const_cast<OSString *>(&content_object);
	}
	if (strcmp(key, kIOMediaWritableKey) == 0) {
		const_cast<IOMedia *>(this)->writable_object.setValue(
		    writable_value);
		return const_cast<OSBoolean *>(&writable_object);
	}
	return 0;
}

struct gta4l_test_media_completion {
	IOReturn status;
	IOByteCount actual;
	int calls;
};

static void
gta4l_test_media_completion_action(void *target, void *parameter,
    IOReturn status, IOByteCount actual)
{
	struct gta4l_test_media_completion *record =
	    static_cast<struct gta4l_test_media_completion *>(parameter);

	(void)target;
	if (record == 0) {
		return;
	}
	record->calls++;
	record->status = status;
	record->actual = actual;
}

IOReturn
IOMedia::read(IOService *client, UInt64 byteStart,
    IOMemoryDescriptor *buffer, IOStorageAttributes *attributes,
    UInt64 *actualByteCount)
{
	IOService *provider;
	IOBlockStorageDevice *device = 0;
	struct gta4l_test_media_completion record;
	IOStorageCompletion completion;
	IOReturn status;
	UInt64 nblks;

	(void)client;
	if (!open_value) {
		return kIOReturnNotOpen;
	}
	if ((buffer == 0) || (buffer->getLength() < GTA4L_EMMC_BLOCK_BYTES) ||
	    (buffer->address() == 0)) {
		return kIOReturnBadArgument;
	}
	if (gta4l_test_media_read_hook != 0) {
		return gta4l_test_media_read_hook(this, byteStart, buffer,
		    actualByteCount);
	}
	provider = provider_;
	while (provider != 0) {
		device = dynamic_cast<IOBlockStorageDevice *>(provider);
		if (device != 0) {
			break;
		}
		provider = provider->getProvider();
	}
	if (device == 0) {
		gta4l_test_fill_sector(byteStart / GTA4L_EMMC_BLOCK_BYTES,
		    buffer->address());
		if (actualByteCount != 0) {
			*actualByteCount = GTA4L_EMMC_BLOCK_BYTES;
		}
		return kIOReturnSuccess;
	}
	if ((byteStart % GTA4L_EMMC_BLOCK_BYTES) != 0) {
		return kIOReturnBadArgument;
	}
	nblks = buffer->getLength() / GTA4L_EMMC_BLOCK_BYTES;
	if (nblks == 0) {
		return kIOReturnBadArgument;
	}
	record.status = kIOReturnSuccess;
	record.actual = 0;
	record.calls = 0;
	completion.target = 0;
	completion.action = gta4l_test_media_completion_action;
	completion.parameter = &record;
	status = device->doAsyncReadWrite(buffer,
	    byteStart / GTA4L_EMMC_BLOCK_BYTES, nblks, attributes,
	    &completion);
	if (status != kIOReturnSuccess) {
		if (actualByteCount != 0) {
			*actualByteCount = 0;
		}
		return status;
	}
	while (gta4l_test.media_read_replay > 0) {
		gta4l_test.media_read_replay--;
		record.calls = 0;
		record.status = kIOReturnSuccess;
		record.actual = 0;
		(void)device->doAsyncReadWrite(buffer,
		    byteStart / GTA4L_EMMC_BLOCK_BYTES, nblks, attributes,
		    &completion);
	}
	if (actualByteCount != 0) {
		*actualByteCount = record.actual;
	}
	return record.status;
}
