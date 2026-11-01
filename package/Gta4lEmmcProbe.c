// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l eMMC diagnostic probe.
//
// The probe is opt-in with the boot argument "gta4l-emmc-probe=1". It maps the
// SDHCI, GCC SDCC1 and SDC1 TLMM windows, runs the portable core, reads the
// protective MBR and primary GPT header, and reports the result. It does not
// publish a block device, mount a filesystem, write the card or touch the PMIC.
//
// The probe is a bring-up diagnostic: it panics with the report so the serial
// log captures the last completed stage. The working RAM-root path is unchanged.

#include <sys/types.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lEmmc.h"

void gta4l_emmc_probe_preflight(void);
void gta4l_emmc_probe_run(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

#define GTA4L_EMMC_PROBE_SDHCI_BASE 0x04744000u
#define GTA4L_EMMC_PROBE_GCC_BASE   0x01438000u
#define GTA4L_EMMC_PROBE_TLMM_BASE  0x00575000u
#define GTA4L_EMMC_PROBE_WINDOW     0x00001000u

struct gta4l_emmc_probe {
	uintptr_t sdhci;
	uintptr_t gcc;
	uintptr_t tlmm;
	uint64_t counter_hz;
	uint32_t log_count;
	uint32_t last_event;
	uint32_t last_a;
	uint32_t last_b;
};

/* Sampled SDHCI and GCC register values passed to the report formatters. The
 * report functions are separate from the sampling code so the host test can
 * format synthetic field-wise bounds through the same production format
 * strings and argument lists. */
struct gta4l_emmc_probe_regs {
	uint32_t version;
	uint8_t power;
	uint8_t host_control;
	uint16_t clock;
	uint16_t host_control2;
	uint16_t host_version;
	uint32_t vendor_spec;
	uint32_t vendor_spec3;
	uint32_t dll_config;
	uint32_t dll_status;
	uint32_t ddr_config;
	uint32_t pwr_status;
	uint32_t cfg;
	uint32_t gcc_m;
	uint32_t gcc_n;
	uint32_t gcc_d;
	uint32_t cmd;
	uint32_t bcr;
	uint32_t apps_cbcr;
	uint32_t ahb_cbcr;
};

/* CMD7 R1b capture block shared by the failure and success reports. The field
 * order is part of GTA4L_EMMC_CAPTURE_FMT. Every value is an observed sample,
 * not proof of continuous bus visibility. c7p: pre-command state and raw
 * window. c7q: poll OR mask, first/last normal, iterations and ticks. c7c:
 * first observed CC/TC. c7t: terminal state and raw window. c7x: bounded trace
 * (iteration:normal:error) and overflow. */
#define GTA4L_EMMC_PROBE_CMD7_FMT \
	"\nc7p=%u/%04x/%04x/%08x/%04x/%04x/%08x,%08x,%08x,%08x" \
	"\nc7q=%u/%04x/%04x/%04x/%04x/%u/%08x%08x" \
	" c7c=%u/%u/%08x%08x/%u/%u/%08x%08x" \
	"\nc7t=%u/%04x/%04x/%08x/%08x/%08x/%08x,%08x,%08x,%08x" \
	"\nc7x=%u/%u/%u:%04x:%04x,%u:%04x:%04x,%u:%04x:%04x," \
	"%u:%04x:%04x"

#define GTA4L_EMMC_PROBE_CMD7_ARGS(host) \
	(unsigned int)(host).cmd7_pre_capture_valid, \
	(unsigned int)(host).cmd7_pre_normal, \
	(unsigned int)(host).cmd7_pre_error, \
	(host).cmd7_pre_present, \
	(unsigned int)(host).cmd7_pre_normal_enable, \
	(unsigned int)(host).cmd7_pre_error_enable, \
	(host).cmd7_pre_response[0], (host).cmd7_pre_response[1], \
	(host).cmd7_pre_response[2], (host).cmd7_pre_response[3], \
	(unsigned int)(host).cmd7_poll_capture_valid, \
	(unsigned int)(host).cmd7_poll_or_normal, \
	(unsigned int)(host).cmd7_poll_or_error, \
	(unsigned int)(host).cmd7_poll_first_normal, \
	(unsigned int)(host).cmd7_poll_last_normal, \
	(host).cmd7_poll_iterations, \
	(unsigned int)((host).cmd7_poll_ticks >> 32), \
	(unsigned int)(host).cmd7_poll_ticks, \
	(unsigned int)(host).cmd7_cc_valid, (host).cmd7_cc_iteration, \
	(unsigned int)((host).cmd7_cc_ticks >> 32), \
	(unsigned int)(host).cmd7_cc_ticks, \
	(unsigned int)(host).cmd7_tc_valid, (host).cmd7_tc_iteration, \
	(unsigned int)((host).cmd7_tc_ticks >> 32), \
	(unsigned int)(host).cmd7_tc_ticks, \
	(unsigned int)(host).cmd7_term_capture_valid, \
	(unsigned int)(host).cmd7_term_normal, \
	(unsigned int)(host).cmd7_term_error, \
	(host).cmd7_term_present, \
	(unsigned int)(host).cmd7_term_command, \
	(host).cmd7_term_argument, \
	(host).cmd7_term_response[0], (host).cmd7_term_response[1], \
	(host).cmd7_term_response[2], (host).cmd7_term_response[3], \
	(host).cmd7_trace_count, (host).cmd7_trace_overflow, \
	(host).cmd7_trace[0].iteration, \
	(unsigned int)(host).cmd7_trace[0].normal, \
	(unsigned int)(host).cmd7_trace[0].error, \
	(host).cmd7_trace[1].iteration, \
	(unsigned int)(host).cmd7_trace[1].normal, \
	(unsigned int)(host).cmd7_trace[1].error, \
	(host).cmd7_trace[2].iteration, \
	(unsigned int)(host).cmd7_trace[2].normal, \
	(unsigned int)(host).cmd7_trace[2].error, \
	(host).cmd7_trace[3].iteration, \
	(unsigned int)(host).cmd7_trace[3].normal, \
	(unsigned int)(host).cmd7_trace[3].error

static int gta4l_emmc_probe_ran;

static inline uint64_t
gta4l_emmc_probe_counter(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));
	return value;
}

