/*
 * Host-side simulated-register harness for the gta4l bounded SDHCI init probe.
 *
 * The test runner copies Gta4lSdhciHostInit.c, replaces the generic counter,
 * the barrier instruction and the six MMIO helpers with the test hooks below,
 * and links it against this harness and minimal kernel-header stubs. The
 * probe's stage order, preconditions, register offsets, write values and panic
 * paths are the production code. Every hooked access is recorded so the tests
 * can reject a transient write that leaves final memory unchanged.
 */
#include <sched.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "Gta4lSdhciHostInit.h"

void gta4l_sdhci_host_init_preflight(void);
void gta4l_sdhci_host_init_run(void);
uint64_t gta4l_sdhci_host_init_test_counter(void);
void gta4l_sdhci_host_init_test_barrier(void);
uint8_t gta4l_sdhci_host_init_test_read8(uintptr_t base, uint32_t offset);
uint16_t gta4l_sdhci_host_init_test_read16(uintptr_t base, uint32_t offset);
uint32_t gta4l_sdhci_host_init_test_read32(uintptr_t base, uint32_t offset);
void gta4l_sdhci_host_init_test_write8(uintptr_t base, uint32_t offset,
    uint8_t value);
void gta4l_sdhci_host_init_test_write16(uintptr_t base, uint32_t offset,
    uint16_t value);
void gta4l_sdhci_host_init_test_write32(uintptr_t base, uint32_t offset,
    uint32_t value);

#define SDHCI_OFF 0x0000u
#define GCC_OFF   0x2000u
#define SIM_SIZE  0x4000u

static unsigned char sim[SIM_SIZE] __attribute__((aligned(4096)));
static int map_count;
static int map_fail;
static int map_fail_gcc;
static int arg_host_init = 1;
static int arg_audit;
static int arg_snapshot;
static jmp_buf panic_jmp;
static int panic_ready;
static char panic_message[2048];
static int device_clear_reset;
static int device_set_vendor;
static uint32_t device_vendor_value;
static int device_clock_ready;
static int device_ignore_irq_clear;
static int device_change_pwr_status;
static int pwr_status_reads;
static int device_post_reset_version;
static int device_post_reset_power;
static int device_post_reset_clock;
static int device_post_reset_irq;
static uint64_t device_reset_delay;
static uint64_t device_clock_delay;
static uint64_t device_reset_ready_at;
static uint64_t device_clock_ready_at;
static uint64_t counter_value;
static uint64_t counter_step;
static int trace_enabled;
static const char *current_phase = "capture";

#define SPACE_SDHCI 0
#define SPACE_GCC   1
#define SPACE_OTHER 2
#define MAX_MMIO_EVENTS 400000

struct mmio_event {
	int write;
	int space;
	unsigned int offset;
	unsigned int width;
	uint32_t value;
	uint64_t seq;
	uint64_t time;
	const char *phase;
};

struct access_rule {
	int space;
	unsigned int offset;
	unsigned int width;
};

struct write_expect {
	int space;
	unsigned int offset;
	unsigned int width;
	uint32_t value;
};

static struct mmio_event mmio_events[MAX_MMIO_EVENTS];
static unsigned int mmio_event_count;
static int mmio_event_overflow;

static void
wr8(unsigned int offset, uint8_t value)
{
	sim[SDHCI_OFF + offset] = value;
}

static void
wr16(unsigned int offset, uint16_t value)
{
	*(volatile uint16_t *)(sim + SDHCI_OFF + offset) = value;
}

static void
wr32(unsigned int offset, uint32_t value)
{
	*(volatile uint32_t *)(sim + SDHCI_OFF + offset) = value;
}

static uint8_t
rd8(unsigned int offset)
{
	return sim[SDHCI_OFF + offset];
}

static uint16_t
rd16(unsigned int offset)
{
	return *(volatile uint16_t *)(sim + SDHCI_OFF + offset);
}

static uint32_t
rd32(unsigned int offset)
{
	return *(volatile uint32_t *)(sim + SDHCI_OFF + offset);
}

static void
gcc_wr32(unsigned int offset, uint32_t value)
{
	*(volatile uint32_t *)(sim + GCC_OFF + offset) = value;
}

static int
mmio_space(uintptr_t base)
{
	if (base == (uintptr_t)(sim + SDHCI_OFF)) {
		return SPACE_SDHCI;
	}
	if (base == (uintptr_t)(sim + GCC_OFF)) {
		return SPACE_GCC;
	}
	return SPACE_OTHER;
}

