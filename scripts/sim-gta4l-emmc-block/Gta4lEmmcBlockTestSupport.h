// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Host-only IOKit seam for the gta4l block adapter.
//
// This is a faithful minimal model of the IOKit objects the adapter uses. It
// is not target ABI evidence: the adapter is also compiled against the real
// qualified headers by the build contract test. The seam keeps the adapter
// source unchanged between host and target; only the included headers and the
// transport callbacks differ.
#ifndef GTA4L_EMMC_BLOCK_TEST_SUPPORT_H
#define GTA4L_EMMC_BLOCK_TEST_SUPPORT_H

#include <csetjmp>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Gta4lEmmc.h"
#include "Gta4lEmmcPlatform.h"
#include "Gta4lEmmcBlock.h"
#include "Gta4lEmmcBlockCore.h"

typedef uint64_t UInt64;
typedef uint32_t UInt32;
typedef uint64_t IOByteCount;
typedef uint32_t IOOptionBits;
typedef int IOStorageAccess;
typedef int IOReturn;
typedef int kern_return_t;
typedef int wait_result_t;
typedef void *thread_t;

#define KERN_SUCCESS 0

#define kIOStorageAccessNone 0
#define kIOStorageAccessReader 0x01
#define kIOStorageAccessReaderWriter 0x03

#define kIOReturnSuccess ((IOReturn)0)
#define kIOReturnBadArgument ((IOReturn)0xe00002c2u)
#define kIOReturnMessageTooLarge ((IOReturn)0xe00002e1u)
#define kIOReturnNotReady ((IOReturn)0xe00002d8u)
#define kIOReturnNotOpen ((IOReturn)0xe00002cdu)
#define kIOReturnNotWritable ((IOReturn)0xe00002cfu)
#define kIOReturnNoMedia ((IOReturn)0xe00002e4u)
#define kIOReturnNoMemory ((IOReturn)0xe00002bdu)
#define kIOReturnIOError ((IOReturn)0xe00002cau)
#define kIOReturnUnsupported ((IOReturn)0xe00002c7u)
#define kIOReturnTimeout ((IOReturn)0xe00002d6u)

#define kIOMaximumBlockCountReadKey "IOMaximumBlockCountRead"
#define kIOMaximumByteCountReadKey "IOMaximumByteCountRead"
#define kIOMaximumSegmentCountReadKey "IOMaximumSegmentCountRead"
#define kIOMediaWholeKey "Whole"
#define kIOMediaBaseKey "Base"
#define kIOMediaSizeKey "Size"
#define kIOMediaContentKey "Content"
#define kIOMediaWritableKey "Writable"
#define kIOMessageMediaStateHasChanged 0xe0000001u
#define kIOMediaStateOffline 0

#define kIODirectionNone 0
#define kIODirectionIn 1
#define kIODirectionOut 2

struct IOStorageAttributes {
	int unused;
};

struct IOStorageCompletion {
	void *target;
	void (*action)(void *target, void *parameter, IOReturn status,
	    IOByteCount actualByteCount);
	void *parameter;
};

struct IOBlockStorageDeviceExtent {
	UInt64 block;
	UInt64 nblks;
};

struct IOBlockStorageProvisionDeviceExtent {
	UInt64 block;
	UInt64 nblks;
	UInt32 provisionType;
};

typedef uint32_t IOStorageUnmapOptions;
typedef uint32_t IOStorageSynchronizeOptions;
typedef uint32_t IOStorageGetProvisionStatusOptions;

class OSObject {
public:
	virtual ~OSObject() {}
	virtual const void *typeTag() const = 0;
	virtual void retain() {}
	virtual void release() {}
};

template <typename T>
T *gta4l_test_dynamic_cast(OSObject *object)
{
	return dynamic_cast<T *>(object);
}

#define OSDynamicCast(type, object) gta4l_test_dynamic_cast<type>(object)

