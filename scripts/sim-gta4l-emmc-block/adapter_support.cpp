// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host-only IOKit seam implementation for the gta4l block adapter tests.

#include "Gta4lEmmcBlockTestSupport.h"

gta4l_test_state gta4l_test;
gta4l_test_media_read_fn gta4l_test_media_read_hook;
gta4l_test_deblock_sample_fn gta4l_test_deblock_hook;
gta4l_test_breakup_sample_fn gta4l_test_breakup_hook;
gta4l_test_partition_sample_fn gta4l_test_partition_sample_hook;
IOMedia *gta4l_test_media[GTA4L_TEST_MAX_MEDIA];
int gta4l_test_media_count;
IOService *gta4l_test_schemes[GTA4L_TEST_MAX_SCHEMES];
int gta4l_test_scheme_count;
static const IORegistryPlane gta4l_test_service_plane;
const IORegistryPlane *gIOServicePlane = &gta4l_test_service_plane;

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
	gta4l_test.part_publish_active = 0;
	gta4l_test.part_publish_count = 0;
	gta4l_test.part_publish_calls = 0;
	gta4l_test.part_publish_error_at = -1;
	gta4l_test.part_publish_errors = 0;
	gta4l_test.deblock_model = 0;
	gta4l_test.deblock_segment_override = 0;
	gta4l_test.deblock_fail_at = -1;
	gta4l_test.deblock_fail_status = 0;
	gta4l_test.deblock_short_at = -1;
	gta4l_test.deblock_short_mode = 0;
	gta4l_test.deblock_corrupt_at = -1;
	gta4l_test.deblock_cancel_at = -1;
	gta4l_test.deblock_advance_ms = 0;
	gta4l_test.deblock_read_calls = 0;
	gta4l_test.deblock_adapter_calls = 0;
	gta4l_test.deblock_dma_errors = 0;
	gta4l_test.deblock_unaligned_bounce = 0;
	gta4l_test.breakup_model = 0;
	gta4l_test.breakup_limit_blocks = 0;
	gta4l_test.breakup_limit_bytes = 0;
	gta4l_test.breakup_limit_segments = 0;
	gta4l_test.breakup_fail_at = -1;
	gta4l_test.breakup_fail_status = 0;
	gta4l_test.breakup_short_at = -1;
	gta4l_test.breakup_short_mode = 0;
	gta4l_test.breakup_corrupt_at = -1;
	gta4l_test.breakup_cancel_at = -1;
	gta4l_test.breakup_advance_ms = 0;
	gta4l_test.breakup_read_calls = 0;
	gta4l_test.breakup_adapter_calls = 0;
	gta4l_test.breakup_dma_errors = 0;
	gta4l_test.breakup_unaligned_bounce = 0;
	gta4l_test.breakup_split_mode = 0;
	gta4l_test.breakup_foreign_at = -1;
	gta4l_test.breakup_foreign_mode = 0;
	gta4l_test.sweep_reference_corrupt_at = -1;
	gta4l_test.sweep_reference_corrupt_count = 0;
	gta4l_test.sweep_reference_corrupt_mode = 0;
	gta4l_test.sweep_reference_short_at = -1;
	gta4l_test_media_read_hook = 0;
	gta4l_test_deblock_hook = 0;
	gta4l_test_breakup_hook = 0;
	gta4l_test_partition_sample_hook = 0;
	gta4l_test_media_count = 0;
	gta4l_test_scheme_count = 0;
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
gta4l_test_add_child(IOService *parent, IOService *child)
{
	if ((parent == 0) || (child == 0)) {
		return;
	}
	if (parent->child_count_ < (int)(sizeof(parent->children_) /
	    sizeof(parent->children_[0]))) {
		parent->children_[parent->child_count_++] = child;
	}
}

void
gta4l_test_add_scheme(IOService *scheme)
{
	if ((scheme != 0) &&
	    (gta4l_test_scheme_count < GTA4L_TEST_MAX_SCHEMES)) {
		gta4l_test_schemes[gta4l_test_scheme_count++] = scheme;
	}
}