static void
record_mmio(int write, uintptr_t base, unsigned int offset,
    unsigned int width, uint32_t value)
{
	struct mmio_event *event;

	if (mmio_event_count >= MAX_MMIO_EVENTS) {
		mmio_event_overflow = 1;
		return;
	}
	event = &mmio_events[mmio_event_count++];
	event->write = write;
	event->space = mmio_space(base);
	event->offset = offset;
	event->width = width;
	event->value = value;
	event->seq = mmio_event_count - 1;
	event->time = counter_value;
	event->phase = current_phase;
}

uint8_t
gta4l_sdhci_host_init_test_read8(uintptr_t base, uint32_t offset)
{
	uint8_t value = *(volatile uint8_t *)(base + offset);

	record_mmio(0, base, offset, 8, value);
	return value;
}

uint16_t
gta4l_sdhci_host_init_test_read16(uintptr_t base, uint32_t offset)
{
	uint16_t value = *(volatile uint16_t *)(base + offset);

	if (mmio_space(base) == SPACE_SDHCI &&
	    offset == GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL &&
	    device_clock_ready && counter_value >= device_clock_ready_at &&
	    (value & GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN) != 0) {
		value |= GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE;
	}
	record_mmio(0, base, offset, 16, value);
	return value;
}

uint32_t
gta4l_sdhci_host_init_test_read32(uintptr_t base, uint32_t offset)
{
	uint32_t value = *(volatile uint32_t *)(base + offset);

	if (device_change_pwr_status && mmio_space(base) == SPACE_SDHCI &&
	    offset == GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS) {
		pwr_status_reads++;
		if (pwr_status_reads > 1) {
			value = 0x80000000u;
		}
	}
	record_mmio(0, base, offset, 32, value);
	return value;
}

static void
note_write_phase(int space, unsigned int offset)
{
	if (space != SPACE_SDHCI) {
		current_phase = "gcc-write";
		return;
	}
	if (offset == GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE ||
	    offset == GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE) {
		current_phase = "irq-suppress";
	} else if (offset == GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) {
		current_phase = "reset";
	} else if (offset == GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) {
		current_phase = "clock";
	} else {
		current_phase = "other-write";
	}
}

void
gta4l_sdhci_host_init_test_write8(uintptr_t base, uint32_t offset,
    uint8_t value)
{
	note_write_phase(mmio_space(base), offset);
	record_mmio(1, base, offset, 8, value);
	*(volatile uint8_t *)(base + offset) = value;
	if (mmio_space(base) == SPACE_SDHCI &&
	    offset == GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET &&
	    device_clear_reset) {
		device_reset_ready_at = counter_value + device_reset_delay;
	}
}

void
gta4l_sdhci_host_init_test_write16(uintptr_t base, uint32_t offset,
    uint16_t value)
{
	note_write_phase(mmio_space(base), offset);
	record_mmio(1, base, offset, 16, value);
	*(volatile uint16_t *)(base + offset) = value;
	if (mmio_space(base) == SPACE_SDHCI &&
	    offset == GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL &&
	    (value & GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN) != 0) {
		device_clock_ready_at = counter_value + device_clock_delay;
		if (device_clock_delay == 0 && device_clock_ready) {
			*(volatile uint16_t *)(base + offset) |=
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE;
		}
	}
}

void
gta4l_sdhci_host_init_test_write32(uintptr_t base, uint32_t offset,
    uint32_t value)
{
	note_write_phase(mmio_space(base), offset);
	record_mmio(1, base, offset, 32, value);
	if (device_ignore_irq_clear && mmio_space(base) == SPACE_SDHCI &&
	    (offset == GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE ||
	    offset == GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE)) {
		return;
	}
	*(volatile uint32_t *)(base + offset) = value;
}

static void
reset_sim(void)
{
	memset(sim, 0, sizeof(sim));
	map_count = 0;
	map_fail = 0;
	map_fail_gcc = 0;
	device_clear_reset = 0;
	device_set_vendor = 0;
	device_vendor_value = 0;
	device_clock_ready = 1;
	device_ignore_irq_clear = 0;
	device_change_pwr_status = 0;
	pwr_status_reads = 0;
	device_post_reset_version = 0;
	device_post_reset_power = 0;
	device_post_reset_clock = 0;
	device_post_reset_irq = 0;
	device_reset_delay = 0;
	device_clock_delay = 0;
	device_reset_ready_at = 0;
	device_clock_ready_at = 0;
	current_phase = "capture";
	counter_value = 0;
	counter_step = 1;
	mmio_event_count = 0;
	mmio_event_overflow = 0;
	arg_host_init = 1;
	arg_audit = 0;
	arg_snapshot = 0;

	wr32(GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION, 0x10000072u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_PRESENT_STATE, 0x01f800f0u);
	wr16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 0x0003u);
	wr8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL, 0x00u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_GENERICS, 0x00f04003u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS, 0x00000000u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK, 0x00000000u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC, 0x00300a0cu);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_DDR_CONFIG, 0x80040873u);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_DLL_STATUS, 0x00000000u);

	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_BCR, 0x00000000u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CBCR, 0x00004221u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_AHB_CBCR, 0x00000001u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_ICE_CBCR, 0x00004229u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CMD, 0x00000000u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CFG, 0x00000401u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_M, 0x00000001u);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_N, 0x000000feu);
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_D, 0x000000fdu);
}

