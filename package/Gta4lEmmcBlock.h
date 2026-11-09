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
#define GTA4L_EMMC_BLOCK_PARTCMP_ARG "gta4l-emmc-partcmp"
#define GTA4L_EMMC_BLOCK_PARTREAD_ARG "gta4l-emmc-partread"
#define GTA4L_EMMC_BLOCK_DEBLOCK_ARG "gta4l-emmc-deblock"
#define GTA4L_EMMC_BLOCK_BREAKUP_ARG "gta4l-emmc-breakup"

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

#define GTA4L_EMMC_BLOCK_PARTCMP_OFF 0
#define GTA4L_EMMC_BLOCK_PARTCMP_ON 1
#define GTA4L_EMMC_BLOCK_PARTCMP_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_PARTREAD_OFF 0
#define GTA4L_EMMC_BLOCK_PARTREAD_ON 1
#define GTA4L_EMMC_BLOCK_PARTREAD_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_DEBLOCK_OFF 0
#define GTA4L_EMMC_BLOCK_DEBLOCK_ON 1
#define GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_BREAKUP_OFF 0
#define GTA4L_EMMC_BLOCK_BREAKUP_ON 1
#define GTA4L_EMMC_BLOCK_BREAKUP_REFUSED (-1)

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

/* N9.2 exact GPT-to-IOMedia comparator. The read budget covers every GPT
 * read; the snapshot deadline is separate. 20,000 ms media wait + 4,000 ms
 * read budget + one in-flight 1,000 ms read + 2,000 ms snapshot deadline
 * stays below the 30,000 ms watchdog. */
#define GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_PARTMAP_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_PARTMAP_STABLE_MS 250u
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_BUDGET_MS 4000u
#define GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS 128u
#define GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA 128u
#define GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS 2u
#define GTA4L_EMMC_BLOCK_PARTMAP_ARRAY_BYTES \
	(GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS * 128u)
/* Every comparator read is copied through one page-aligned bounce buffer.
 * A sub-page first segment is truncated to the request block size by
 * IOBlockStorageDriver; a first segment shorter than one block truncates
 * to zero and returns kIOReturnDMAError before the adapter sees it. */
#define GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES \
	(GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS * GTA4L_EMMC_BLOCK_BYTES)
#define GTA4L_EMMC_BLOCK_PARTMAP_LINE_BYTES 384u
#define GTA4L_EMMC_BLOCK_PARTMAP_DETAIL_LINE_BYTES 256u
#define GTA4L_EMMC_BLOCK_PARTMAP_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_PARTMAP_READ_BUDGET_MS + 1000u + \
	GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS)
#define GTA4L_EMMC_BLOCK_READ_WAIT_MS 5000u

/* N10.1 bounded read-only IOMedia content qualification. Every request is a
 * whole number of 512-byte media blocks and never exceeds the 64-block
 * request limit. The media wait, the read budget, one in-flight 1,000 ms read
 * and the partition stability deadline stay below the 30 s watchdog. */
#define GTA4L_EMMC_BLOCK_PARTREAD_MAX_BLOCKS 64u
#define GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES 512u
#define GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_PARTREAD_MAX_BLOCKS * \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_PARTREAD_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_PARTREAD_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_PARTREAD_STABLE_MS 250u
#define GTA4L_EMMC_BLOCK_PARTREAD_READ_BUDGET_MS 4000u
#define GTA4L_EMMC_BLOCK_PARTREAD_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_PARTREAD_READ_BUDGET_MS + 1000u + \
	GTA4L_EMMC_BLOCK_PARTREAD_DEADLINE_MS)

/* The standard boot partition of the retained GPT layout. The base and size
 * are layout constants, not identifiers; the expected content digests below
 * bind the test to the retained GPT and pre-flash tail evidence. */