class OSBoolean : public OSObject {
public:
	inline static const int test_tag = 0;
	explicit OSBoolean(bool value = false) : value_(value) {}
	const void *typeTag() const override { return &test_tag; }
	void setValue(bool value) { value_ = value; }
	bool getValue() const { return value_; }
private:
	bool value_;
};

class OSNumber : public OSObject {
public:
	inline static const int test_tag = 0;
	explicit OSNumber(UInt64 value = 0) : value_(value) {}
	const void *typeTag() const override { return &test_tag; }
	void setValue(UInt64 value) { value_ = value; }
	UInt64 unsigned64BitValue() const { return value_; }
private:
	UInt64 value_;
};

class OSString : public OSObject {
public:
	inline static const int test_tag = 0;
	explicit OSString(const char *value = 0) : value_(value) {}
	const void *typeTag() const override { return &test_tag; }
	void setValue(const char *value) { value_ = value; }
	const char *getCStringNoCopy() const { return value_; }
	unsigned int getLength() const
	{
		return (value_ == 0) ? 0u : (unsigned int)strlen(value_);
	}
private:
	const char *value_;
};

class OSDictionary : public OSObject {
public:
	inline static const int test_tag = 0;
	const void *typeTag() const override { return &test_tag; }
};

class OSIterator : public OSObject {
public:
	inline static const int test_tag = 0;
	explicit OSIterator(OSObject **items = 0, int count = 0)
	    : items_(items), count_(count), at_(0) {}
	const void *typeTag() const override { return &test_tag; }
	OSObject *getNextObject()
	{
		if ((items_ == 0) || (at_ >= count_)) {
			return 0;
		}
		return items_[at_++];
	}
private:
	OSObject **items_;
	int count_;
	int at_;
};

class IOMemoryDescriptor : public OSObject {
public:
	inline static const int test_tag = 0;
	static IOMemoryDescriptor *withAddress(void *address, UInt64 length,
	    int direction);
	const void *typeTag() const override { return &test_tag; }
	IOByteCount writeBytes(IOByteCount offset, const void *source,
	    IOByteCount length);
	UInt64 getLength() const { return length_; }
	int getDirection() const { return direction_; }
	void *address() const { return address_; }
private:
	void *address_;
	UInt64 length_;
	int direction_;
};

class IOService : public OSObject {
public:
	inline static const int test_tag = 0;
	IOService() : property_count_(0), provider_(0), registered_(false) {}
	const void *typeTag() const override { return &test_tag; }
	virtual bool start(IOService *provider) { provider_ = provider; return true; }
	virtual void stop(IOService *provider) { (void)provider; }
	virtual void free() {}
	virtual IOService *getProvider() const { return provider_; }
	virtual void registerService();
	virtual void messageClients(UInt32 type, void *argument);
	virtual bool setProperty(const char *key, const char *value);
	virtual bool setProperty(const char *key, bool value);
	virtual bool setProperty(const char *key, unsigned long long value,
	    unsigned int bits);
	virtual OSObject *getProperty(const char *key) const;
	static OSDictionary *serviceMatching(const char *className);
	static OSIterator *getMatchingServices(OSDictionary *matching);

	struct test_property {
		const char *key;
		unsigned long long value;
		unsigned int bits;
	};
	test_property properties_[8];
	int property_count_;
	IOService *provider_;
	bool registered_;
};

