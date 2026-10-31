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
 * The controller/host path and a minimal card-command engine are modelled: the
 * GCC block reset, the branch enable/CLK_OFF handshake, a configurable MCI
 * version, MMIO-map failure, and CMD0/CMD1/CMD2 with a CMD2 CRC error. No data
 * transfer is issued by any scenario.
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

/* Independent expectations for the offsets the failure report samples. The
 * production constants are also checked here so a wrong constant cannot make
 * a read-count assertion agree with itself. */
_Static_assert(GTA4L_EMMC_REG_ARGUMENT == 0x08u, "ARGUMENT offset");
_Static_assert(GTA4L_EMMC_REG_COMMAND == 0x0Eu, "COMMAND offset");
_Static_assert(GTA4L_EMMC_REG_RESPONSE == 0x10u, "RESPONSE offset");
_Static_assert(GTA4L_EMMC_REG_PRESENT_STATE == 0x24u, "PRESENT_STATE offset");
_Static_assert(GTA4L_EMMC_REG_HOST_CONTROL == 0x28u, "HOST_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_POWER_CONTROL == 0x29u, "POWER_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_CLOCK_CONTROL == 0x2Cu, "CLOCK_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_SOFTWARE_RESET == 0x2Fu,
    "SOFTWARE_RESET offset");
_Static_assert(GTA4L_EMMC_REG_NORMAL_INT_STATUS == 0x30u,
    "NORMAL_INT_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_ERROR_INT_STATUS == 0x32u,
    "ERROR_INT_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_HOST_CONTROL2 == 0x3Eu, "HOST_CONTROL2 offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DLL_CONFIG == 0x200u,
    "CORE_DLL_CONFIG offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DLL_STATUS == 0x208u,
    "CORE_DLL_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_CORE_VENDOR_SPEC == 0x20Cu,
    "CORE_VENDOR_SPEC offset");
_Static_assert(GTA4L_EMMC_REG_CORE_VENDOR_SPEC3 == 0x250u,
    "CORE_VENDOR_SPEC3 offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DDR_CONFIG == 0x25Cu,
    "CORE_DDR_CONFIG offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_STATUS == 0x240u,
    "CORE_PWRCTL_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR == 0x248u,
    "CORE_PWRCTL_CLEAR offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_CTL == 0x24Cu,
    "CORE_PWRCTL_CTL offset");
_Static_assert(GTA4L_EMMC_REG_MCI_VERSION == 0x318u, "MCI_VERSION offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_BCR == 0x00u, "SDCC1 BCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CBCR == 0x04u,
    "SDCC1 APPS CBCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_AHB_CBCR == 0x08u,
    "SDCC1 AHB CBCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CMD == 0x28u,
    "SDCC1 APPS CMD offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CFG == 0x2Cu,
    "SDCC1 APPS CFG offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_M == 0x30u, "SDCC1 APPS M offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_N == 0x34u, "SDCC1 APPS N offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_D == 0x38u, "SDCC1 APPS D offset");

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
static int rcg_stuck;
static int clock_timeout;
static int bad_access;
static unsigned long sdhci_accesses;
static unsigned long sdhci_writes;
static unsigned long gcc_accesses;
static unsigned int sdhci_read_count[SIM_WINDOW];
static unsigned int gcc_read_count[SIM_WINDOW];
static uint64_t counter_value;
static uint16_t normal_int;
static uint16_t error_int;
static uint32_t pwr_status;
static uint16_t last_command;
static uint32_t last_argument;
static unsigned int cmd_count;
static int bad_command_format;
static int bad_command_order;

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
	if ((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CMD) && (rcg_stuck == 0)) {
		/* The RCG update bit is self-clearing. */
		value &= ~GTA4L_EMMC_RCG_CMD_UPDATE;
	}
	sim_store(gcc, offset, 4u, value);
	if (offset == GTA4L_EMMC_GCC_SDCC1_BCR) {
		if ((value & SIM_BCR_RESET) == 0) {
			return;
		}
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_BCR, 4u, 0);
		/* Reset asserts CLK_OFF but preserves the other CBCR fields,
		 * so a seeded handoff value survives the reset. */
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR, 4u,
		    sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR, 4u) |
		    SIM_CBCR_CLK_OFF);
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR, 4u,
		    sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR, 4u) |
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

