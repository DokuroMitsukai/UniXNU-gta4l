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
	struct gta4l_emmc_probe_bus base;
	struct gta4l_emmc_probe_bus w4;
	struct gta4l_emmc_probe_bus w8;
	struct gta4l_emmc_probe_b2 b2;
	struct gta4l_emmc_probe_h10 h10;
	struct gta4l_emmc_probe_h11 h11;
	struct gta4l_emmc_probe_h12 h12;
	struct gta4l_emmc_probe_h13 h13;
	struct gta4l_emmc_probe_h14 h14;
	struct gta4l_emmc_host host;
	uint32_t index;

	memset(&probe, 0, sizeof(probe));
	memset(&regs, 0, sizeof(regs));
	memset(&range, 0, sizeof(range));
	memset(&base, 0, sizeof(base));
	memset(&w4, 0, sizeof(w4));
	memset(&w8, 0, sizeof(w8));
	memset(&b2, 0, sizeof(b2));
	memset(&h10, 0, sizeof(h10));
	memset(&h11, 0, sizeof(h11));
	memset(&h12, 0, sizeof(h12));
	memset(&h13, 0, sizeof(h13));
	memset(&h14, 0, sizeof(h14));
	memset(&host, 0, sizeof(host));
	memset(block0, 0xFF, sizeof(block0));
	memset(block1, 0xFF, sizeof(block1));

	/* Counter frequency is accepted up to 1000000000 (ten digits). The
	 * POWER poll bound (500000 us / 10 us) logs at most once per
	 * iteration, and the one-shot call sites add fewer than ten more, so
	 * log_count stays within five digits; 99999 makes that width explicit.
	 * last_event is 8. last_a and last_b carry arbitrary 32-bit register
	 * values. */
	probe.platform.counter_hz = 1000000000u;
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

	/* Stage-B1 field bounds. The switched width is the masked EXT_CSD
	 * BUS_WIDTH value, so 0x0F is its widest readback; the switch and test
	 * results stay inside the production error codes; the sums and first
	 * differing LBA take the full 32-bit range. */
	base.attempted = 1u;
	base.tested = 1u;
	base.same = 1u;
	base.div = 0u;
	base.result.requested = 1u;
	base.result.switched = 0x0Fu;
	base.result.switch_result = GTA4L_EMMC_OK;
	base.test.test_result = GTA4L_EMMC_OK;
	base.test.test_word = 0xFFFFFFFFu;
	base.test.test_expected = 0xFFFFFFFFu;
	base.result.host_control = 0xFFu;
	base.result.switch_status = 0xFFFFFFFFu;
	base.result.host_before = 0xFFu;
	base.result.host_after = 0xFFu;
	base.result.readback_sum = 0xFFFFFFFFu;
	base.pad_qdsd = 0xFFFFFFFFu;
	base.pad_dll = 0xFFFFFFFFu;
	base.pad_dst = 0xFFFFFFFFu;
	base.block0_sum = 0xFFFFFFFFu;
	base.block1_sum = 0xFFFFFFFFu;
	base.data_sum = 0xFFFFFFFFu;
	base.last_sum = 0xFFFFFFFFu;
	memset(base.blocks, 0xFF, sizeof(base.blocks));

	w4.attempted = 1u;
	w4.tested = 1u;
	w4.same = 0u;
	w4.div = 0xFFFFFFFFu;
	w4.result.requested = 4u;
	w4.result.switched = 0x0Fu;
	w4.result.switch_result = GTA4L_EMMC_ERR_BUS_TEST;
	w4.test.test_result = GTA4L_EMMC_ERR_BUS_TEST;
	w4.test.test_word = 0xFFFFFFFFu;
	w4.test.test_expected = 0xFFFFFFFFu;
	w4.b1c_enabled = 1u;
	w4.continued = 1u;
	w4.reread_width = 0xFFFFFFFFu;
	w4.reread_sum = 0xFFFFFFFFu;
	w4.result.host_control = 0xFFu;
	w4.result.switch_status = 0xFFFFFFFFu;
	w4.result.host_before = 0xFFu;
	w4.result.host_after = 0xFFu;
	w4.result.readback_sum = 0xFFFFFFFFu;
	w4.block0_sum = 0xFFFFFFFFu;
	w4.block1_sum = 0xFFFFFFFFu;
	w4.data_sum = 0xFFFFFFFFu;
	w4.last_sum = 0xFFFFFFFFu;
	memset(w4.blocks, 0xFF, sizeof(w4.blocks));

	w8 = w4;
	w8.result.requested = 8u;
	w8.div = 0xFFFFFFFEu;

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
	} else if (form == 3) {
		/* MD form: step 19, all CMD18 sums at their field bound and
		 * the widest mismatch suffix. */
		host.last_step = GTA4L_EMMC_STEP_PROBE_MB;
		range.mb_same = 0;
		range.mb_div = 0xFFFFFFFFu;
		memset(range.mb, 0xFF, sizeof(range.mb));
		gta4l_emmc_probe_report_probe(&host, &probe, &regs, block0,
		    block1, &range, 1);
	} else if (form == 4) {
		/* Stage-B1 form: both width rungs present, every content sum
		 * and the first differing LBA at their 32-bit bound, and the
		 * widest switch/test result codes. */
		host.fail_step = GTA4L_EMMC_STEP_BUS_TEST;
		host.last_step = GTA4L_EMMC_STEP_BUS_TEST;
		gta4l_emmc_probe_report_b1(&host, &probe, &range, &base, &w4,
		    &w8, GTA4L_EMMC_PROBE_ERR_CONTENT, 8);
	} else if (form == 5) {
		/* Stage-B2 form: both HS results at their field bounds, the
		 * content pass at its widest, the failed revert and a failed
		 * recovery read. */
		b2.pre_clock = 0xFFFFu;
		b2.pre_hs = 0xFFu;
		b2.pre_sum = 0xFFFFFFFFu;
		b2.pre_qdsd = 0xFFFFFFFFu;
		b2.pre_dll = 0xFFFFFFFFu;
		b2.pre_dst = 0xFFFFFFFFu;
		b2.hs.timing = 1u;
		b2.hs.hs_before = 0xFFu;
		b2.hs.hs_after = 0xFFu;
		b2.hs.rcg_before = 0xFFFFFFFFu;
		b2.hs.rcg_after = 0xFFFFFFFFu;
		b2.hs.clock = 0xFFFFu;
		b2.hs.switch_status = 0xFFFFFFFFu;
		b2.hs.readback_sum = 0xFFFFFFFFu;
		b2.hs.switch_result = GTA4L_EMMC_ERR_TIMING;
		b2.hs.clock_result = GTA4L_EMMC_ERR_CLOCK;
		b2.revert = b2.hs;
		b2.revert.timing = 0u;
		b2.revert.switch_result = GTA4L_EMMC_ERR_TIMING;
		b2.revert.clock_result = GTA4L_EMMC_ERR_CLOCK;
		b2.content_attempted = 1u;
		b2.content.same = 0u;
		b2.content.div = 0xFFFFFFFFu;
		b2.content.block0_sum = 0xFFFFFFFFu;
		b2.content.block1_sum = 0xFFFFFFFFu;
		b2.content.data_sum = 0xFFFFFFFFu;
		b2.content.last_sum = 0xFFFFFFFFu;
		memset(b2.content.blocks, 0xFF, sizeof(b2.content.blocks));
		b2.switch_rc = GTA4L_EMMC_ERR_TIMING;
		b2.content_rc = GTA4L_EMMC_PROBE_ERR_CONTENT;
		b2.revert_rc = GTA4L_EMMC_ERR_CLOCK;
		b2.q_result = GTA4L_EMMC_ERR_INHIBIT;
		b2.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_b2(&host, &probe, &b2,
		    GTA4L_EMMC_ERR_CLOCK, 13);
	} else if (form == 6) {
		/* H9 form: both attempt slots at their field bounds and a
		 * failed duplicate so the stop code stays non-clean. */
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.cmd3_retry_enabled = 1u;
		host.cmd3_retry_engaged = 1u;
		host.cmd3_retry_ok = 0u;
		host.cmd3_attempt_count = GTA4L_EMMC_CMD3_ATTEMPTS;
		for (index = 0; index < GTA4L_EMMC_CMD3_ATTEMPTS; index++) {
			struct gta4l_emmc_cmd3_capture *capture =
			    &host.cmd3_attempt[index];

			capture->number = index + 1u;
			capture->result = GTA4L_EMMC_ERR_UNSUPPORTED;
			capture->r1_error = 1u;
			capture->fail_valid = host.fail_valid;
			capture->fail_index = host.fail_index;
			capture->fail_argument = host.fail_argument;
			capture->fail_command = host.fail_command;
			capture->fail_error = host.fail_error;
			capture->fail_normal = host.fail_normal;
			capture->fail_present = host.fail_present;
			capture->fail_step = host.fail_step;
			capture->pre_valid = host.pre_cmd3_capture_valid;
			capture->pre_normal = host.pre_cmd3_normal;
			capture->pre_error = host.pre_cmd3_error;
			capture->pre_present = host.pre_cmd3_present;
			capture->pre_normal_enable = host.pre_cmd3_normal_enable;
			capture->pre_error_enable = host.pre_cmd3_error_enable;
			capture->timeout_valid = host.timeout_capture_valid;
			capture->timeout_index = host.timeout_index;
			capture->timeout_command = host.timeout_command;
			capture->timeout_argument = host.timeout_argument;
			capture->timeout_response_stable =
			    host.timeout_response_stable;
			capture->poll_valid = host.poll_capture_valid;
			capture->poll_iterations = host.poll_iterations;
			capture->poll_index = host.poll_index;
			capture->poll_first_normal = host.poll_first_normal;
			capture->poll_last_normal = host.poll_last_normal;
			capture->poll_ticks = host.poll_ticks;
			capture->fail_response[0] = host.fail_response[0];
			capture->fail_response[1] = host.fail_response[1];
			capture->fail_response[2] = host.fail_response[2];
			capture->fail_response[3] = host.fail_response[3];
			capture->timeout_response[0] =
			    host.timeout_response[0];
			capture->timeout_response[1] =
			    host.timeout_response[1];
			capture->timeout_response[2] =
			    host.timeout_response[2];
			capture->timeout_response[3] =
			    host.timeout_response[3];
			capture->response[0] = 0xFFFFFFFFu;
			capture->response[1] = 0xFFFFFFFFu;
			capture->response[2] = 0xFFFFFFFFu;
			capture->response[3] = 0xFFFFFFFFu;
		}
		gta4l_emmc_probe_report_h9(&host, &probe, &regs,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	} else if (form == 7) {
		/* H10 form: both switch results, the bus test, the content
		 * pass and the reverts at their field bounds, and a failed
		 * recovery read. */
		h10.pre_hs = 0xFFu;
		h10.pre_width = 0xFFu;
		h10.pre_clock = 0xFFFFu;
		h10.pre_hc = 0xFFu;
		h10.pre_sum = 0xFFFFFFFFu;
		h10.pre_qdsd = 0xFFFFFFFFu;
		h10.pre_dll = 0xFFFFFFFFu;
		h10.pre_dst = 0xFFFFFFFFu;
		h10.hs.timing = 1u;
		h10.hs.hs_before = 0xFFu;
		h10.hs.hs_after = 0xFFu;
		h10.hs.rcg_before = 0xFFFFFFFFu;
		h10.hs.rcg_after = 0xFFFFFFFFu;
		h10.hs.clock = 0xFFFFu;
		h10.hs.switch_status = 0xFFFFFFFFu;
		h10.hs.readback_sum = 0xFFFFFFFFu;
		h10.hs.switch_result = GTA4L_EMMC_ERR_TIMING;
		h10.hs.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h10.width.requested = 4u;
		h10.width.switched = 0xFFu;
		h10.width.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h10.width.switch_status = 0xFFFFFFFFu;
		h10.width.host_before = 0xFFu;
		h10.width.host_after = 0xFFu;
		h10.width.readback_sum = 0xFFFFFFFFu;
		h10.width_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h10.tested = 1u;
		h10.bus_test.test_result = GTA4L_EMMC_ERR_BUS_TEST;
		h10.bus_test.test_word = 0xFFFFFFFFu;
		h10.bus_test.test_expected = 0xFFFFFFFFu;
		h10.content_attempted = 1u;
		h10.content.same = 0u;
		h10.content.div = 0xFFFFFFFFu;
		h10.content.block0_sum = 0xFFFFFFFFu;
		h10.content.block1_sum = 0xFFFFFFFFu;
		h10.content.data_sum = 0xFFFFFFFFu;
		h10.content.last_sum = 0xFFFFFFFFu;
		memset(h10.content.blocks, 0xFF, sizeof(h10.content.blocks));
		h10.content_rc = GTA4L_EMMC_ERR_DATA_CRC;
		h10.width_revert.requested = 1u;
		h10.width_revert.switched = 0xFFu;
		h10.width_revert.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h10.width_revert.switch_status = 0xFFFFFFFFu;
		h10.width_revert.host_before = 0xFFu;
		h10.width_revert.host_after = 0xFFu;
		h10.width_revert.readback_sum = 0xFFFFFFFFu;
		h10.width_revert_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h10.hs_revert = h10.hs;
		h10.hs_revert.timing = 0u;
		h10.hs_revert.switch_result = GTA4L_EMMC_ERR_TIMING;
		h10.hs_revert.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h10.hs_revert_rc = GTA4L_EMMC_ERR_TIMING;
		h10.q_result = GTA4L_EMMC_ERR_DATA_CRC;
		h10.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_h10(&host, &probe, &h10,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	} else if (form == 8) {
		/* H11 form: both width attempts, the bus test, the content
		 * pass and the reverts at their field bounds, and a failed
		 * recovery read. */
		h11.pre_hs = 0xFFu;
		h11.pre_width = 0xFFu;
		h11.pre_clock = 0xFFFFu;
		h11.pre_hc = 0xFFu;
		h11.pre_sum = 0xFFFFFFFFu;
		h11.pre_qdsd = 0xFFFFFFFFu;
		h11.pre_dll = 0xFFFFFFFFu;
		h11.pre_dst = 0xFFFFFFFFu;
		h11.hs.timing = 1u;
		h11.hs.hs_before = 0xFFu;
		h11.hs.hs_after = 0xFFu;
		h11.hs.rcg_before = 0xFFFFFFFFu;
		h11.hs.rcg_after = 0xFFFFFFFFu;
		h11.hs.clock = 0xFFFFu;
		h11.hs.switch_status = 0xFFFFFFFFu;
		h11.hs.readback_sum = 0xFFFFFFFFu;
		h11.hs.switch_result = GTA4L_EMMC_ERR_TIMING;
		h11.hs.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h11.w4.requested = 4u;
		h11.w4.switched = 0xFFu;
		h11.w4.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.w4.switch_status = 0xFFFFFFFFu;
		h11.w4.host_before = 0xFFu;
		h11.w4.host_after = 0xFFu;
		h11.w4.readback_sum = 0xFFFFFFFFu;
		h11.w4_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.w8.requested = 8u;
		h11.w8.switched = 0xFFu;
		h11.w8.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.w8.switch_status = 0xFFFFFFFFu;
		h11.w8.host_before = 0xFFu;
		h11.w8.host_after = 0xFFu;
		h11.w8.readback_sum = 0xFFFFFFFFu;
		h11.w8_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.tested = 1u;
		h11.bus_test.test_result = GTA4L_EMMC_ERR_BUS_TEST;
		h11.bus_test.test_word = 0xFFFFFFFFu;
		h11.bus_test.test_expected = 0xFFFFFFFFu;
		h11.content_attempted = 1u;
		h11.content.same = 0u;
		h11.content.div = 0xFFFFFFFFu;
		h11.content.block0_sum = 0xFFFFFFFFu;
		h11.content.block1_sum = 0xFFFFFFFFu;
		h11.content.data_sum = 0xFFFFFFFFu;
		h11.content.last_sum = 0xFFFFFFFFu;
		memset(h11.content.blocks, 0xFF, sizeof(h11.content.blocks));
		h11.content_rc = GTA4L_EMMC_ERR_DATA_CRC;
		h11.width_revert.requested = 1u;
		h11.width_revert.switched = 0xFFu;
		h11.width_revert.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.width_revert.switch_status = 0xFFFFFFFFu;
		h11.width_revert.host_before = 0xFFu;
		h11.width_revert.host_after = 0xFFu;
		h11.width_revert.readback_sum = 0xFFFFFFFFu;
		h11.width_revert_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h11.hs_revert = h11.hs;
		h11.hs_revert.timing = 0u;
		h11.hs_revert.switch_result = GTA4L_EMMC_ERR_TIMING;
		h11.hs_revert.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h11.hs_revert_rc = GTA4L_EMMC_ERR_TIMING;
		h11.q_result = GTA4L_EMMC_ERR_DATA_CRC;
		h11.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_h11(&host, &probe, &h11,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	} else if (form == 9) {
		/* H12 form: the record-only DDR attempt, the HS entry, both
		 * reverts and the recovery read at their field bounds, and a
		 * failed recovery read. */
		h12.pre_hs = 0xFFu;
		h12.pre_width = 0xFFu;
		h12.pre_clock = 0xFFFFu;
		h12.pre_hc = 0xFFu;
		h12.pre_sum = 0xFFFFFFFFu;
		h12.pre_qdsd = 0xFFFFFFFFu;
		h12.pre_dll = 0xFFFFFFFFu;
		h12.pre_dst = 0xFFFFFFFFu;
		h12.hs.timing = 1u;
		h12.hs.hs_before = 0xFFu;
		h12.hs.hs_after = 0xFFu;
		h12.hs.rcg_before = 0xFFFFFFFFu;
		h12.hs.rcg_after = 0xFFFFFFFFu;
		h12.hs.clock = 0xFFFFu;
		h12.hs.switch_status = 0xFFFFFFFFu;
		h12.hs.readback_sum = 0xFFFFFFFFu;
		h12.hs.switch_result = GTA4L_EMMC_ERR_TIMING;
		h12.hs.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h12.switch_rc = GTA4L_EMMC_ERR_TIMING;
		h12.wddr.requested = 4u;
		h12.wddr.switched = 0xFFu;
		h12.wddr.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h12.wddr.switch_status = 0xFFFFFFFFu;
		h12.wddr.host_before = 0xFFu;
		h12.wddr.host_after = 0xFFu;
		h12.wddr.readback_sum = 0xFFFFFFFFu;
		h12.wddr_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h12.width_revert.requested = 1u;
		h12.width_revert.switched = 0xFFu;
		h12.width_revert.switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		h12.width_revert.switch_status = 0xFFFFFFFFu;
		h12.width_revert.host_before = 0xFFu;
		h12.width_revert.host_after = 0xFFu;
		h12.width_revert.readback_sum = 0xFFFFFFFFu;
		h12.width_revert_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		h12.hs_revert = h12.hs;
		h12.hs_revert.timing = 0u;
		h12.hs_revert.switch_result = GTA4L_EMMC_ERR_TIMING;
		h12.hs_revert.clock_result = GTA4L_EMMC_ERR_CLOCK;
		h12.hs_revert_rc = GTA4L_EMMC_ERR_TIMING;
		h12.q_result = GTA4L_EMMC_ERR_DATA_CRC;
		h12.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_h12(&host, &probe, &h12,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	} else if (form == 10) {
		/* H13 form: the rung-1 write-phase wedge, the bounded data
		 * reset, the post-reset present state and the CMD14 read at
		 * their field bounds, and a failed recovery read. */
		h13.pre_clock = 0xFFFFu;
		h13.pre_hc = 0xFFu;
		h13.pre_bw = 0xFFu;
		h13.pre_sum = 0xFFFFFFFFu;
		h13.pre_qdsd = 0xFFFFFFFFu;
		h13.pre_dll = 0xFFFFFFFFu;
		h13.pre_dst = 0xFFFFFFFFu;
		h13.bus.requested = 1u;
		h13.bus.host_control = 0xFFu;
		h13.bus.write_result = GTA4L_EMMC_ERR_UNSUPPORTED;
		h13.bus.write_error = 0xFFFFu;
		h13.bus.write_normal = 0xFFFFu;
		h13.bus.write_present = 0xFFFFFFFFu;
		h13.bus.reset_result = GTA4L_EMMC_ERR_RESET;
		h13.bus.reset_written = 0xFFu;
		h13.bus.reset_readback = 0xFFu;
		h13.bus.reset_iterations = 0xFFFFFFFFu;
		h13.bus.post_present = 0xFFFFFFFFu;
		h13.bus.dat_inhibit = 0xFFFFFFFFu;
		h13.bus.test_result = GTA4L_EMMC_ERR_BUS_TEST;
		h13.bus.test_word = 0xFFFFFFFFu;
		h13.bus.test_expected = 0xFFFFFFFFu;
		h13.q_result = GTA4L_EMMC_ERR_DATA_CRC;
		h13.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_h13(&host, &probe, &h13,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	} else if (form == 11) {
		/* H14 form: the rung-2 write-phase wedge, the bounded data
		 * reset, the post-reset present state, both forced drain
		 * commands and the CMD14 read at their field bounds, and a
		 * failed recovery read. */
		h14.pre_clock = 0xFFFFu;
		h14.pre_hc = 0xFFu;
		h14.pre_bw = 0xFFu;
		h14.pre_sum = 0xFFFFFFFFu;
		h14.pre_qdsd = 0xFFFFFFFFu;
		h14.pre_dll = 0xFFFFFFFFu;
		h14.pre_dst = 0xFFFFFFFFu;
		h14.bus.requested = 1u;
		h14.bus.host_control = 0xFFu;
		h14.bus.write_result = GTA4L_EMMC_ERR_UNSUPPORTED;
		h14.bus.write_error = 0xFFFFu;
		h14.bus.write_normal = 0xFFFFu;
		h14.bus.write_present = 0xFFFFFFFFu;
		h14.bus.reset_result = GTA4L_EMMC_ERR_RESET;
		h14.bus.reset_written = 0xFFu;
		h14.bus.reset_readback = 0xFFu;
		h14.bus.reset_iterations = 0xFFFFFFFFu;
		h14.bus.post_present = 0xFFFFFFFFu;
		h14.bus.dat_inhibit = 0xFFFFFFFFu;
		h14.bus.test_result = GTA4L_EMMC_ERR_BUS_TEST;
		h14.bus.test_word = 0xFFFFFFFFu;
		h14.bus.test_expected = 0xFFFFFFFFu;
		h14.drain.stop_result = GTA4L_EMMC_ERR_UNSUPPORTED;
		h14.drain.stop_response = 0xFFFFFFFFu;
		h14.drain.stop_error = 0xFFFFu;
		h14.drain.stop_normal = 0xFFFFu;
		h14.drain.stop_present = 0xFFFFFFFFu;
		h14.drain.status_attempted = 1u;
		h14.drain.status_result = GTA4L_EMMC_ERR_UNSUPPORTED;
		h14.drain.status_response = 0xFFFFFFFFu;
		h14.drain.status_error = 0xFFFFu;
		h14.drain.status_normal = 0xFFFFu;
		h14.drain.status_present = 0xFFFFFFFFu;
		h14.q_result = GTA4L_EMMC_ERR_DATA_CRC;
		h14.q_block0_sum = 0xFFFFFFFFu;
		host.fail_valid = 1u;
		host.fail_index = 0xFFFFFFFFu;
		host.fail_argument = 0xFFFFFFFFu;
		host.fail_command = 0xFFFFFFFFu;
		host.fail_error = 0xFFFFu;
		host.fail_normal = 0xFFFFu;
		host.fail_present = 0xFFFFFFFFu;
		host.fail_step = GTA4L_EMMC_STEP_HS_CLOCK;
		host.last_step = GTA4L_EMMC_STEP_HS_CLOCK;
		gta4l_emmc_probe_report_h14(&host, &probe, &h14,
		    GTA4L_EMMC_ERR_UNSUPPORTED, 13);
	}
}
