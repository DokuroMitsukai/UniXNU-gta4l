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
#define GTA4L_EMMC_BLOCK_SWEEP_ARG "gta4l-emmc-sweep"
#define GTA4L_EMMC_BLOCK_CONSUME_ARG "gta4l-emmc-consume"
#define GTA4L_EMMC_BLOCK_WIDTH_ARG "gta4l-emmc-width"

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

#define GTA4L_EMMC_BLOCK_SWEEP_OFF 0
#define GTA4L_EMMC_BLOCK_SWEEP_ON 1
#define GTA4L_EMMC_BLOCK_SWEEP_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_CONSUME_OFF 0
#define GTA4L_EMMC_BLOCK_CONSUME_ON 1
#define GTA4L_EMMC_BLOCK_CONSUME_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_WIDTH_OFF 0
#define GTA4L_EMMC_BLOCK_WIDTH_ON 1
#define GTA4L_EMMC_BLOCK_WIDTH_REFUSED (-1)

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

/* N10.12 bounded aligned read sweep. Six boot-partition cases (two pinned
 * anchors and four runtime-derived windows) under a mode-scoped 8-block /
 * 4,096-byte / one-segment advertisement. Each case is read twice: once by
 * the direct CMD17 reference path under the controller gate and once through
 * the real media path, then compared byte-for-byte. The two 16,384-byte
 * page-aligned buffers are allocated only in sweep mode. The media wait, the
 * discovery deadline, the case-loop budget and one overrunning case stay
 * below the 30 s watchdog. */
#define GTA4L_EMMC_BLOCK_SWEEP_CASES 6u
#define GTA4L_EMMC_BLOCK_SWEEP_RUNTIME_CASES 4u
#define GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES 2u
#define GTA4L_EMMC_BLOCK_SWEEP_MAX_BLOCKS 32u
#define GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS 8u
#define GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES 512u
#define GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_SWEEP_MAX_BLOCKS * \
	GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_SWEEP_READ_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS * \
	GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS 1u
#define GTA4L_EMMC_BLOCK_SWEEP_READ_BUDGET_MS 4000u
#define GTA4L_EMMC_BLOCK_SWEEP_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_SWEEP_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_SWEEP_MAX_CASE_MS 1700u
#define GTA4L_EMMC_BLOCK_SWEEP_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_SWEEP_DEADLINE_MS + \
	GTA4L_EMMC_BLOCK_SWEEP_READ_BUDGET_MS + \
	GTA4L_EMMC_BLOCK_SWEEP_MAX_CASE_MS)
#define GTA4L_EMMC_BLOCK_SWEEP_LINE_BYTES 384u
#define GTA4L_EMMC_BLOCK_SWEEPREAD_LINE_BYTES 352u
#define GTA4L_EMMC_BLOCK_SWEEP_DERIVE_MULTIPLIER 104729u

/* N10.15 read-only consumer. One fixed BOOT window (offset 0, 1,024 bytes)
 * read by a distinct IOService client under the production 64-block /
 * 32 KiB / one-segment advertisement. The buffer stays the pinned 32 KiB
 * contiguous allocation; the descriptor and request are 1,024 bytes. The
 * per-chunk CMD18/CMD12 bound is 3,000 ms and the discovery deadline is
 * 2,000 ms after the 20,000 ms media wait, for a conditional 25,000 ms
 * against the 30,000 ms watchdog. */