static void
sim_clear_response(void)
{
	uint32_t index;

	for (index = 0; index < 4u; index++) {
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE + index * 4u, 4u, 0);
	}
}

/* Minimal command engine for the stage-3 CMD2 CRC scenario. CMD0 and CMD1
 * complete normally; CMD2 raises CMD_CRC and loads synthetic R2 words so the
 * production failure report has something to format. */
static void
sim_command(uint16_t command)
{
	uint32_t index = ((uint32_t)command >> 8) & 0x3Fu;
	uint32_t flags = (uint32_t)command & 0xFFu;
	uint32_t words[4];
	uint32_t word;

	last_command = command;
	cmd_count++;
	if (cmd_count == 1u) {
		if ((index != 0u) || (last_argument != 0u)) {
			bad_command_order = 1;
		}
	} else if (cmd_count == 2u) {
		if ((index != 1u) ||
		    (last_argument != GTA4L_EMMC_OCR_VALUE)) {
			bad_command_order = 1;
		}
	} else if (cmd_count == 3u) {
		if ((index != 2u) || (last_argument != 0u)) {
			bad_command_order = 1;
		}
	} else {
		bad_command_order = 1;
	}
	if (index == 0u) {
		if (flags != GTA4L_EMMC_CMD_RESP_NONE) {
			bad_command_format = 1;
		}
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == 1u) {
		if (flags != GTA4L_EMMC_CMD_RESP_SHORT) {
			bad_command_format = 1;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
		    GTA4L_EMMC_OCR_VALUE | 0x80000000u);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == 2u) {
		if (flags != (GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC)) {
			bad_command_format = 1;
		}
		/* Synthetic R2 words: this is a formatting/capture test, not a
		 * real CID. */
		words[0] = 0x11111111u;
		words[1] = 0x22222222u;
		words[2] = 0x33333333u;
		words[3] = 0x44444444u;
		for (word = 0; word < 4u; word++) {
			sim_store(sdhci,
			    GTA4L_EMMC_REG_RESPONSE + word * 4u, 4u,
			    words[word]);
		}
		error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
		normal_int |= 0x8000u;
		return;
	}
	/* No later command is expected. Report a CRC error so an unexpected
	 * command fails the scenario instead of hanging. */
	error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
	normal_int |= 0x8000u;
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
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			return normal_int;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			return error_int;
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
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) {
			return pwr_status;
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
		if ((offset == GTA4L_EMMC_REG_POWER_CONTROL) &&
		    ((value & GTA4L_EMMC_POWER_BUS_POWER) != 0)) {
			pwr_status |= GTA4L_EMMC_PWRCTL_BUS_ON;
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
		if (offset == GTA4L_EMMC_REG_COMMAND) {
			sim_store(sdhci, offset, 2u, value);
			sim_command(value);
			return;
		}
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			normal_int &= (uint16_t)~value;
			sim_store(sdhci, offset, 2u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			error_int &= (uint16_t)~value;
			if ((value & GTA4L_EMMC_INT_ERR_CMD_CRC) != 0) {
				sim_clear_response();
			}
			sim_store(sdhci, offset, 2u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_CLOCK_CONTROL) {
			sim_store(sdhci, offset, 2u, value);
			if (((value & GTA4L_EMMC_CLOCK_INT_EN) != 0) &&
			    (clock_timeout == 0)) {
				value |= GTA4L_EMMC_CLOCK_INT_STABLE;
				sim_store(sdhci, offset, 2u, value);
			}
			return;
		}
		if (offset == GTA4L_EMMC_REG_HOST_CONTROL2) {
			uint16_t previous = (uint16_t)sim_load(sdhci, offset,
			    2u);

			sim_store(sdhci, offset, 2u, value);
			if (((value & GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) !=
			    ((previous &
			    GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0)) {
				if ((value &
				    GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) {
					pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_LOW;
				} else {
					pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_HIGH;
				}
			}
			return;
		}
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
		if (offset == GTA4L_EMMC_REG_ARGUMENT) {
			last_argument = value;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR) {
			pwr_status &= ~value;
			sim_store(sdhci, offset, 4u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CTL) {
			sim_store(sdhci, offset, 4u, value);
			return;
		}
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
		/* Keep the RCG update bit set so configure_clock() fails after
		 * programming the 400 kHz RCG. */
		stage = 2;
		rcg_stuck = 1;
	} else if (strcmp(scenario, "clock-stable-timeout") == 0) {
		/* Leave CLOCK_CONTROL without INT_STABLE so configure_clock()
		 * fails after the RCG update completed. */
		stage = 2;
		clock_timeout = 1;
	} else if (strcmp(scenario, "cmd2-crc") == 0) {
		/* Run the full identification sequence and fail the first
		 * CRC-checked command with a synthetic R2 response. */
		stage = 3;
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
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 1u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 1u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 1u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 1u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 1u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
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
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 1u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
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
	} else if (strcmp(scenario, "clock-stable-timeout") == 0) {
		check(panicked == 1, "a clock-stability timeout did not panic");
		check(strstr(panic_message, "stage=2") != NULL,
		    "the clock-stability report does not name the stage");
		check(strstr(panic_message, "rc=-5") != NULL,
		    "the clock-stability report does not name the clock error");
		check(strstr(panic_message, "cfg=00002017") != NULL,
		    "the report does not show the 400 kHz RCG config");
		check(strstr(panic_message,
		    "mnd=00000001/000000fc/000000fb") != NULL,
		    "the report does not show the 400 kHz RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000000") != NULL,
		    "the report does not show the released RCG update bit");
		check(strstr(panic_message, "clk=0001") != NULL,
		    "the report does not show the missing clock stability");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 3u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
	} else if (strcmp(scenario, "cmd2-crc") == 0) {
		check(panicked == 1, "a CMD2 CRC error did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the CMD2 CRC report does not name the stage");
		check(strstr(panic_message, "rc=-8") != NULL,
		    "the CMD2 CRC report does not name the CRC error");
		check(strstr(panic_message,
		    "fail=1/2/0/00000209/0002/8000/00000000/8") != NULL,
		    "the report does not name the failed CMD2");
		check(strstr(panic_message,
		    "rsp=11111111,22222222,33333333,44444444") != NULL,
		    "the report did not capture the raw R2 response");
		check(strstr(panic_message, "cmd1=1/0/c0ff8080") != NULL,
		    "the report does not show the accepted CMD1 OCR");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order or with the "
		    "wrong argument");
		check(cmd_count == 3u,
		    "the init did not send exactly CMD0/CMD1/CMD2");
		check(last_command == (uint16_t)((GTA4L_EMMC_CMD2 << 8) |
		    GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC),
		    "the failing command was not CMD2 with CRC checking");
		check(strstr(panic_message, "pwr=0f") != NULL,
		    "the report does not show the powered bus");
		check(strstr(panic_message, "clk=0007") != NULL,
		    "the report does not show the enabled card clock");
		check(strstr(panic_message, "hc2=0008") != NULL,
		    "the report does not show the 1.8 V signalling bit");
		/* MCI_VERSION: two core reads plus the report; POWER_CONTROL:
		 * the completed-init log plus the report; HOST_CONTROL: report
		 * only; CLOCK_CONTROL: two configure-clock reads, the card-clock
		 * enable read and the report; HOST_CONTROL2 and VENDOR_SPEC3:
		 * one core read plus the report; VENDOR_SPEC and DLL_CONFIG:
		 * two core reads plus the report; DLL_STATUS and DDR_CONFIG:
		 * report only; PWRCTL_STATUS: the status read, the clear
		 * verify, the acknowledge verify, the settling read and the
		 * report. */
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 2u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 4u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 5u,
		    "the report did not sample the power status");
		/* The RCG config is read once by configure_clock(); M/N/D are
		 * report-only; APPS_CMD is read twice by the update handshake
		 * and once by the report; each branch CBCR is read by the
		 * enable handshake and once by the report. */
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 3u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CBCR] == 3u,
		    "the report did not sample the APPS branch");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_AHB_CBCR] == 3u,
		    "the report did not sample the AHB branch");
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