static inline uint64_t
gta4l_emmc_probe_counter_hz(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (value));
	/* The architectural counter frequency is the primary source. Keep a
	 * documented 19.2 MHz fallback if firmware left it unprogrammed. */
	if ((value < 1000000u) || (value > 1000000000u)) {
		value = 19200000u;
	}
	return value;
}

static uint8_t
gta4l_emmc_probe_read8(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint8_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint8_t *)(probe->gcc + offset);
	}
	return *(volatile uint8_t *)(probe->tlmm + offset);
}

static uint16_t
gta4l_emmc_probe_read16(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint16_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint16_t *)(probe->gcc + offset);
	}
	return *(volatile uint16_t *)(probe->tlmm + offset);
}

static uint32_t
gta4l_emmc_probe_read32(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint32_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint32_t *)(probe->gcc + offset);
	}
	return *(volatile uint32_t *)(probe->tlmm + offset);
}

static void
gta4l_emmc_probe_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint8_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint8_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint8_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint16_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint16_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint16_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint32_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint32_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint32_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_barrier(void *context)
{
	(void)context;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void
gta4l_emmc_probe_delay_us(void *context, uint32_t microseconds)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;
	uint64_t start;
	uint64_t ticks;
	uint64_t spins = 0;
	uint64_t limit;
	uint64_t hz = probe->counter_hz;

	if (hz == 0u) {
		hz = 19200000u;
	}
	ticks = (((uint64_t)microseconds * hz) + 999999u) / 1000000u;
	limit = ticks + 1000000u;
	start = gta4l_emmc_probe_counter();
	while ((gta4l_emmc_probe_counter() - start) < ticks) {
		if (++spins > limit) {
			/* A stalled counter would silently skip the settle
			 * interval. Stop the diagnostic instead. */
			panic("GTA4L-EMMC PROBE FAILED: generic counter "
			    "stalled (hz=%u us=%u)",
			    (unsigned int)hz,
			    (unsigned int)microseconds);
		}
		__asm__ volatile("yield");
	}
}

