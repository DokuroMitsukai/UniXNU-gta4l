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
typedef uint64_t IOPhysicalAddress;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096u
#endif
#define kIOMemoryMapperNone 0u
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
#define kIOReturnDMAError ((IOReturn)0xe00002d4u)
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
#define kIOGUIDPartitionSchemeUUIDKey "UUID"
#define kIOMessageMediaStateHasChanged 0xe0000001u
#define kIOMediaStateOffline 0

#define kIODirectionNone 0
#define kIODirectionIn 1
#define kIODirectionOut 2

#define kIOMemoryIncoherentIOFlush 1
#define kIOMemoryIncoherentIOStore 2

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

class OSObject;
OSObject *gta4l_test_meta_cast(OSObject *object, const char *name);

class OSObject {
public:
	virtual ~OSObject() {}
	virtual const void *typeTag() const = 0;
	virtual void retain() {}
	virtual void release() {}
	OSObject *metaCast(const char *name) const;
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
	explicit OSDictionary(const char *className = 0)
	    : class_name_(className) {}
	const void *typeTag() const override { return &test_tag; }
	const char *className() const { return class_name_; }
private:
	const char *class_name_;
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

class IORegistryPlane {
public:
	IORegistryPlane() : unused(0) {}
	int unused;
};

extern const IORegistryPlane *gIOServicePlane;

class IOMemoryDescriptor : public OSObject {
public:
	inline static const int test_tag = 0;
	static IOMemoryDescriptor *withAddress(void *address, UInt64 length,
	    int direction);
	const void *typeTag() const override { return &test_tag; }
	virtual IOByteCount writeBytes(IOByteCount offset, const void *source,
	    IOByteCount length);
	virtual UInt64 getLength() const { return length_; }
	virtual int getDirection() const { return direction_; }
	virtual void *address() const { return address_; }
	virtual void setTag(UInt32 tag) { tag_ = tag; }
	virtual UInt32 getTag() const { return tag_; }
	virtual void release() override;
	virtual IOPhysicalAddress getPhysicalSegment(IOByteCount offset,
	    IOByteCount *length, IOOptionBits options);
	virtual IOReturn prepare(int direction);
	virtual IOReturn complete(int direction);
	virtual IOReturn performOperation(IOOptionBits options,
	    IOByteCount offset, IOByteCount length);
protected:
	void *address_ = nullptr;
	UInt64 length_ = 0;
	int direction_ = 0;
	UInt32 tag_ = 0;
};

class IOService : public OSObject {
public:
	inline static const int test_tag = 0;
	IOService() : property_count_(0), provider_(0), registered_(false),
	    uuid_value_(0), child_count_(0) {}
	const void *typeTag() const override { return &test_tag; }	virtual bool init();
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
	virtual OSIterator *getChildIterator(const IORegistryPlane *plane) const;
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
	const char *uuid_value_;
	OSString uuid_object_;
	OSNumber property_number_;
	IOService *children_[8];
	int child_count_;
};

class IOGUIDPartitionScheme : public IOService {
public:
	inline static const int test_tag = 0;
	const void *typeTag() const override { return &test_tag; }
};

inline OSObject *
gta4l_test_meta_cast(OSObject *object, const char *name)
{
	if (object == nullptr) {
		return nullptr;
	}
	if (strcmp(name, "IOGUIDPartitionScheme") == 0) {
		return dynamic_cast<IOGUIDPartitionScheme *>(object) != nullptr ?
		    object : nullptr;
	}
	return nullptr;
}

inline OSObject *
OSObject::metaCast(const char *name) const
{
	return gta4l_test_meta_cast(const_cast<OSObject *>(this), name);
}

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
	uint32_t property_faults;
	uint32_t base_reads;
	uint32_t size_reads;
	bool open_value;
	bool open_refuse;
	int open_calls;
	int close_calls;
	IOService *open_client;
	IOService *close_client;
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
	int thread_start_fail_at;
	int thread_start_fail_result;
	int lock_alloc_fail;
	uint32_t platform_maps;
	uint32_t smmu_maps;
	uint32_t smmu_reads;
	int smmu_map_result;
	uint32_t smmu_fault_after;
	uint32_t smmu_fault_count;
	uint32_t smmu_fault_value;
	uint32_t smmu_regs[GTA4L_EMMC_PLATFORM_SMMU_WINDOW / 4u];
	uint32_t adma_calls;
	int adma_fail_at;
	int adma_corrupt_at;
	int adma_short_blocks;
	int adma_partial_blocks;
	int adma_canary_byte;
	int adma_status_override;
	int descriptor_physical_high;
	uint8_t *adma_buffer;
	uint32_t adma_buffer_bytes;
	/* Non-coherent master model: the fake controller fetches descriptors
	 * from a DRAM shadow and writes data into a second shadow. Only a
	 * Store moves CPU lines into the shadows and only a Flush moves the
	 * data shadow back, so a dropped or reordered maintenance call is
	 * visible as a real fault. */
	int adma_noncoherent;
	uint32_t cache_calls;
	uint32_t cache_store_calls;
	uint32_t cache_flush_calls;
	uint32_t cache_store_bytes;
	uint32_t cache_flush_bytes;
	uint32_t cache_table_store;
	uint32_t cache_buffer_store;
	uint32_t cache_buffer_flush;
	int cache_fail_at;
	int adma_table_shadow_valid;
	int adma_buffer_shadow_valid;
	/* B10.13 stack-path gate witness. dmaReadLocked records the controller
	 * lock depth at every transfer; a zero depth means the command ran
	 * outside the one gate. The mutation controls remove the gate lock
	 * from doAsyncReadWrite or the dmaRead wrapper and require a
	 * violation. */
	int dma_lock_depth;
	uint32_t dma_lock_violations;
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
	int multi_stop_at;
	int multi_short_at;
	int multi_corrupt_at;
	int multi_corrupt_count;
	uint64_t multi_corrupt_block;
	int multi_corrupt_pair;
	int read_corrupt_at;
	uint64_t read_corrupt_block;
	int media_read_replay;
	uint32_t copy_calls;
	int short_copy_at;
	int descriptor_alloc_fail;
	uint32_t descriptor_alloc_calls;
	uint32_t descriptor_release_calls;
	uint32_t consumer_release_calls;
	uint32_t registered;
	uint32_t sleeps;
	uint32_t now_ms;
	uint32_t property_calls;
	int property_fail_at;
	uint32_t sleep_with_lock;
	uint32_t message_count;
	uint32_t message_type;
	void *message_argument;
	int message_lock_depth;
	uint32_t log_count;
	char log[4096];
	char panic[GTA4L_EMMC_BLOCK_REPORT_BYTES];
	int panicked;
	uint32_t panic_calls;
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
	int deblock_model;
	int deblock_segment_override;
	int deblock_fail_at;
	int deblock_fail_status;
	int deblock_short_at;
	int deblock_short_mode;
	int deblock_corrupt_at;
	int deblock_cancel_at;
	uint32_t deblock_advance_ms;
	int deblock_read_calls;
	int deblock_adapter_calls;
	int deblock_dma_errors;
	int deblock_unaligned_bounce;
	int breakup_model;
	int breakup_limit_blocks;
	int breakup_limit_bytes;
	int breakup_limit_segments;
	int breakup_fail_at;
	int breakup_fail_status;
	int breakup_short_at;
	int breakup_short_mode;
	int breakup_corrupt_at;
	int breakup_cancel_at;
	uint32_t breakup_advance_ms;
	int breakup_read_calls;
	int breakup_adapter_calls;
	int breakup_dma_errors;
	int breakup_unaligned_bounce;
	int breakup_split_mode;
	int breakup_foreign_at;
	int breakup_foreign_mode;
	int widthhold_media_tag_delta;
	int widthhold_media_block_delta;
	int widthhold_media_extra;
	int sweep_reference_corrupt_at;
	int sweep_reference_corrupt_count;
	int sweep_reference_corrupt_mode;
	int sweep_reference_short_at;
	int consume_client_alloc_fail;
	int consume_client_init_fail;
	int consume_alloc_fail;
	int consume_segment_zero;
	int consume_segment_short;
	int consume_advance_on_open;
	uint32_t consume_alloc_calls;
	uint32_t consume_free_calls;
	uint64_t consume_alloc_size;
	uint64_t consume_alloc_alignment;
	uint64_t consume_free_size;
	void *consume_alloc_address;
	int consume_live_alloc;
	/* N10.16 width-mode seam. The verified switch and the baseline-
	 * captured recover helper are transport helpers, not part of the
	 * portable core. The model fills the observed result fields and the
	 * HOST_CONTROL model the adapter reads back. */
	int width_ext_csd_fail;
	int width_switch_fail;
	int width_switch_verified;
	int width_switch_host_bit;
	int width_switch_switched;
	int width_switch_cmd6;
	int width_switch_advance_ms;
	int width_recover_fail;
	int width_recover_verified;
	int width_recover_host_clear;
	int width_recover_card_keep;
	int width_recover_cmd6;
	int width_recover_advance_ms;
	int width_host_control;
	IOLock *width_gate;
	int width_gate_release_ms;
	int width_wait_active;
	uint32_t width_ext_csd_calls;
	uint32_t width_switch_calls;
	uint32_t width_recover_calls;
	/* S1 speed-mode seam. The HS entry, the verified 8-bit then 4-bit
	 * switch and the combined width/timing revert are transport helpers.
	 * The speed-active flag selects the speed knobs for the shared
	 * verified-switch mock so width-mode scenarios keep their model. */
	int speed_active;
	int speed_hispd;
	int speed_ext_csd_fail;
	int speed_hs_rc;
	int speed_hs_before;
	int speed_hs_after;
	int speed_hs_clock_rc;
	int speed_hs_rcg_after;
	int speed_hs_advance_ms;
	int speed_switch8_rc;
	int speed_switch8_verified;
	int speed_switch8_rollback;
	int speed_switch8_host_bit;
	int speed_switch8_cmd6;
	int speed_switch8_advance_ms;
	int speed_switch4_rc;
	int speed_switch4_verified;
	int speed_switch4_rollback;
	int speed_switch4_host_bit;
	int speed_switch4_cmd6;
	int speed_switch4_advance_ms;
	int speed_recover_rc;
	int speed_recover_verified;
	int speed_recover_host_clear;
	int speed_recover_card_keep;
	int speed_recover_hs_after;
	int speed_recover_clock_rc;
	int speed_recover_hs_rc;
	int speed_recover_width_rc;
	int speed_recover_cmd6;
	int speed_recover_advance_ms;
	uint32_t speed_hs_calls;
	uint32_t speed_switch8_calls;
	uint32_t speed_switch4_calls;
	uint32_t speed_recover_calls;
	uint32_t speed_recover_budget_ms;
	uint32_t deadline_arm_calls;
	uint32_t deadline_clear_calls;
	uint32_t deadline_last_budget_ms;
};