void
panic(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(panic_message, sizeof(panic_message), format, args);
	va_end(args);

	if (!panic_ready) {
		fprintf(stderr, "unexpected panic: %s\n", panic_message);
		abort();
	}
	longjmp(panic_jmp, 1);
}

int
PE_parse_boot_argn(const char *arg, void *value, int size)
{
	int parsed;

	if (size < (int)sizeof(int)) {
		return 0;
	}
	if (strcmp(arg, "gta4l-sdhci-host-init") == 0) {
		parsed = arg_host_init;
	} else if (strcmp(arg, "gta4l-sdhci-audit") == 0) {
		parsed = arg_audit;
	} else if (strcmp(arg, "gta4l-sdhci-snapshot") == 0) {
		parsed = arg_snapshot;
	} else {
		return 0;
	}
	*(int *)value = parsed;
	return 1;
}

vm_offset_t
ml_io_map(vm_offset_t phys, vm_size_t size)
{
	if (map_fail) {
		return 0;
	}
	if (size > 0x1000u) {
		return 0;
	}
	if (phys == GTA4L_SDHCI_HOST_INIT_SDHCI_BASE) {
		map_count++;
		return (vm_offset_t)(uintptr_t)(sim + SDHCI_OFF);
	}
	if (phys == GTA4L_SDHCI_HOST_INIT_GCC_BASE) {
		if (map_fail_gcc) {
			return 0;
		}
		map_count++;
		return (vm_offset_t)(uintptr_t)(sim + GCC_OFF);
	}
	return 0;
}

uint64_t
gta4l_sdhci_host_init_test_counter(void)
{
	counter_value += counter_step;
	if (device_clear_reset &&
	    sim[SDHCI_OFF + GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET] ==
	    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL &&
	    counter_value >= device_reset_ready_at) {
		sim[SDHCI_OFF +
		    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET] = 0;
		wr8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL, 0);
		wr16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 0);
		if (device_post_reset_version) {
			wr32(GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION,
			    0x20000072u);
		}
		if (device_post_reset_power) {
			wr8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL,
			    GTA4L_SDHCI_HOST_INIT_POWER_BUS_POWER);
		}
		if (device_post_reset_clock) {
			wr16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN |
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE |
			    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN);
		}
		if (device_post_reset_irq) {
			wr32(GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 1);
		}
		if (device_set_vendor) {
			wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC,
			    device_vendor_value);
		}
	}
	if ((counter_value & 0x3ffu) == 0) {
		sched_yield();
	}
	return counter_value;
}

void
gta4l_sdhci_host_init_test_barrier(void)
{
}

static void
start_device(int clear_reset)
{
	device_clear_reset = clear_reset;
}

static void
stop_device(void)
{
	device_clear_reset = 0;
}

static const struct access_rule allowed_reads[] = {
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_PRESENT_STATE, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL, 8 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_GENERICS, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_DDR_CONFIG, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CORE_DLL_STATUS, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_BCR, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CBCR, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_AHB_CBCR, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_ICE_CBCR, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CMD, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CFG, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_M, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_N, 32 },
	{ SPACE_GCC, GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_D, 32 },
};

static const struct access_rule allowed_writes[] = {
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 32 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8 },
	{ SPACE_SDHCI, GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16 },
};

static int
rule_matches(const struct access_rule *rules, size_t count,
    const struct mmio_event *event)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (rules[i].space == event->space &&
		    rules[i].offset == event->offset &&
		    rules[i].width == event->width) {
			return 1;
		}
	}
	return 0;
}

static int
check_events(void)
{
	unsigned int i;

	if (mmio_event_overflow) {
		printf("FAIL event-trace: event log overflowed\n");
		return 1;
	}
	for (i = 0; i < mmio_event_count; i++) {
		const struct mmio_event *event = &mmio_events[i];
		const struct access_rule *rules;
		size_t count;

		if (event->write) {
			rules = allowed_writes;
			count = sizeof(allowed_writes) / sizeof(allowed_writes[0]);
		} else {
			rules = allowed_reads;
			count = sizeof(allowed_reads) / sizeof(allowed_reads[0]);
		}
		if (!rule_matches(rules, count, event)) {
			printf("FAIL event-trace: forbidden %s space=%d "
			    "offset=%#x width=%u value=%#x\n",
			    event->write ? "write" : "read", event->space,
			    event->offset, event->width, event->value);
			return 1;
		}
	}
	return 0;
}