static void
gta4l_emmc_probe_log(void *context, uint32_t event, uint32_t a, uint32_t b)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	probe->log_count++;
	probe->last_event = event;
	probe->last_a = a;
	probe->last_b = b;
}

static uint64_t
gta4l_emmc_probe_counter_op(void *context)
{
	(void)context;
	return gta4l_emmc_probe_counter();
}

static const struct gta4l_emmc_ops gta4l_emmc_probe_ops = {
	.read8 = gta4l_emmc_probe_read8,
	.read16 = gta4l_emmc_probe_read16,
	.read32 = gta4l_emmc_probe_read32,
	.write8 = gta4l_emmc_probe_write8,
	.write16 = gta4l_emmc_probe_write16,
	.write32 = gta4l_emmc_probe_write32,
	.barrier = gta4l_emmc_probe_barrier,
	.delay_us = gta4l_emmc_probe_delay_us,
	.log = gta4l_emmc_probe_log,
	.counter = gta4l_emmc_probe_counter_op,
};

static int
gta4l_emmc_probe_parse(const char *name, int *value)
{
	return PE_parse_boot_argn(name, value, sizeof(*value)) && (*value == 1);
}

/* 1 = full probe, 2 = controller/host init only, 3 = host init plus card
 * identification without sector reads. */
static int
gta4l_emmc_probe_stage(void)
{
	int value = 0;

	if (!PE_parse_boot_argn("gta4l-emmc-probe", &value, sizeof(value))) {
		return 0;
	}
	if ((value < 1) || (value > 3)) {
		return 0;
	}
	return value;
}

static int
gta4l_emmc_probe_conflict(void)
{
	int value = 0;

	return gta4l_emmc_probe_parse("gta4l-sdhci-audit", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-snapshot", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-host-init", &value);
}

void
gta4l_emmc_probe_preflight(void)
{
	if (gta4l_emmc_probe_stage() == 0) {
		return;
	}
	if (gta4l_emmc_probe_conflict()) {
		panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic "
		    "arguments");
	}
}

static uint32_t
gta4l_emmc_probe_sum(const uint8_t *buffer, uint32_t length)
{
	uint32_t sum = 0;
	uint32_t index;

	for (index = 0; index < length; index++) {
		sum = (sum * 31u) + buffer[index];
	}
	return sum;
}

static uint32_t
gta4l_emmc_probe_word(const uint8_t *buffer, uint32_t offset)
{
	return (uint32_t)buffer[offset] |
	    ((uint32_t)buffer[offset + 1u] << 8) |
	    ((uint32_t)buffer[offset + 2u] << 16) |
	    ((uint32_t)buffer[offset + 3u] << 24);
}