#define GTA4L_EMMC_BLOCK_CONSUME_CASES 1u
#define GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS 2u
#define GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES 512u
#define GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES \
	(GTA4L_EMMC_BLOCK_CONSUME_CASE_BLOCKS * \
	GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS 64u
#define GTA4L_EMMC_BLOCK_CONSUME_MAX_BYTES \
	(GTA4L_EMMC_BLOCK_CONSUME_MAX_BLOCKS * \
	GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES 32768u
#define GTA4L_EMMC_BLOCK_CONSUME_SEGMENTS 1u
#define GTA4L_EMMC_BLOCK_CONSUME_CASE_TAG 1u
#define GTA4L_EMMC_BLOCK_CONSUME_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_CONSUME_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_CONSUME_READ_MS 3000u
#define GTA4L_EMMC_BLOCK_CONSUME_WORST_MS \
	(GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS + \
	GTA4L_EMMC_BLOCK_CONSUME_DEADLINE_MS + \
	GTA4L_EMMC_BLOCK_CONSUME_READ_MS)
#define GTA4L_EMMC_BLOCK_CONSUME_LINE_BYTES 320u
#define GTA4L_EMMC_BLOCK_CONSUMEREAD_LINE_BYTES 352u
#define GTA4L_EMMC_BLOCK_CONSUMEFAIL_LINE_BYTES 384u
#define GTA4L_EMMC_BLOCK_CONSUME_REPORT_WORST_BYTES 1959u

/* N10.16 opt-in staged 4-bit SDR width publication at the existing
 * 400 kHz. The mode reads one pinned two-block BOOT window at 1-bit,
 * switches the card and host to 4-bit with the verified helper, re-reads
 * the same window, compares it byte-for-byte and reverts to 1-bit with the
 * captured baseline. The clock, chunk shape, DMA/interrupt policy and the
 * production request advertisement are unchanged. The 26,230 ms bound is
 * the audit's conditional success sum: 1-bit EXT_CSD 2,000 + reference
 * read 3,000 + verified switch 6,010 + existing LBA-1 baseline 2,000 +
 * bounded gate 2,000 + 4-bit read 3,000 + verified revert 8,220. A failed
 * switch fails closed at 21,030 ms; the success margin is not a
 * path-condition proof. */
#define GTA4L_EMMC_BLOCK_WIDTH_BITS_4 4u
#define GTA4L_EMMC_BLOCK_WIDTH_CASES 1u
#define GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS 2u
#define GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES \
	(GTA4L_EMMC_BLOCK_WIDTH_CASE_BLOCKS * \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_WIDTH_LBA \
	(GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES / \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_WIDTH_GATE_MS 2000u
#define GTA4L_EMMC_BLOCK_WIDTH_GATE_STEP_MS 10u
#define GTA4L_EMMC_BLOCK_WIDTH_WORST_MS 26230u
#define GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS 13220u
#define GTA4L_EMMC_BLOCK_WIDTH_LINE_BYTES 513u
#define GTA4L_EMMC_BLOCK_WIDTHREAD_LINE_BYTES 329u
#define GTA4L_EMMC_BLOCK_WIDTHFAIL_LINE_BYTES 127u
/* Exact worst-case sum of the lines the width mode can emit, measured from
 * the formatter with every numeric field at its type maximum and the
 * width-mode structural shape: P5 REPORT 171 + DRV 147 + XFER 223 + MEDIA
 * 123 + WIDTH 513 + WIDTHREAD 329 + PART summary 105 + SNAP 37 + FAIL 115
 * + 9 newlines = 1772. WIDTHREAD is the larger of the two mutually
 * exclusive detail lines (WIDTHFAIL is 127). Width mode returns before
 * media discovery, so no PART detail lines are emitted and the media
 * scalars stay zero; the bound is therefore conservative. */
#define GTA4L_EMMC_BLOCK_WIDTH_REPORT_WORST_BYTES 1772u

/* N10.17 sustained 4-bit published-media read qualification. The mode reads
 * the first eight BOOT blocks (4,096 bytes) at 1-bit through the block core,
 * switches to 4-bit with the verified helper, then issues four fixed
 * two-block tagged IOMedia reads over the same window while 4-bit stays
 * active, compares each slice to its 1-bit reference slice and reverts to
 * 1-bit on every post-switch exit. The report-thread bound is the 8,000 ms
 * media wait + 2,000 ms BOOT deadline + 3,000 ms case budget + one 4,500 ms
 * two-block case + 2,000 ms revert gate + 8,220 ms verified revert =
 * 27,720 ms, leaving 2,280 ms under the 30,000 ms watchdog. */
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_ARG "gta4l-emmc-widthhold"
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_OFF 0
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_ON 1
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED (-1)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_BITS_4 4u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES 4u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS 2u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES \
	(GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BLOCKS * \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS 8u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES \
	(GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS * \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_LBA \
	(GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES / \
	GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_TAG 1u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_MEDIA_WAIT_MS 8000u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_DEADLINE_MS 2000u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_STEP_MS 50u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_READ_BUDGET_MS 3000u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_MS 2000u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_GATE_STEP_MS 10u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_WORST_MS 27720u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES 922u
#define GTA4L_EMMC_BLOCK_WIDTHHOLDREAD_LINE_BYTES 303u
#define GTA4L_EMMC_BLOCK_WIDTHHOLDFAIL_LINE_BYTES 133u
/* Formatter-exact maxima with every numeric field holding its largest u32 and
 * 16-byte digests filled: WIDTHHOLD 922 (failure shape; the success shape is
 * 913 because failed=0), WIDTHHOLDREAD 303 and WIDTHHOLDFAIL 133. The
 * established base REPORT/DRV/XFER/MEDIA/SNAP/FAIL bound is 838, so the
 * success worst case is 838 + 922 + 303 + two newlines + NUL = 2,066; the
 * failure path replaces the read line with WIDTHHOLDFAIL and totals 1,896.
 * The summary bound uses the larger 922 line so it covers both shapes. */
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REPORT_WORST_BYTES 2066u

/* N10.24 bounded 4-bit published-media read qualification. This is the same
 * pre-report-revert sequence as widthhold, but it serves two passes of the
 * four two-block offsets under one shared 3,000 ms total case-loop budget and
 * adds a `pub` marker that is 1 only after the media and BOOT partition were
 * published at 4-bit. The token, report prefix and reason namespace are
 * distinct so a widthpub report is never read as a widthhold report.
 *
 * Timing stays the widthhold bound because the second pass adds cases, not
 * budget: 8,000 ms media wait + 2,000 ms BOOT deadline + 3,000 ms shared loop
 * budget + one 4,500 ms in-flight case + 2,000 ms revert gate + 8,220 ms
 * verified revert = 27,720 ms, leaving 2,280 ms under the 30,000 ms watchdog.
 * The pre-watchdog start path is the widthhold sequence: 28,010 ms on
 * success, 36,030 ms if the switch itself fails, and 38,230 ms after any
 * other post-switch pre-report failure with the bounded recovery. */
#define GTA4L_EMMC_BLOCK_WIDTHPUB_ARG "gta4l-emmc-widthpub"
#define GTA4L_EMMC_BLOCK_WIDTHPUB_OFF 0
#define GTA4L_EMMC_BLOCK_WIDTHPUB_ON 1
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED (-1)
#define GTA4L_EMMC_BLOCK_WIDTHPUB_BITS_4 4u
#define GTA4L_EMMC_BLOCK_WIDTHPUB_PASSES 2u
#define GTA4L_EMMC_BLOCK_WIDTHPUB_CASE_OFFSETS 4u
#define GTA4L_EMMC_BLOCK_WIDTHPUB_CASES \
	(GTA4L_EMMC_BLOCK_WIDTHPUB_PASSES * \
	GTA4L_EMMC_BLOCK_WIDTHPUB_CASE_OFFSETS)
/* Formatter-exact maxima: the shared widthhold summary is 922 bytes; the
 * one-character-shorter WIDTHPUB prefix and the widest ` pub=<u32>` field
 * add 14 bytes, giving 936. The read and failure detail prefixes are one
 * character shorter than their widthhold counterparts: 302 and 132. The base
 * bound is still 838, so the success worst case is
 * 838 + 936 + 302 + two newlines + NUL = 2,079; the failure path totals
 * 1,909 and is covered by the larger summary bound. */
#define GTA4L_EMMC_BLOCK_WIDTHPUB_LINE_BYTES 936u
#define GTA4L_EMMC_BLOCK_WIDTHPUBREAD_LINE_BYTES 302u
#define GTA4L_EMMC_BLOCK_WIDTHPUBFAIL_LINE_BYTES 132u
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REPORT_WORST_BYTES 2079u

/* S1 staged 4-bit/50 MHz speed qualification. The token is accepted only
 * with exactly one gta4l-emmc-block=3 token, the value 1, no duplicate and
 * none of the partition, read, consumer or width instruments selected in
 * the same boot. B1 plumbs the selector; the S1 start path consumes it. */
#define GTA4L_EMMC_BLOCK_SPEED_ARG "gta4l-emmc-speed"
#define GTA4L_EMMC_BLOCK_SPEED_OFF 0
#define GTA4L_EMMC_BLOCK_SPEED_ON 1
#define GTA4L_EMMC_BLOCK_SPEED_REFUSED (-1)

#define GTA4L_EMMC_BLOCK_REPORT_BYTES 2560u
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
	/* Opt-in bounded aligned read sweep. Each boot-partition case is read
	 * by the direct CMD17 reference path and by the real media path; the
	 * two buffers are compared byte-for-byte. The two digests are
	 * 16-byte prefixes of the media and reference SHA-256. */
	uint32_t sweep;
	uint32_t sweep_cases;
	uint32_t sweep_passed;
	uint32_t sweep_failed;
	uint32_t sweep_reason;
	uint32_t sweep_complete;
	uint32_t sweep_reads;
	uint32_t sweep_bytes;
	uint32_t sweep_span;
	uint32_t sweep_blocks;
	uint32_t sweep_subrequests;
	uint32_t sweep_max;
	uint32_t sweep_foreign;
	uint32_t sweep_fails;
	uint32_t sweep_cancel;
	uint32_t sweep_budget;
	uint32_t sweep_media;
	uint32_t sweep_part;
	uint32_t sweep_refs;
	uint32_t sweep_refbytes;
	uint32_t sweep_anchor;
	uint32_t sweep_mismatches;
	uint32_t sweep_limit;
	uint32_t sweep_segments;
	uint32_t sweep_capacity;
	uint32_t sweep_last_index;
	uint32_t sweep_last_target;
	uint64_t sweep_last_start;
	uint32_t sweep_last_length;
	uint32_t sweep_last_span;
	int32_t sweep_last_status;
	uint64_t sweep_last_actual;
	uint32_t sweep_last_subrequests;
	uint32_t sweep_last_blocks;
	uint32_t sweep_last_max;
	uint32_t sweep_last_ref;
	uint32_t sweep_last_refbytes;
	int32_t sweep_last_refstatus;
	uint32_t sweep_last_mvalid;
	uint32_t sweep_last_mismatch;
	uint8_t sweep_last_digest[16];
	uint8_t sweep_last_refdigest[16];
	uint32_t sweep_fail_index;
	uint32_t sweep_fail_reason;
	int32_t sweep_fail_status;
	uint64_t sweep_fail_actual;
	uint32_t sweep_fail_target;
	uint64_t sweep_fail_start;
	uint32_t sweep_fail_length;
	uint32_t sweep_fail_span;
	uint32_t sweep_fail_subrequests;
	uint32_t sweep_fail_blocks;
	uint32_t sweep_fail_max;
	uint32_t sweep_fail_ref;
	uint32_t sweep_fail_refbytes;
	int32_t sweep_fail_refstatus;
	uint32_t sweep_fail_mvalid;
	uint32_t sweep_fail_mismatch;
	uint8_t sweep_fail_digest[16];
	uint8_t sweep_fail_refdigest[16];
	/* Opt-in single-window read-only consumer. A distinct IOService client
	 * opens the BOOT partition, reads 1,024 bytes through the production
	 * advertisement and records the observed SHA-256 prefix. Option A
	 * compares that prefix on the host, so complete=1 covers transport,
	 * attribution, open/close and allocation only. */
	uint32_t consume;
	uint32_t consume_cases;
	uint32_t consume_passed;
	uint32_t consume_failed;
	uint32_t consume_reason;
	uint32_t consume_complete;
	uint32_t consume_reads;
	uint32_t consume_bytes;
	uint32_t consume_span;
	uint32_t consume_blocks;
	uint32_t consume_subrequests;
	uint32_t consume_max;
	uint32_t consume_foreign;
	uint32_t consume_fails;
	uint32_t consume_cancel;
	uint32_t consume_budget;
	uint32_t consume_media;
	uint32_t consume_part;
	uint32_t consume_limit;
	uint32_t consume_segments;
	uint32_t consume_capacity;
	uint32_t consume_open;
	uint32_t consume_close;
	uint32_t consume_alloc;
	uint32_t consume_client;
	uint32_t consume_mismatch;
	uint32_t consume_last_index;
	uint32_t consume_last_target;
	uint64_t consume_last_start;
	uint32_t consume_last_length;
	uint32_t consume_last_span;
	int32_t consume_last_status;
	uint64_t consume_last_actual;
	uint32_t consume_last_subrequests;
	uint32_t consume_last_blocks;
	uint32_t consume_last_max;
	uint8_t consume_last_digest[16];
	uint32_t consume_fail_index;
	uint32_t consume_fail_reason;
	int32_t consume_fail_status;
	uint64_t consume_fail_actual;
	uint32_t consume_fail_target;
	uint64_t consume_fail_start;
	uint32_t consume_fail_length;
	uint32_t consume_fail_span;
	uint32_t consume_fail_requested;
	uint32_t consume_fail_completed;
	uint8_t consume_fail_digest[16];
	/* Opt-in N10.16 staged 4-bit SDR width publication at the existing
	 * 400 kHz. The mode reads one pinned two-block BOOT window at 1-bit,
	 * switches the card and host to 4-bit with the verified helper,
	 * re-reads the same window and compares it byte-for-byte, then
	 * reverts to 1-bit with the captured baseline. No expected digest is
	 * compiled in; the host compares both printed prefixes with the
	 * packaged image after the run. */
	uint32_t width;
	uint32_t width_cases;
	uint32_t width_passed;
	uint32_t width_failed;
	uint32_t width_reason;
	uint32_t width_complete;
	uint32_t width_switch;
	uint32_t width_verified;
	uint32_t width_hb;
	uint32_t width_ha;
	uint32_t width_rb;
	uint32_t width_cmd6;
	uint32_t width_revert;
	uint32_t width_rverified;
	uint32_t width_rhb;
	uint32_t width_rha;
	uint32_t width_rcmd6;
	uint32_t width_reads;
	uint32_t width_bytes;
	uint32_t width_blocks;
	uint32_t width_subrequests;
	uint32_t width_max;
	uint32_t width_match;
	uint32_t width_mismatch;
	uint32_t width_fails;
	uint32_t width_cancel;
	uint32_t width_budget;
	uint32_t width_limit;
	uint32_t width_segments;
	uint32_t width_capacity;
	uint32_t width_last_index;
	uint64_t width_last_lba;
	uint64_t width_last_start;
	uint32_t width_last_length;
	uint32_t width_last_span;
	int32_t width_last_status;
	uint64_t width_last_actual;
	uint32_t width_last_subrequests;
	uint32_t width_last_blocks;
	uint32_t width_last_max;
	uint8_t width_last_digest[16];
	uint8_t width_last_predigest[16];
	uint32_t width_last_match;
	uint32_t width_fail_stage;
	int32_t width_fail_status;
	uint64_t width_fail_actual;
	uint8_t width_fail_digest[16];
	/* Opt-in N10.17 sustained 4-bit published-media read qualification.
	 * Four fixed two-block BOOT reads at 4-bit, each compared to the
	 * same-boot 1-bit reference slice, then a verified 1-bit revert. New
	 * numeric fields are capped at u32 so the bounded report fits. */
	uint32_t widthhold;
	uint32_t widthhold_cases;
	uint32_t widthhold_passed;
	uint32_t widthhold_failed;
	uint32_t widthhold_reason;
	uint32_t widthhold_complete;
	uint32_t widthhold_switch;
	uint32_t widthhold_verified;
	uint32_t widthhold_hb;
	uint32_t widthhold_ha;
	uint32_t widthhold_rb;
	uint32_t widthhold_cmd6;
	uint32_t widthhold_refs;
	uint32_t widthhold_refbytes;
	uint32_t widthhold_refblocks;
	int32_t widthhold_refstatus;
	uint32_t widthhold_refchunks;
	uint32_t widthhold_refstops;
	uint32_t widthhold_refstopfail;
	uint8_t widthhold_refdigest[16];
	uint32_t widthhold_reads;
	uint32_t widthhold_bytes;
	uint32_t widthhold_blocks;
	uint32_t widthhold_subrequests;
	uint32_t widthhold_max;
	uint32_t widthhold_c18;
	uint32_t widthhold_stops;
	uint32_t widthhold_stopfail;
	uint32_t widthhold_match;
	uint32_t widthhold_mismatch;
	uint32_t widthhold_fails;
	uint32_t widthhold_foreign;
	uint32_t widthhold_cancel;
	uint32_t widthhold_budget;
	uint8_t widthhold_obsdigest[16];
	uint32_t widthhold_partbase;
	uint32_t widthhold_partsize;
	uint32_t widthhold_partro;
	uint32_t widthhold_partdiag;
	uint32_t widthhold_revert;
	uint32_t widthhold_rverified;
	uint32_t widthhold_rhb;
	uint32_t widthhold_rha;
	uint32_t widthhold_rcmd6;
	int32_t widthhold_rstatus;
	uint32_t widthhold_limit;
	uint32_t widthhold_segments;
	uint32_t widthhold_capacity;
	uint32_t widthhold_last_index;
	uint32_t widthhold_last_lba;
	uint32_t widthhold_last_start;
	uint32_t widthhold_last_length;
	uint32_t widthhold_last_span;
	uint32_t widthhold_last_actual;
	int32_t widthhold_last_status;
	uint32_t widthhold_last_subrequests;
	uint32_t widthhold_last_blocks;
	uint32_t widthhold_last_max;
	uint8_t widthhold_last_digest[16];
	uint8_t widthhold_last_predigest[16];
	uint32_t widthhold_last_match;
	uint32_t widthhold_fail_index;
	int32_t widthhold_fail_status;
	uint32_t widthhold_fail_actual;
	uint32_t widthhold_fail_start;
	uint32_t widthhold_fail_length;
	uint32_t widthhold_fail_span;
	uint32_t widthhold_fail_blocks;
	uint32_t widthhold_fail_max;
	uint8_t widthhold_fail_digest[16];
	/* N10.24 bounded published-media marker and mode. The widthpub report
	 * reuses the widthhold field block; widthpub != 0 selects the
	 * WIDTHPUB formatter prefix and widthpub_pub is the `pub` marker. */
	uint32_t widthpub;
	uint32_t widthpub_pub;
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
    int duplicate_tokens, int block_selected, int speed_selected);

/* Select the orthogonal GPT-to-IOMedia comparator. The token is accepted
 * only when exactly one block mode 1/3 token is selected, the token value is
 * exactly 1, the token is not duplicated and the N8 count observation is not
 * selected in the same boot. */
int gta4l_emmc_block_partcmp_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int speed_selected);

/* Select the orthogonal read-only IOMedia content qualification. The token
 * is accepted only when exactly one block mode 1/3 token is selected, the
 * value is exactly 1, the token is not duplicated and neither partition
 * observation is selected in the same boot. */
int gta4l_emmc_block_partread_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int speed_selected);

/* Select the orthogonal read-only unaligned IOMedia qualification. The token
 * is accepted only when exactly one block mode 1/3 token is selected, the
 * value is exactly 1, the token is not duplicated and none of the other
 * partition/read instruments is selected in the same boot. */
int gta4l_emmc_block_deblock_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int speed_selected);

/* Select the deterministic breakup qualification. The token is accepted only
 * when exactly one block mode 1/3 token is selected, the value is exactly 1,
 * the token is not duplicated and none of the other partition/read
 * instruments (including deblock) is selected in the same boot. */
int gta4l_emmc_block_breakup_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int deblock_selected,
    int speed_selected);

/* Select the bounded aligned read sweep. The token is accepted only when
 * exactly one block mode 1/3 token is selected, the value is exactly 1, the
 * token is not duplicated and none of the other partition/read instruments
 * (including deblock and breakup) is selected in the same boot. */
int gta4l_emmc_block_sweep_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int deblock_selected,
    int breakup_selected, int speed_selected);

/* Select the single-window read-only consumer. The token is accepted only
 * when exactly one gta4l-emmc-block=3 token is selected, the value is
 * exactly 1, the token is not duplicated and none of the other
 * partition/read instruments is selected in the same boot. A block mode 1
 * token is refused because the consumer exercises the multi-block path. */
int gta4l_emmc_block_consume_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int speed_selected);