void
gta4l_test_scheme_configure(IOService *scheme, IOService *provider,
    const char *uuid)
{
	if (scheme == 0) {
		return;
	}
	scheme->provider_ = provider;
	scheme->uuid_value_ = uuid;
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
	if (gta4l_test.lock_depth != 0) {
		gta4l_test.sleep_with_lock++;
	}
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
	uint32_t call = gta4l_test.read_calls;

	(void)host;
	if ((gta4l_test.fail_read_at >= 0) &&
	    ((uint32_t)gta4l_test.fail_read_at == gta4l_test.read_calls)) {
		return -5;
	}
	if ((gta4l_test.sweep_reference_corrupt_at >= 0) &&
	    (call >= (uint32_t)gta4l_test.sweep_reference_corrupt_at) &&
	    (call < (uint32_t)gta4l_test.sweep_reference_corrupt_at +
	    (uint32_t)gta4l_test.sweep_reference_corrupt_count)) {
		switch (gta4l_test.sweep_reference_corrupt_mode) {
		case 1:
			gta4l_test_fill_sector(block + 8u, buffer);
			break;
		case 2:
			gta4l_test_fill_sector(block, buffer);
			memmove(bytes, bytes + 1,
			    GTA4L_EMMC_BLOCK_BYTES - 1u);
			bytes[GTA4L_EMMC_BLOCK_BYTES - 1u] = 0u;
			break;
		case 3:
			memset(bytes, 0, GTA4L_EMMC_BLOCK_BYTES);
			break;
		default:
			gta4l_test_fill_sector(block, buffer);
			break;
		}
		gta4l_test.read_calls++;
		return 0;
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
	int index;

	if (key != 0) {
		if ((strcmp(key, kIOGUIDPartitionSchemeUUIDKey) == 0) &&
		    (uuid_value_ != 0)) {
			const_cast<IOService *>(this)->uuid_object_.setValue(
			    uuid_value_);
			return const_cast<OSString *>(&uuid_object_);
		}
		for (index = 0; index < property_count_; index++) {
			if ((properties_[index].key != 0) &&
			    (strcmp(properties_[index].key, key) == 0)) {
				const_cast<IOService *>(this)->property_number_.
				    setValue(properties_[index].value);
				return const_cast<OSNumber *>(
				    &property_number_);
			}
		}
	}
	return 0;
}

OSDictionary *
IOService::serviceMatching(const char *className)
{
	return new OSDictionary(className);
}

OSIterator *
IOService::getChildIterator(const IORegistryPlane *plane) const
{
	static OSObject *items[8];
	int count = 0;
	int index;

	if (plane != gIOServicePlane) {
		return new OSIterator(items, 0);
	}
	for (index = 0; index < child_count_; index++) {
		items[count++] = children_[index];
	}
	return new OSIterator(items, count);
}

OSIterator *
IOService::getMatchingServices(OSDictionary *matching)
{
	static OSObject *items[GTA4L_TEST_MAX_MEDIA];
	int index;
	int visible = 0;
	int nonWhole = 0;
	uint32_t published;
	uint32_t call;
	const char *className;

	className = (matching != 0) ? matching->className() : 0;
	if ((className != 0) &&
	    (strcmp(className, "IOGUIDPartitionScheme") == 0)) {
		int count = 0;

		for (index = 0; index < gta4l_test_scheme_count; index++) {
			if (gta4l_test_schemes[index]->registered_) {
				items[count++] = gta4l_test_schemes[index];
			}
		}
		return new OSIterator(items, count);
	}
	if ((className != 0) && (strcmp(className, "IOMedia") != 0)) {
		return new OSIterator(items, 0);
	}
	if ((gta4l_test.part_publish_active != 0) &&
	    (gta4l_test.part_publish_count > 0)) {
		call = gta4l_test.part_publish_calls++;
		if ((gta4l_test.part_publish_error_at >= 0) &&
		    (call == (uint32_t)gta4l_test.part_publish_error_at)) {
			gta4l_test.part_publish_errors++;
			if (gta4l_test_partition_sample_hook != 0) {
				gta4l_test_partition_sample_hook(call, 0);
			}
			return 0;
		}
		published = gta4l_test.part_publish_counts[
		    (call < (uint32_t)gta4l_test.part_publish_count) ?
		    call : (uint32_t)gta4l_test.part_publish_count - 1u];
		for (index = 0; index < gta4l_test_media_count; index++) {
			IOMedia *media = gta4l_test_media[index];

			if (media == 0) {
				continue;
			}
			if (media->whole_value) {
				if (visible < GTA4L_TEST_MAX_MEDIA) {
					items[visible++] = media;
				}
			} else if (published != 0) {
				if (visible < GTA4L_TEST_MAX_MEDIA) {
					items[visible++] = media;
					nonWhole++;
				}
				published--;
			}
		}
		if (gta4l_test_partition_sample_hook != 0) {
			gta4l_test_partition_sample_hook(call,
			    (uint32_t)nonWhole);
		}
		return new OSIterator(items, visible);
	}
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
      property_faults(0),
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
		if ((property_faults & GTA4L_TEST_PROP_WHOLE_MISSING) != 0) {
			return 0;
		}
		if ((property_faults & GTA4L_TEST_PROP_WHOLE_WRONG) != 0) {
			return const_cast<OSNumber *>(&base_object);
		}
		const_cast<IOMedia *>(this)->whole_object.setValue(whole_value);
		return const_cast<OSBoolean *>(&whole_object);
	}
	if (strcmp(key, kIOMediaBaseKey) == 0) {
		if ((property_faults & GTA4L_TEST_PROP_BASE_MISSING) != 0) {
			return 0;
		}
		if ((property_faults & GTA4L_TEST_PROP_BASE_WRONG) != 0) {
			return const_cast<OSBoolean *>(&whole_object);
		}
		const_cast<IOMedia *>(this)->base_object.setValue(base_value);
		return const_cast<OSNumber *>(&base_object);
	}
	if (strcmp(key, kIOMediaSizeKey) == 0) {
		if ((property_faults & GTA4L_TEST_PROP_SIZE_MISSING) != 0) {
			return 0;
		}
		if ((property_faults & GTA4L_TEST_PROP_SIZE_WRONG) != 0) {
			return const_cast<OSString *>(&content_object);
		}
		const_cast<IOMedia *>(this)->size_object.setValue(size_value);
		return const_cast<OSNumber *>(&size_object);
	}
	if (strcmp(key, kIOMediaContentKey) == 0) {
		if ((property_faults &
		    GTA4L_TEST_PROP_CONTENT_MISSING) != 0) {
			return 0;
		}
		if ((property_faults &
		    GTA4L_TEST_PROP_CONTENT_WRONG) != 0) {
			return const_cast<OSNumber *>(&base_object);
		}
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

#define GTA4L_TEST_DEBLOCK_CHUNK_MAX 3
#define GTA4L_TEST_DEBLOCK_SEGMENT_MAX 16

struct gta4l_test_deblock_chunk {
	uint8_t *address;
	uint64_t physical_address;
	uint64_t length;
	uint64_t span_offset;
};

struct gta4l_test_deblock_segment {
	uint64_t address;
	uint64_t length;
};

class gta4l_test_deblock_descriptor : public IOMemoryDescriptor {
public:
	gta4l_test_deblock_descriptor(
	    const struct gta4l_test_deblock_chunk *chunks, int count,
	    uint64_t span_offset, uint64_t length, int direction)
	{
		chunks_ = chunks;
		count_ = count;
		span_offset_ = span_offset;
		descriptor_length_ = length;
		descriptor_direction_ = direction;
	}
	IOByteCount writeBytes(IOByteCount offset, const void *source,
	    IOByteCount length) override
	{
		const uint8_t *bytes = static_cast<const uint8_t *>(source);
		uint64_t position;
		uint64_t remaining;
		int index;

		if ((source == 0) || (offset > descriptor_length_) ||
		    (length > descriptor_length_ - offset)) {
			return 0;
		}
		position = span_offset_ + offset;
		remaining = length;
		while (remaining > 0) {
			int found = 0;

			for (index = 0; index < count_; index++) {
				uint64_t begin = chunks_[index].span_offset;
				uint64_t end = begin + chunks_[index].length;
				uint64_t chunk_offset;
				uint64_t copy;

				if ((position < begin) || (position >= end)) {
					continue;
				}
				chunk_offset = position - begin;
				copy = end - position;
				if (copy > remaining) {
					copy = remaining;
				}
				if (chunks_[index].address != 0) {
					memcpy(chunks_[index].address +
					    chunk_offset, bytes, (size_t)copy);
				}
				bytes += copy;
				position += copy;
				remaining -= copy;
				found = 1;
				break;
			}
			if (found == 0) {
				return 0;
			}
		}
		return length;
	}
	UInt64 getLength() const override { return descriptor_length_; }
	int getDirection() const override { return descriptor_direction_; }
	void *address() const override { return 0; }
private:
	const struct gta4l_test_deblock_chunk *chunks_;
	int count_;
	uint64_t span_offset_;
	UInt64 descriptor_length_;
	int descriptor_direction_;
};

static int
gta4l_test_deblock_segments(
    const struct gta4l_test_deblock_chunk *chunks, int count,
    uint64_t scratch_address,
    struct gta4l_test_deblock_segment *segments, int capacity)
{
	int used = 0;
	int index;

	for (index = 0; index < count; index++) {
		uint64_t address = (chunks[index].physical_address != 0) ?
		    chunks[index].physical_address :
		    (uint64_t)(uintptr_t)chunks[index].address;
		uint64_t offset = 0;

		if (chunks[index].length == 0) {
			continue;
		}
		if (address == scratch_address) {
			if (used >= capacity) {
				return used;
			}
			segments[used].address = address;
			segments[used].length = chunks[index].length;
			used++;
			continue;
		}
		while (offset < chunks[index].length) {
			uint64_t segment_size = PAGE_SIZE -
			    ((address + offset) & (PAGE_SIZE - 1u));

			if (segment_size > chunks[index].length - offset) {
				segment_size = chunks[index].length - offset;
			}
			if (used >= capacity) {
				return used;
			}
			segments[used].address = address + offset;
			segments[used].length = segment_size;
			used++;
			offset += segment_size;
		}
	}
	return used;
}

static UInt64
gta4l_test_deblock_break_size(
    const struct gta4l_test_deblock_segment *segments, int count,
    UInt64 max_block_count, UInt64 max_byte_count, UInt64 max_segment_count,
    UInt64 block_size)
{
	UInt64 break_size = 0;
	UInt64 chunk = 0;
	UInt64 chunk_size = 0;
	UInt64 limit = 0;
	UInt32 segment_count = 0;
	int index;

	if (max_block_count != 0) {
		limit = max_block_count * block_size;
	}
	if ((max_byte_count != 0) &&
	    ((limit == 0) || (max_byte_count < limit))) {
		limit = max_byte_count;
	}
	for (index = 0; index < count; index++) {
		uint64_t address = segments[index].address;
		uint64_t size = segments[index].length;

		if (chunk == 0) {
			break_size += size;
			chunk = address;
			chunk_size = size;
			segment_count++;
		} else if (chunk + chunk_size == address) {
			break_size += size;
			chunk_size += size;
		} else {
			if ((max_segment_count != 0) &&
			    (segment_count == max_segment_count)) {
				break;
			}
			break_size += size;
			chunk = address;
			chunk_size = size;
			segment_count++;
		}
		if ((limit != 0) && (break_size >= limit)) {
			break_size = limit;
			break;
		}
	}
	if (block_size != 0) {
		break_size -= break_size % block_size;
	}
	return break_size;
}

static IOReturn
gta4l_test_deblock_read(IOBlockStorageDevice *device,
    UInt64 absolute_start, IOMemoryDescriptor *buffer,
    UInt64 *actual_byte_count)
{
	struct gta4l_test_deblock_chunk chunks[GTA4L_TEST_DEBLOCK_CHUNK_MAX];
	struct gta4l_test_deblock_segment segments[GTA4L_TEST_DEBLOCK_SEGMENT_MAX];
	uint8_t scratch[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t request_length = (uint32_t)buffer->getLength();
	uint32_t head = (uint32_t)(absolute_start %
	    GTA4L_EMMC_BLOCK_BYTES);
	uint32_t tail;
	uint32_t span;
	uint64_t block_start;
	uint64_t max_blocks = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	uint64_t max_bytes = GTA4L_EMMC_BLOCK_MAX_BYTES;
	uint64_t max_segments = 1;
	uint64_t break_size;
	uint64_t offset;
	uint64_t remaining;
	uint32_t call = (uint32_t)gta4l_test.deblock_read_calls++;
	OSNumber *property;
	IOReturn status;
	int chunk_count = 0;
	int segment_count;
	int request_index;

	memset(scratch, 0, sizeof(scratch));
	if (gta4l_test.deblock_advance_ms != 0) {
		gta4l_test.now_ms += gta4l_test.deblock_advance_ms;
	}
	if (gta4l_test_deblock_hook != 0) {
		gta4l_test_deblock_hook(call, 0);
	}
	if ((gta4l_test.deblock_cancel_at >= 0) &&
	    (call == (uint32_t)gta4l_test.deblock_cancel_at) &&
	    (gta4l_test_deblock_hook != 0)) {
		gta4l_test_deblock_hook(call, 2);
	}
	if ((gta4l_test.deblock_fail_at >= 0) &&
	    (call == (uint32_t)gta4l_test.deblock_fail_at)) {
		if (actual_byte_count != 0) {
			*actual_byte_count = 0;
		}
		return (gta4l_test.deblock_fail_status != 0) ?
		    gta4l_test.deblock_fail_status : kIOReturnIOError;
	}
	tail = (uint32_t)((absolute_start + request_length) %
	    GTA4L_EMMC_BLOCK_BYTES);
	if (tail != 0) {
		tail = GTA4L_EMMC_BLOCK_BYTES - tail;
	}
	span = head + request_length + tail;
	block_start = absolute_start - head;
	if (head != 0) {
		chunks[chunk_count].address = scratch;
		chunks[chunk_count].physical_address = 0;
		chunks[chunk_count].length = head;
		chunks[chunk_count].span_offset = 0;
		chunk_count++;
	}
	request_index = chunk_count;
	chunks[chunk_count].address =
	    static_cast<uint8_t *>(buffer->address());
	chunks[chunk_count].physical_address = 0;
	chunks[chunk_count].length = request_length;
	chunks[chunk_count].span_offset = head;
	chunk_count++;
	if (tail != 0) {
		chunks[chunk_count].address = scratch;
		chunks[chunk_count].physical_address = 0;
		chunks[chunk_count].length = tail;
		chunks[chunk_count].span_offset = head + request_length;
		chunk_count++;
	}
	if (gta4l_test.deblock_unaligned_bounce != 0) {
		chunks[request_index].physical_address =
		    0x7fff0000ull + PAGE_SIZE - 8u;
	}
	if ((head != 0) || (tail != 0)) {
		uint64_t request_address =
		    (gta4l_test.deblock_unaligned_bounce != 0) ?
		    0x7fff0000ull + PAGE_SIZE - 8u :
		    (uint64_t)(uintptr_t)buffer->address();
		uint64_t first_segment = PAGE_SIZE -
		    (request_address & (PAGE_SIZE - 1u));

		if (first_segment < GTA4L_EMMC_BLOCK_BYTES) {
			gta4l_test.deblock_dma_errors++;
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return kIOReturnDMAError;
		}
	}

	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumBlockCountReadKey));
	if (property != 0) {
		max_blocks = property->unsigned64BitValue();
	}
	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumByteCountReadKey));
	if (property != 0) {
		max_bytes = property->unsigned64BitValue();
	}
	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumSegmentCountReadKey));
	if (property != 0) {
		max_segments = property->unsigned64BitValue();
	}
	if (gta4l_test.deblock_segment_override > 0) {
		max_segments = (uint64_t)gta4l_test.deblock_segment_override;
	}
	segment_count = gta4l_test_deblock_segments(chunks, chunk_count,
	    (uint64_t)(uintptr_t)scratch, segments,
	    GTA4L_TEST_DEBLOCK_SEGMENT_MAX);
	break_size = gta4l_test_deblock_break_size(segments, segment_count,
	    max_blocks, max_bytes, max_segments, GTA4L_EMMC_BLOCK_BYTES);
	if (break_size == 0) {
		gta4l_test.deblock_dma_errors++;
		if (actual_byte_count != 0) {
			*actual_byte_count = 0;
		}
		return kIOReturnDMAError;
	}

	offset = 0;
	remaining = span;
	while (remaining > 0) {
		uint64_t part = (break_size < remaining) ? break_size :
		    remaining;
		gta4l_test_deblock_descriptor descriptor(chunks, chunk_count,
		    offset, part, kIODirectionIn);
		struct gta4l_test_media_completion record;
		IOStorageCompletion completion;

		record.status = kIOReturnSuccess;
		record.actual = 0;
		record.calls = 0;
		completion.target = 0;
		completion.action = gta4l_test_media_completion_action;
		completion.parameter = &record;
		gta4l_test.deblock_adapter_calls++;
		status = device->doAsyncReadWrite(&descriptor,
		    (block_start + offset) / GTA4L_EMMC_BLOCK_BYTES,
		    part / GTA4L_EMMC_BLOCK_BYTES, 0, &completion);
		if (status != kIOReturnSuccess) {
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return status;
		}
		if (record.status != kIOReturnSuccess) {
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return record.status;
		}
		if (record.actual != part) {
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return kIOReturnIOError;
		}
		offset += part;
		remaining -= part;
	}

	if (gta4l_test_deblock_hook != 0) {
		gta4l_test_deblock_hook(call, 1);
	}
	if ((gta4l_test.deblock_corrupt_at >= 0) &&
	    (call == (uint32_t)gta4l_test.deblock_corrupt_at)) {
		uint8_t *destination =
		    static_cast<uint8_t *>(buffer->address());

		destination[request_length / 2u] ^= 0x5au;
	}
	if (actual_byte_count != 0) {
		*actual_byte_count = request_length;
		if ((gta4l_test.deblock_short_at >= 0) &&
		    (call == (uint32_t)gta4l_test.deblock_short_at)) {
			if (gta4l_test.deblock_short_mode == 1) {
				*actual_byte_count = 0;
			} else if (gta4l_test.deblock_short_mode == 2) {
				*actual_byte_count = request_length + 1u;
			} else {
				*actual_byte_count = (request_length == 0) ?
				    0 : request_length - 1u;
			}
		}
	}
	return kIOReturnSuccess;
}