static void
gta4l_emmc_probe_report_failure(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe *probe, int stage, int result,
    const struct gta4l_emmc_probe_regs *regs)
{
	panic("GTA4L-EMMC PROBE FAILED: stage=%d rc=%d "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u "
	    "rsp=%08x,%08x,%08x,%08x "
	    "cmd1rsp=%u/%08x,%08x,%08x,%08x "
	    "pre2rsp=%u/%08x,%08x,%08x,%08x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x fmt=4 hv=%04x "
	    "cmd1=%u/%u/%08x "
	    "clocks=%u mci=%08x pwr=%02x clk=%04x hc=%02x "
	    "hc2=%04x ven=%08x v3=%08x dll=%08x dst=%08x "
	    "ddr=%08x pst=%08x cfg=%08x mnd=%08x/%08x/%08x "
	    "cmd=%08x bcr=%08x apps=%08x ahb=%08x step=%u "
	    "log=%u/%u/%u/%u" GTA4L_EMMC_PROBE_CMD7_FMT,
	    stage, result, (unsigned int)host->fail_valid,
	    host->fail_index, host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step, host->fail_response[0],
	    host->fail_response[1], host->fail_response[2],
	    host->fail_response[3],
	    (unsigned int)host->cmd1_response_valid,
	    host->cmd1_response[0], host->cmd1_response[1],
	    host->cmd1_response[2], host->cmd1_response[3],
	    (unsigned int)host->pre_cmd2_response_valid,
	    host->pre_cmd2_response[0], host->pre_cmd2_response[1],
	    host->pre_cmd2_response[2], host->pre_cmd2_response[3],
	    (unsigned int)host->cmd2_capture_valid,
	    (unsigned int)host->cmd2_normal,
	    (unsigned int)host->cmd2_error,
	    host->cmd2_raw_response[0], host->cmd2_raw_response[1],
	    host->cmd2_raw_response[2], host->cmd2_raw_response[3],
	    (unsigned int)host->pre_cmd3_capture_valid,
	    (unsigned int)host->pre_cmd3_normal,
	    (unsigned int)host->pre_cmd3_error,
	    host->pre_cmd3_present,
	    (unsigned int)host->pre_cmd3_normal_enable,
	    (unsigned int)host->pre_cmd3_error_enable,
	    (unsigned int)host->timeout_capture_valid,
	    (unsigned int)host->timeout_index,
	    (unsigned int)host->timeout_command,
	    host->timeout_argument,
	    (unsigned int)host->timeout_response_stable,
	    host->timeout_response[0], host->timeout_response[1],
	    host->timeout_response[2], host->timeout_response[3],
	    (unsigned int)host->poll_capture_valid,
	    host->poll_index,
	    (unsigned int)host->poll_first_normal,
	    (unsigned int)host->poll_last_normal,
	    host->poll_iterations,
	    (unsigned int)(host->poll_ticks >> 32),
	    (unsigned int)host->poll_ticks,
	    (unsigned int)probe->counter_hz,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    (unsigned int)regs->host_version,
	    host->cmd1_attempts,
	    host->cmd1_timeouts, host->ocr,
	    (unsigned int)host->clocks_enabled, regs->version,
	    (unsigned int)regs->power, (unsigned int)regs->clock,
	    (unsigned int)regs->host_control, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->vendor_spec3, regs->dll_config,
	    regs->dll_status, regs->ddr_config, regs->pwr_status, regs->cfg,
	    regs->gcc_m, regs->gcc_n, regs->gcc_d, regs->cmd, regs->bcr,
	    regs->apps_cbcr, regs->ahb_cbcr, host->last_step, probe->log_count,
	    probe->last_event, probe->last_a, probe->last_b,
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*host));
}

