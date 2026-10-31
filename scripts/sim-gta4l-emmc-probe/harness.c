/*
 * Host-side simulated-MMIO harness for the gta4l eMMC probe failure report.
 *
 * The test runner copies Gta4lEmmcProbe.c, replaces the generic-counter and
 * barrier instructions and the six MMIO helpers with the hooks below, and
 * links it against this harness and minimal kernel-header stubs. The probe's
 * stage selection, conflict check, failure-report guard, register offsets and
 * panic strings remain the production code. Every hooked access is counted so
 * the tests can reject an SDHCI read while the branch clocks are off.
 *
 * Only the failure-report path is modelled: the GCC block reset, the branch
 * enable/CLK_OFF handshake, a configurable MCI version and MMIO-map failure.
 * No card command or data transfer is issued by any scenario.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "Gta4lEmmc.h"

void gta4l_emmc_probe_run(void);
uint64_t gta4l_emmc_probe_test_counter(void);
uint64_t gta4l_emmc_probe_test_counter_hz(void);
uint8_t gta4l_emmc_probe_test_read8(uint32_t space, uint32_t offset);
uint16_t gta4l_emmc_probe_test_read16(uint32_t space, uint32_t offset);
uint32_t gta4l_emmc_probe_test_read32(uint32_t space, uint32_t offset);
void gta4l_emmc_probe_test_write8(uint32_t space, uint32_t offset,
    uint8_t value);
void gta4l_emmc_probe_test_write16(uint32_t space, uint32_t offset,
    uint16_t value);
void gta4l_emmc_probe_test_write32(uint32_t space, uint32_t offset,
    uint32_t value);

#define SIM_WINDOW        0x1000u
#define SIM_GCC_WORDS     (SIM_WINDOW / 4u)
#define SIM_SDHCI_BASE    0x04744000u
#define SIM_GCC_BASE      0x01438000u
#define SIM_TLMM_BASE     0x00575000u
#define SIM_COUNTER_HZ    1000000u
#define SIM_CBCR_ENABLE   0x00000001u
#define SIM_CBCR_CLK_OFF  0x80000000u
#define SIM_BCR_RESET     0x00000001u
#define SIM_VERSION_GOOD  0x10000072u
#define SIM_VERSION_BAD   0x20000000u
#define SIM_BRANCH_APPS   1
#define SIM_BRANCH_AHB    2

static unsigned char sdhci[SIM_WINDOW];
static unsigned char gcc[SIM_WINDOW];
static unsigned char tlmm[SIM_WINDOW];
static jmp_buf panic_jmp;
static char panic_message[2048];
static int panic_ready;
static int stage;
static int conflict;
static int map_fail;
static int branch_stuck;
static int bad_access;
static unsigned long sdhci_accesses;
static unsigned long sdhci_writes;
static unsigned long gcc_accesses;
static unsigned int sdhci_read_count[SIM_WINDOW];
static unsigned int gcc_read_count[SIM_WINDOW];
static uint64_t counter_value;

static uint32_t
sim_load(const unsigned char *window, uint32_t offset, uint32_t width)
{
	uint32_t value = 0;
	uint32_t index;

	if ((offset > SIM_WINDOW) || (width > SIM_WINDOW) ||
	    ((offset + width) > SIM_WINDOW)) {
		bad_access = 1;
		return 0;
	}
	for (index = 0; index < width; index++) {
		value |= (uint32_t)window[offset + index] << (8u * index);
	}
	return value;
}

static void
sim_store(unsigned char *window, uint32_t offset, uint32_t width,
    uint32_t value)
{
	uint32_t index;

	if ((offset > SIM_WINDOW) || (width > SIM_WINDOW) ||
	    ((offset + width) > SIM_WINDOW)) {
		bad_access = 1;
		return;
	}
	for (index = 0; index < width; index++) {
		window[offset + index] =
		    (unsigned char)((value >> (8u * index)) & 0xFFu);
	}
}

/* Model the GCC SDCC1 block reset and branch-clock handshake. */
static void
sim_gcc_write32(uint32_t offset, uint32_t value)
{
	sim_store(gcc, offset, 4u, value);
	if (offset == GTA4L_EMMC_GCC_SDCC1_BCR) {
		if ((value & SIM_BCR_RESET) == 0) {
			return;
		}
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_BCR, 4u, 0);
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR, 4u,
		    SIM_CBCR_CLK_OFF);
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR, 4u,
		    SIM_CBCR_CLK_OFF);
		return;
	}
	if (((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CBCR) &&
	    (branch_stuck == SIM_BRANCH_APPS)) ||
	    ((offset == GTA4L_EMMC_GCC_SDCC1_AHB_CBCR) &&
	    (branch_stuck == SIM_BRANCH_AHB))) {
		if ((value & SIM_CBCR_ENABLE) != 0) {
			sim_store(gcc, offset, 4u, value | SIM_CBCR_CLK_OFF);
		}
		return;
	}
	if ((value & SIM_CBCR_ENABLE) != 0) {
		sim_store(gcc, offset, 4u, value & ~SIM_CBCR_CLK_OFF);
	}
}