/* Select the opt-in staged 4-bit SDR width publication. The token is
 * accepted only when exactly one gta4l-emmc-block=3 token is selected, the
 * value is exactly 4, the token is not duplicated and none of the other
 * partition/read/consumer instruments is selected in the same boot. A block
 * mode 1 token is refused because the width case uses the two-block CMD18
 * path. */
int gta4l_emmc_block_width_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int speed_selected);

/* Select the opt-in sustained 4-bit published-media qualification. The token
 * is accepted only when exactly one gta4l-emmc-block=3 token is selected, the
 * value is exactly 1, the token is not duplicated and none of the other
 * partition/read/consumer/width instruments is selected in the same boot. */
int gta4l_emmc_block_widthhold_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int speed_selected);

/* Select the opt-in bounded published-media read qualification. The token is
 * accepted only when exactly one gta4l-emmc-block=3 token is selected, the
 * value is exactly 1, the token is not duplicated and none of the other
 * partition/read/consumer/width instruments is selected in the same boot. */
int gta4l_emmc_block_widthpub_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int widthhold_selected,
    int speed_selected);

/* Select the opt-in staged S1 speed qualification. The token is accepted
 * only when exactly one gta4l-emmc-block=3 token is selected, the value is
 * exactly 1, the token is not duplicated and none of the partition, read,
 * consumer or width instruments is selected in the same boot. */