#define GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES \
	((uint64_t)2255360u * GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES \
	((uint64_t)196608u * GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)

/* The boot-image kernel section begins at page 1 and is pinned by the
 * boot-image gate to the hardware-tested baseline. Both partread kernel
 * ranges lie inside the 1,798,052-byte section. */
#define GTA4L_EMMC_BLOCK_PARTREAD_BOOT_KERNEL_OFFSET 4096u
#define GTA4L_EMMC_BLOCK_PARTREAD_BOOT_KERNEL_MID_OFFSET (1024u * 1024u)

/* N10.4 bounded read-only unaligned IOMedia qualification. Each case is a
 * fixed byte range; the deblocker's aligned span is at most 64 media blocks.
 * The bounce buffer covers the largest requested length and the aligned
 * spans with one page-aligned allocation. The media wait, the read budget,
 * one in-flight 1,000 ms read and the partition deadline stay below the 30 s
 * watchdog. */
#define GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BLOCKS 66u
#define GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES 512u
#define GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BLOCKS * \
	GTA4L_EMMC_BLOCK_DEBLOCK_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_DEBLOCK_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_DEBLOCK_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_DEBLOCK_STABLE_MS 250u
#define GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS 4000u
#define GTA4L_EMMC_BLOCK_DEBLOCK_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS + 1000u + \
	GTA4L_EMMC_BLOCK_DEBLOCK_DEADLINE_MS)
#define GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS 3u
#define GTA4L_EMMC_BLOCK_DEBLOCK_LINE_BYTES 384u
#define GTA4L_EMMC_BLOCK_DEBLOCK_DETAIL_LINE_BYTES 256u

/* N10.7 deterministic IOBreaker breakup qualification. The mode-scoped
 * advertisement is 8 read blocks (4,096 bytes) and three segments; the
 * 65,536-byte page-aligned bounce covers the largest requested length and
 * the largest aligned span. The media wait, the 6,000 ms read budget, one
 * in-flight 1,000 ms read and the partition deadline stay below the 30 s
 * watchdog. */
#define GTA4L_EMMC_BLOCK_BREAKUP_MAX_BLOCKS 128u
#define GTA4L_EMMC_BLOCK_BREAKUP_MAX_SUBREQUESTS 16u
#define GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS 8u
#define GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES 512u
#define GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_BREAKUP_MAX_BLOCKS * \
	GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_BREAKUP_READ_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS * \
	GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS 3u
#define GTA4L_EMMC_BLOCK_BREAKUP_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_BREAKUP_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_BREAKUP_STABLE_MS 250u
#define GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS 6000u
#define GTA4L_EMMC_BLOCK_BREAKUP_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS + 1000u + \
	GTA4L_EMMC_BLOCK_BREAKUP_DEADLINE_MS)