static unsigned char *
sim_window(uint32_t space)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return sdhci;
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return gcc;
	}
	return tlmm;
}

uint8_t
gta4l_emmc_probe_test_read8(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return (uint8_t)sim_load(sim_window(space), offset, 1u);
}

uint16_t
gta4l_emmc_probe_test_read16(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return (uint16_t)sim_load(sim_window(space), offset, 2u);
}

uint32_t
gta4l_emmc_probe_test_read32(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return sim_load(sim_window(space), offset, 4u);
}

void
gta4l_emmc_probe_test_write8(uint32_t space, uint32_t offset, uint8_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
		/* The software-reset bits are self-clearing. Model the
		 * completion so later stages can be exercised. */
		if (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) {
			value = 0;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
	}
	sim_store(sim_window(space), offset, 1u, value);
}

void
gta4l_emmc_probe_test_write16(uint32_t space, uint32_t offset, uint16_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
	}
	sim_store(sim_window(space), offset, 2u, value);
}

void
gta4l_emmc_probe_test_write32(uint32_t space, uint32_t offset, uint32_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		sim_gcc_write32(offset, value);
		return;
	}
	sim_store(sim_window(space), offset, 4u, value);
}

uint64_t
gta4l_emmc_probe_test_counter(void)
{
	return ++counter_value;
}

uint64_t
gta4l_emmc_probe_test_counter_hz(void)
{
	return SIM_COUNTER_HZ;
}

int
PE_parse_boot_argn(const char *arg, void *value, int size)
{
	if (size < (int)sizeof(int)) {
		return 0;
	}
	if (strcmp(arg, "gta4l-emmc-probe") == 0) {
		*(int *)value = stage;
		return 1;
	}
	if (conflict != 0) {
		if ((strcmp(arg, "gta4l-sdhci-audit") == 0) ||
		    (strcmp(arg, "gta4l-sdhci-snapshot") == 0) ||
		    (strcmp(arg, "gta4l-sdhci-host-init") == 0)) {
			*(int *)value = 1;
			return 1;
		}
	}
	return 0;
}

vm_offset_t
ml_io_map(vm_offset_t phys, vm_size_t size)
{
	(void)size;
	if (map_fail != 0) {
		return 0;
	}
	switch (phys) {
	case SIM_SDHCI_BASE:
		return (vm_offset_t)(uintptr_t)sdhci;
	case SIM_GCC_BASE:
		return (vm_offset_t)(uintptr_t)gcc;
	case SIM_TLMM_BASE:
		return (vm_offset_t)(uintptr_t)tlmm;
	default:
		return 0;
	}
}

void
panic(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(panic_message, sizeof(panic_message), format, arguments);
	va_end(arguments);
	if (panic_ready == 0) {
		fprintf(stderr, "unexpected panic: %s\n", panic_message);
		exit(2);
	}
	longjmp(panic_jmp, 1);
}

static int failures;
static int panicked;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void
sim_init(void)
{
	sim_store(sdhci, GTA4L_EMMC_REG_MCI_VERSION, 4u, SIM_VERSION_GOOD);
}

