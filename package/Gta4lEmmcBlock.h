// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l read-only block publication policy.
//
// This header is shared by the C++ IOBlockStorageDevice adapter and the
// host-only policy harness. The functions here contain no IOKit and no
// transport access: they decide the diagnostic mode, pre-validate a request,
// format the bounded P5 report and check the whole-media geometry. The
// adapter maps their results to IOReturn and performs the controller calls.
#ifndef GTA4L_EMMC_BLOCK_H
#define GTA4L_EMMC_BLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTA4L_EMMC_BLOCK_BOOT_ARG "gta4l-emmc-block"
#define GTA4L_EMMC_BLOCK_PROBE_ARG "gta4l-emmc-probe"
#define GTA4L_EMMC_BLOCK_PARTOBS_ARG "gta4l-emmc-partobs"

#define GTA4L_EMMC_BLOCK_MODE_OFF 0
#define GTA4L_EMMC_BLOCK_MODE_REPORT 1
#define GTA4L_EMMC_BLOCK_MODE_PUBLISH 2
#define GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED 3

/* Report mode is the first rung. Publish-only mode is deferred. Mode 3 is
 * report mode with two-sector CMD18 chunks; mode 1 keeps the CMD17 backend. */
#define GTA4L_EMMC_BLOCK_SELECT_INERT 0
#define GTA4L_EMMC_BLOCK_SELECT_REPORT 1
#define GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED 2
#define GTA4L_EMMC_BLOCK_SELECT_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_PARTOBS_OFF 0
#define GTA4L_EMMC_BLOCK_PARTOBS_ON 1
#define GTA4L_EMMC_BLOCK_PARTOBS_REFUSED (-1)

/* Bounded observation budgets. The media wait is the design's 20 s window;
 * the partition wait is an additional, non-fatal observation. The N8
 * partition-publication observation is opt-in and orthogonal to the read
 * mode: 4 s deadline, 50 ms report-thread sample interval, 250 ms
 * count-unchanged window. The 30 s watchdog in the adapter stays above the
 * 20 s media wait plus the 4 s observation. */
#define GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS 20000u
#define GTA4L_EMMC_BLOCK_MEDIA_STEP_MS 100u
#define GTA4L_EMMC_BLOCK_PART_WAIT_MS 2000u
#define GTA4L_EMMC_BLOCK_PART_OBSERVE_MS 4000u
#define GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_PART_STABLE_MS 250u
#define GTA4L_EMMC_BLOCK_READ_WAIT_MS 5000u
#define GTA4L_EMMC_BLOCK_REPORT_BYTES 2048u
#define GTA4L_EMMC_BLOCK_REPORT_LBA 1u
#define GTA4L_EMMC_BLOCK_PART_REPORT 3u
#define GTA4L_EMMC_BLOCK_QUAL_LBA 2u
#define GTA4L_EMMC_BLOCK_QUAL_REF_BLOCKS 3u
#define GTA4L_EMMC_BLOCK_QUAL_TWO_BLOCKS 2u
#define GTA4L_EMMC_BLOCK_QUAL_THREE_BLOCKS 3u
#define GTA4L_EMMC_BLOCK_QUAL_BYTES \
	(GTA4L_EMMC_BLOCK_QUAL_REF_BLOCKS * GTA4L_EMMC_BLOCK_BYTES)

#define GTA4L_EMMC_BLOCK_DIRECTION_READ 0u
#define GTA4L_EMMC_BLOCK_DIRECTION_WRITE 1u
#define GTA4L_EMMC_BLOCK_DIRECTION_INVALID 2u

enum gta4l_emmc_block_request_result {
	GTA4L_EMMC_BLOCK_REQUEST_OK = 0,
	GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT = 1,
	GTA4L_EMMC_BLOCK_REQUEST_NOT_WRITABLE = 2,
	GTA4L_EMMC_BLOCK_REQUEST_NO_MEDIA = 3,
	GTA4L_EMMC_BLOCK_REQUEST_TOO_LARGE = 4
};

struct gta4l_emmc_block_part {
	uint64_t base;
	uint64_t size;
	const char *content;
};