int gta4l_emmc_block_speed_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int widthhold_selected,
    int widthpub_selected);

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

/* SWEEP reason codes. Codes 0-13 reuse the breakup mapping; 14 and 15 are
 * sweep-only. The first failing case latches the reason; later read, short,
 * descriptor, mismatch, split or order failures keep the counters. */
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_MEDIA 1u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_PART 2u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_OPEN 3u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_DESC 4u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_READ 5u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_SHORT 6u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_HASH 7u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_BUDGET 8u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_CANCEL 9u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_ARG 10u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_SPLIT 11u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER 12u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_FOREIGN 13u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH 14u
#define GTA4L_EMMC_BLOCK_SWEEP_REASON_REF 15u

/* CONSUME reason codes. The first failure latches the reason and suppresses
 * suffix I/O. HASH is reserved for the option B compiled comparison; option
 * A compares the observed digest on the host after the run. CLOSE is a host
 * accounting guard. */
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_MEDIA 1u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_PART 2u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_OPEN 3u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC 4u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC 5u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_READ 6u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_SHORT 7u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_HASH 8u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_BUDGET 9u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_CANCEL 10u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_ARG 11u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_ORDER 12u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_FOREIGN 13u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_CLIENT 14u
#define GTA4L_EMMC_BLOCK_CONSUME_REASON_CLOSE 15u