class IOBlockStorageDevice : public IOService {
public:
	virtual bool start(IOService *provider) override
	{
		return IOService::start(provider);
	}
	virtual void stop(IOService *provider) override
	{
		IOService::stop(provider);
	}
	virtual void free() override {}
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer,
	    UInt64 block, UInt64 nblks, IOStorageAttributes *attributes,
	    IOStorageCompletion *completion)
	{
		(void)buffer;
		(void)block;
		(void)nblks;
		(void)attributes;
		(void)completion;
		return kIOReturnUnsupported;
	}
	virtual IOReturn doEjectMedia() { return kIOReturnUnsupported; }
	virtual IOReturn doFormatMedia(UInt64 byteCapacity)
	{
		(void)byteCapacity;
		return kIOReturnUnsupported;
	}
	virtual UInt32 doGetFormatCapacities(UInt64 *capacities,
	    UInt32 capacitiesMaxCount) const
	{
		(void)capacities;
		(void)capacitiesMaxCount;
		return 0;
	}
	virtual IOReturn doSynchronizeCache() { return kIOReturnSuccess; }
	virtual IOReturn getWriteCacheState(bool *enabled)
	{
		if (enabled != 0) {
			*enabled = false;
		}
		return kIOReturnSuccess;
	}
	virtual IOReturn setWriteCacheState(bool enabled)
	{
		(void)enabled;
		return kIOReturnUnsupported;
	}
	virtual IOReturn doUnmap(IOBlockStorageDeviceExtent *extents,
	    UInt32 extentsCount, IOStorageUnmapOptions options)
	{
		(void)extents;
		(void)extentsCount;
		(void)options;
		return kIOReturnUnsupported;
	}
	virtual IOReturn doSynchronize(UInt64 block, UInt64 nblks,
	    IOStorageSynchronizeOptions options)
	{
		(void)block;
		(void)nblks;
		(void)options;
		return kIOReturnSuccess;
	}
	virtual IOReturn doGetProvisionStatus(UInt64 block, UInt64 nblks,
	    UInt32 *extentsCount, IOBlockStorageProvisionDeviceExtent *extents,
	    IOStorageGetProvisionStatusOptions options)
	{
		(void)block;
		(void)nblks;
		(void)extentsCount;
		(void)extents;
		(void)options;
		return kIOReturnUnsupported;
	}
	virtual char *getVendorString() { return 0; }
	virtual char *getProductString() { return 0; }
	virtual char *getRevisionString() { return 0; }
	virtual char *getAdditionalDeviceInfoString() { return 0; }
	virtual IOReturn reportBlockSize(UInt64 *blockSize)
	{
		(void)blockSize;
		return kIOReturnUnsupported;
	}
	virtual IOReturn reportEjectability(bool *isEjectable)
	{
		(void)isEjectable;
		return kIOReturnUnsupported;
	}
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock)
	{
		(void)maxBlock;
		return kIOReturnUnsupported;
	}
	virtual IOReturn reportMediaState(bool *mediaPresent,
	    bool *changedState = 0)
	{
		(void)mediaPresent;
		(void)changedState;
		return kIOReturnUnsupported;
	}
	virtual IOReturn reportRemovability(bool *isRemovable)
	{
		(void)isRemovable;
		return kIOReturnUnsupported;
	}
	virtual IOReturn reportWriteProtection(bool *isWriteProtected)
	{
		(void)isWriteProtected;
		return kIOReturnUnsupported;
	}
};

class IOMedia : public IOService {
public:
	inline static const int test_tag = 0;
	IOMedia();
	const void *typeTag() const override { return &test_tag; }
	OSObject *getProperty(const char *key) const override;
	bool open(IOService *client, IOOptionBits options,
	    IOStorageAccess access);
	void close(IOService *client, IOOptionBits options = 0);
	IOReturn read(IOService *client, UInt64 byteStart,
	    IOMemoryDescriptor *buffer, IOStorageAttributes *attributes,
	    UInt64 *actualByteCount);

	bool whole_value;
	UInt64 base_value;
	UInt64 size_value;
	bool writable_value;
	const char *content_value;
	bool open_value;
	bool open_refuse;
	int open_calls;
	int close_calls;
	IOService *open_client;
	IOStorageAccess open_access;
	OSBoolean whole_object;
	OSBoolean writable_object;
	OSNumber base_object;
	OSNumber size_object;
	OSString content_object;
};

class IOLock {
public:
	int depth;
};

#define OSDeclareDefaultStructors(className) \
	public: \
		className(); \
		virtual ~className();

#define OSDefineMetaClassAndStructors(className, superclassName) \
	className::className() {} \
	className::~className() {}

struct gta4l_test_thread {
	void (*func)(void *, wait_result_t);
	void *context;
};

#define GTA4L_TEST_MAX_MEDIA 96

