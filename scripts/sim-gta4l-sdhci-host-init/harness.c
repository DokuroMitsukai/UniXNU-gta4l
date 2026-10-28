/*
 * Host-side simulated-register harness for the gta4l bounded SDHCI init probe.
 *
 * The test runner copies Gta4lSdhciHostInit.c, replaces only the generic
 * counter and barrier instructions with the two test hooks below, and links it
 * against this harness and minimal kernel-header stubs. The probe's stage
 * order, preconditions, writes and panic paths are the production code.
 */
#include <pthread.h>
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

#define SDHCI_OFF 0x0000u
#define GCC_OFF   0x2000u
#define SIM_SIZE  0x4000u

static unsigned char sim[SIM_SIZE] __attribute__((aligned(4096)));
static int map_count;
static int arg_host_init = 1;
static int arg_audit;
static int arg_snapshot;
static jmp_buf panic_jmp;
static int panic_ready;
static char panic_message[2048];
static volatile int device_run;
static int device_clear_reset;
static int device_started;
static pthread_t device_thread_id;
static uint64_t counter_value;

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

static void
reset_sim(void)
{
	memset(sim, 0, sizeof(sim));
	map_count = 0;
	counter_value = 0;
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
	if (size > 0x1000u) {
		return 0;
	}
	if (phys == GTA4L_SDHCI_HOST_INIT_SDHCI_BASE) {
		map_count++;
		return (vm_offset_t)(uintptr_t)(sim + SDHCI_OFF);
	}
	if (phys == GTA4L_SDHCI_HOST_INIT_GCC_BASE) {
		map_count++;
		return (vm_offset_t)(uintptr_t)(sim + GCC_OFF);
	}
	return 0;
}

uint64_t
gta4l_sdhci_host_init_test_counter(void)
{
	counter_value++;
	if ((counter_value & 0x3ffu) == 0) {
		sched_yield();
	}
	return counter_value;
}

void
gta4l_sdhci_host_init_test_barrier(void)
{
}

static void *
device_thread(void *unused)
{
	(void)unused;
	while (device_run) {
		if (device_clear_reset &&
		    sim[SDHCI_OFF + GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET] ==
		    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL) {
			sim[SDHCI_OFF +
			    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET] = 0;
		}
		sched_yield();
	}
	return NULL;
}

static void
start_device(int clear_reset)
{
	device_clear_reset = clear_reset;
	device_run = 1;
	if (pthread_create(&device_thread_id, NULL, device_thread, NULL) == 0) {
		device_started = 1;
	} else {
		device_run = 0;
	}
}

static void
stop_device(void)
{
	if (device_started) {
		device_run = 0;
		pthread_join(device_thread_id, NULL);
		device_started = 0;
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
	return 0;
}

static int
test_clock_timeout(void)
{
	int result;

	reset_sim();
	wr16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, 0x0000u);
	start_device(1);
	result = expect_panic("unstable-clock", "internal clock timeout",
	    gta4l_sdhci_host_init_run);
	stop_device();
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
	return 0;
}

static int
test_vendor_power_request(void)
{
	int result;

	reset_sim();
	wr32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS, 0x80000000u);
	start_device(1);
	result = expect_panic("unexpected-vendor-power-request",
	    "power stage blocked", gta4l_sdhci_host_init_run);
	stop_device();
	if (result != 0) {
		return result;
	}
	if (rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS) != 0x80000000u ||
	    rd32(GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK) != 0) {
		printf("FAIL unexpected-vendor-power-request: vendor power "
		    "registers changed\n");
		return 1;
	}
	if (rd8(GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL) != 0 ||
	    (rd16(GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL) &
	    GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) != 0) {
		printf("FAIL unexpected-vendor-power-request: bus/card power "
		    "changed\n");
		return 1;
	}
	if (strstr(panic_message, "pst=80000000") == NULL) {
		printf("FAIL unexpected-vendor-power-request: report lost pst\n");
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <conflict|bus-power|reset-timeout|"
		    "clock-timeout|vendor-power>\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "conflict") == 0) {
		return test_conflict();
	}
	if (strcmp(argv[1], "bus-power") == 0) {
		return test_bus_power();
	}
	if (strcmp(argv[1], "reset-timeout") == 0) {
		return test_reset_timeout();
	}
	if (strcmp(argv[1], "clock-timeout") == 0) {
		return test_clock_timeout();
	}
	if (strcmp(argv[1], "vendor-power") == 0) {
		return test_vendor_power_request();
	}
	fprintf(stderr, "unknown test: %s\n", argv[1]);
	return 2;
}