/* WIDTH reason and first-failure stage codes. The same code is the summary
 * reason and the WIDTHFAIL stage: the first failing stage latches and no
 * later stage overwrites it. */
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_BASELINE 1u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_PREREAD 2u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_SWITCH 3u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_READ 4u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_MATCH 5u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_REVERT 6u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_CANCEL 7u
#define GTA4L_EMMC_BLOCK_WIDTH_REASON_GATE 8u

/* WIDTHHOLD reason and first-failure stage codes. This is a separate
 * namespace and report field from the N10.16 WIDTH codes; numeric overlap is
 * not a conflict and a widthhold code is never read as a width code. */
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE 0u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BASELINE 1u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PREREAD 2u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SWITCH 3u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MEDIA 4u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PART 5u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_OPEN 6u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ARG 7u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_DESC 8u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_READ 9u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SHORT 10u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MATCH 11u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ORDER 12u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_FOREIGN 13u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BUDGET 14u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_CANCEL 15u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_GATE 16u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_REVERT 17u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BASE4 18u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ADVERT 19u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_THREAD 20u
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ALLOC 21u

/* WIDTHPUB reuses the widthhold engine and therefore its numeric reason and
 * stage space. The aliases keep widthpub code and tests explicit without
 * creating a second, drifting table. A widthpub reason is only ever printed
 * on a WIDTHPUB line. */
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_NONE \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_BASELINE \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BASELINE
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_PREREAD \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PREREAD
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_SWITCH \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SWITCH
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_MEDIA \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MEDIA
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_PART \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PART
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_OPEN \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_OPEN
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_ARG \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ARG
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_DESC \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_DESC
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_READ \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_READ
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_SHORT \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SHORT
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_MATCH \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MATCH
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_ORDER \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ORDER
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_FOREIGN \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_FOREIGN
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_BUDGET \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BUDGET
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_CANCEL \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_CANCEL
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_GATE \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_GATE
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_REVERT \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_REVERT
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_BASE4 \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BASE4
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_ADVERT \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ADVERT
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_THREAD \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_THREAD
#define GTA4L_EMMC_BLOCK_WIDTHPUB_REASON_ALLOC \
	GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ALLOC