typedef void (*gta4l_test_partition_sample_fn)(uint32_t sample,
    uint32_t visible);
extern gta4l_test_partition_sample_fn gta4l_test_partition_sample_hook;

/* Width-mode interleaving seam. The gate-wait phase fires from IOSleep while
 * the bounded acquisition is polling; the after-read and before-terminal
 * phases fire from the adapter at those exact boundaries. */
#define GTA4L_TEST_WIDTH_PHASE_GATE_WAIT 0u
#define GTA4L_TEST_WIDTH_PHASE_AFTER_READ 1u
#define GTA4L_TEST_WIDTH_PHASE_BEFORE_TERMINAL 2u
typedef void (*gta4l_test_width_sample_fn)(uint32_t phase);
extern gta4l_test_width_sample_fn gta4l_test_width_hook;

/* Widthhold recovery seam. The before-revert phase fires while the card is
 * still at 4-bit; the after-revert phase fires once the verified 1-bit
 * revert has completed but before the terminal report is emitted. */
#define GTA4L_TEST_WIDTHHOLD_PHASE_BEFORE_REVERT 0u
#define GTA4L_TEST_WIDTHHOLD_PHASE_AFTER_REVERT 1u
typedef void (*gta4l_test_widthhold_sample_fn)(uint32_t phase);
extern gta4l_test_widthhold_sample_fn gta4l_test_widthhold_hook;