#define GTA4L_TEST_BREAKUP_PART_MAX 130

struct gta4l_test_breakup_part {
	uint64_t offset;
	uint32_t length;
};

class gta4l_test_breakup_descriptor : public IOMemoryDescriptor {
public:
	gta4l_test_breakup_descriptor(void *address, UInt64 length,
	    int direction, UInt32 tag)
	{
		address_ = address;
		length_ = length;
		direction_ = direction;
		tag_ = tag;
	}
};

static void
gta4l_test_breakup_fill(UInt64 absolute_start, void *buffer,
    uint32_t length)
{
	uint8_t *bytes = static_cast<uint8_t *>(buffer);
	uint8_t sector[GTA4L_EMMC_BLOCK_BYTES];
	uint64_t position;
	uint64_t block;
	uint64_t last_block = ~0ull;
	uint32_t offset;
	uint32_t index;

	for (index = 0; index < length; index++) {
		position = absolute_start + index;
		block = position / GTA4L_EMMC_BLOCK_BYTES;
		offset = (uint32_t)(position % GTA4L_EMMC_BLOCK_BYTES);
		if (block != last_block) {
			gta4l_test_fill_sector(block, sector);
			last_block = block;
		}
		bytes[index] = sector[offset];
	}
}

static int
gta4l_test_breakup_parts(uint32_t request_length, uint64_t break_size,
    struct gta4l_test_breakup_part *parts, int capacity)
{
	uint64_t offset = 0;
	int count = 0;