int
main(int argc, char **argv)
{
	const char *scenario = (argc > 1) ? argv[1] : "branch-apps";

	sim_init();
	if (strcmp(scenario, "stage-off") == 0) {
		stage = 0;
	} else if (strcmp(scenario, "map-fail") == 0) {
		stage = 2;
		map_fail = 1;
	} else if (strcmp(scenario, "conflict") == 0) {
		stage = 1;
		conflict = 1;
	} else if (strcmp(scenario, "branch-apps") == 0) {
		stage = 2;
		branch_stuck = SIM_BRANCH_APPS;
	} else if (strcmp(scenario, "branch-ahb") == 0) {
		stage = 2;
		branch_stuck = SIM_BRANCH_AHB;
	} else if (strcmp(scenario, "version-fail") == 0) {
		stage = 2;
		sim_store(sdhci, GTA4L_EMMC_REG_MCI_VERSION, 4u,
		    SIM_VERSION_BAD);
	} else if (strcmp(scenario, "clock-timeout") == 0) {
		/* Leave CLOCK_CONTROL without INT_STABLE so configure_clock()
		 * fails after programming the 400 kHz RCG. */
		stage = 2;
	} else {
		fprintf(stderr, "unknown scenario %s\n", scenario);
		return 2;
	}

	if (setjmp(panic_jmp) == 0) {
		panic_ready = 1;
		gta4l_emmc_probe_run();
	} else {
		panicked = 1;
	}
	panic_ready = 0;

	check(bad_access == 0, "a simulated register access left its window");

	if (strcmp(scenario, "stage-off") == 0) {
		check(panicked == 0, "a disabled probe still panicked");
		check(sdhci_accesses == 0ul,
		    "a disabled probe touched SDHCI");
		check(gcc_accesses == 0ul, "a disabled probe touched GCC");
	} else if (strcmp(scenario, "map-fail") == 0) {
		check(panicked == 1, "an MMIO map failure did not panic");
		check(strstr(panic_message, "MMIO map failed") != NULL,
		    "panic does not name the MMIO map failure");
		check(sdhci_accesses == 0ul,
		    "an MMIO map failure touched SDHCI");
		check(gcc_accesses == 0ul, "an MMIO map failure touched GCC");
	} else if (strcmp(scenario, "conflict") == 0) {
		check(panicked == 1, "a conflicting argument was not refused");
		check(strstr(panic_message, "REFUSED") != NULL,
		    "panic does not name the argument conflict");
		check(sdhci_accesses == 0ul,
		    "a refused probe touched SDHCI");
		check(gcc_accesses == 0ul, "a refused probe touched GCC");
	} else if (strcmp(scenario, "version-fail") == 0) {
		check(panicked == 1, "a bad MCI version did not panic");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		/* The core reads the version once before failing, so the
		 * report's own sample makes this count two; every other
		 * diagnostic register is read exactly once by the report. */
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 2u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 1u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 1u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 1u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 1u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
	} else if (strcmp(scenario, "clock-timeout") == 0) {
		check(panicked == 1, "a clock timeout did not panic");
		check(strstr(panic_message, "stage=2") != NULL,
		    "the clock-timeout report does not name the stage");
		check(strstr(panic_message, "rc=-5") != NULL,
		    "the clock-timeout report does not name the clock error");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		check(strstr(panic_message, "cfg=00002017") != NULL,
		    "the report does not show the 400 kHz RCG config");
		check(strstr(panic_message,
		    "mnd=00000001/000000fc/000000fb") != NULL,
		    "the report does not show the 400 kHz RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000001") != NULL,
		    "the report does not show the stuck RCG update bit");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		check(strstr(panic_message, "apps=00000001") != NULL,
		    "the report does not show the running APPS branch");
		check(strstr(panic_message, "ahb=00000001") != NULL,
		    "the report does not show the running AHB branch");
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
	} else {
		check(panicked == 1, "a branch-clock failure did not panic");
		check(strstr(panic_message, "GTA4L-EMMC PROBE FAILED") != NULL,
		    "panic is not the probe failure report");
		check(strstr(panic_message, "stage=2") != NULL,
		    "failure report does not name the stage");
		check(strstr(panic_message, "clocks=0") != NULL,
		    "failure report claims the branch clocks were enabled");
		check(sdhci_accesses == 0ul,
		    "SDHCI was read while the branch clocks were off");
		check(sdhci_writes == 0ul,
		    "SDHCI was written while the branch clocks were off");
		check(gcc_accesses > 0ul,
		    "the GCC failure state was not reported");
		/* The report's GCC reads are the only reads of these offsets
		 * in the branch-failure path (reset writes BCR; enable_branch
		 * reads and polls CBCR). */
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 1u,
		    "the report did not sample the GCC apps config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 1u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
		check(strstr(panic_message, "cfg=00000000") != NULL,
		    "the report does not show the untouched RCG config");
		check(strstr(panic_message,
		    "mnd=00000000/00000000/00000000") != NULL,
		    "the report does not show the untouched RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000000") != NULL,
		    "the report does not show the untouched RCG command");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		if (strcmp(scenario, "branch-apps") == 0) {
			check(strstr(panic_message, "apps=80000001") != NULL,
			    "the report does not show the stuck APPS branch");
			check(strstr(panic_message, "ahb=00000001") != NULL,
			    "the report does not show the running AHB branch");
		} else {
			check(strstr(panic_message, "apps=80000000") != NULL,
			    "the report does not show the idle APPS branch");
			check(strstr(panic_message, "ahb=80000001") != NULL,
			    "the report does not show the stuck AHB branch");
		}
	}

	if (failures != 0) {
		return 1;
	}
	printf("PASS %s\n", scenario);
	return 0;
}