/* S1 speed start-path seam. The start phase fires from start() after the
 * report marker is set and before the first HS operation, so a scenario can
 * interleave a competing request or release the controller gate. */
#define GTA4L_TEST_SPEED_PHASE_START 0u
typedef void (*gta4l_test_speed_sample_fn)(uint32_t phase);
extern gta4l_test_speed_sample_fn gta4l_test_speed_hook;

extern gta4l_test_state gta4l_test;

typedef IOReturn (*gta4l_test_media_read_fn)(IOMedia *media, UInt64 byteStart,
    IOMemoryDescriptor *buffer, UInt64 *actualByteCount);
extern gta4l_test_media_read_fn gta4l_test_media_read_hook;

typedef void (*gta4l_test_deblock_sample_fn)(uint32_t call, uint32_t phase);
extern gta4l_test_deblock_sample_fn gta4l_test_deblock_hook;

typedef void (*gta4l_test_breakup_sample_fn)(uint32_t call, uint32_t phase);
extern gta4l_test_breakup_sample_fn gta4l_test_breakup_hook;

typedef void (*gta4l_test_multi_sample_fn)(uint32_t call);
extern gta4l_test_multi_sample_fn gta4l_test_multi_hook;

void gta4l_test_fill_sector(uint64_t block, void *buffer);

