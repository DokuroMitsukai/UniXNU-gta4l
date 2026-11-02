/*
 * Host-test-only field-wise width fixtures for the gta4l eMMC probe reports.
 *
 * scripts/test-gta4l-emmc-probe.py includes this file at the end of the
 * rewritten production probe source, so the fixtures call the production
 * report formatters with the actual format strings and argument lists. No
 * production build includes it.
 *
 * Every value below is a synthetic formatting bound, not an observed
 * controller or card state, and not a claim that the fields are jointly
 * reachable. The bounds are justified in
 * docs/emmc-cmd7-timeout-finding.md; narrow invariants stay narrow
 * (validity flags, command indices, protocol steps, RCA, CMD1 attempts) and
 * only genuinely arbitrary 32-bit arguments use UINT32_MAX.
 */
#include <string.h>

void gta4l_emmc_probe_width_fixture(int form);

void
gta4l_emmc_probe_width_fixture(int form)
{
	static uint8_t block0[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t block1[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe probe;
	struct gta4l_emmc_probe_regs regs;
	struct gta4l_emmc_probe_range range;
	struct gta4l_emmc_host host;

	memset(&probe, 0, sizeof(probe));
	memset(&regs, 0, sizeof(regs));
	memset(&range, 0, sizeof(range));
	memset(&host, 0, sizeof(host));
	memset(block0, 0xFF, sizeof(block0));
	memset(block1, 0xFF, sizeof(block1));

	/* Counter frequency is accepted up to 1000000000 (ten digits). The
	 * POWER poll bound (500000 us / 10 us) logs at most once per
	 * iteration, and the one-shot call sites add fewer than ten more, so
	 * log_count stays within five digits; 99999 makes that width explicit.
	 * last_event is 8. last_a and last_b carry arbitrary 32-bit register
	 * values. */
	probe.counter_hz = 1000000000u;
	probe.log_count = 99999u;
	probe.last_event = GTA4L_EMMC_LOG_ERROR;
	probe.last_a = 0xFFFFFFFFu;
	probe.last_b = 0xFFFFFFFFu;

	/* Arbitrary 32-bit register and argument values. */
	regs.version = 0xFFFFFFFFu;
	regs.power = 0xFFu;
	regs.host_control = 0xFFu;
	regs.clock = 0xFFFFu;
	regs.host_control2 = 0xFFFFu;
	regs.host_version = 0xFFFFu;
	regs.vendor_spec = 0xFFFFFFFFu;
	regs.vendor_spec3 = 0xFFFFFFFFu;
	regs.dll_config = 0xFFFFFFFFu;
	regs.dll_status = 0xFFFFFFFFu;
	regs.ddr_config = 0xFFFFFFFFu;
	regs.pwr_status = 0xFFFFFFFFu;
	regs.cfg = 0xFFFFFFFFu;
	regs.gcc_m = 0xFFFFFFFFu;
	regs.gcc_n = 0xFFFFFFFFu;
	regs.gcc_d = 0xFFFFFFFFu;
	regs.cmd = 0xFFFFFFFFu;
	regs.bcr = 0xFFFFFFFFu;
	regs.apps_cbcr = 0xFFFFFFFFu;
	regs.ahb_cbcr = 0xFFFFFFFFu;

	host.clocks_enabled = 1;
	host.mci_version = 0xFFFFFFFFu;
	host.rca = GTA4L_EMMC_DEFAULT_RCA;
	host.capacity_blocks = 0xFFFFFFFFu;
	host.ext_csd_rev = 0xFFu;
	host.bus_width = 0x0Fu;
	host.ocr = 0xFFFFFFFFu;
	host.ext_csd_sum = 0xFFFFFFFFu;
	host.last_step = GTA4L_EMMC_STEP_READY;
	host.cid[0] = 0xFFFFFFFFu;
	host.cid[1] = 0xFFFFFFFFu;
	host.cid[2] = 0xFFFFFFFFu;
	host.cid[3] = 0xFFFFFFFFu;
	host.csd[0] = 0xFFFFFFFFu;
	host.csd[1] = 0xFFFFFFFFu;
	host.csd[2] = 0xFFFFFFFFu;
	host.csd[3] = 0xFFFFFFFFu;

	/* Failure capture: command index 0..63, step 0..18, 20 CMD1
	 * attempts, arbitrary argument/raw windows. */
	host.fail_valid = 1;
	host.fail_index = 63u;
	host.fail_argument = 0xFFFFFFFFu;
	host.fail_command = 0xFFFFu;
	host.fail_error = 0xFFFFu;
	host.fail_normal = 0xFFFFu;
	host.fail_present = 0xFFFFFFFFu;
	host.fail_response[0] = 0xFFFFFFFFu;
	host.fail_response[1] = 0xFFFFFFFFu;
	host.fail_response[2] = 0xFFFFFFFFu;
	host.fail_response[3] = 0xFFFFFFFFu;
	host.fail_step = GTA4L_EMMC_STEP_READY;
	host.cmd1_response_valid = 1;
	host.cmd1_response[0] = 0xFFFFFFFFu;
	host.cmd1_response[1] = 0xFFFFFFFFu;
	host.cmd1_response[2] = 0xFFFFFFFFu;
	host.cmd1_response[3] = 0xFFFFFFFFu;
	host.pre_cmd2_response_valid = 1;
	host.pre_cmd2_response[0] = 0xFFFFFFFFu;
	host.pre_cmd2_response[1] = 0xFFFFFFFFu;
	host.pre_cmd2_response[2] = 0xFFFFFFFFu;
	host.pre_cmd2_response[3] = 0xFFFFFFFFu;
	host.cmd2_capture_valid = 1;
	host.cmd2_normal = 0xFFFFu;
	host.cmd2_error = 0xFFFFu;
	host.cmd2_raw_response[0] = 0xFFFFFFFFu;
	host.cmd2_raw_response[1] = 0xFFFFFFFFu;
	host.cmd2_raw_response[2] = 0xFFFFFFFFu;
	host.cmd2_raw_response[3] = 0xFFFFFFFFu;
	host.pre_cmd3_capture_valid = 1;
	host.pre_cmd3_normal = 0xFFFFu;
	host.pre_cmd3_error = 0xFFFFu;
	host.pre_cmd3_present = 0xFFFFFFFFu;
	host.pre_cmd3_normal_enable = 0xFFFFu;
	host.pre_cmd3_error_enable = 0xFFFFu;
	host.timeout_capture_valid = 1;
	host.timeout_index = 63u;
	host.timeout_command = 0xFFFFu;
	host.timeout_argument = 0xFFFFFFFFu;
	host.timeout_response_stable = 1;
	host.timeout_response[0] = 0xFFFFFFFFu;
	host.timeout_response[1] = 0xFFFFFFFFu;
	host.timeout_response[2] = 0xFFFFFFFFu;
	host.timeout_response[3] = 0xFFFFFFFFu;
	host.poll_capture_valid = 1;
	host.poll_index = 63u;
	host.poll_first_normal = 0xFFFFu;
	host.poll_last_normal = 0xFFFFu;
	host.poll_iterations = GTA4L_EMMC_MAX_POLL_ITERATIONS;
	host.poll_ticks = 0xFFFFFFFFFFFFFFFFull;
	host.cmd1_attempts = 20u;
	host.cmd1_timeouts = 20u;

	/* Post-bound drain bound: the block field can reach the 16-bit
	 * transfer count and the iteration field the poll cap. */
	host.multi_drain_used = 1;
	host.multi_drain_resolved = 1;
	host.multi_drain_blocks = 0xFFFFu;
	host.multi_drain_iterations = GTA4L_EMMC_MAX_POLL_ITERATIONS;

	/* CMD7 capture: a valid late CC and TC at the last poll iteration,
	 * a full trace with a late stored entry, and a field-wise overflow
	 * bound (one observed change per poll iteration at most). */
	host.cmd7_pre_capture_valid = 1;
	host.cmd7_pre_normal = 0xFFFFu;
	host.cmd7_pre_error = 0xFFFFu;
	host.cmd7_pre_present = 0xFFFFFFFFu;
	host.cmd7_pre_normal_enable = 0xFFFFu;
	host.cmd7_pre_error_enable = 0xFFFFu;
	host.cmd7_pre_response[0] = 0xFFFFFFFFu;
	host.cmd7_pre_response[1] = 0xFFFFFFFFu;
	host.cmd7_pre_response[2] = 0xFFFFFFFFu;
	host.cmd7_pre_response[3] = 0xFFFFFFFFu;
	host.cmd7_poll_capture_valid = 1;
	host.cmd7_poll_or_normal = 0xFFFFu;
	host.cmd7_poll_or_error = 0xFFFFu;
	host.cmd7_poll_first_normal = 0xFFFFu;
	host.cmd7_poll_last_normal = 0xFFFFu;
	host.cmd7_poll_iterations = GTA4L_EMMC_MAX_POLL_ITERATIONS;
	host.cmd7_poll_ticks = 0xFFFFFFFFFFFFFFFFull;
	host.cmd7_cc_valid = 1;
	host.cmd7_cc_iteration = GTA4L_EMMC_MAX_POLL_ITERATIONS - 1u;
	host.cmd7_cc_ticks = 0xFFFFFFFFFFFFFFFFull;
	host.cmd7_tc_valid = 1;
	host.cmd7_tc_iteration = GTA4L_EMMC_MAX_POLL_ITERATIONS - 1u;
	host.cmd7_tc_ticks = 0xFFFFFFFFFFFFFFFFull;
	host.cmd7_trace_count = GTA4L_EMMC_CMD7_TRACE_ENTRIES;
	host.cmd7_trace_overflow = GTA4L_EMMC_MAX_POLL_ITERATIONS -
	    GTA4L_EMMC_CMD7_TRACE_ENTRIES;
	host.cmd7_trace[0].iteration = 0u;
	host.cmd7_trace[0].normal = 0xFFFFu;
	host.cmd7_trace[0].error = 0xFFFFu;
	host.cmd7_trace[1].iteration = 99997u;
	host.cmd7_trace[1].normal = 0u;
	host.cmd7_trace[1].error = 0u;
	host.cmd7_trace[2].iteration = 99998u;
	host.cmd7_trace[2].normal = 0xFFFFu;
	host.cmd7_trace[2].error = 0u;
	host.cmd7_trace[3].iteration = GTA4L_EMMC_MAX_POLL_ITERATIONS - 1u;
	host.cmd7_trace[3].normal = 0u;
	host.cmd7_trace[3].error = 0xFFFFu;
	host.cmd7_term_capture_valid = 1;
	host.cmd7_term_normal = 0xFFFFu;
	host.cmd7_term_error = 0xFFFFu;
	host.cmd7_term_present = 0xFFFFFFFFu;
	host.cmd7_term_command = 0xFFFFu;
	host.cmd7_term_argument = 0xFFFFFFFFu;
	host.cmd7_term_response[0] = 0xFFFFFFFFu;
	host.cmd7_term_response[1] = 0xFFFFFFFFu;
	host.cmd7_term_response[2] = 0xFFFFFFFFu;
	host.cmd7_term_response[3] = 0xFFFFFFFFu;

	/* Extended content phase bounds: the gated header values stay pinned
	 * (array LBA 2, 84 x 128-byte entries, first usable LBA 34) and only
	 * the capacity-derived last/backup pair takes the widest form. */
	range.entries_lba = GTA4L_EMMC_PROBE_ARRAY_LBA;
	range.entry_count = GTA4L_EMMC_PROBE_ENTRY_COUNT;
	range.entry_size = GTA4L_EMMC_PROBE_ENTRY_SIZE;
	range.first_usable = GTA4L_EMMC_PROBE_FIRST_USABLE;
	range.last_usable = 0xFFFFFFDDu;
	range.backup_lba = 0xFFFFFFFEu;
	range.data_sum = 0xFFFFFFFFu;
	range.last_sum = 0xFFFFFFFFu;
	memset(range.array, 0xFF, sizeof(range.array));

	if (form == 0) {
		/* stage 1..3 and the most negative result code, so the
		 * signed-decimal field carries its widest production host
		 * form. */
		gta4l_emmc_probe_report_failure(&host, &probe, 3,
		    GTA4L_EMMC_ERR_UNSUPPORTED, &regs);
	} else if (form == 1) {
		gta4l_emmc_probe_report_ident(&host, &probe, &regs);
	} else if (form == 2) {
		host.last_step = GTA4L_EMMC_STEP_PROBE_LAST;
		gta4l_emmc_probe_report_probe(&host, &probe, &regs, block0,
		    block1, &range, 0);
	} else {
		/* MD form: step 19, all CMD18 sums at their field bound and
		 * the widest mismatch suffix. */
		host.last_step = GTA4L_EMMC_STEP_PROBE_MB;
		range.mb_same = 0;
		range.mb_div = 0xFFFFFFFFu;
		memset(range.mb, 0xFF, sizeof(range.mb));
		gta4l_emmc_probe_report_probe(&host, &probe, &regs, block0,
		    block1, &range, 1);
	}
}