	if (break_size == 0) {
		return -1;
	}
	while (offset < request_length) {
		uint64_t remaining = request_length - offset;
		uint32_t part = (uint32_t)((remaining < break_size) ?
		    remaining : break_size);

		if (count >= capacity) {
			return -1;
		}
		parts[count].offset = offset;
		parts[count].length = part;
		count++;
		offset += part;
	}
	return count;
}

static IOReturn
gta4l_test_breakup_issue(IOBlockStorageDevice *device,
    IOMemoryDescriptor *descriptor, UInt64 block, UInt64 nblks)
{
	struct gta4l_test_media_completion record;
	IOStorageCompletion completion;
	IOReturn status;

	record.status = kIOReturnSuccess;
	record.actual = 0;
	record.calls = 0;
	completion.target = 0;
	completion.action = gta4l_test_media_completion_action;
	completion.parameter = &record;
	gta4l_test.breakup_adapter_calls++;
	status = device->doAsyncReadWrite(descriptor, block, nblks, 0,
	    &completion);
	if (status != kIOReturnSuccess) {
		return status;
	}
	if (record.status != kIOReturnSuccess) {
		return record.status;
	}
	if (record.actual != nblks * GTA4L_EMMC_BLOCK_BYTES) {
		return kIOReturnIOError;
	}
	return kIOReturnSuccess;
}