struct gta4l_test_state {
	const char *boot_args;
	int map_result;
	int init_result;
	uint32_t capacity_blocks;
	int reporter_start_result;
	int watchdog_start_result;
	int lock_alloc_fail;
	uint32_t platform_maps;
	uint32_t init_calls;
	uint32_t read_calls;
	int fail_read_at;
	uint32_t multi_calls;
	int fail_multi_at;
	int multi_rc;
	int multi_data_result;
	int multi_short_blocks;
	int multi_stop_attempted;
	int multi_stop_result;
	int multi_corrupt_at;
	uint64_t multi_corrupt_block;
	int multi_corrupt_pair;
	int read_corrupt_at;
	uint64_t read_corrupt_block;
	int media_read_replay;
	uint32_t copy_calls;
	int short_copy_at;
	int descriptor_alloc_fail;
	uint32_t registered;
	uint32_t sleeps;
	uint32_t now_ms;
	uint32_t sleep_with_lock;
	uint32_t message_count;
	uint32_t message_type;
	void *message_argument;
	int message_lock_depth;
	uint32_t log_count;
	char log[4096];
	char panic[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	int panicked;
	jmp_buf panic_jump;
	gta4l_test_thread threads[2];
	int thread_count;
	int lock_depth;
	int lock_depth_max;
	int part_publish_active;
	int part_publish_count;
	uint32_t part_publish_counts[GTA4L_TEST_MAX_MEDIA];
	uint32_t part_publish_calls;
	int part_publish_error_at;
	uint32_t part_publish_errors;
};

typedef void (*gta4l_test_partition_sample_fn)(uint32_t sample,
    uint32_t visible);
extern gta4l_test_partition_sample_fn gta4l_test_partition_sample_hook;

extern gta4l_test_state gta4l_test;

typedef IOReturn (*gta4l_test_media_read_fn)(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount);
extern gta4l_test_media_read_fn gta4l_test_media_read_hook;

extern IOMedia *gta4l_test_media[GTA4L_TEST_MAX_MEDIA];
extern int gta4l_test_media_count;

void gta4l_test_reset(void);
void gta4l_test_media_reset(void);
void gta4l_test_add_media(IOMedia *media);
void gta4l_test_media_configure(IOMedia *media, bool whole, UInt64 base,
    UInt64 size, bool writable, const char *content);
void gta4l_test_media_chain(IOMedia *media, IOService *driver,
    IOService *root);
void gta4l_test_fill_sector(uint64_t block, void *buffer);

IOLock *IOLockAlloc(void);
void IOLockFree(IOLock *lock);
void IOLockLock(IOLock *lock);
bool IOLockTryLock(IOLock *lock);
void IOLockUnlock(IOLock *lock);

void IOLog(const char *format, ...);
void IOSleep(unsigned milliseconds);
void panic(const char *format, ...) __attribute__((noreturn));

char *PE_boot_args(void);
int PE_parse_boot_argn(const char *name, void *value, int size);

kern_return_t kernel_thread_start(void (*func)(void *, wait_result_t),
    void *context, thread_t *thread);
void thread_deallocate(thread_t thread);
thread_t current_thread(void);
kern_return_t thread_terminate(thread_t thread);

extern "C" {
int gta4l_emmc_platform_map(struct gta4l_emmc_platform *platform);
uint64_t gta4l_emmc_platform_counter(void);
uint64_t gta4l_emmc_platform_counter_hz(void);
uint8_t gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
uint16_t gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
uint32_t gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
void gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value);
void gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value);
void gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value);
void gta4l_emmc_platform_barrier(void);
void gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds);
int gta4l_emmc_init(struct gta4l_emmc_host *host);
int gta4l_emmc_read_block(struct gta4l_emmc_host *host, uint64_t block,
    void *buffer);
int gta4l_emmc_read_multi_result(struct gta4l_emmc_host *host,
    uint64_t start_lba, uint32_t count, void *buffer,
    struct gta4l_emmc_multi_result *result);
}

#endif