#define GTA4L_EMMC_BLOCK_BREAKUP_LINE_BYTES 448u
#define GTA4L_EMMC_BLOCK_BREAKUP_DETAIL_LINE_BYTES 320u

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
	/* Opt-in exact GPT-to-IOMedia comparator. partmap selects the PARTMAP
	 * report form; every field is a bounded count or a boolean. */
	uint32_t partmap;
	uint32_t partmap_gpt;
	uint32_t partmap_reason;
	uint32_t partmap_slots;
	uint32_t partmap_used;
	uint32_t partmap_pmbr;
	uint32_t partmap_hdr;
	uint32_t partmap_arr;
	uint32_t partmap_bak;
	uint32_t partmap_xcheck;
	uint32_t partmap_media;
	uint32_t partmap_match;
	uint32_t partmap_missing;
	uint32_t partmap_extra;
	uint32_t partmap_dup;
	uint32_t partmap_nested;
	uint32_t partmap_prop;
	uint32_t partmap_samples;
	uint32_t partmap_changes;
	uint32_t partmap_stable;
	uint32_t partmap_stable_ms;
	uint32_t partmap_deadline_ms;
	uint32_t partmap_timed_out;
	uint32_t partmap_budget;
	uint32_t partmap_reads;
	uint32_t partmap_blocks;
	uint32_t partmap_fails;
	uint32_t partmap_cancel;
	uint32_t partmap_complete;
	/* Bounded comparator read detail. state is one of the
	 * GTA4L_EMMC_BLOCK_PARTMAP_READ_* values; status is the raw 32-bit
	 * IOReturn and actual is the returned UInt64 byte count. */
	uint32_t partmap_read_state;
	uint32_t partmap_read_blocks;
	uint32_t partmap_read_bytes;
	uint32_t partmap_read_desc_len;
	uint32_t partmap_read_desc_dir;
	int32_t partmap_read_status;
	uint64_t partmap_read_lba;
	uint64_t partmap_read_actual;
	/* Opt-in bounded read-only IOMedia content qualification. Each case is
	 * a fixed range whose SHA-256 is pinned from retained evidence. */
	uint32_t partread;
	uint32_t partread_cases;
	uint32_t partread_passed;
	uint32_t partread_failed;
	uint32_t partread_reason;
	uint32_t partread_complete;
	uint32_t partread_reads;
	uint32_t partread_blocks;
	uint32_t partread_bytes;
	uint32_t partread_fails;
	uint32_t partread_cancel;
	uint32_t partread_budget;
	uint32_t partread_media;
	uint32_t partread_part;
	uint32_t partread_capacity;
	uint32_t partread_last_index;
	uint32_t partread_last_target;
	uint32_t partread_last_blocks;
	uint64_t partread_last_offset;
	uint32_t partread_last_ok;
	int32_t partread_last_status;
	uint64_t partread_last_actual;
	uint8_t partread_last_digest[16];
	uint32_t partread_fail_index;
	uint32_t partread_fail_reason;
	int32_t partread_fail_status;
	uint64_t partread_fail_actual;
	uint8_t partread_fail_digest[16];
	/* Opt-in bounded read-only unaligned IOMedia qualification. Each case
	 * is a fixed byte range whose SHA-256 is pinned from retained
	 * evidence; span is the deblocker's aligned byte count. */
	uint32_t deblock;
	uint32_t deblock_cases;
	uint32_t deblock_passed;
	uint32_t deblock_failed;
	uint32_t deblock_reason;
	uint32_t deblock_complete;
	uint32_t deblock_reads;
	uint32_t deblock_bytes;
	uint32_t deblock_span;
	uint32_t deblock_blocks;
	uint32_t deblock_fails;
	uint32_t deblock_cancel;
	uint32_t deblock_budget;
	uint32_t deblock_media;
	uint32_t deblock_part;
	uint32_t deblock_segments;
	uint32_t deblock_capacity;
	uint32_t deblock_last_index;
	uint32_t deblock_last_target;
	uint64_t deblock_last_start;
	uint32_t deblock_last_length;
	uint32_t deblock_last_span;
	int32_t deblock_last_status;
	uint64_t deblock_last_actual;
	uint8_t deblock_last_digest[16];
	uint32_t deblock_fail_index;
	uint32_t deblock_fail_reason;
	int32_t deblock_fail_status;
	uint64_t deblock_fail_actual;
	uint8_t deblock_fail_digest[16];
	/* Opt-in deterministic IOBreaker breakup qualification. The mode
	 * advertises eight read blocks and three segments; each case records
	 * the observed driver-level decomposition and the digest prefix. */
	uint32_t breakup;
	uint32_t breakup_cases;
	uint32_t breakup_passed;
	uint32_t breakup_failed;
	uint32_t breakup_reason;
	uint32_t breakup_complete;
	uint32_t breakup_reads;
	uint32_t breakup_bytes;
	uint32_t breakup_span;
	uint32_t breakup_blocks;
	uint32_t breakup_subrequests;
	uint32_t breakup_max;
	uint32_t breakup_foreign;
	uint32_t breakup_fails;
	uint32_t breakup_cancel;
	uint32_t breakup_budget;
	uint32_t breakup_media;
	uint32_t breakup_part;
	uint32_t breakup_limit;
	uint32_t breakup_segments;
	uint32_t breakup_capacity;
	uint32_t breakup_last_index;
	uint32_t breakup_last_target;
	uint64_t breakup_last_start;
	uint32_t breakup_last_length;
	uint32_t breakup_last_span;
	int32_t breakup_last_status;
	uint64_t breakup_last_actual;
	uint32_t breakup_last_subrequests;
	uint32_t breakup_last_blocks;
	uint32_t breakup_last_max;
	uint8_t breakup_last_digest[16];
	uint32_t breakup_fail_index;
	uint32_t breakup_fail_reason;
	int32_t breakup_fail_status;
	uint64_t breakup_fail_actual;
	uint32_t breakup_fail_target;
	uint64_t breakup_fail_start;
	uint32_t breakup_fail_length;
	uint32_t breakup_fail_span;
	uint32_t breakup_fail_subrequests;
	uint32_t breakup_fail_blocks;
	uint32_t breakup_fail_max;
	uint8_t breakup_fail_digest[16];
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