static IOReturn
gta4l_test_breakup_read(IOBlockStorageDevice *device,
    UInt64 absolute_start, IOMemoryDescriptor *buffer,
    UInt64 *actual_byte_count)
{
	struct gta4l_test_breakup_part parts[GTA4L_TEST_BREAKUP_PART_MAX];
	struct gta4l_test_deblock_chunk chunks[GTA4L_TEST_DEBLOCK_CHUNK_MAX];
	struct gta4l_test_deblock_segment segments[GTA4L_TEST_DEBLOCK_SEGMENT_MAX];
	uint8_t scratch[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t request_length = (uint32_t)buffer->getLength();
	uint32_t head = (uint32_t)(absolute_start %
	    GTA4L_EMMC_BLOCK_BYTES);
	uint32_t tail;
	uint32_t span;
	uint64_t block_start;
	uint64_t max_blocks = GTA4L_EMMC_BLOCK_MAX_BLOCKS;
	uint64_t max_bytes = GTA4L_EMMC_BLOCK_MAX_BYTES;
	uint64_t max_segments = 1;
	uint64_t limit;
	uint64_t break_size;
	uint64_t offset;
	uint64_t remaining;
	uint32_t tag = buffer->getTag();
	uint32_t call = (uint32_t)gta4l_test.breakup_read_calls++;
	OSNumber *property;
	IOReturn status;
	int chunk_count = 0;
	int segment_count;
	int request_index;
	int count;
	int index;

	memset(scratch, 0, sizeof(scratch));
	if (gta4l_test.breakup_advance_ms != 0) {
		gta4l_test.now_ms += gta4l_test.breakup_advance_ms;
	}
	if (gta4l_test_breakup_hook != 0) {
		gta4l_test_breakup_hook(call, 0);
	}
	if ((gta4l_test.breakup_cancel_at >= 0) &&
	    (call == (uint32_t)gta4l_test.breakup_cancel_at) &&
	    (gta4l_test_breakup_hook != 0)) {
		gta4l_test_breakup_hook(call, 2);
	}
	if ((gta4l_test.breakup_fail_at >= 0) &&
	    (call == (uint32_t)gta4l_test.breakup_fail_at)) {
		if (actual_byte_count != 0) {
			*actual_byte_count = 0;
		}
		return (gta4l_test.breakup_fail_status != 0) ?
		    gta4l_test.breakup_fail_status : kIOReturnIOError;
	}
	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumBlockCountReadKey));
	if (property != 0) {
		max_blocks = property->unsigned64BitValue();
	}
	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumByteCountReadKey));
	if (property != 0) {
		max_bytes = property->unsigned64BitValue();
	}
	property = OSDynamicCast(OSNumber,
	    device->getProperty(kIOMaximumSegmentCountReadKey));
	if (property != 0) {
		max_segments = property->unsigned64BitValue();
	}
	if (gta4l_test.breakup_limit_blocks > 0) {
		max_blocks = (uint64_t)gta4l_test.breakup_limit_blocks;
	}
	if (gta4l_test.breakup_limit_bytes > 0) {
		max_bytes = (uint64_t)gta4l_test.breakup_limit_bytes;
	}
	if (gta4l_test.breakup_limit_segments > 0) {
		max_segments = (uint64_t)gta4l_test.breakup_limit_segments;
	}
	if ((gta4l_test.breakup_foreign_at >= 0) &&
	    (call == (uint32_t)gta4l_test.breakup_foreign_at)) {
		uint8_t foreign[GTA4L_EMMC_BLOCK_BYTES];
		uint64_t foreign_block =
		    absolute_start / GTA4L_EMMC_BLOCK_BYTES;
		uint32_t foreign_tag = 0;
		uint32_t foreign_blocks = 1;

		memset(foreign, 0, sizeof(foreign));
		if (gta4l_test.breakup_foreign_mode == 1) {
			foreign_block = 0;
		} else if (gta4l_test.breakup_foreign_mode == 2) {
			foreign_tag = tag + 1u;
			if (foreign_tag == 0) {
				foreign_tag = 0x100u;
			}
		} else if (gta4l_test.breakup_foreign_mode == 3) {
			foreign_block++;
		}
		{
			gta4l_test_breakup_descriptor descriptor(foreign,
			    (UInt64)foreign_blocks *
			    GTA4L_EMMC_BLOCK_BYTES, kIODirectionIn,
			    foreign_tag);

			status = gta4l_test_breakup_issue(device,
			    &descriptor, foreign_block, foreign_blocks);
		}
		if (status != kIOReturnSuccess) {
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return status;
		}
	}
	status = kIOReturnSuccess;
	if ((head == 0) &&
	    ((request_length % GTA4L_EMMC_BLOCK_BYTES) == 0)) {
		limit = 0;
		if (max_blocks != 0) {
			limit = max_blocks * GTA4L_EMMC_BLOCK_BYTES;
		}
		if ((max_bytes != 0) &&
		    ((limit == 0) || (max_bytes < limit))) {
			limit = max_bytes;
		}
		if (limit == 0) {
			limit = request_length;
		}
		break_size = limit - (limit % GTA4L_EMMC_BLOCK_BYTES);
		if (break_size == 0) {
			gta4l_test.breakup_dma_errors++;
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return kIOReturnDMAError;
		}
		count = gta4l_test_breakup_parts(request_length,
		    break_size, parts, GTA4L_TEST_BREAKUP_PART_MAX);
		if (count < 0) {
			gta4l_test.breakup_dma_errors++;
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return kIOReturnDMAError;
		}
		switch (gta4l_test.breakup_split_mode) {
		case 1:
			if (count > 1) {
				gta4l_test_breakup_fill(
				    absolute_start +
				    parts[count - 1].offset,
				    static_cast<uint8_t *>(
				    buffer->address()) +
				    parts[count - 1].offset,
				    parts[count - 1].length);
				count--;
			}
			break;
		case 2:
			if (count < GTA4L_TEST_BREAKUP_PART_MAX) {
				parts[count] = parts[count - 1];
				count++;
			}
			break;
		case 3:
			if (count > 1) {
				struct gta4l_test_breakup_part swap =
				    parts[0];

				parts[0] = parts[1];
				parts[1] = swap;
			}
			break;
		case 4:
			if (count > 1) {
				parts[1].offset =
				    parts[0].offset +
				    GTA4L_EMMC_BLOCK_BYTES;
			}
			break;
		case 5:
			if (count > 0) {
				parts[0].length =
				    (GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS +
				    1u) * GTA4L_EMMC_BLOCK_BYTES;
			}
			break;
		case 6:
			if (count > 0) {
				parts[0].offset = 0;
				parts[0].length = request_length;
				count = 1;
			}
			break;
		default:
			break;
		}
		for (index = 0; index < count; index++) {
			gta4l_test_breakup_descriptor descriptor(
			    static_cast<uint8_t *>(buffer->address()) +
			    parts[index].offset, parts[index].length,
			    kIODirectionIn, tag);

			status = gta4l_test_breakup_issue(device,
			    &descriptor,
			    (absolute_start + parts[index].offset) /
			    GTA4L_EMMC_BLOCK_BYTES,
			    parts[index].length /
			    GTA4L_EMMC_BLOCK_BYTES);
			if (status != kIOReturnSuccess) {
				if (actual_byte_count != 0) {
					*actual_byte_count = 0;
				}
				return status;
			}
		}
	} else {
		tail = (uint32_t)((absolute_start + request_length) %
		    GTA4L_EMMC_BLOCK_BYTES);
		if (tail != 0) {
			tail = GTA4L_EMMC_BLOCK_BYTES - tail;
		}
		span = head + request_length + tail;
		block_start = absolute_start - head;
		chunks[chunk_count].address = scratch;
		chunks[chunk_count].physical_address = 0;
		chunks[chunk_count].length = head;
		chunks[chunk_count].span_offset = 0;
		chunk_count++;
		request_index = chunk_count;
		chunks[chunk_count].address =
		    static_cast<uint8_t *>(buffer->address());
		chunks[chunk_count].physical_address = 0;
		chunks[chunk_count].length = request_length;
		chunks[chunk_count].span_offset = head;
		chunk_count++;
		if (tail != 0) {
			chunks[chunk_count].address = scratch;
			chunks[chunk_count].physical_address = 0;
			chunks[chunk_count].length = tail;
			chunks[chunk_count].span_offset =
			    head + request_length;
			chunk_count++;
		}
		if (gta4l_test.breakup_unaligned_bounce != 0) {
			chunks[request_index].physical_address =
			    0x7fff0000ull + PAGE_SIZE - 8u;
		}
		if ((head != 0) || (tail != 0)) {
			uint64_t request_address =
			    (gta4l_test.breakup_unaligned_bounce != 0) ?
			    0x7fff0000ull + PAGE_SIZE - 8u :
			    (uint64_t)(uintptr_t)buffer->address();
			uint64_t first_segment = PAGE_SIZE -
			    (request_address & (PAGE_SIZE - 1u));

			if (first_segment < GTA4L_EMMC_BLOCK_BYTES) {
				gta4l_test.breakup_dma_errors++;
				if (actual_byte_count != 0) {
					*actual_byte_count = 0;
				}
				return kIOReturnDMAError;
			}
		}
		segment_count = gta4l_test_deblock_segments(chunks,
		    chunk_count, (uint64_t)(uintptr_t)scratch, segments,
		    GTA4L_TEST_DEBLOCK_SEGMENT_MAX);
		break_size = gta4l_test_deblock_break_size(segments,
		    segment_count, max_blocks, max_bytes, max_segments,
		    GTA4L_EMMC_BLOCK_BYTES);
		if (break_size == 0) {
			gta4l_test.breakup_dma_errors++;
			if (actual_byte_count != 0) {
				*actual_byte_count = 0;
			}
			return kIOReturnDMAError;
		}
		offset = 0;
		remaining = span;
		while (remaining > 0) {
			uint64_t part = (break_size < remaining) ?
			    break_size : remaining;
			gta4l_test_deblock_descriptor descriptor(chunks,
			    chunk_count, offset, part, kIODirectionIn);

			status = gta4l_test_breakup_issue(device,
			    &descriptor,
			    (block_start + offset) /
			    GTA4L_EMMC_BLOCK_BYTES,
			    part / GTA4L_EMMC_BLOCK_BYTES);
			if (status != kIOReturnSuccess) {
				if (actual_byte_count != 0) {
					*actual_byte_count = 0;
				}
				return status;
			}
			offset += part;
			remaining -= part;
		}
	}
	if (gta4l_test_breakup_hook != 0) {
		gta4l_test_breakup_hook(call, 1);
	}
	if ((gta4l_test.breakup_corrupt_at >= 0) &&
	    (call == (uint32_t)gta4l_test.breakup_corrupt_at)) {
		uint8_t *destination =
		    static_cast<uint8_t *>(buffer->address());

		destination[request_length / 2u] ^= 0x5au;
	}
	if (actual_byte_count != 0) {
		*actual_byte_count = request_length;
		if ((gta4l_test.breakup_short_at >= 0) &&
		    (call == (uint32_t)gta4l_test.breakup_short_at)) {
			if (gta4l_test.breakup_short_mode == 1) {
				*actual_byte_count = 0;
			} else if (gta4l_test.breakup_short_mode == 2) {
				*actual_byte_count = request_length + 1u;
			} else {
				*actual_byte_count = (request_length == 0) ?
				    0 : request_length - 1u;
			}
		}
	}
	return kIOReturnSuccess;
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
	if ((buffer == 0) || (buffer->address() == 0) ||
	    (buffer->getLength() == 0) ||
	    ((gta4l_test.deblock_model == 0) &&
	    (buffer->getLength() < GTA4L_EMMC_BLOCK_BYTES))) {
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
		if ((byteStart == (UInt64)GTA4L_EMMC_BLOCK_REPORT_LBA *
		    GTA4L_EMMC_BLOCK_BYTES) &&
		    (gta4l_test.part_publish_count > 0)) {
			gta4l_test.part_publish_active = 1;
		}
		return kIOReturnSuccess;
	}
	if (gta4l_test.deblock_model != 0) {
		return gta4l_test_deblock_read(device, base_value + byteStart,
		    buffer, actualByteCount);
	}
	if (gta4l_test.breakup_model != 0) {
		return gta4l_test_breakup_read(device, base_value + byteStart,
		    buffer, actualByteCount);
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
	if ((record.status == kIOReturnSuccess) &&
	    (byteStart == (UInt64)GTA4L_EMMC_BLOCK_REPORT_LBA *
	    GTA4L_EMMC_BLOCK_BYTES) &&
	    (gta4l_test.part_publish_count > 0)) {
		gta4l_test.part_publish_active = 1;
	}
	return record.status;
}