static void
gta4l_emmc_probe_report_ident(const struct gta4l_emmc_host *ident_host,
    const struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_regs *regs)
{
	panic("GTA4L-EMMC IDENT REPORT: rc=0 mci=%08x rca=%u cap=%u "
	    "rev=%u bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x "
	    "dll=%08x dst=%08x pst=%08x step=%u log=%u\n"
	    "GTA4L-EMMC IDENT CAPTURE fmt=4 hv=%04x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x" GTA4L_EMMC_PROBE_CMD7_FMT,
	    ident_host->mci_version, ident_host->rca,
	    ident_host->capacity_blocks,
	    (unsigned int)ident_host->ext_csd_rev,
	    (unsigned int)ident_host->bus_width, ident_host->ocr,
	    ident_host->ext_csd_sum, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->dll_config, regs->dll_status,
	    regs->pwr_status, ident_host->last_step, probe->log_count,
	    (unsigned int)regs->host_version,
	    (unsigned int)ident_host->cmd2_capture_valid,
	    (unsigned int)ident_host->cmd2_normal,
	    (unsigned int)ident_host->cmd2_error,
	    ident_host->cmd2_raw_response[0],
	    ident_host->cmd2_raw_response[1],
	    ident_host->cmd2_raw_response[2],
	    ident_host->cmd2_raw_response[3],
	    (unsigned int)ident_host->pre_cmd3_capture_valid,
	    (unsigned int)ident_host->pre_cmd3_normal,
	    (unsigned int)ident_host->pre_cmd3_error,
	    ident_host->pre_cmd3_present,
	    (unsigned int)ident_host->pre_cmd3_normal_enable,
	    (unsigned int)ident_host->pre_cmd3_error_enable,
	    (unsigned int)ident_host->timeout_capture_valid,
	    (unsigned int)ident_host->timeout_index,
	    (unsigned int)ident_host->timeout_command,
	    ident_host->timeout_argument,
	    (unsigned int)ident_host->timeout_response_stable,
	    ident_host->timeout_response[0], ident_host->timeout_response[1],
	    ident_host->timeout_response[2], ident_host->timeout_response[3],
	    (unsigned int)ident_host->poll_capture_valid,
	    ident_host->poll_index,
	    (unsigned int)ident_host->poll_first_normal,
	    (unsigned int)ident_host->poll_last_normal,
	    ident_host->poll_iterations,
	    (unsigned int)(ident_host->poll_ticks >> 32),
	    (unsigned int)ident_host->poll_ticks,
	    (unsigned int)probe->counter_hz,
	    ident_host->cid[0], ident_host->cid[1],
	    ident_host->cid[2], ident_host->cid[3],
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*ident_host));
}

static void
gta4l_emmc_probe_report_probe(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_regs *regs,
    const uint8_t *block0, const uint8_t *block1)
{
	panic("GTA4L-EMMC PROBE REPORT: rc=0 mci=%08x rca=%u cap=%u rev=%u "
	    "bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x dll=%08x "
	    "dst=%08x pst=%08x step=%u log=%u\n"
	    "GTA4L-EMMC PROBE CID %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE CSD %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK0 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE CAPTURE fmt=4 hv=%04x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x" GTA4L_EMMC_PROBE_CMD7_FMT,
	    host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)host->bus_width,
	    host->ocr, host->ext_csd_sum, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->dll_config, regs->dll_status,
	    regs->pwr_status, host->last_step, probe->log_count,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    host->csd[0], host->csd[1], host->csd[2], host->csd[3],
	    gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block0, 0),
	    gta4l_emmc_probe_word(block0, 446),
	    gta4l_emmc_probe_word(block0, 450),
	    gta4l_emmc_probe_word(block0, 454),
	    gta4l_emmc_probe_sum(block1, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block1, 0),
	    gta4l_emmc_probe_word(block1, 4),
	    gta4l_emmc_probe_word(block1, 8),
	    gta4l_emmc_probe_word(block1, 12),
	    (unsigned int)regs->host_version,
	    (unsigned int)host->cmd2_capture_valid,
	    (unsigned int)host->cmd2_normal,
	    (unsigned int)host->cmd2_error,
	    host->cmd2_raw_response[0], host->cmd2_raw_response[1],
	    host->cmd2_raw_response[2], host->cmd2_raw_response[3],
	    (unsigned int)host->pre_cmd3_capture_valid,
	    (unsigned int)host->pre_cmd3_normal,
	    (unsigned int)host->pre_cmd3_error,
	    host->pre_cmd3_present,
	    (unsigned int)host->pre_cmd3_normal_enable,
	    (unsigned int)host->pre_cmd3_error_enable,
	    (unsigned int)host->timeout_capture_valid,
	    (unsigned int)host->timeout_index,
	    (unsigned int)host->timeout_command,
	    host->timeout_argument,
	    (unsigned int)host->timeout_response_stable,
	    host->timeout_response[0], host->timeout_response[1],
	    host->timeout_response[2], host->timeout_response[3],
	    (unsigned int)host->poll_capture_valid,
	    host->poll_index,
	    (unsigned int)host->poll_first_normal,
	    (unsigned int)host->poll_last_normal,
	    host->poll_iterations,
	    (unsigned int)(host->poll_ticks >> 32),
	    (unsigned int)host->poll_ticks,
	    (unsigned int)probe->counter_hz,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*host));
}