/* Select the orthogonal GPT-to-IOMedia comparator. The token is accepted
 * only when exactly one block mode 1/3 token is selected, the token value is
 * exactly 1, the token is not duplicated and the N8 count observation is not
 * selected in the same boot. */
int gta4l_emmc_block_partcmp_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected);

/* Select the orthogonal read-only IOMedia content qualification. The token
 * is accepted only when exactly one block mode 1/3 token is selected, the
 * value is exactly 1, the token is not duplicated and neither partition
 * observation is selected in the same boot. */
int gta4l_emmc_block_partread_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected);

/* Select the orthogonal read-only unaligned IOMedia qualification. The token
 * is accepted only when exactly one block mode 1/3 token is selected, the
 * value is exactly 1, the token is not duplicated and none of the other
 * partition/read instruments is selected in the same boot. */
int gta4l_emmc_block_deblock_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected);

/* Select the deterministic breakup qualification. The token is accepted only
 * when exactly one block mode 1/3 token is selected, the value is exactly 1,
 * the token is not duplicated and none of the other partition/read
 * instruments (including deblock) is selected in the same boot. */
int gta4l_emmc_block_breakup_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int deblock_selected);

/* PARTMAP reason codes (section 4.6 of the N9.1 design). */
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_PMBR 1u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_HEADER 2u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_ARRAY 3u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_BACKUP_HEADER 4u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_BACKUP_ARRAY 5u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_DISAGREE 6u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_ENTRY 7u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_READ 8u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_BUDGET 9u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA 10u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_MISMATCH 11u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_CHANGING 12u
#define GTA4L_EMMC_BLOCK_PARTMAP_REASON_CANCELLED 13u

/* PARTREAD reason codes. The first failing case latches the reason; the
 * counters keep every case that ran. */
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA 1u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART 2u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_OPEN 3u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_DESC 4u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_READ 5u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_SHORT 6u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_HASH 7u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_BUDGET 8u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_CANCEL 9u
#define GTA4L_EMMC_BLOCK_PARTREAD_REASON_ARG 10u

#define GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE 0u
#define GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT 1u

/* One pinned read. target selects the whole media or the boot partition
 * media; from_end_blocks, when nonzero, makes the whole-media offset relative
 * to the reported capacity. expected is the SHA-256 of the exact bytes. */
struct gta4l_emmc_block_partread_case {
	uint32_t target;
	uint32_t blocks;
	uint32_t from_end_blocks;
	uint64_t offset;
	uint8_t expected[32];
};

uint32_t gta4l_emmc_block_partread_case_count(void);
const struct gta4l_emmc_block_partread_case *
gta4l_emmc_block_partread_case_at(uint32_t index);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_partread_case *
gta4l_emmc_block_partread_case_mutable(uint32_t index);
#endif

/* DEBLOCK reason codes. The first failing case latches the reason; the
 * counters keep every case that ran. */
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA 1u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_PART 2u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_OPEN 3u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_DESC 4u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_READ 5u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_SHORT 6u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_HASH 7u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_BUDGET 8u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_CANCEL 9u
#define GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG 10u

#define GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE 0u
#define GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT 1u

/* One pinned unaligned read. target selects the whole media or the boot
 * partition media; start and length are byte quantities relative to that
 * media. expected is the SHA-256 of the exact requested bytes. */
struct gta4l_emmc_block_deblock_case {
	uint32_t target;
	uint32_t length;
	uint64_t start;
	uint8_t expected[32];
};

uint32_t gta4l_emmc_block_deblock_case_count(void);
const struct gta4l_emmc_block_deblock_case *
gta4l_emmc_block_deblock_case_at(uint32_t index);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_deblock_case *
gta4l_emmc_block_deblock_case_mutable(uint32_t index);
#endif

/* BREAKUP reason codes. The first failing case latches the reason; later
 * read/short/hash/descriptor/split/order failures keep the counters. */
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA 1u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_PART 2u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_OPEN 3u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_DESC 4u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_READ 5u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_SHORT 6u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_HASH 7u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_BUDGET 8u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_CANCEL 9u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_ARG 10u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_SPLIT 11u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER 12u
#define GTA4L_EMMC_BLOCK_BREAKUP_REASON_FOREIGN 13u

#define GTA4L_EMMC_BLOCK_BREAKUP_TARGET_WHOLE 0u
#define GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT 1u