static int
check_writes(const struct write_expect *expected, size_t count)
{
	size_t index = 0;
	unsigned int i;

	for (i = 0; i < mmio_event_count; i++) {
		const struct mmio_event *event = &mmio_events[i];

		if (!event->write) {
			continue;
		}
		if (index >= count ||
		    event->space != expected[index].space ||
		    event->offset != expected[index].offset ||
		    event->width != expected[index].width ||
		    event->value != expected[index].value) {
			printf("FAIL write-sequence: unexpected write %zu "
			    "space=%d offset=%#x width=%u value=%#x\n",
			    index, event->space, event->offset, event->width,
			    event->value);
			return 1;
		}
		index++;
	}
	if (index != count) {
		printf("FAIL write-sequence: saw %zu writes, expected %zu\n",
		    index, count);
		return 1;
	}
	return 0;
}

static int
write_seen(int space, unsigned int offset)
{
	unsigned int i;

	for (i = 0; i < mmio_event_count; i++) {
		if (mmio_events[i].write && mmio_events[i].space == space &&
		    mmio_events[i].offset == offset) {
			return 1;
		}
	}
	return 0;
}

static int
space_seen(int space)
{
	unsigned int i;

	for (i = 0; i < mmio_event_count; i++) {
		if (mmio_events[i].space == space) {
			return 1;
		}
	}
	return 0;
}

static unsigned int
count_reads(int space, unsigned int offset)
{
	unsigned int i;
	unsigned int count = 0;

	for (i = 0; i < mmio_event_count; i++) {
		if (!mmio_events[i].write && mmio_events[i].space == space &&
		    mmio_events[i].offset == offset) {
			count++;
		}
	}
	return count;
}

static const char *
space_name(int space)
{
	if (space == SPACE_SDHCI) {
		return "sdhci";
	}
	if (space == SPACE_GCC) {
		return "gcc";
	}
	return "other";
}

static void
dump_trace(const char *scenario)
{
	unsigned int i;

	if (!trace_enabled) {
		return;
	}
	printf("TRACE v1 scenario=%s events=%u\n", scenario, mmio_event_count);
	for (i = 0; i < mmio_event_count; i++) {
		const struct mmio_event *event = &mmio_events[i];

		printf("TRACE v1 seq=%llu time=%llu space=%s off=%#x width=%u "
		    "dir=%c value=%#x phase=%s\n",
		    (unsigned long long)event->seq,
		    (unsigned long long)event->time,
		    space_name(event->space), event->offset, event->width,
		    event->write ? 'w' : 'r', event->value, event->phase);
	}
}

static int
expect_panic(const char *name, const char *needle, void (*function)(void))
{
	panic_message[0] = '\0';
	if (setjmp(panic_jmp) == 0) {
		panic_ready = 1;
		function();
		panic_ready = 0;
		printf("FAIL %s: probe did not panic\n", name);
		return 1;
	}
	panic_ready = 0;
	if (strstr(panic_message, needle) == NULL) {
		printf("FAIL %s: panic does not contain '%s': %s\n",
		    name, needle, panic_message);
		return 1;
	}
	printf("PASS %s: %s\n", name, panic_message);
	return 0;
}

static int
test_conflict(void)
{
	int result;

	reset_sim();
	arg_audit = 1;
	result = expect_panic("conflicting-arguments",
	    "conflicting diagnostic arguments",
	    gta4l_sdhci_host_init_preflight);
	if (result != 0) {
		return result;
	}
	if (map_count != 0) {
		printf("FAIL conflicting-arguments: mapped %d windows before refusing\n",
		    map_count);
		return 1;
	}
	if (mmio_event_count != 0) {
		printf("FAIL conflicting-arguments: %u MMIO accesses before "
		    "refusing\n", mmio_event_count);
		return 1;
	}
	return 0;
}