void
gta4l_emmc_probe_run(void)
{
	static uint8_t block0[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t block1[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe probe;
	struct gta4l_emmc_probe_regs regs;
	struct gta4l_emmc_host host;
	vm_offset_t sdhci_mapping;
	vm_offset_t gcc_mapping;
	vm_offset_t tlmm_mapping;
	uint32_t version;
	uint8_t power;
	uint8_t host_control;
	uint16_t clock;
	uint16_t host_control2;
	uint16_t host_version;
	uint32_t vendor_spec;
	uint32_t vendor_spec3;
	uint32_t dll_config;
	uint32_t dll_status;
	uint32_t ddr_config;
	uint32_t pwr_status;
	uint32_t cfg;
	uint32_t cmd;
	uint32_t bcr;
	uint32_t apps_cbcr;
	uint32_t ahb_cbcr;
	uint32_t gcc_m;
	uint32_t gcc_n;
	uint32_t gcc_d;
	int stage;
	int result;

	if (gta4l_emmc_probe_ran != 0) {
		return;
	}
	stage = gta4l_emmc_probe_stage();
	if (stage == 0) {
		return;
	}
	if (gta4l_emmc_probe_conflict()) {
		panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic "
		    "arguments");
	}
	gta4l_emmc_probe_ran = 1;

	sdhci_mapping = ml_io_map(GTA4L_EMMC_PROBE_SDHCI_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	gcc_mapping = ml_io_map(GTA4L_EMMC_PROBE_GCC_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	tlmm_mapping = ml_io_map(GTA4L_EMMC_PROBE_TLMM_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	if ((sdhci_mapping == 0) || (gcc_mapping == 0) ||
	    (tlmm_mapping == 0)) {
		panic("GTA4L-EMMC PROBE REFUSED: MMIO map failed "
		    "(sdhci=%u gcc=%u tlmm=%u)",
		    (unsigned int)(sdhci_mapping != 0),
		    (unsigned int)(gcc_mapping != 0),
		    (unsigned int)(tlmm_mapping != 0));
	}

	bzero(&probe, sizeof(probe));
	probe.sdhci = (uintptr_t)sdhci_mapping;
	probe.gcc = (uintptr_t)gcc_mapping;
	probe.tlmm = (uintptr_t)tlmm_mapping;
	probe.counter_hz = gta4l_emmc_probe_counter_hz();

	bzero(&regs, sizeof(regs));
	bzero(&host, sizeof(host));
	host.ops = &gta4l_emmc_probe_ops;
	host.context = &probe;
	/* Board policy: L11/VCCQ is a fixed 1.8 V rail, so select 1.8 V
	 * signalling and keep the pad power switch in the 1.8 V position.
	 * The mainline gta4l DT declares no SDHC1 vqmmc consumer, so the
	 * Linux probe leaves both vendor pad bits at the POR value; this
	 * explicit policy follows the sdhci-msm fixed-1.8 V comment instead
	 * of that DT-derived state. See docs/emmc-driver.md. */
	host.fixed_1v8 = 1;
	host.pad_pwr_switch = 1;

	if (stage == 2) {
		result = gta4l_emmc_controller_init(&host);
	} else {
		result = gta4l_emmc_init(&host);
	}
	if (result != GTA4L_EMMC_OK) {
		if (host.clocks_enabled != 0) {
			version = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_MCI_VERSION);
			power = gta4l_emmc_probe_read8(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_POWER_CONTROL);
			host_control = gta4l_emmc_probe_read8(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL);
			clock = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CLOCK_CONTROL);
			host_control2 = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL2);
			host_version = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_VERSION);
			vendor_spec = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
			vendor_spec3 = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3);
			dll_config = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DLL_CONFIG);
			dll_status = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DLL_STATUS);
			ddr_config = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DDR_CONFIG);
			pwr_status = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);
		} else {
			/* Without the AHB/APPS branches the SDHCI window may not
			 * respond; report the cached result and GCC state only. */
			version = 0;
			power = 0;
			host_control = 0;
			clock = 0;
			host_control2 = 0;
			host_version = 0;
			vendor_spec = 0;
			vendor_spec3 = 0;
			dll_config = 0;
			dll_status = 0;
			ddr_config = 0;
			pwr_status = 0;
		}
		cfg = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
		gcc_m = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_M);
		gcc_n = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_N);
		gcc_d = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_D);
		cmd = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CMD);
		bcr = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_BCR);
		apps_cbcr = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_GCC, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR);
		ahb_cbcr = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_GCC, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR);
		regs.version = version;
		regs.power = power;
		regs.host_control = host_control;
		regs.clock = clock;
		regs.host_control2 = host_control2;
		regs.host_version = host_version;
		regs.vendor_spec = vendor_spec;
		regs.vendor_spec3 = vendor_spec3;
		regs.dll_config = dll_config;
		regs.dll_status = dll_status;
		regs.ddr_config = ddr_config;
		regs.pwr_status = pwr_status;
		regs.cfg = cfg;
		regs.gcc_m = gcc_m;
		regs.gcc_n = gcc_n;
		regs.gcc_d = gcc_d;
		regs.cmd = cmd;
		regs.bcr = bcr;
		regs.apps_cbcr = apps_cbcr;
		regs.ahb_cbcr = ahb_cbcr;
		gta4l_emmc_probe_report_failure(&host, &probe, stage, result,
		    &regs);
	}

	version = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	power = gta4l_emmc_probe_read8(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL);
	clock = gta4l_emmc_probe_read16(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);
	host_control2 = gta4l_emmc_probe_read16(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL2);
	host_version = gta4l_emmc_probe_read16(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_VERSION);
	vendor_spec = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
	dll_config = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	dll_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	pwr_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);
	regs.version = version;
	regs.power = power;
	regs.clock = clock;
	regs.host_control2 = host_control2;
	regs.host_version = host_version;
	regs.vendor_spec = vendor_spec;
	regs.dll_config = dll_config;
	regs.dll_status = dll_status;
	regs.pwr_status = pwr_status;

	if (stage == 2) {
		panic("GTA4L-EMMC HOST REPORT: rc=0 mci=%08x pwr=%02x "
		    "clk=%04x hc2=%04x ven=%08x dll=%08x dst=%08x "
		    "pst=%08x step=%u log=%u/%u/%u/%u",
		    version, (unsigned int)power, (unsigned int)clock,
		    (unsigned int)host_control2, vendor_spec, dll_config,
		    dll_status, pwr_status, host.last_step, probe.log_count,
		    probe.last_event, probe.last_a, probe.last_b);
	}
	if (stage == 3) {
		gta4l_emmc_probe_report_ident(&host, &probe, &regs);
	}

	result = gta4l_emmc_read_block(&host, 0, block0);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 0 read rc=%d step=%u "
		    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,
		    host.last_step, (unsigned int)host.fail_valid,
		    host.fail_index, host.fail_argument, host.fail_command,
		    (unsigned int)host.fail_error,
		    (unsigned int)host.fail_normal, host.fail_present,
		    host.fail_step);
	}
	result = gta4l_emmc_read_block(&host, 1, block1);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 1 read rc=%d step=%u "
		    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,
		    host.last_step, (unsigned int)host.fail_valid,
		    host.fail_index, host.fail_argument, host.fail_command,
		    (unsigned int)host.fail_error,
		    (unsigned int)host.fail_normal, host.fail_present,
		    host.fail_step);
	}

	gta4l_emmc_probe_report_probe(&host, &probe, &regs, block0, block1);
}