/* One pinned breakup read. start and length are byte quantities relative to
 * the target media; span is the deblocker-aligned byte count and blocks,
 * subrequests and max_blocks are the expected decomposition. expected is the
 * SHA-256 of the exact requested bytes. */
struct gta4l_emmc_block_breakup_case {
	uint32_t target;
	uint64_t start;
	uint32_t length;
	uint32_t span;
	uint32_t blocks;
	uint32_t subrequests;
	uint32_t max_blocks;
	uint8_t expected[32];
};

uint32_t gta4l_emmc_block_breakup_case_count(void);
const struct gta4l_emmc_block_breakup_case *
gta4l_emmc_block_breakup_case_at(uint32_t index);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_breakup_case *
gta4l_emmc_block_breakup_case_mutable(uint32_t index);
#endif

/* Derive and bound-check one breakup case's aligned span and expected
 * decomposition. Returns 0 on success and fills every output. */
int gta4l_emmc_block_breakup_geometry(uint64_t start, uint32_t length,
    uint32_t *span, uint32_t *blocks, uint32_t *subrequests,
    uint32_t *max_blocks);

/* PARTMAP comparator read-detail states. The detail records the most recent
 * comparator media read while no failure has been observed; the first error
 * or short transfer freezes the fields on that request. */
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_NONE 0u
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_FLIGHT 1u
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_ERROR 2u
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_SHORT 3u
#define GTA4L_EMMC_BLOCK_PARTMAP_READ_FULL 4u

struct gta4l_emmc_block_partmap_media {
	uint64_t base;
	uint64_t size;
};

struct gta4l_emmc_block_gpt {
	uint32_t slots;
	uint32_t used;
	uint32_t first_usable;
	uint32_t last_usable;
	uint32_t pmbr;
	uint32_t hdr;
	uint32_t arr;
	uint32_t bak;
	uint32_t xcheck;
	uint32_t reason;
	uint32_t entry_size;
	uint32_t entry_crc;
	uint64_t disk_guid_low;
	uint64_t disk_guid_high;
	struct gta4l_emmc_block_partmap_media
	    entry[GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS];
};

/* EFI-style reflected CRC32 (polynomial 0xEDB88320, seed and final xor
 * 0xFFFFFFFF), independent of the Linux reference implementation. */
uint32_t gta4l_emmc_block_crc32(const uint8_t *data, uint32_t length);

/* Validate one header for the expected copy and return the bounded
 * entry-array location and slot count. Returns 1 on success. */
int gta4l_emmc_block_gpt_geometry(const uint8_t *header,
    uint32_t capacity_blocks, int primary, uint64_t *entry_lba,
    uint32_t *slots);

/* Validate the protective MBR, the primary header and the primary entry
 * array. Returns 1 when all three pass, 0 on the first failure with
 * gpt->reason set and the stage flags updated. The populated entries are
 * normalized to (base_bytes, size_bytes) and sorted by (base, size). */
int gta4l_emmc_block_gpt_primary(const uint8_t *pmbr,
    const uint8_t *header, const uint8_t *array, uint32_t capacity_blocks,
    struct gta4l_emmc_block_gpt *gpt);

/* Validate the backup header and array with the same rules, then require the
 * header fields and the normalized populated set to equal the primary's.
 * `backup` is scratch space; the stage flags and reason are updated on
 * `gpt`. Returns 1 only when both copies are valid and agree. */
int gta4l_emmc_block_gpt_backup(const uint8_t *header,
    const uint8_t *array, uint32_t capacity_blocks,
    struct gta4l_emmc_block_gpt *gpt,
    struct gta4l_emmc_block_gpt *backup);

/* Normalized media tuple check: nonzero size, sector alignment and a range
 * inside the whole media. Returns 0 when valid. */
int gta4l_emmc_block_partmap_media_check(uint64_t base, uint64_t size,
    uint64_t whole_size);

/* Sort the media tuples by (base, size) and compare them with the sorted
 * validated GPT set as exact multisets. Returns 1 on equality. missing and
 * extra count tuples without a counterpart; dup counts duplicate media
 * tuples beyond the first occurrence. */
int gta4l_emmc_block_partmap_compare(
    const struct gta4l_emmc_block_gpt *gpt,
    struct gta4l_emmc_block_partmap_media *media, uint32_t media_count,
    uint32_t *missing, uint32_t *extra, uint32_t *dup);

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