static int
test_bus_power(void)
{
	int result;

	reset_sim();
	wr8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL,
	    GTA4L_SDHCI_HOST_INIT_POWER_BUS_POWER);
	result = expect_panic("unexpected-bus-power",
	    "bus power on before writes", gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (map_count != 2) {
		printf("FAIL unexpected-bus-power: mapped %d windows, expected 2\n",
		    map_count);
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) != 0 ||
	    rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) != 0x0003u) {
		printf("FAIL unexpected-bus-power: refusal wrote reset/clock\n");
		return 1;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_reset_timeout(void)
{
	int result;

	reset_sim();
	result = expect_panic("reset-timeout", "reset timeout",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) !=
	    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL) {
		printf("FAIL reset-timeout: reset did not remain asserted\n");
		return 1;
	}
	if (rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) != 0x0003u) {
		printf("FAIL reset-timeout: clock stage ran after reset failure\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		unsigned int reads = count_reads(SPACE_SDHCI,
		    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET);

		if (reads == 0 ||
		    reads > GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS + 1u) {
			printf("FAIL reset-timeout: reset read count %u is "
			    "outside the poll bound\n", reads);
			return 1;
		}
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_clock_timeout(void)
{
	int result;

	reset_sim();
	device_clock_ready = 0;
	start_device(1);
	result = expect_panic("unstable-clock", "internal clock timeout",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_clock_ready = 1;
	if (result != 0) {
		return result;
	}
	if ((rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL unstable-clock: card clock bit set\n");
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) != 0) {
		printf("FAIL unstable-clock: reset did not self-clear\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		unsigned int reads = count_reads(SPACE_SDHCI,
		    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);

		if (reads == 0 ||
		    reads > GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS + 2u) {
			printf("FAIL unstable-clock: clock read count %u is "
			    "outside the poll bound\n", reads);
			return 1;
		}
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_reset_deadline(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS;
	result = expect_panic("reset-deadline", "reset timeout",
	    gta4l_sdhci_host_init_run);
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET);
	if (reads == 0 || reads > 4) {
		printf("FAIL reset-deadline: deadline did not stop the poll "
		    "(reads=%u)\n", reads);
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_clock_deadline(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS;
	device_clock_ready = 0;
	start_device(1);
	result = expect_panic("clock-deadline", "internal clock timeout",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_clock_ready = 1;
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	if ((rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL clock-deadline: card clock bit set\n");
		return 1;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	if (reads == 0 || reads > 4) {
		printf("FAIL clock-deadline: deadline did not stop the poll "
		    "(reads=%u)\n", reads);
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_stuck_counter(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = 0;
	result = expect_panic("stuck-counter", "reset timeout",
	    gta4l_sdhci_host_init_run);
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET);
	if (reads != GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS) {
		printf("FAIL stuck-counter: iteration cap did not bound the "
		    "poll (reads=%u)\n", reads);
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_gcc_reset_asserted(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_BCR, 1);
	result = expect_panic("gcc-reset-asserted",
	    "SDCC1 block reset asserted", gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_gcc_apps_branch(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CBCR, 0);
	result = expect_panic("gcc-apps-branch", "apps branch not running",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_gcc_ahb_branch(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_AHB_CBCR, 0);
	result = expect_panic("gcc-ahb-branch", "AHB branch not running",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_gcc_ice_branch(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_ICE_CBCR, 0);
	result = expect_panic("gcc-ice-branch", "ICE branch not running",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_rcg_busy(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CMD,
	    GTA4L_SDHCI_HOST_INIT_RCG_CMD_UPDATE);
	result = expect_panic("rcg-busy", "apps RCG busy or off",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_rcg_config(void)
{
	int result;

	reset_sim();
	gcc_wr32(GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CFG, 0);
	result = expect_panic("rcg-config",
	    "unexpected apps RCG configuration", gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_post_reset_version(void)
{
	int result;

	reset_sim();
	device_post_reset_version = 1;
	start_device(1);
	result = expect_panic("post-reset-version",
	    "MCI version changed after reset", gta4l_sdhci_host_init_run);
	stop_device();
	device_post_reset_version = 0;
	if (result != 0) {
		return result;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_post_reset_power(void)
{
	int result;

	reset_sim();
	device_post_reset_power = 1;
	start_device(1);
	result = expect_panic("post-reset-power", "bus power on after reset",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_post_reset_power = 0;
	if (result != 0) {
		return result;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_post_reset_clock(void)
{
	int result;

	reset_sim();
	device_post_reset_clock = 1;
	start_device(1);
	result = expect_panic("post-reset-clock",
	    "card clock on after reset", gta4l_sdhci_host_init_run);
	stop_device();
	device_post_reset_clock = 0;
	if (result != 0) {
		return result;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_post_reset_irq(void)
{
	int result;

	reset_sim();
	device_post_reset_irq = 1;
	start_device(1);
	result = expect_panic("post-reset-irq",
	    "interrupts re-enabled after reset", gta4l_sdhci_host_init_run);
	stop_device();
	device_post_reset_irq = 0;
	if (result != 0) {
		return result;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_reset_delayed_success(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = 1000;
	device_reset_delay = GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS - 5000;
	start_device(1);
	result = expect_panic("reset-delayed-success",
	    "host-local stages complete", gta4l_sdhci_host_init_run);
	stop_device();
	device_reset_delay = 0;
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET);
	if (reads < 2 || reads > 300) {
		printf("FAIL reset-delayed-success: reset poll took %u "
		    "reads\n", reads);
		return 1;
	}
	if ((rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL reset-delayed-success: card clock bit set\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_clock_delayed_success(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = 1000;
	device_clock_delay = GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS - 5000;
	start_device(1);
	result = expect_panic("clock-delayed-success",
	    "host-local stages complete", gta4l_sdhci_host_init_run);
	stop_device();
	device_clock_delay = 0;
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	if (reads < 2 || reads > 300) {
		printf("FAIL clock-delayed-success: clock poll took %u "
		    "reads\n", reads);
		return 1;
	}
	if ((rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL clock-delayed-success: card clock bit set\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_clock_stuck_counter(void)
{
	int result;
	unsigned int reads;

	reset_sim();
	counter_step = 0;
	device_clock_ready = 0;
	start_device(1);
	result = expect_panic("clock-stuck-counter", "internal clock timeout",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_clock_ready = 1;
	counter_step = 1;
	if (result != 0) {
		return result;
	}
	reads = count_reads(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	if (reads != GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS + 2) {
		printf("FAIL clock-stuck-counter: iteration cap did not bound "
		    "the clock poll (reads=%u)\n", reads);
		return 1;
	}
	if ((rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL clock-stuck-counter: card clock bit set\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_vendor_power_request(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS, 0x80000000u);
	start_device(1);
	result = expect_panic("vendor-power-report",
	    "power stage blocked", gta4l_sdhci_host_init_run);
	stop_device();
	if (result != 0) {
		return result;
	}
	if (rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS) != 0x80000000u ||
	    rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK) != 0) {
		printf("FAIL vendor-power-report: vendor power registers "
		    "changed\n");
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL) != 0 ||
	    (rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL vendor-power-report: bus/card power changed\n");
		return 1;
	}
	if (strstr(panic_message, "pst=80000000") == NULL) {
		printf("FAIL vendor-power-report: report lost pst\n");
		return 1;
	}
	if (strstr(panic_message, "vendor state reported, not enforced") ==
	    NULL) {
		printf("FAIL vendor-power-report: report does not state the "
		    "enforcement limit\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_vendor_change_report(void)
{
	int result;

	reset_sim();
	start_device(1);
	device_set_vendor = 1;
	device_vendor_value = 0x00000001u;
	result = expect_panic("vendor-change-report", "power stage blocked",
	    gta4l_sdhci_host_init_run);
	stop_device();
	if (result != 0) {
		return result;
	}
	if (strstr(panic_message, "vs=00000001") == NULL) {
		printf("FAIL vendor-change-report: report did not surface the "
		    "device-side vendor change\n");
		return 1;
	}
	if (write_seen(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC)) {
		printf("FAIL vendor-change-report: probe wrote the vendor "
		    "register\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_vendor_arbitrary_report(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS, 0x5a5a5a5au);
	start_device(1);
	result = expect_panic("vendor-arbitrary-report", "power stage blocked",
	    gta4l_sdhci_host_init_run);
	stop_device();
	if (result != 0) {
		return result;
	}
	if (rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS) != 0x5a5a5a5au ||
	    rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK) != 0) {
		printf("FAIL vendor-arbitrary-report: vendor power registers "
		    "changed\n");
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL) != 0 ||
	    (rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL vendor-arbitrary-report: bus/card power changed\n");
		return 1;
	}
	if (strstr(panic_message, "pst=5a5a5a5a") == NULL) {
		printf("FAIL vendor-arbitrary-report: report lost the arbitrary "
		    "status\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_no_argument(void)
{
	reset_sim();
	arg_host_init = 0;
	panic_ready = 1;
	gta4l_sdhci_host_init_run();
	panic_ready = 0;
	if (map_count != 0) {
		printf("FAIL no-argument: mapped %d windows without the argument\n",
		    map_count);
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) != 0 ||
	    rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) != 0x0003u) {
		printf("FAIL no-argument: probe wrote reset/clock without the "
		    "argument\n");
		return 1;
	}
	if (mmio_event_count != 0) {
		printf("FAIL no-argument: %u MMIO accesses without the argument\n",
		    mmio_event_count);
		return 1;
	}
	printf("PASS no-argument: no mapping and no write\n");
	return 0;
}

static int
test_map_failure(void)
{
	int result;

	reset_sim();
	map_fail = 1;
	result = expect_panic("map-failure", "SDHCI map failed",
	    gta4l_sdhci_host_init_run);
	map_fail = 0;
	if (result != 0) {
		return result;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) != 0 ||
	    rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) != 0x0003u) {
		printf("FAIL map-failure: probe wrote reset/clock after a failed "
		    "map\n");
		return 1;
	}
	if (mmio_event_count != 0) {
		printf("FAIL map-failure: %u MMIO accesses after a failed map\n",
		    mmio_event_count);
		return 1;
	}
	return 0;
}

static int
test_irq_suppress(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0xffffffffu);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 0x00000001u);
	start_device(1);
	result = expect_panic("irq-suppress", "power stage blocked",
	    gta4l_sdhci_host_init_run);
	stop_device();
	if (result != 0) {
		return result;
	}
	if (rd32(GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE) != 0 ||
	    rd32(GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE) != 0) {
		printf("FAIL irq-suppress: interrupts not cleared\n");
		return 1;
	}
	if (rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) != 0x0003u) {
		printf("FAIL irq-suppress: final clock is not %#06x\n", 0x0003);
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 32, 0 },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 32, 0 },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_irq_suppress_failure(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0xffffffffu);
	wr32(GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 0x00000001u);
	device_ignore_irq_clear = 1;
	start_device(1);
	result = expect_panic("irq-suppress-failure",
	    "interrupt suppression did not clear",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_ignore_irq_clear = 0;
	if (result != 0) {
		return result;
	}
	if (write_seen(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET) ||
	    write_seen(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL)) {
		printf("FAIL irq-suppress-failure: reset/clock written after a "
		    "failed suppression\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 32, 0 },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 32, 0 },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

static int
test_gcc_map_failure(void)
{
	int result;

	reset_sim();
	map_fail_gcc = 1;
	result = expect_panic("gcc-map-failure", "GCC map failed",
	    gta4l_sdhci_host_init_run);
	map_fail_gcc = 0;
	if (result != 0) {
		return result;
	}
	if (map_count != 1) {
		printf("FAIL gcc-map-failure: mapped %d windows, expected 1\n",
		    map_count);
		return 1;
	}
	if (check_writes(NULL, 0) != 0) {
		return 1;
	}
	if (space_seen(SPACE_GCC)) {
		printf("FAIL gcc-map-failure: accessed GCC space after a failed "
		    "map\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	return 0;
}

static int
test_bad_version(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION, 0x20000072u);
	result = expect_panic("unexpected-version", "unexpected MCI version",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (map_count != 1) {
		printf("FAIL unexpected-version: mapped %d windows, expected 1\n",
		    map_count);
		return 1;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	if (space_seen(SPACE_GCC)) {
		printf("FAIL unexpected-version: accessed GCC after a version "
		    "refusal\n");
		return 1;
	}
	return 0;
}

static int
test_read_unavailable(void)
{
	int result;

	reset_sim();
	memset(sim + SDHCI_OFF, 0xff, 0x1000);
	result = expect_panic("read-unavailable", "unexpected MCI version",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_card_clock_on(void)
{
	int result;

	reset_sim();
	wr16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 0x0007u);
	result = expect_panic("card-clock-on", "card clock on before writes",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_inhibit(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_PRESENT_STATE, 0x01f800f1u);
	result = expect_panic("inhibit-set", "command/data inhibit set",
	    gta4l_sdhci_host_init_run);
	if (result != 0) {
		return result;
	}
	if (check_events() != 0 || check_writes(NULL, 0) != 0) {
		return 1;
	}
	return 0;
}

static int
test_status_change(void)
{
	int result;

	reset_sim();
	device_change_pwr_status = 1;
	start_device(1);
	result = expect_panic("status-change", "power stage blocked",
	    gta4l_sdhci_host_init_run);
	stop_device();
	device_change_pwr_status = 0;
	if (result != 0) {
		return result;
	}
	if (strstr(panic_message, "pst=80000000") == NULL) {
		printf("FAIL status-change: report did not surface the changed "
		    "power status\n");
		return 1;
	}
	if (write_seen(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS) ||
	    write_seen(SPACE_SDHCI,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK) ||
	    write_seen(SPACE_SDHCI, 0x24cu)) {
		printf("FAIL status-change: probe wrote a power-status "
		    "register\n");
		return 1;
	}
	if (check_events() != 0) {
		return 1;
	}
	{
		static const struct write_expect expected[] = {
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, 8,
			    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL },
			{ SPACE_SDHCI,
			    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 16,
			    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN },
		};

		if (check_writes(expected,
		    sizeof(expected) / sizeof(expected[0])) != 0) {
			return 1;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	int result = 2;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <conflict|no-argument|map-failure|"
		    "gcc-map-failure|bus-power|reset-timeout|clock-timeout|"
		    "reset-deadline|clock-deadline|stuck-counter|"
		    "clock-stuck-counter|reset-delayed-success|"
		    "clock-delayed-success|gcc-reset-asserted|"
		    "gcc-apps-branch|gcc-ahb-branch|gcc-ice-branch|"
		    "rcg-busy|rcg-config|post-reset-version|post-reset-power|"
		    "post-reset-clock|post-reset-irq|"
		    "irq-suppress|irq-suppress-failure|vendor-power|"
		    "vendor-change|vendor-arbitrary|unexpected-version|"
		    "read-unavailable|card-clock|inhibit|status-change>\n",
		    argv[0]);
		return 2;
	}
	trace_enabled = getenv("GTA4L_SDHCI_TRACE") != NULL;
	if (strcmp(argv[1], "conflict") == 0) {
		result = test_conflict();
	} else if (strcmp(argv[1], "no-argument") == 0) {
		result = test_no_argument();
	} else if (strcmp(argv[1], "map-failure") == 0) {
		result = test_map_failure();
	} else if (strcmp(argv[1], "gcc-map-failure") == 0) {
		result = test_gcc_map_failure();
	} else if (strcmp(argv[1], "bus-power") == 0) {
		result = test_bus_power();
	} else if (strcmp(argv[1], "reset-timeout") == 0) {
		result = test_reset_timeout();
	} else if (strcmp(argv[1], "clock-timeout") == 0) {
		result = test_clock_timeout();
	} else if (strcmp(argv[1], "reset-deadline") == 0) {
		result = test_reset_deadline();
	} else if (strcmp(argv[1], "clock-deadline") == 0) {
		result = test_clock_deadline();
	} else if (strcmp(argv[1], "stuck-counter") == 0) {
		result = test_stuck_counter();
	} else if (strcmp(argv[1], "clock-stuck-counter") == 0) {
		result = test_clock_stuck_counter();
	} else if (strcmp(argv[1], "reset-delayed-success") == 0) {
		result = test_reset_delayed_success();
	} else if (strcmp(argv[1], "clock-delayed-success") == 0) {
		result = test_clock_delayed_success();
	} else if (strcmp(argv[1], "gcc-reset-asserted") == 0) {
		result = test_gcc_reset_asserted();
	} else if (strcmp(argv[1], "gcc-apps-branch") == 0) {
		result = test_gcc_apps_branch();
	} else if (strcmp(argv[1], "gcc-ahb-branch") == 0) {
		result = test_gcc_ahb_branch();
	} else if (strcmp(argv[1], "gcc-ice-branch") == 0) {
		result = test_gcc_ice_branch();
	} else if (strcmp(argv[1], "rcg-busy") == 0) {
		result = test_rcg_busy();
	} else if (strcmp(argv[1], "rcg-config") == 0) {
		result = test_rcg_config();
	} else if (strcmp(argv[1], "post-reset-version") == 0) {
		result = test_post_reset_version();
	} else if (strcmp(argv[1], "post-reset-power") == 0) {
		result = test_post_reset_power();
	} else if (strcmp(argv[1], "post-reset-clock") == 0) {
		result = test_post_reset_clock();
	} else if (strcmp(argv[1], "post-reset-irq") == 0) {
		result = test_post_reset_irq();
	} else if (strcmp(argv[1], "irq-suppress") == 0) {
		result = test_irq_suppress();
	} else if (strcmp(argv[1], "irq-suppress-failure") == 0) {
		result = test_irq_suppress_failure();
	} else if (strcmp(argv[1], "vendor-power") == 0) {
		result = test_vendor_power_request();
	} else if (strcmp(argv[1], "vendor-change") == 0) {
		result = test_vendor_change_report();
	} else if (strcmp(argv[1], "vendor-arbitrary") == 0) {
		result = test_vendor_arbitrary_report();
	} else if (strcmp(argv[1], "unexpected-version") == 0) {
		result = test_bad_version();
	} else if (strcmp(argv[1], "read-unavailable") == 0) {
		result = test_read_unavailable();
	} else if (strcmp(argv[1], "card-clock") == 0) {
		result = test_card_clock_on();
	} else if (strcmp(argv[1], "inhibit") == 0) {
		result = test_inhibit();
	} else if (strcmp(argv[1], "status-change") == 0) {
		result = test_status_change();
	} else {
		fprintf(stderr, "unknown test: %s\n", argv[1]);
	}
	dump_trace(argv[1]);
	return result;
}