extern IOMedia *gta4l_test_media[GTA4L_TEST_MAX_MEDIA];
extern int gta4l_test_media_count;

#define GTA4L_TEST_MAX_SCHEMES 4
extern IOService *gta4l_test_schemes[GTA4L_TEST_MAX_SCHEMES];
extern int gta4l_test_scheme_count;

#define GTA4L_TEST_PROP_WHOLE_MISSING 0x01u
#define GTA4L_TEST_PROP_BASE_MISSING 0x02u
#define GTA4L_TEST_PROP_SIZE_MISSING 0x04u
#define GTA4L_TEST_PROP_CONTENT_MISSING 0x08u
#define GTA4L_TEST_PROP_WHOLE_WRONG 0x10u
#define GTA4L_TEST_PROP_BASE_WRONG 0x20u
#define GTA4L_TEST_PROP_SIZE_WRONG 0x40u
#define GTA4L_TEST_PROP_CONTENT_WRONG 0x80u
#define GTA4L_TEST_PROP_WRITABLE_MISSING 0x100u
#define GTA4L_TEST_PROP_WRITABLE_WRONG 0x200u
/* Faults that only fire after the first two reads of that property. The
 * whole-media scan reads base/size once on every candidate and the
 * partition lookup reads them once more, so the third read models a
 * property that changes after selection and reaches the post-selection
 * discovery checks. */
#define GTA4L_TEST_PROP_BASE_MISSING_LATE 0x400u
#define GTA4L_TEST_PROP_SIZE_WRONG_LATE 0x800u

void gta4l_test_reset(void);
void gta4l_test_media_reset(void);
void gta4l_test_add_media(IOMedia *media);
void gta4l_test_media_configure(IOMedia *media, bool whole, UInt64 base,
    UInt64 size, bool writable, const char *content);
void gta4l_test_media_chain(IOMedia *media, IOService *driver,
    IOService *root);
void gta4l_test_add_child(IOService *parent, IOService *child);
void gta4l_test_add_scheme(IOService *scheme);
void gta4l_test_scheme_configure(IOService *scheme, IOService *provider,
    const char *uuid);
void gta4l_test_fill_sector(uint64_t block, void *buffer);

IOLock *IOLockAlloc(void);
void IOLockFree(IOLock *lock);
void IOLockLock(IOLock *lock);
bool IOLockTryLock(IOLock *lock);
void IOLockUnlock(IOLock *lock);

static inline void *IOMalloc(size_t size)
{
	return malloc(size);
}

static inline void IOFree(void *address, size_t size)
{
	(void)size;
	free(address);
}

static inline void *IOMallocAligned(size_t size, size_t alignment)
{
	void *address = nullptr;

	if (posix_memalign(&address, alignment, size) != 0) {
		return nullptr;
	}
	return address;
}

static inline void IOFreeAligned(void *address, size_t size)
{
	(void)size;
	free(address);
}

void *IOMallocContiguous(size_t size, size_t alignment, void *tag);
void IOFreeContiguous(void *address, size_t size);

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