/* WIDTHHOLD BOOT-partition qualification diagnostics. The first failing
 * category is latched with reason PART and printed in the summary; zero
 * means no PART failure was latched. The bits never carry a raw Content
 * string, GUID or label. */
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_NOTFOUND (1u << 0)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_DUPLICATE (1u << 1)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_SIZE (1u << 2)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_BASE (1u << 3)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_WRITABLE (1u << 4)
#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT (1u << 5)

#define GTA4L_EMMC_BLOCK_CONSUME_TARGET_BOOT 1u

/* Validate the fixed consumer window against the discovered BOOT size. The
 * window is offset 0, 1,024 bytes, whole 512-byte blocks, and entirely
 * inside the partition. Returns 0 on success. */
int gta4l_emmc_block_consume_validate(uint64_t start, uint32_t length,
    uint64_t boot_size);

#define GTA4L_EMMC_BLOCK_SWEEP_TARGET_WHOLE 0u
#define GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT 1u

/* One bounded aligned sweep case. start is a byte offset relative to the
 * target media; runtime entries derive start from the discovered
 * boot-partition block count and set the runtime flag. expected is the
 * pinned SHA-256 of the anchor bytes and zero for a runtime window. */
struct gta4l_emmc_block_sweep_case {
	uint32_t target;
	uint32_t runtime;
	uint64_t start;
	uint32_t length;
	uint32_t span;
	uint32_t blocks;
	uint32_t subrequests;
	uint32_t max_blocks;
	uint8_t expected[32];
};

uint32_t gta4l_emmc_block_sweep_case_count(void);
const struct gta4l_emmc_block_sweep_case *
gta4l_emmc_block_sweep_case_at(uint32_t index);
#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_sweep_case *
gta4l_emmc_block_sweep_case_mutable(uint32_t index);
#endif

/* Derive one runtime window's start block from the boot-partition block
 * count. This is the pure production recipe; the host test exercises it with
 * a different capacity to prove the starts change. Returns 0 on success. */
int gta4l_emmc_block_sweep_derive(uint32_t runtime_index,
    uint32_t window_blocks, uint32_t capacity_blocks,
    uint64_t *start_block);

/* Validate one sweep case against the discovered boot-partition geometry.
 * start is the byte offset the caller will use: pinned for anchors, derived
 * for runtime windows. Returns 0 on success. */
int gta4l_emmc_block_sweep_validate(
    const struct gta4l_emmc_block_sweep_case *item, uint64_t start,
    uint32_t capacity_blocks, uint64_t boot_base_bytes);

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