struct gta4l_emmc_block_report {
	uint32_t rc;
	uint32_t stop;
	uint32_t mode;
	uint32_t capacity_blocks;
	uint32_t block_size;
	uint64_t bytes;
	uint32_t bus_width;
	uint32_t timing;
	uint32_t clock_hz;
	uint32_t online;
	uint32_t write_protected;
	uint64_t requests;
	uint64_t blocks;
	uint64_t failures;
	uint32_t max_request_blocks;
	uint32_t first_fail_valid;
	uint64_t first_fail_block;
	int32_t first_fail_error;
	uint64_t requested_blocks;
	uint64_t cmd17_reads;
	uint64_t cmd18_chunks;
	uint64_t cmd18_blocks;
	uint64_t chunk_failures;
	uint64_t stop_attempts;
	uint64_t stop_failures;
	int32_t last_stop_result;
	uint32_t media_valid;
	uint32_t whole;
	uint64_t base;
	uint64_t size;
	uint32_t writable;
	const char *content;
	uint32_t part_count;
	uint32_t part_expected;
	uint32_t part_wait_ms;
	uint32_t part_error;
	uint32_t part_reported;
	struct gta4l_emmc_block_part part[GTA4L_EMMC_BLOCK_PART_REPORT];
	/* Opt-in bounded partition-publication observation. part_obs is the
	 * enable flag; the observation is a count-only instrument and
	 * part_obs_complete is always an explicit non-claim (0). */
	uint32_t part_obs;
	uint32_t part_obs_samples;
	uint32_t part_obs_first;
	uint32_t part_obs_first_ms;
	uint32_t part_obs_first_nz;
	uint32_t part_obs_first_nz_ms;
	uint32_t part_obs_final;
	uint32_t part_obs_final_ms;
	uint32_t part_obs_changes;
	uint32_t part_obs_stable;
	uint32_t part_obs_stable_ms;
	uint32_t part_obs_deadline_ms;
	uint32_t part_obs_timed_out;
	uint32_t part_obs_none;
	uint32_t part_obs_error;
	uint32_t part_obs_slots;
	uint32_t part_obs_reported;
	uint32_t part_obs_complete;
	uint32_t read_attempted;
	uint32_t read_ok;
	uint64_t read_bytes;
	uint32_t read_sum;
	uint32_t baseline_sum;
	uint32_t same;
	/* Mode-3 controlled qualification. Reason codes: 0 qualified,
	 * 1 reference unavailable, 2 request status/length failure,
	 * 3 exact-byte mismatch, 4 unexpected command/stop shape,
	 * 5 ambiguous capture. The reference is a same-boot direct
	 * CMD17 read of the primary GPT array; the controlled requests
	 * are explicit IOMedia reads through the real adapter path. */
	uint32_t qual_ref_valid;
	uint64_t qual_ref_lba;
	uint32_t qual_ref_blocks;
	uint32_t qual_two_attempted;
	uint32_t qual_two_ok;
	uint32_t qual_two_reason;
	uint32_t qual_two_bytes;
	uint32_t qual_two_mismatch_valid;
	uint32_t qual_two_mismatch_offset;
	uint32_t qual_two_matches;
	uint32_t qual_two_cmd18;
	uint32_t qual_two_cmd17;
	uint32_t qual_two_stops;
	uint32_t qual_two_stop_failures;
	uint32_t qual_three_attempted;
	uint32_t qual_three_ok;
	uint32_t qual_three_reason;
	uint32_t qual_three_bytes;
	uint32_t qual_three_mismatch_valid;
	uint32_t qual_three_mismatch_offset;
	uint32_t qual_three_matches;
	uint32_t qual_three_cmd18;
	uint32_t qual_three_cmd17;
	uint32_t qual_three_stops;
	uint32_t qual_three_stop_failures;
	uint32_t snapshot_locked;
	uint32_t fail_valid;
	uint32_t fail_stage;
	int32_t fail_code;
};

/* Count whole boot-argument tokens with this exact name (name=value). A
 * prefix or suffix does not match. */
uint32_t gta4l_emmc_block_token_count(const char *arguments,
    const char *name);

/* Select the diagnostic mode from the boot-argument facts. A probe-family
 * argument, another SDHCI diagnostic or a duplicate block token makes a
 * block request a refusal, not a silent second user of the controller.
 * Publish-only mode 2 is deferred and is also a refusal in this batch.
 * Mode 1 keeps the CMD17 backend; mode 3 selects the same report mode with
 * two-sector CMD18 chunks. */
int gta4l_emmc_block_mode_select(int arg_present, int arg_value,
    int probe_present, int other_conflict, int duplicate_tokens);

/* Select the orthogonal partition-publication observation. The token is
 * accepted only when exactly one block mode 1/3 token is selected, the token
 * value is exactly 1 and the token is not duplicated. A token without a block
 * selection is refused, not inert, so a mistyped diagnostic cannot silently
 * fall back to the legacy reporter. */
int gta4l_emmc_block_partobs_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected);

/* Pre-validate a request before the adapter takes the controller lock. The
 * request helper repeats the checks; this mapping is what gives the caller
 * distinct IOReturn values. */
enum gta4l_emmc_block_request_result gta4l_emmc_block_request_check(
    uint32_t capacity_blocks, uint64_t block, uint64_t nblks,
    uint64_t buffer_bytes, uint32_t direction, uint8_t online);

/* Same-boot baseline and media-path comparison sum. This is the probe's
 * per-sector accumulator (sum = sum * 31 + byte), not a cryptographic hash. */
uint32_t gta4l_emmc_block_sum(const uint8_t *data, uint32_t length);

/* Expected GPT entry count from the protective header in sector 1. Returns
 * zero when the header is absent or malformed; a zero result is not proof
 * that no partition media will appear. */
uint32_t gta4l_emmc_block_gpt_entries(const uint8_t *sector, uint32_t length);

/* Whole-media geometry check. Returns zero on a match. */
int gta4l_emmc_block_media_check(uint64_t base, uint64_t size,
    uint32_t capacity_blocks, int whole, int writable);

/* Format the bounded P5 report. Returns the number of bytes written without
 * the terminator, or zero when the buffer is too small. */
uint32_t gta4l_emmc_block_report_format(char *buffer, uint32_t size,
    const struct gta4l_emmc_block_report *report);

#ifdef __cplusplus
}
#endif

#endif /* GTA4L_EMMC_BLOCK_H */
