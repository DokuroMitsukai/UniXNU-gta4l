// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l read-only block publication policy.
//
// No IOKit, no transport and no libc. The adapter supplies the boot-argument
// facts, the request metadata, the media geometry and the observed counters;
// this file decides the mode, validates requests, checks geometry and writes
// the bounded P5 report.

#include "Gta4lEmmcBlock.h"

#include "Gta4lEmmcBlockCore.h"

/* Option A prints a zero expected digest; the host owns the external
 * reference and compares the observed prefix after the run. */
static const uint8_t gta4l_emmc_block_zero_digest[16] = { 0 };

_Static_assert(GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHHOLD line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHHOLDREAD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHHOLDREAD line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHHOLDFAIL_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHHOLDFAIL line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHHOLD_REPORT_WORST_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHHOLD worst case must fit the report buffer");
_Static_assert((GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES +
    GTA4L_EMMC_BLOCK_WIDTHHOLDREAD_LINE_BYTES) <
    GTA4L_EMMC_BLOCK_WIDTHHOLD_REPORT_WORST_BYTES,
    "WIDTHHOLD worst case must cover both detail lines");
/* The widthhold line caps are derived with every new numeric field capped at
 * u32. Keep the formerly 64-bit report fields at 32 bits so widening one is a
 * compile-time failure instead of an unguarded report overrun. */
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_partbase) == 4u, "WIDTHHOLD partbase must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_partsize) == 4u, "WIDTHHOLD partsize must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_partdiag) == 4u, "WIDTHHOLD partdiag must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_last_lba) == 4u, "WIDTHHOLD last_lba must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_last_start) == 4u, "WIDTHHOLD last_start must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_last_actual) == 4u, "WIDTHHOLD last_actual must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_fail_actual) == 4u, "WIDTHHOLD fail_actual must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthhold_fail_start) == 4u, "WIDTHHOLD fail_start must stay u32");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUB_CASES ==
    (GTA4L_EMMC_BLOCK_WIDTHPUB_PASSES *
    GTA4L_EMMC_BLOCK_WIDTHPUB_CASE_OFFSETS),
    "WIDTHPUB case count must cover both passes");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUB_CASE_OFFSETS ==
    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES,
    "WIDTHPUB must repeat the widthhold case offsets");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUB_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHPUB line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUBREAD_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHPUBREAD line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUBFAIL_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHPUBFAIL line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_WIDTHPUB_REPORT_WORST_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "WIDTHPUB worst case must fit the report buffer");
_Static_assert((GTA4L_EMMC_BLOCK_WIDTHPUB_LINE_BYTES +
    GTA4L_EMMC_BLOCK_WIDTHPUBREAD_LINE_BYTES) <
    GTA4L_EMMC_BLOCK_WIDTHPUB_REPORT_WORST_BYTES,
    "WIDTHPUB worst case must cover both detail lines");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthpub) == 4u, "WIDTHPUB mode field must stay u32");
_Static_assert(sizeof(((struct gta4l_emmc_block_report *)0)->
    widthpub_pub) == 4u, "WIDTHPUB pub field must stay u32");
_Static_assert(GTA4L_EMMC_BLOCK_SPEED_LINE_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "SPEED line must fit the report buffer");
_Static_assert(GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_BYTES <=
    GTA4L_EMMC_BLOCK_REPORT_BYTES,
    "SPEED worst case must fit the report buffer");

static int
gta4l_emmc_block_is_space(char value)
{
	return (value == ' ') || (value == '\t') || (value == '\n') ||
	    (value == '\r');
}

uint32_t
gta4l_emmc_block_token_count(const char *arguments, const char *name)
{
	uint32_t count = 0;
	uint32_t index = 0;
	uint32_t name_length = 0;

	if ((arguments == 0) || (name == 0) || (name[0] == 0)) {
		return 0;
	}
	while (name[name_length] != 0) {
		name_length++;
	}
	while (arguments[index] != 0) {
		uint32_t start;
		uint32_t offset;

		while (gta4l_emmc_block_is_space(arguments[index])) {
			index++;
		}
		start = index;
		while ((arguments[index] != 0) &&
		    !gta4l_emmc_block_is_space(arguments[index])) {
			index++;
		}
		if (index - start <= name_length) {
			continue;
		}
		for (offset = 0; offset < name_length; offset++) {
			if (arguments[start + offset] != name[offset]) {
				break;
			}
		}
		if ((offset == name_length) &&
		    (arguments[start + name_length] == '=')) {
			count++;
		}
	}
	return count;
}

int
gta4l_emmc_block_mode_select(int arg_present, int arg_value,
    int probe_present, int other_conflict, int duplicate_tokens)
{
	if ((arg_present == 0) || (arg_value == GTA4L_EMMC_BLOCK_MODE_OFF)) {
		return GTA4L_EMMC_BLOCK_SELECT_INERT;
	}
	if ((duplicate_tokens != 0) || (probe_present != 0) ||
	    (other_conflict != 0)) {
		return GTA4L_EMMC_BLOCK_SELECT_REFUSED;
	}
	if (arg_value == GTA4L_EMMC_BLOCK_MODE_REPORT) {
		return GTA4L_EMMC_BLOCK_SELECT_REPORT;
	}
	if (arg_value == GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) {
		return GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED;
	}
	/* Publish-only mode 2 is deferred, and any other value is malformed.
	 * Both are explicit refusals rather than a silent fallback. */
	return GTA4L_EMMC_BLOCK_SELECT_REFUSED;
}

int
gta4l_emmc_block_partobs_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_PARTOBS_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_PARTOBS_ON;
}

int
gta4l_emmc_block_partcmp_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_PARTCMP_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0) || (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_PARTCMP_ON;
}

int
gta4l_emmc_block_partread_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_PARTREAD_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_PARTREAD_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_PARTREAD_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_PARTREAD_ON;
}

int
gta4l_emmc_block_deblock_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_DEBLOCK_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_DEBLOCK_ON;
}

int
gta4l_emmc_block_breakup_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int deblock_selected,
    int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_BREAKUP_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_BREAKUP_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_BREAKUP_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_BREAKUP_ON;
}

int
gta4l_emmc_block_sweep_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected,
    int partcmp_selected, int partread_selected, int deblock_selected,
    int breakup_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_SWEEP_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_SWEEP_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_SWEEP_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_SWEEP_ON;
}

int
gta4l_emmc_block_consume_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_CONSUME_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (sweep_selected != 0) ||
	    (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_CONSUME_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_CONSUME_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_CONSUME_ON;
}

int
gta4l_emmc_block_width_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_WIDTH_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (sweep_selected != 0) ||
	    (consume_selected != 0) || (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_WIDTH_REFUSED;
	}
	if (token_value != (int)GTA4L_EMMC_BLOCK_WIDTH_BITS_4) {
		return GTA4L_EMMC_BLOCK_WIDTH_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_WIDTH_ON;
}

int
gta4l_emmc_block_widthhold_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_WIDTHHOLD_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (sweep_selected != 0) ||
	    (consume_selected != 0) || (width_selected != 0) ||
	    (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;
	}
	if (token_value != (int)GTA4L_EMMC_BLOCK_WIDTHHOLD_ON) {
		return GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_WIDTHHOLD_ON;
}

int
gta4l_emmc_block_widthpub_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int widthhold_selected,
    int speed_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_WIDTHPUB_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (sweep_selected != 0) ||
	    (consume_selected != 0) || (width_selected != 0) ||
	    (widthhold_selected != 0) || (speed_selected != 0)) {
		return GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;
	}
	if (token_value != (int)GTA4L_EMMC_BLOCK_WIDTHPUB_ON) {
		return GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_WIDTHPUB_ON;
}

int
gta4l_emmc_block_speed_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int block_value,
    int partobs_selected, int partcmp_selected, int partread_selected,
    int deblock_selected, int breakup_selected, int sweep_selected,
    int consume_selected, int width_selected, int widthhold_selected,
    int widthpub_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_SPEED_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||
	    (partobs_selected != 0) || (partcmp_selected != 0) ||
	    (partread_selected != 0) || (deblock_selected != 0) ||
	    (breakup_selected != 0) || (sweep_selected != 0) ||
	    (consume_selected != 0) || (width_selected != 0) ||
	    (widthhold_selected != 0) || (widthpub_selected != 0)) {
		return GTA4L_EMMC_BLOCK_SPEED_REFUSED;
	}
	if (token_value != (int)GTA4L_EMMC_BLOCK_SPEED_ON) {
		return GTA4L_EMMC_BLOCK_SPEED_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_SPEED_ON;
}

static struct gta4l_emmc_block_deblock_case gta4l_emmc_block_deblock_cases[] = {
	/* Head-only: the first 100 bytes of the block are the deblocker's
	 * excess-start scratch and are discarded. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 924u, 100u,
		{
			0x5f, 0x4f, 0x36, 0x3c, 0xaf, 0xbe, 0xde, 0x86,
			0xc6, 0x46, 0x43, 0xa6, 0xda, 0xa0, 0xb3, 0xdb,
			0x7e, 0xdc, 0xcd, 0x83, 0xb0, 0x13, 0xeb, 0x62,
			0x9f, 0x2f, 0x95, 0x6a, 0x30, 0x1d, 0x37, 0x19,
		},
	},
	/* Tail-only: the aligned start takes the direct path. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 600u, 0u,
		{
			0xe3, 0xef, 0x99, 0x6d, 0xf6, 0xc4, 0x22, 0x66,
			0x79, 0x98, 0x86, 0xce, 0x59, 0x02, 0x87, 0x68,
			0x71, 0x57, 0xc4, 0xc9, 0x20, 0x35, 0x7a, 0x0f,
			0xb1, 0x15, 0x81, 0x0b, 0x07, 0xd5, 0x9d, 0x01,
		},
	},
	/* Both ends inside one block: _requestIsOneBlock. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 200u, 100u,
		{
			0x6d, 0x9c, 0x54, 0xde, 0xe5, 0x66, 0x0c, 0x46,
			0x88, 0x6f, 0x32, 0xd8, 0x0e, 0x57, 0xe9, 0xdd,
			0x0f, 0xfa, 0x57, 0xee, 0x0c, 0xd2, 0xa7, 0x62,
			0xb0, 0x36, 0xd9, 0xc8, 0xe0, 0xc3, 0xa3, 0x3a,
		},
	},
	/* Both ends across two blocks. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 600u, 100u,
		{
			0x06, 0x2e, 0x84, 0xac, 0x7f, 0x7b, 0x5b, 0xcd,
			0xfb, 0xbd, 0x30, 0xb5, 0x88, 0x3b, 0xfd, 0xf1,
			0x88, 0xdb, 0xf2, 0x47, 0xab, 0xd7, 0x8e, 0xd9,
			0xfd, 0xb5, 0xbf, 0xb8, 0xd9, 0x0d, 0xae, 0x55,
		},
	},
	/* Both ends across a multi-page 32-block span. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 16000u, 100u,
		{
			0x4f, 0x49, 0x9b, 0xf0, 0xf0, 0x58, 0x6d, 0x0b,
			0x2b, 0x29, 0xf0, 0xdf, 0x7a, 0x64, 0xdd, 0x84,
			0x98, 0x56, 0x69, 0x8b, 0xc7, 0x19, 0x4e, 0x93,
			0xb8, 0x44, 0xbb, 0x3e, 0x19, 0xcd, 0x0f, 0xe2,
		},
	},
	/* Aligned control: no deblocker is constructed. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_WHOLE, 1024u, 512u,
		{
			0x4e, 0x35, 0x75, 0xfe, 0xb7, 0x62, 0xc8, 0xfe,
			0xce, 0x37, 0x0e, 0x60, 0x5a, 0x83, 0x34, 0xe8,
			0xbf, 0xe1, 0xf8, 0x48, 0xf7, 0x2e, 0x73, 0xed,
			0x54, 0xa4, 0xb2, 0xe1, 0x47, 0x4d, 0x1f, 0x70,
		},
	},
	/* Boot base plus a head-only read. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT, 924u, 4196u,
		{
			0xff, 0x34, 0x6b, 0x8d, 0xdf, 0xd7, 0xc9, 0x82,
			0xd4, 0x03, 0xc1, 0x28, 0x16, 0xaf, 0x31, 0x06,
			0x4d, 0x8b, 0x80, 0x03, 0xd9, 0x89, 0x1c, 0x02,
			0x93, 0xd4, 0xfd, 0x67, 0x3e, 0x36, 0xd7, 0xd1,
		},
	},
	/* Boot base plus a same-block read. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT, 200u, 1048676u,
		{
			0xcd, 0xb2, 0xd0, 0xc4, 0x75, 0x5f, 0x34, 0x23,
			0x93, 0x0b, 0x44, 0x25, 0x76, 0x53, 0xbc, 0x60,
			0x12, 0x26, 0xb3, 0x47, 0x96, 0x66, 0x16, 0x6d,
			0x66, 0xa0, 0x8b, 0x78, 0xf6, 0xb7, 0xbf, 0x53,
		},
	},
	/* Boot base plus the retained 64-block tail; the last 100 bytes are
	 * the deblocker's excess-final scratch and are discarded. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT, 32668u, 100630528u,
		{
			0x09, 0x5a, 0x65, 0x73, 0xcb, 0x4a, 0xe8, 0xd8,
			0x55, 0x08, 0x0b, 0x4f, 0x46, 0xc1, 0xe7, 0xb1,
			0x32, 0xbc, 0x3f, 0x9a, 0x69, 0x0b, 0xc1, 0x42,
			0x89, 0x8f, 0x4a, 0x05, 0x5b, 0xf4, 0xcc, 0x63,
		},
	},
	/* Boot base plus a same-block read at the media end. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT, 200u, 100662884u,
		{
			0x6d, 0x9c, 0x54, 0xde, 0xe5, 0x66, 0x0c, 0x46,
			0x88, 0x6f, 0x32, 0xd8, 0x0e, 0x57, 0xe9, 0xdd,
			0x0f, 0xfa, 0x57, 0xee, 0x0c, 0xd2, 0xa7, 0x62,
			0xb0, 0x36, 0xd9, 0xc8, 0xe0, 0xc3, 0xa3, 0x3a,
		},
	},
	/* Boot base plus an aligned control. */
	{
		GTA4L_EMMC_BLOCK_DEBLOCK_TARGET_BOOT, 1024u, 4096u,
		{
			0xb5, 0x0a, 0xab, 0x9d, 0x52, 0xe4, 0xd1, 0xe3,
			0x60, 0x78, 0xfa, 0x51, 0x20, 0xe3, 0xaa, 0x1c,
			0xc5, 0x1e, 0xb4, 0x75, 0x45, 0x4b, 0xcc, 0x7f,
			0x79, 0xec, 0xa1, 0xbd, 0xaa, 0x95, 0x97, 0x74,
		},
	},
};

static struct gta4l_emmc_block_breakup_case gta4l_emmc_block_breakup_cases[] = {
	/* Aligned control: the eight-block request is at most one break. */
	{
		GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 4096u,
		4096u, 8u, 1u, 8u,
		{
			0x58, 0x78, 0xbc, 0xf8, 0xa6, 0xaf, 0xe4, 0xdd,
			0x79, 0xa0, 0x02, 0xca, 0xa8, 0xb6, 0xae, 0xea,
			0xe1, 0x61, 0x25, 0xb8, 0x90, 0xab, 0x75, 0x0b,
			0x67, 0xd6, 0xab, 0x3c, 0x25, 0xf3, 0xb1, 0xb2,
		},
	},
	/* Aligned split: sixteen exact eight-block stages. */
	{
		GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 65536u,
		65536u, 128u, 16u, 8u,
		{
			0xb4, 0xc3, 0xc6, 0x43, 0xe5, 0xa4, 0x3d, 0x82,
			0xa5, 0x2e, 0xc8, 0xc1, 0x01, 0xba, 0xcf, 0xa5,
			0x12, 0x10, 0x24, 0x4d, 0x7b, 0xf4, 0x0e, 0x97,
			0xef, 0x14, 0x36, 0x44, 0x78, 0x80, 0xfa, 0x9c,
		},
	},
	/* Aligned split: fifteen full stages and a seven-block final stage. */
	{
		GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4096u, 65024u,
		65024u, 127u, 16u, 8u,
		{
			0x5f, 0x97, 0x04, 0x72, 0x40, 0x2e, 0x70, 0xb5,
			0x4b, 0xb0, 0xf8, 0xb4, 0x94, 0x5f, 0x5f, 0x02,
			0x0d, 0x54, 0x79, 0x8b, 0x42, 0xca, 0x32, 0xa2,
			0x17, 0x42, 0xa4, 0x57, 0x0f, 0x61, 0xa9, 0x5c,
		},
	},
	/* Unaligned split through the deblocker: head 100, tail 412. */
	{
		GTA4L_EMMC_BLOCK_BREAKUP_TARGET_BOOT, 4196u, 65024u,
		65536u, 128u, 16u, 8u,
		{
			0xbc, 0x0b, 0x9b, 0xb9, 0xc6, 0xdd, 0x11, 0xc3,
			0xae, 0xa6, 0x65, 0xd0, 0x1d, 0xc5, 0xf1, 0x08,
			0x14, 0xa4, 0x60, 0xd4, 0x1e, 0xb7, 0x7e, 0x0a,
			0x7a, 0x01, 0xbb, 0x2b, 0xe7, 0x26, 0xdb, 0x17,
		},
	},
};

static struct gta4l_emmc_block_sweep_case gta4l_emmc_block_sweep_cases[] = {
	/* Anchor A0: the pinned aligned direct control. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 0u, 4096u,
		4096u, 4096u, 8u, 1u, 8u,
		{
			0x58, 0x78, 0xbc, 0xf8, 0xa6, 0xaf, 0xe4, 0xdd,
			0x79, 0xa0, 0x02, 0xca, 0xa8, 0xb6, 0xae, 0xea,
			0xe1, 0x61, 0x25, 0xb8, 0x90, 0xab, 0x75, 0x0b,
			0x67, 0xd6, 0xab, 0x3c, 0x25, 0xf3, 0xb1, 0xb2,
		},
	},
	/* Anchor A1: the pinned aligned split control. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 0u, 4096u,
		16384u, 16384u, 32u, 4u, 8u,
		{
			0xfa, 0x43, 0xaa, 0x5d, 0x40, 0x59, 0x1e, 0x9b,
			0x61, 0x9c, 0x99, 0xc4, 0xe4, 0x90, 0x05, 0x4f,
			0xea, 0xc8, 0x5a, 0xc5, 0x45, 0xf5, 0xc8, 0x4a,
			0x38, 0xf8, 0xaa, 0x04, 0x82, 0x07, 0x82, 0xda,
		},
	},
	/* R0: 32 blocks / 16,384 bytes, four subrequests. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 1u, 0u,
		16384u, 16384u, 32u, 4u, 8u,
		{ 0 },
	},
	/* R1: 16 blocks / 8,192 bytes, two subrequests. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 1u, 0u,
		8192u, 8192u, 16u, 2u, 8u,
		{ 0 },
	},
	/* R2: 8 blocks / 4,096 bytes, one subrequest. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 1u, 0u,
		4096u, 4096u, 8u, 1u, 8u,
		{ 0 },
	},
	/* R3: 8 blocks / 4,096 bytes, one subrequest. */
	{
		GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT, 1u, 0u,
		4096u, 4096u, 8u, 1u, 8u,
		{ 0 },
	},
};

static struct gta4l_emmc_block_partread_case gta4l_emmc_block_partread_cases[] = {
	/* The GPT primary copy: PMBR, header and the 128-entry array. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 34u, 0u, 0u,
		{
			0x0c, 0x7e, 0x9c, 0x03, 0xce, 0x25, 0x23, 0x2f,
			0x6a, 0xc1, 0xe5, 0x88, 0x26, 0x8d, 0xcd, 0xa1,
			0x62, 0xea, 0x82, 0xbf, 0xf3, 0x31, 0xba, 0xe9,
			0x06, 0x99, 0x65, 0xd5, 0xfe, 0x71, 0x8b, 0xef,
		},
	},
	/* The GPT backup copy: the last 34 sectors of the whole media. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 34u, 34u, 0u,
		{
			0x41, 0x91, 0x2e, 0x59, 0xe6, 0x6e, 0xe2, 0xc9,
			0xa4, 0x35, 0xfe, 0x17, 0x25, 0x0d, 0x20, 0xc8,
			0x6e, 0xe5, 0xd7, 0xcd, 0x62, 0xe5, 0xbd, 0xd7,
			0xf6, 0x29, 0x84, 0xa0, 0x51, 0x0c, 0x20, 0x20,
		},
	},
	/* PMBR only, so a primary-copy mismatch localizes to the first sector. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 1u, 0u, 0u,
		{
			0xa4, 0x83, 0xd5, 0x93, 0xb0, 0x74, 0xc9, 0xe7,
			0xda, 0x9d, 0x7c, 0x43, 0x4c, 0x90, 0x03, 0x8b,
			0xf5, 0x18, 0x2e, 0xb1, 0xe6, 0x21, 0x5b, 0x2b,
			0x69, 0x4b, 0x33, 0x3b, 0x5c, 0xb9, 0xb6, 0x99,
		},
	},
	/* Primary GPT header only. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 1u, 0u,
		1u * GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES,
		{
			0xef, 0xd3, 0x55, 0x6b, 0x76, 0x1b, 0xf5, 0x02,
			0x7d, 0x5c, 0xc6, 0xb0, 0xc4, 0xb7, 0xd3, 0xe6,
			0x9d, 0x05, 0xc2, 0x1b, 0x99, 0x1b, 0x9c, 0xd5,
			0xff, 0x06, 0x38, 0xa6, 0xb1, 0x18, 0x23, 0xac,
		},
	},
	/* The full 128-entry primary array. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 32u, 0u,
		2u * GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES,
		{
			0xec, 0xde, 0x9f, 0x6f, 0xc8, 0xd5, 0x8b, 0xb6,
			0xc7, 0x49, 0xbf, 0x1d, 0x7f, 0xe4, 0xa6, 0xde,
			0x15, 0xfe, 0x85, 0x0e, 0x64, 0x37, 0xc2, 0xb9,
			0xcf, 0x46, 0x02, 0x20, 0x46, 0xe2, 0x72, 0xaa,
		},
	},
	/* The first usable LBA after the primary array. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 1u, 0u,
		34u * GTA4L_EMMC_BLOCK_PARTREAD_SECTOR_BYTES,
		{
			0x2d, 0xdc, 0x82, 0xb7, 0xb3, 0xfb, 0x5b, 0x8b,
			0x90, 0xc0, 0xe6, 0x10, 0xd2, 0xb6, 0xea, 0xe0,
			0x6f, 0x60, 0x03, 0x30, 0x94, 0x67, 0xac, 0x40,
			0x55, 0x1e, 0xdd, 0xff, 0xd8, 0x2c, 0x0e, 0xeb,
		},
	},
	/* The full backup array, identical to the primary array. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 32u, 33u, 0u,
		{
			0xec, 0xde, 0x9f, 0x6f, 0xc8, 0xd5, 0x8b, 0xb6,
			0xc7, 0x49, 0xbf, 0x1d, 0x7f, 0xe4, 0xa6, 0xde,
			0x15, 0xfe, 0x85, 0x0e, 0x64, 0x37, 0xc2, 0xb9,
			0xcf, 0x46, 0x02, 0x20, 0x46, 0xe2, 0x72, 0xaa,
		},
	},
	/* The last sector: the backup GPT header. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_WHOLE, 1u, 1u, 0u,
		{
			0x5e, 0xa6, 0xf4, 0xbf, 0x15, 0x18, 0x79, 0x0b,
			0x25, 0xd6, 0x59, 0xce, 0xef, 0xb5, 0x6f, 0xe5,
			0x88, 0xf3, 0x57, 0xdc, 0x43, 0xe9, 0xe5, 0xd5,
			0x53, 0x89, 0x26, 0x2a, 0x36, 0x08, 0x9b, 0x48,
		},
	},
	/* The untouched boot-partition tail, maximum 64-block request. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 64u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 32768u,
		{
			0xef, 0x31, 0x3f, 0x71, 0x2a, 0x8f, 0x43, 0x6e,
			0x98, 0x9c, 0xe7, 0x37, 0x66, 0xb4, 0x9c, 0xc6,
			0x57, 0xf2, 0x71, 0x3d, 0xa0, 0x1a, 0xe9, 0x4f,
			0x1c, 0xa1, 0x6e, 0x62, 0x00, 0x5c, 0x21, 0x50,
		},
	},
	/* Odd three-block tail. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 3u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 1536u,
		{
			0x02, 0xac, 0xbb, 0x3a, 0x7b, 0x14, 0xae, 0x99,
			0x29, 0xeb, 0x10, 0xcd, 0x1a, 0xd7, 0xba, 0x59,
			0x00, 0xd0, 0x95, 0x2c, 0x78, 0xc6, 0x49, 0x92,
			0x01, 0x45, 0x3c, 0xd0, 0xba, 0xfd, 0x8b, 0xba,
		},
	},
	/* Even two-block tail. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 2u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 1024u,
		{
			0xd7, 0x37, 0x41, 0x4c, 0x43, 0x3e, 0xa3, 0xc7,
			0x47, 0x50, 0xf3, 0x80, 0xca, 0x18, 0xbd, 0x06,
			0xd7, 0x67, 0x53, 0xcc, 0xe4, 0xc3, 0xa1, 0x07,
			0x1e, 0x66, 0x84, 0x1a, 0xec, 0x60, 0x8a, 0xb0,
		},
	},
	/* Singleton tail sector. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 1u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES - 512u,
		{
			0x40, 0x00, 0x39, 0xfd, 0x62, 0xda, 0xf3, 0x49,
			0x9b, 0x37, 0x7c, 0xed, 0x75, 0x42, 0x89, 0x18,
			0x05, 0x21, 0xfa, 0x7a, 0x86, 0x30, 0x66, 0xe2,
			0xea, 0xd1, 0xf5, 0x1b, 0x67, 0x8a, 0x77, 0x3e,
		},
	},
	/* The boot-image kernel section starts at page 1; the boot-image gate
	 * keeps this section byte-identical to the hardware-tested baseline,
	 * so both kernel digests are invariant across candidates. */
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 64u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_KERNEL_OFFSET,
		{
			0x97, 0x9d, 0xf1, 0xff, 0x23, 0x02, 0x41, 0xb4,
			0x38, 0x62, 0xf8, 0x90, 0x7c, 0x3f, 0x42, 0xd9,
			0x7e, 0x25, 0xed, 0xe7, 0x22, 0x2f, 0x41, 0x00,
			0x7e, 0xda, 0xcb, 0x42, 0x29, 0x0e, 0x20, 0x93,
		},
	},
	{
		GTA4L_EMMC_BLOCK_PARTREAD_TARGET_BOOT, 64u, 0u,
		GTA4L_EMMC_BLOCK_PARTREAD_BOOT_KERNEL_MID_OFFSET,
		{
			0x05, 0x0f, 0x2b, 0x0d, 0x7b, 0x20, 0xcc, 0xb5,
			0x8c, 0xc4, 0x29, 0x9d, 0x18, 0xdd, 0xe5, 0x72,
			0x41, 0x52, 0x5d, 0xa3, 0x3e, 0x11, 0xa4, 0xef,
			0x71, 0x17, 0x55, 0x14, 0x19, 0x2a, 0x12, 0x02,
		},
	},
};

uint32_t
gta4l_emmc_block_partread_case_count(void)
{
	return (uint32_t)(sizeof(gta4l_emmc_block_partread_cases) /
	    sizeof(gta4l_emmc_block_partread_cases[0]));
}

const struct gta4l_emmc_block_partread_case *
gta4l_emmc_block_partread_case_at(uint32_t index)
{
	if (index >= gta4l_emmc_block_partread_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_partread_cases[index];
}

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_partread_case *
gta4l_emmc_block_partread_case_mutable(uint32_t index)
{
	if (index >= gta4l_emmc_block_partread_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_partread_cases[index];
}
#endif

uint32_t
gta4l_emmc_block_deblock_case_count(void)
{
	return (uint32_t)(sizeof(gta4l_emmc_block_deblock_cases) /
	    sizeof(gta4l_emmc_block_deblock_cases[0]));
}

const struct gta4l_emmc_block_deblock_case *
gta4l_emmc_block_deblock_case_at(uint32_t index)
{
	if (index >= gta4l_emmc_block_deblock_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_deblock_cases[index];
}

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_deblock_case *
gta4l_emmc_block_deblock_case_mutable(uint32_t index)
{
	if (index >= gta4l_emmc_block_deblock_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_deblock_cases[index];
}
#endif

uint32_t
gta4l_emmc_block_breakup_case_count(void)
{
	return (uint32_t)(sizeof(gta4l_emmc_block_breakup_cases) /
	    sizeof(gta4l_emmc_block_breakup_cases[0]));
}

const struct gta4l_emmc_block_breakup_case *
gta4l_emmc_block_breakup_case_at(uint32_t index)
{
	if (index >= gta4l_emmc_block_breakup_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_breakup_cases[index];
}

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_breakup_case *
gta4l_emmc_block_breakup_case_mutable(uint32_t index)
{
	if (index >= gta4l_emmc_block_breakup_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_breakup_cases[index];
}
#endif

int
gta4l_emmc_block_breakup_geometry(uint64_t start, uint32_t length,
    uint32_t *span, uint32_t *blocks, uint32_t *subrequests,
    uint32_t *max_blocks)
{
	uint64_t first;
	uint64_t last;
	uint64_t aligned;
	uint64_t count;

	if ((span == 0) || (blocks == 0) || (subrequests == 0) ||
	    (max_blocks == 0) || (length == 0)) {
		return -1;
	}
	first = start / GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
	last = (start + length +
	    (GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES - 1u)) /
	    GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
	if (last <= first) {
		return -1;
	}
	aligned = (last - first) * GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
	if (aligned > GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES) {
		return -1;
	}
	count = aligned / GTA4L_EMMC_BLOCK_BREAKUP_SECTOR_BYTES;
	if ((count == 0) || (count > GTA4L_EMMC_BLOCK_BREAKUP_MAX_BLOCKS)) {
		return -1;
	}
	*span = (uint32_t)aligned;
	*blocks = (uint32_t)count;
	*subrequests = (uint32_t)((count +
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS - 1u) /
	    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS);
	if (*subrequests > GTA4L_EMMC_BLOCK_BREAKUP_MAX_SUBREQUESTS) {
		return -1;
	}
	*max_blocks = (count < GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS) ?
	    (uint32_t)count : GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS;
	return 0;
}

uint32_t
gta4l_emmc_block_sweep_case_count(void)
{
	return (uint32_t)(sizeof(gta4l_emmc_block_sweep_cases) /
	    sizeof(gta4l_emmc_block_sweep_cases[0]));
}

const struct gta4l_emmc_block_sweep_case *
gta4l_emmc_block_sweep_case_at(uint32_t index)
{
	if (index >= gta4l_emmc_block_sweep_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_sweep_cases[index];
}

#ifdef GTA4L_EMMC_BLOCK_HOST_TEST
struct gta4l_emmc_block_sweep_case *
gta4l_emmc_block_sweep_case_mutable(uint32_t index)
{
	if (index >= gta4l_emmc_block_sweep_case_count()) {
		return 0;
	}
	return &gta4l_emmc_block_sweep_cases[index];
}
#endif

int
gta4l_emmc_block_sweep_derive(uint32_t runtime_index,
    uint32_t window_blocks, uint32_t capacity_blocks,
    uint64_t *start_block)
{
	uint64_t slots;

	if ((start_block == 0) || (window_blocks == 0) ||
	    (window_blocks > GTA4L_EMMC_BLOCK_SWEEP_MAX_BLOCKS) ||
	    ((window_blocks % 8u) != 0u) ||
	    ((capacity_blocks % 8u) != 0u) ||
	    (capacity_blocks < window_blocks)) {
		return -1;
	}
	slots = ((uint64_t)capacity_blocks - window_blocks) / 8u;
	if (slots == 0u) {
		return -1;
	}
	*start_block = ((((uint64_t)runtime_index + 1u) *
	    GTA4L_EMMC_BLOCK_SWEEP_DERIVE_MULTIPLIER) % slots) * 8u;
	return 0;
}

int
gta4l_emmc_block_sweep_validate(
    const struct gta4l_emmc_block_sweep_case *item, uint64_t start,
    uint32_t capacity_blocks, uint64_t boot_base_bytes)
{
	uint64_t start_block;
	uint64_t end_block;
	uint64_t end_bytes;
	uint64_t expected_subrequests;
	uint64_t expected_max;
	uint64_t length;

	if (item == 0) {
		return -1;
	}
	if (item->target != GTA4L_EMMC_BLOCK_SWEEP_TARGET_BOOT) {
		return -1;
	}
	if ((item->blocks == 0u) ||
	    (item->blocks > GTA4L_EMMC_BLOCK_SWEEP_MAX_BLOCKS) ||
	    ((item->blocks % 8u) != 0u)) {
		return -1;
	}
	length = (uint64_t)item->blocks *
	    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
	if ((uint64_t)item->length != length) {
		return -1;
	}
	if ((item->span != item->length) ||
	    ((start % GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES) != 0u)) {
		return -1;
	}
	expected_subrequests = (item->blocks +
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS - 1u) /
	    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS;
	expected_max = (item->blocks < GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS) ?
	    item->blocks : GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS;
	if ((item->subrequests != expected_subrequests) ||
	    (item->max_blocks != expected_max)) {
		return -1;
	}
	if ((capacity_blocks % 8u) != 0u) {
		return -1;
	}
	if (capacity_blocks < item->blocks) {
		return -1;
	}
	if ((((uint64_t)capacity_blocks - item->blocks) / 8u) == 0u) {
		return -1;
	}
	start_block = start / GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;
	if (start_block > (~0ull - item->blocks)) {
		return -1;
	}
	end_block = start_block + item->blocks;
	if (end_block > capacity_blocks) {
		return -1;
	}
	if (start > (~0ull - length)) {
		return -1;
	}
	end_bytes = start + length;
	if (end_bytes > (~0ull - boot_base_bytes)) {
		return -1;
	}
	if ((end_block > (~0ull /
	    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES)) ||
	    ((end_block * GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES) >
	    (~0ull - boot_base_bytes))) {
		return -1;
	}
	return 0;
}

int
gta4l_emmc_block_consume_validate(uint64_t start, uint32_t length,
    uint64_t boot_size)
{
	if (start != 0u) {
		return -1;
	}
	if (length != GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES) {
		return -1;
	}
	if ((length % GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES) != 0u) {
		return -1;
	}
	if ((boot_size % GTA4L_EMMC_BLOCK_CONSUME_SECTOR_BYTES) != 0u) {
		return -1;
	}
	if (start > (~0ull - length)) {
		return -1;
	}
	if ((start + length) > boot_size) {
		return -1;
	}
	return 0;
}

enum gta4l_emmc_block_request_result
gta4l_emmc_block_request_check(uint32_t capacity_blocks, uint64_t block,
    uint64_t nblks, uint64_t buffer_bytes, uint32_t direction, uint8_t online)
{
	if (direction == GTA4L_EMMC_BLOCK_DIRECTION_WRITE) {
		return GTA4L_EMMC_BLOCK_REQUEST_NOT_WRITABLE;
	}
	if (direction != GTA4L_EMMC_BLOCK_DIRECTION_READ) {
		return GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT;
	}
	if (nblks == 0) {
		return GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT;
	}
	if (block >= (uint64_t)capacity_blocks) {
		return GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT;
	}
	if (nblks > (uint64_t)capacity_blocks - block) {
		return GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT;
	}
	if (nblks > GTA4L_EMMC_BLOCK_MAX_BLOCKS) {
		return GTA4L_EMMC_BLOCK_REQUEST_TOO_LARGE;
	}
	if (nblks * GTA4L_EMMC_BLOCK_BYTES > buffer_bytes) {
		return GTA4L_EMMC_BLOCK_REQUEST_BAD_ARGUMENT;
	}
	if (online == 0) {
		return GTA4L_EMMC_BLOCK_REQUEST_NO_MEDIA;
	}
	return GTA4L_EMMC_BLOCK_REQUEST_OK;
}

uint32_t
gta4l_emmc_block_sum(const uint8_t *data, uint32_t length)
{
	uint32_t sum = 0;
	uint32_t index;

	if (data == 0) {
		return 0;
	}
	for (index = 0; index < length; index++) {
		sum = (sum * 31u) + data[index];
	}
	return sum;
}

uint32_t
gta4l_emmc_block_gpt_entries(const uint8_t *sector, uint32_t length)
{
	uint32_t entries;
	uint32_t entry_size;

	if ((sector == 0) || (length < 0x58u)) {
		return 0;
	}
	if ((sector[0] != 'E') || (sector[1] != 'F') ||
	    (sector[2] != 'I') || (sector[3] != ' ')) {
		return 0;
	}
	if ((sector[4] != 'P') || (sector[5] != 'A') ||
	    (sector[6] != 'R') || (sector[7] != 'T')) {
		return 0;
	}
	entries = (uint32_t)sector[0x50u] |
	    ((uint32_t)sector[0x51u] << 8) |
	    ((uint32_t)sector[0x52u] << 16) |
	    ((uint32_t)sector[0x53u] << 24);
	entry_size = (uint32_t)sector[0x54u] |
	    ((uint32_t)sector[0x55u] << 8) |
	    ((uint32_t)sector[0x56u] << 16) |
	    ((uint32_t)sector[0x57u] << 24);
	if ((entries == 0) || (entries > 1024u) || (entry_size != 128u)) {
		return 0;
	}
	return entries;
}

int
gta4l_emmc_block_media_check(uint64_t base, uint64_t size,
    uint32_t capacity_blocks, int whole, int writable)
{
	int result = 0;

	if (capacity_blocks == 0) {
		result |= 0x10;
	}
	if (whole == 0) {
		result |= 0x01;
	}
	if (base != 0) {
		result |= 0x02;
	}
	if ((capacity_blocks == 0) ||
	    (size != (uint64_t)capacity_blocks * GTA4L_EMMC_BLOCK_BYTES)) {
		result |= 0x04;
	}
	if (writable != 0) {
		result |= 0x08;
	}
	return result;
}

static uint32_t
gta4l_emmc_block_get32(const uint8_t *data)
{
	return (uint32_t)data[0] |
	    ((uint32_t)data[1] << 8) |
	    ((uint32_t)data[2] << 16) |
	    ((uint32_t)data[3] << 24);
}

static uint64_t
gta4l_emmc_block_get64(const uint8_t *data)
{
	return (uint64_t)gta4l_emmc_block_get32(data) |
	    ((uint64_t)gta4l_emmc_block_get32(data + 4u) << 32);
}

uint32_t
gta4l_emmc_block_crc32(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xffffffffu;
	uint32_t index;
	uint32_t bit;

	if (data == 0) {
		return 0;
	}
	for (index = 0; index < length; index++) {
		crc ^= data[index];
		for (bit = 0; bit < 8u; bit++) {
			if ((crc & 1u) != 0) {
				crc = (crc >> 1) ^ 0xedb88320u;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc ^ 0xffffffffu;
}

static int
gta4l_emmc_block_pmbr_valid(const uint8_t *pmbr, uint32_t capacity_blocks)
{
	uint32_t index;
	uint32_t protective = 0;

	if (pmbr == 0) {
		return 0;
	}
	if ((pmbr[510] != 0x55u) || (pmbr[511] != 0xaau)) {
		return 0;
	}
	for (index = 0; index < 4u; index++) {
		const uint8_t *record = pmbr + 446u + (index * 16u);

		if (record[4] == 0xeeu) {
			uint32_t start = gta4l_emmc_block_get32(record + 8u);
			uint32_t sectors = gta4l_emmc_block_get32(
			    record + 12u);

			if (start != 1u) {
				return 0;
			}
			if ((sectors == 0u) ||
			    (sectors > capacity_blocks)) {
				return 0;
			}
			protective++;
		} else if (record[4] != 0u) {
			return 0;
		}
	}
	return (protective == 1u) ? 1 : 0;
}

struct gta4l_emmc_block_gpt_header {
	uint64_t my_lba;
	uint64_t alternate_lba;
	uint64_t first_usable;
	uint64_t last_usable;
	uint64_t disk_guid_low;
	uint64_t disk_guid_high;
	uint64_t entry_lba;
	uint32_t slots;
	uint32_t entry_size;
	uint32_t entry_crc;
	uint32_t array_blocks;
};

static int
gta4l_emmc_block_gpt_header_parse(const uint8_t *header,
    uint32_t capacity_blocks, uint64_t expected_my_lba,
    uint64_t expected_alternate_lba, int primary,
    struct gta4l_emmc_block_gpt_header *out)
{
	uint8_t copy[92];
	uint32_t index;

	if ((header == 0) || (out == 0) || (capacity_blocks < 3u)) {
		return 0;
	}
	if ((header[0] != 'E') || (header[1] != 'F') ||
	    (header[2] != 'I') || (header[3] != ' ') ||
	    (header[4] != 'P') || (header[5] != 'A') ||
	    (header[6] != 'R') || (header[7] != 'T')) {
		return 0;
	}
	if ((gta4l_emmc_block_get32(header + 8u) != 0x00010000u) ||
	    (gta4l_emmc_block_get32(header + 12u) != 92u) ||
	    (gta4l_emmc_block_get32(header + 20u) != 0u)) {
		return 0;
	}
	for (index = 0; index < 92u; index++) {
		copy[index] = header[index];
	}
	copy[16] = 0;
	copy[17] = 0;
	copy[18] = 0;
	copy[19] = 0;
	if (gta4l_emmc_block_crc32(copy, 92u) !=
	    gta4l_emmc_block_get32(header + 16u)) {
		return 0;
	}
	out->my_lba = gta4l_emmc_block_get64(header + 24u);
	out->alternate_lba = gta4l_emmc_block_get64(header + 32u);
	out->first_usable = gta4l_emmc_block_get64(header + 40u);
	out->last_usable = gta4l_emmc_block_get64(header + 48u);
	out->disk_guid_low = gta4l_emmc_block_get64(header + 56u);
	out->disk_guid_high = gta4l_emmc_block_get64(header + 64u);
	out->entry_lba = gta4l_emmc_block_get64(header + 72u);
	out->slots = gta4l_emmc_block_get32(header + 80u);
	out->entry_size = gta4l_emmc_block_get32(header + 84u);
	out->entry_crc = gta4l_emmc_block_get32(header + 88u);
	if (out->my_lba != expected_my_lba) {
		return 0;
	}
	if (out->alternate_lba != expected_alternate_lba) {
		return 0;
	}
	if (out->first_usable > out->last_usable) {
		return 0;
	}
	if (out->first_usable < 2u) {
		return 0;
	}
	if (out->last_usable > (uint64_t)capacity_blocks - 2u) {
		return 0;
	}
	if ((out->slots == 0u) ||
	    (out->slots > GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS)) {
		return 0;
	}
	if (out->entry_size != 128u) {
		return 0;
	}
	out->array_blocks = ((out->slots * 128u) + 511u) / 512u;
	if (out->entry_lba > (uint64_t)capacity_blocks) {
		return 0;
	}
	if (out->entry_lba + out->array_blocks >
	    (uint64_t)capacity_blocks) {
		return 0;
	}
	if (primary != 0) {
		if (out->entry_lba < 2u) {
			return 0;
		}
		if (out->entry_lba + out->array_blocks >
		    out->first_usable) {
			return 0;
		}
	} else {
		if (out->entry_lba <= out->last_usable) {
			return 0;
		}
		if (out->entry_lba + out->array_blocks >
		    (uint64_t)capacity_blocks - 1u) {
			return 0;
		}
	}
	return 1;
}

int
gta4l_emmc_block_gpt_geometry(const uint8_t *header,
    uint32_t capacity_blocks, int primary, uint64_t *entry_lba,
    uint32_t *slots)
{
	struct gta4l_emmc_block_gpt_header fields;

	if ((entry_lba == 0) || (slots == 0)) {
		return 0;
	}
	if (gta4l_emmc_block_gpt_header_parse(header, capacity_blocks,
	    (primary != 0) ? 1u : (uint64_t)capacity_blocks - 1u,
	    (primary != 0) ? (uint64_t)capacity_blocks - 1u : 1u,
	    primary, &fields) == 0) {
		return 0;
	}
	*entry_lba = fields.entry_lba;
	*slots = fields.slots;
	return 1;
}

static int
gta4l_emmc_block_gpt_array(const uint8_t *array,
    const struct gta4l_emmc_block_gpt_header *header,
    struct gta4l_emmc_block_gpt *target, uint32_t *reason)
{
	uint32_t index;
	uint32_t used = 0;

	if ((array == 0) || (header == 0) || (target == 0) ||
	    (reason == 0)) {
		return 0;
	}
	if (gta4l_emmc_block_crc32(array, header->slots * 128u) !=
	    header->entry_crc) {
		*reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_ARRAY;
		return 0;
	}
	for (index = 0; index < header->slots; index++) {
		const uint8_t *entry = array + (index * 128u);
		uint64_t first;
		uint64_t last;
		uint32_t prior;
		int empty = 1;

		for (prior = 0; prior < 16u; prior++) {
			if (entry[prior] != 0u) {
				empty = 0;
				break;
			}
		}
		if (empty != 0) {
			continue;
		}
		first = gta4l_emmc_block_get64(entry + 32u);
		last = gta4l_emmc_block_get64(entry + 40u);
		if ((first > last) ||
		    (first < header->first_usable) ||
		    (last > header->last_usable)) {
			*reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_ENTRY;
			return 0;
		}
		for (prior = 0; prior < used; prior++) {
			uint64_t prior_first =
			    target->entry[prior].base / 512u;
			uint64_t prior_last = prior_first +
			    (target->entry[prior].size / 512u) - 1u;

			if ((first <= prior_last) &&
			    (prior_first <= last)) {
				*reason =
				    GTA4L_EMMC_BLOCK_PARTMAP_REASON_ENTRY;
				return 0;
			}
		}
		target->entry[used].base = first * 512u;
		target->entry[used].size =
		    (last - first + 1u) * 512u;
		used++;
	}
	target->slots = header->slots;
	target->used = used;
	target->first_usable = (uint32_t)header->first_usable;
	target->last_usable = (uint32_t)header->last_usable;
	for (index = 1; index < used; index++) {
		struct gta4l_emmc_block_partmap_media value =
		    target->entry[index];
		uint32_t at = index;

		while ((at > 0u) &&
		    ((target->entry[at - 1u].base > value.base) ||
		    ((target->entry[at - 1u].base == value.base) &&
		    (target->entry[at - 1u].size > value.size)))) {
			target->entry[at] = target->entry[at - 1u];
			at--;
		}
		target->entry[at] = value;
	}
	return 1;
}

int
gta4l_emmc_block_gpt_primary(const uint8_t *pmbr, const uint8_t *header,
    const uint8_t *array, uint32_t capacity_blocks,
    struct gta4l_emmc_block_gpt *gpt)
{
	struct gta4l_emmc_block_gpt_header fields;
	uint32_t reason = 0;

	if (gpt == 0) {
		return 0;
	}
	if (gta4l_emmc_block_pmbr_valid(pmbr, capacity_blocks) == 0) {
		gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_PMBR;
		return 0;
	}
	gpt->pmbr = 1;
	if (gta4l_emmc_block_gpt_header_parse(header, capacity_blocks, 1u,
	    (uint64_t)capacity_blocks - 1u, 1, &fields) == 0) {
		gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_HEADER;
		return 0;
	}
	gpt->hdr = 1;
	if (gta4l_emmc_block_gpt_array(array, &fields, gpt, &reason) == 0) {
		gpt->reason = reason;
		return 0;
	}
	gpt->arr = 1;
	gpt->entry_size = fields.entry_size;
	gpt->entry_crc = fields.entry_crc;
	gpt->disk_guid_low = fields.disk_guid_low;
	gpt->disk_guid_high = fields.disk_guid_high;
	gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_NONE;
	return 1;
}

int
gta4l_emmc_block_gpt_backup(const uint8_t *header, const uint8_t *array,
    uint32_t capacity_blocks, struct gta4l_emmc_block_gpt *gpt,
    struct gta4l_emmc_block_gpt *backup)
{
	struct gta4l_emmc_block_gpt_header fields;
	uint32_t reason = 0;
	uint32_t index;

	if ((gpt == 0) || (backup == 0)) {
		return 0;
	}
	if (gta4l_emmc_block_gpt_header_parse(header, capacity_blocks,
	    (uint64_t)capacity_blocks - 1u, 1u, 0, &fields) == 0) {
		gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_BACKUP_HEADER;
		return 0;
	}
	if (gta4l_emmc_block_gpt_array(array, &fields, backup,
	    &reason) == 0) {
		gpt->reason = (reason ==
		    GTA4L_EMMC_BLOCK_PARTMAP_REASON_ARRAY) ?
		    GTA4L_EMMC_BLOCK_PARTMAP_REASON_BACKUP_ARRAY : reason;
		return 0;
	}
	gpt->bak = 1;
	if ((fields.disk_guid_low != gpt->disk_guid_low) ||
	    (fields.disk_guid_high != gpt->disk_guid_high) ||
	    (fields.entry_size != gpt->entry_size) ||
	    (fields.entry_crc != gpt->entry_crc) ||
	    (backup->first_usable != gpt->first_usable) ||
	    (backup->last_usable != gpt->last_usable) ||
	    (backup->slots != gpt->slots) ||
	    (backup->used != gpt->used)) {
		gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_DISAGREE;
		return 0;
	}
	for (index = 0; index < gpt->used; index++) {
		if ((backup->entry[index].base != gpt->entry[index].base) ||
		    (backup->entry[index].size != gpt->entry[index].size)) {
			gpt->reason =
			    GTA4L_EMMC_BLOCK_PARTMAP_REASON_DISAGREE;
			return 0;
		}
	}
	gpt->xcheck = 1;
	gpt->reason = GTA4L_EMMC_BLOCK_PARTMAP_REASON_NONE;
	return 1;
}

int
gta4l_emmc_block_partmap_media_check(uint64_t base, uint64_t size,
    uint64_t whole_size)
{
	if (size == 0u) {
		return 1;
	}
	if ((base % GTA4L_EMMC_BLOCK_BYTES) != 0u) {
		return 1;
	}
	if ((size % GTA4L_EMMC_BLOCK_BYTES) != 0u) {
		return 1;
	}
	if (base > whole_size) {
		return 1;
	}
	if (size > whole_size - base) {
		return 1;
	}
	return 0;
}

static int
gta4l_emmc_block_partmap_find(
    const struct gta4l_emmc_block_partmap_media *sorted, uint32_t count,
    const struct gta4l_emmc_block_partmap_media *key)
{
	uint32_t low = 0;
	uint32_t high = count;

	while (low < high) {
		uint32_t middle = low + ((high - low) / 2u);

		if ((sorted[middle].base < key->base) ||
		    ((sorted[middle].base == key->base) &&
		    (sorted[middle].size < key->size))) {
			low = middle + 1u;
		} else {
			high = middle;
		}
	}
	if (low >= count) {
		return 0;
	}
	return ((sorted[low].base == key->base) &&
	    (sorted[low].size == key->size)) ? 1 : 0;
}

int
gta4l_emmc_block_partmap_compare(
    const struct gta4l_emmc_block_gpt *gpt,
    struct gta4l_emmc_block_partmap_media *media, uint32_t media_count,
    uint32_t *missing, uint32_t *extra, uint32_t *dup)
{
	uint32_t index;
	uint32_t missing_count = 0;
	uint32_t extra_count = 0;
	uint32_t dup_count = 0;

	if ((gpt == 0) || (missing == 0) || (extra == 0) || (dup == 0)) {
		return 0;
	}
	if ((media == 0) && (media_count != 0u)) {
		return 0;
	}
	if (media_count > GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA) {
		media_count = GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA;
	}
	for (index = 1; index < media_count; index++) {
		struct gta4l_emmc_block_partmap_media value = media[index];
		uint32_t at = index;

		while ((at > 0u) &&
		    ((media[at - 1u].base > value.base) ||
		    ((media[at - 1u].base == value.base) &&
		    (media[at - 1u].size > value.size)))) {
			media[at] = media[at - 1u];
			at--;
		}
		media[at] = value;
	}
	for (index = 1; index < media_count; index++) {
		if ((media[index].base == media[index - 1u].base) &&
		    (media[index].size == media[index - 1u].size)) {
			dup_count++;
		}
	}
	for (index = 1; index < gpt->used; index++) {
		if ((gpt->entry[index].base == gpt->entry[index - 1u].base) &&
		    (gpt->entry[index].size ==
		    gpt->entry[index - 1u].size)) {
			dup_count++;
		}
	}
	for (index = 0; index < gpt->used; index++) {
		if (gta4l_emmc_block_partmap_find(media, media_count,
		    &gpt->entry[index]) == 0) {
			missing_count++;
		}
	}
	for (index = 0; index < media_count; index++) {
		if (gta4l_emmc_block_partmap_find(gpt->entry, gpt->used,
		    &media[index]) == 0) {
			extra_count++;
		}
	}
	*missing = missing_count;
	*extra = extra_count;
	*dup = dup_count;
	if (media_count != gpt->used) {
		return 0;
	}
	if ((missing_count != 0u) || (extra_count != 0u) ||
	    (dup_count != 0u)) {
		return 0;
	}
	return 1;
}

struct gta4l_emmc_block_writer {
	char *buffer;
	uint32_t size;
	uint32_t at;
	int ok;
};

static void
gta4l_emmc_block_put_char(struct gta4l_emmc_block_writer *writer, char value)
{
	if ((writer->ok == 0) || (writer->at + 1u >= writer->size)) {
		writer->ok = 0;
		return;
	}
	writer->buffer[writer->at++] = value;
}

static void
gta4l_emmc_block_put_string(struct gta4l_emmc_block_writer *writer,
    const char *value)
{
	uint32_t index = 0;

	if (value == 0) {
		value = "none";
	}
	while (value[index] != 0) {
		gta4l_emmc_block_put_char(writer, value[index]);
		index++;
	}
}

static void
gta4l_emmc_block_put_u64(struct gta4l_emmc_block_writer *writer,
    uint64_t value)
{
	char digits[20];
	uint32_t count = 0;

	if (value == 0) {
		gta4l_emmc_block_put_char(writer, '0');
		return;
	}
	while ((value != 0) && (count < sizeof(digits))) {
		digits[count++] = (char)('0' + (value % 10u));
		value /= 10u;
	}
	while (count != 0) {
		gta4l_emmc_block_put_char(writer, digits[--count]);
	}
}

static void
gta4l_emmc_block_put_i32(struct gta4l_emmc_block_writer *writer, int32_t value)
{
	uint64_t magnitude;

	if (value < 0) {
		gta4l_emmc_block_put_char(writer, '-');
		magnitude = (uint64_t)(-(int64_t)value);
	} else {
		magnitude = (uint64_t)value;
	}
	gta4l_emmc_block_put_u64(writer, magnitude);
}

static int
gta4l_emmc_block_content_allowed(const char *content)
{
	static const char *const allowed[] = {
		"GUID_partition_scheme",
		"EFI",
		"Apple_APFS",
		"Apple_HFS",
		"Apple_Boot",
		"Apple_Recovery",
		"Microsoft Basic Data",
		"Linux filesystem",
		"Linux swap"
	};
	uint32_t index;
	uint32_t offset;

	if (content == 0) {
		return 0;
	}
	for (index = 0; index < (uint32_t)(sizeof(allowed) / sizeof(allowed[0]));
	    index++) {
		offset = 0;
		while ((allowed[index][offset] != 0) &&
		    (content[offset] == allowed[index][offset])) {
			offset++;
		}
		if ((allowed[index][offset] == 0) && (content[offset] == 0)) {
			return 1;
		}
	}
	return 0;
}

static void
gta4l_emmc_block_put_content(struct gta4l_emmc_block_writer *writer,
    const char *content)
{
	if (gta4l_emmc_block_content_allowed(content) == 0) {
		gta4l_emmc_block_put_string(writer, "other");
		return;
	}
	gta4l_emmc_block_put_string(writer, content);
}

static void
gta4l_emmc_block_put_key(struct gta4l_emmc_block_writer *writer,
    const char *key)
{
	gta4l_emmc_block_put_string(writer, key);
	gta4l_emmc_block_put_char(writer, '=');
}

static void
gta4l_emmc_block_put_digest(struct gta4l_emmc_block_writer *writer,
    const uint8_t *digest, uint32_t bytes)
{
	static const char hex[] = "0123456789abcdef";
	uint32_t index;

	for (index = 0; index < bytes; index++) {
		gta4l_emmc_block_put_char(writer, hex[digest[index] >> 4]);
		gta4l_emmc_block_put_char(writer, hex[digest[index] & 0x0fu]);
	}
}

static void
gta4l_emmc_block_put_hex(struct gta4l_emmc_block_writer *writer,
    uint32_t value, uint32_t digits)
{
	static const char hex[] = "0123456789abcdef";
	uint32_t shift;

	for (shift = digits; shift != 0; shift--) {
		gta4l_emmc_block_put_char(writer,
		    hex[(value >> ((shift - 1u) * 4u)) & 0x0fu]);
	}
}

uint32_t
gta4l_emmc_block_report_format(char *buffer, uint32_t size,
    const struct gta4l_emmc_block_report *report)
{
	struct gta4l_emmc_block_writer writer;
	uint32_t index;
	uint32_t breakup_index;
	uint32_t breakup_target;
	uint64_t breakup_start;
	uint32_t breakup_length;
	uint32_t breakup_span;
	uint64_t breakup_actual;
	int32_t breakup_status;
	uint32_t breakup_subrequests;
	uint32_t breakup_blocks;
	uint32_t breakup_max;
	const uint8_t *breakup_digest;
	uint32_t sweep_index;
	uint32_t sweep_target;
	uint64_t sweep_start;
	uint32_t sweep_length;
	uint32_t sweep_span;
	uint64_t sweep_actual;
	int32_t sweep_status;
	uint32_t sweep_subrequests;
	uint32_t sweep_blocks;
	uint32_t sweep_max;
	uint32_t sweep_ref;
	uint32_t sweep_refbytes;
	int32_t sweep_refstatus;
	uint32_t sweep_mvalid;
	uint32_t sweep_mismatch;
	const uint8_t *sweep_digest;
	const uint8_t *sweep_refdigest;
	uint32_t consume_index;
	uint32_t consume_target;
	uint64_t consume_start;
	uint32_t consume_length;
	uint32_t consume_span;
	uint64_t consume_actual;
	int32_t consume_status;
	uint32_t consume_subrequests;
	uint32_t consume_blocks;
	uint32_t consume_max;
	uint32_t consume_requested;
	uint32_t consume_completed;
	const uint8_t *consume_digest;

	if ((buffer == 0) || (report == 0) || (size < 2u)) {
		if ((buffer != 0) && (size != 0)) {
			buffer[0] = 0;
		}
		return 0;
	}
	writer.buffer = buffer;
	writer.size = size;
	writer.at = 0;
	writer.ok = 1;

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 REPORT: ");
	gta4l_emmc_block_put_key(&writer, "rc");
	gta4l_emmc_block_put_u64(&writer, report->rc);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "stop");
	gta4l_emmc_block_put_u64(&writer, report->stop);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "mode");
	gta4l_emmc_block_put_u64(&writer, report->mode);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "blocks");
	gta4l_emmc_block_put_u64(&writer, report->capacity_blocks);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "bs");
	gta4l_emmc_block_put_u64(&writer, report->block_size);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "bytes");
	gta4l_emmc_block_put_u64(&writer, report->bytes);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "wid");
	gta4l_emmc_block_put_u64(&writer, report->bus_width);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "tim");
	gta4l_emmc_block_put_u64(&writer, report->timing);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "clk");
	gta4l_emmc_block_put_u64(&writer, report->clock_hz);
	gta4l_emmc_block_put_char(&writer, '\n');

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 DRV: ");
	gta4l_emmc_block_put_key(&writer, "online");
	gta4l_emmc_block_put_u64(&writer, report->online);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "wp");
	gta4l_emmc_block_put_u64(&writer, report->write_protected);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "reads");
	gta4l_emmc_block_put_u64(&writer, report->requests);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "blocks");
	gta4l_emmc_block_put_u64(&writer, report->blocks);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "fails");
	gta4l_emmc_block_put_u64(&writer, report->failures);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "max");
	gta4l_emmc_block_put_u64(&writer, report->max_request_blocks);
	gta4l_emmc_block_put_char(&writer, '\n');

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 XFER: ");
	gta4l_emmc_block_put_key(&writer, "req");
	gta4l_emmc_block_put_u64(&writer, report->requested_blocks);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "c17");
	gta4l_emmc_block_put_u64(&writer, report->cmd17_reads);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "c18");
	gta4l_emmc_block_put_u64(&writer, report->cmd18_chunks);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "c18b");
	gta4l_emmc_block_put_u64(&writer, report->cmd18_blocks);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "cfail");
	gta4l_emmc_block_put_u64(&writer, report->chunk_failures);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "stops");
	gta4l_emmc_block_put_u64(&writer, report->stop_attempts);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "sfail");
	gta4l_emmc_block_put_u64(&writer, report->stop_failures);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "last_stop");
	gta4l_emmc_block_put_i32(&writer, report->last_stop_result);
	gta4l_emmc_block_put_char(&writer, '\n');

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 MEDIA: ");
	gta4l_emmc_block_put_key(&writer, "whole");
	gta4l_emmc_block_put_u64(&writer, report->whole);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "base");
	gta4l_emmc_block_put_u64(&writer, report->base);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "size");
	gta4l_emmc_block_put_u64(&writer, report->size);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "content");
	gta4l_emmc_block_put_content(&writer, report->content);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "writable");
	gta4l_emmc_block_put_u64(&writer, report->writable);
	gta4l_emmc_block_put_char(&writer, '\n');

	if (report->partmap != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 PARTMAP: ");
		gta4l_emmc_block_put_key(&writer, "gpt");
		gta4l_emmc_block_put_u64(&writer, report->partmap_gpt);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->partmap_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "slots");
		gta4l_emmc_block_put_u64(&writer, report->partmap_slots);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "used");
		gta4l_emmc_block_put_u64(&writer, report->partmap_used);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "pmbr");
		gta4l_emmc_block_put_u64(&writer, report->partmap_pmbr);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hdr");
		gta4l_emmc_block_put_u64(&writer, report->partmap_hdr);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "arr");
		gta4l_emmc_block_put_u64(&writer, report->partmap_arr);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bak");
		gta4l_emmc_block_put_u64(&writer, report->partmap_bak);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "xcheck");
		gta4l_emmc_block_put_u64(&writer, report->partmap_xcheck);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->partmap_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "match");
		gta4l_emmc_block_put_u64(&writer, report->partmap_match);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "missing");
		gta4l_emmc_block_put_u64(&writer, report->partmap_missing);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "extra");
		gta4l_emmc_block_put_u64(&writer, report->partmap_extra);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "dup");
		gta4l_emmc_block_put_u64(&writer, report->partmap_dup);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "nested");
		gta4l_emmc_block_put_u64(&writer, report->partmap_nested);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "prop");
		gta4l_emmc_block_put_u64(&writer, report->partmap_prop);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "samples");
		gta4l_emmc_block_put_u64(&writer, report->partmap_samples);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "changes");
		gta4l_emmc_block_put_u64(&writer, report->partmap_changes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stable");
		gta4l_emmc_block_put_u64(&writer, report->partmap_stable);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stable_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_stable_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "deadline_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_deadline_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "timed_out");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_timed_out);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->partmap_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->partmap_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->partmap_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->partmap_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->partmap_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->partmap_complete);
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 READDETAIL: ");
		gta4l_emmc_block_put_key(&writer, "state");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_state);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "lba");
		gta4l_emmc_block_put_u64(&writer, report->partmap_read_lba);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->partmap_read_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "desc_len");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_desc_len);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "desc_dir");
		gta4l_emmc_block_put_u64(&writer,
		    report->partmap_read_desc_dir);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->partread != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 PARTREAD: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->partread_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->partread_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->partread_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->partread_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->partread_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->partread_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->partread_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->partread_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->partread_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->partread_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->partread_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->partread_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "part");
		gta4l_emmc_block_put_u64(&writer, report->partread_part);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->partread_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 READTEST: ");
		gta4l_emmc_block_put_key(&writer, "index");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_last_index);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "target");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_last_target);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_last_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "off");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_last_offset);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ok");
		gta4l_emmc_block_put_u64(&writer, report->partread_last_ok);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->partread_last_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_last_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "digest");
		gta4l_emmc_block_put_digest(&writer,
		    report->partread_last_digest,
		    sizeof(report->partread_last_digest));
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 READFAIL: ");
		gta4l_emmc_block_put_key(&writer, "index");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_fail_index);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_fail_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->partread_fail_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer,
		    report->partread_fail_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "digest");
		gta4l_emmc_block_put_digest(&writer,
		    report->partread_fail_digest,
		    sizeof(report->partread_fail_digest));
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->deblock != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 DEBLOCK: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->deblock_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->deblock_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->deblock_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->deblock_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->deblock_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->deblock_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->deblock_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, report->deblock_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->deblock_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->deblock_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->deblock_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->deblock_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->deblock_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "part");
		gta4l_emmc_block_put_u64(&writer, report->deblock_part);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->deblock_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->deblock_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 DEBLOCKREAD: ");
		gta4l_emmc_block_put_key(&writer, "index");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_index);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "target");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_target);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "start");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_start);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "length");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_length);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer,
		    report->deblock_last_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->deblock_last_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "digest");
		gta4l_emmc_block_put_digest(&writer,
		    report->deblock_last_digest,
		    sizeof(report->deblock_last_digest));
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->breakup != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 BREAKUP: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->breakup_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->breakup_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->breakup_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->breakup_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->breakup_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->breakup_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->breakup_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, report->breakup_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->breakup_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer,
		    report->breakup_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, report->breakup_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "foreign");
		gta4l_emmc_block_put_u64(&writer, report->breakup_foreign);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->breakup_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->breakup_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->breakup_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->breakup_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "part");
		gta4l_emmc_block_put_u64(&writer, report->breakup_part);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->breakup_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->breakup_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->breakup_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		if (report->breakup_failed != 0u) {
			breakup_index = report->breakup_fail_index;
			breakup_target = report->breakup_fail_target;
			breakup_start = report->breakup_fail_start;
			breakup_length = report->breakup_fail_length;
			breakup_span = report->breakup_fail_span;
			breakup_actual = report->breakup_fail_actual;
			breakup_status = report->breakup_fail_status;
			breakup_subrequests =
			    report->breakup_fail_subrequests;
			breakup_blocks = report->breakup_fail_blocks;
			breakup_max = report->breakup_fail_max;
			breakup_digest = report->breakup_fail_digest;
		} else {
			breakup_index = report->breakup_last_index;
			breakup_target = report->breakup_last_target;
			breakup_start = report->breakup_last_start;
			breakup_length = report->breakup_last_length;
			breakup_span = report->breakup_last_span;
			breakup_actual = report->breakup_last_actual;
			breakup_status = report->breakup_last_status;
			breakup_subrequests =
			    report->breakup_last_subrequests;
			breakup_blocks = report->breakup_last_blocks;
			breakup_max = report->breakup_last_max;
			breakup_digest = report->breakup_last_digest;
		}
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 BREAKUPREAD: ");
		gta4l_emmc_block_put_key(&writer, "index");
		gta4l_emmc_block_put_u64(&writer, breakup_index);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "target");
		gta4l_emmc_block_put_u64(&writer, breakup_target);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "start");
		gta4l_emmc_block_put_u64(&writer, breakup_start);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "length");
		gta4l_emmc_block_put_u64(&writer, breakup_length);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, breakup_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer, breakup_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)breakup_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer, breakup_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, breakup_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, breakup_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "digest");
		gta4l_emmc_block_put_digest(&writer, breakup_digest, 16u);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->sweep != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 SWEEP: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->sweep_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->sweep_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->sweep_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->sweep_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->sweep_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->sweep_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->sweep_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, report->sweep_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->sweep_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer, report->sweep_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, report->sweep_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "foreign");
		gta4l_emmc_block_put_u64(&writer, report->sweep_foreign);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->sweep_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->sweep_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->sweep_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->sweep_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "part");
		gta4l_emmc_block_put_u64(&writer, report->sweep_part);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refs");
		gta4l_emmc_block_put_u64(&writer, report->sweep_refs);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refbytes");
		gta4l_emmc_block_put_u64(&writer, report->sweep_refbytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "anchor");
		gta4l_emmc_block_put_u64(&writer, report->sweep_anchor);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatches");
		gta4l_emmc_block_put_u64(&writer, report->sweep_mismatches);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->sweep_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->sweep_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->sweep_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		if (report->sweep_failed != 0u) {
			sweep_index = report->sweep_fail_index;
			sweep_target = report->sweep_fail_target;
			sweep_start = report->sweep_fail_start;
			sweep_length = report->sweep_fail_length;
			sweep_span = report->sweep_fail_span;
			sweep_actual = report->sweep_fail_actual;
			sweep_status = report->sweep_fail_status;
			sweep_subrequests = report->sweep_fail_subrequests;
			sweep_blocks = report->sweep_fail_blocks;
			sweep_max = report->sweep_fail_max;
			sweep_ref = report->sweep_fail_ref;
			sweep_refbytes = report->sweep_fail_refbytes;
			sweep_refstatus = report->sweep_fail_refstatus;
			sweep_mvalid = report->sweep_fail_mvalid;
			sweep_mismatch = report->sweep_fail_mismatch;
			sweep_digest = report->sweep_fail_digest;
			sweep_refdigest = report->sweep_fail_refdigest;
		} else {
			sweep_index = report->sweep_last_index;
			sweep_target = report->sweep_last_target;
			sweep_start = report->sweep_last_start;
			sweep_length = report->sweep_last_length;
			sweep_span = report->sweep_last_span;
			sweep_actual = report->sweep_last_actual;
			sweep_status = report->sweep_last_status;
			sweep_subrequests = report->sweep_last_subrequests;
			sweep_blocks = report->sweep_last_blocks;
			sweep_max = report->sweep_last_max;
			sweep_ref = report->sweep_last_ref;
			sweep_refbytes = report->sweep_last_refbytes;
			sweep_refstatus = report->sweep_last_refstatus;
			sweep_mvalid = report->sweep_last_mvalid;
			sweep_mismatch = report->sweep_last_mismatch;
			sweep_digest = report->sweep_last_digest;
			sweep_refdigest = report->sweep_last_refdigest;
		}
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 SWEEPREAD: ");
		gta4l_emmc_block_put_key(&writer, "index");
		gta4l_emmc_block_put_u64(&writer, sweep_index);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "target");
		gta4l_emmc_block_put_u64(&writer, sweep_target);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "start");
		gta4l_emmc_block_put_u64(&writer, sweep_start);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "length");
		gta4l_emmc_block_put_u64(&writer, sweep_length);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, sweep_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "actual");
		gta4l_emmc_block_put_u64(&writer, sweep_actual);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "status");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)sweep_status);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer, sweep_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, sweep_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, sweep_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ref");
		gta4l_emmc_block_put_u64(&writer, sweep_ref);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refbytes");
		gta4l_emmc_block_put_u64(&writer, sweep_refbytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refstatus");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)sweep_refstatus);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mvalid");
		gta4l_emmc_block_put_u64(&writer, sweep_mvalid);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatch");
		gta4l_emmc_block_put_u64(&writer, sweep_mismatch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "digest");
		gta4l_emmc_block_put_digest(&writer, sweep_digest, 16u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refdigest");
		gta4l_emmc_block_put_digest(&writer, sweep_refdigest, 16u);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->consume != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 CONSUME: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->consume_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->consume_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->consume_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->consume_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->consume_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->consume_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->consume_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "span");
		gta4l_emmc_block_put_u64(&writer, report->consume_span);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->consume_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer, report->consume_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, report->consume_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "foreign");
		gta4l_emmc_block_put_u64(&writer, report->consume_foreign);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->consume_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->consume_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->consume_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "media");
		gta4l_emmc_block_put_u64(&writer, report->consume_media);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "part");
		gta4l_emmc_block_put_u64(&writer, report->consume_part);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "open");
		gta4l_emmc_block_put_u64(&writer, report->consume_open);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "close");
		gta4l_emmc_block_put_u64(&writer, report->consume_close);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "alloc");
		gta4l_emmc_block_put_u64(&writer, report->consume_alloc);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "client");
		gta4l_emmc_block_put_u64(&writer, report->consume_client);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatches");
		gta4l_emmc_block_put_u64(&writer, report->consume_mismatch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->consume_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->consume_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->consume_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		if (report->consume_failed != 0u) {
			consume_index = report->consume_fail_index;
			consume_target = report->consume_fail_target;
			consume_start = report->consume_fail_start;
			consume_length = report->consume_fail_length;
			consume_span = report->consume_fail_span;
			consume_requested = report->consume_fail_requested;
			consume_completed = report->consume_fail_completed;
			consume_actual = report->consume_fail_actual;
			consume_status = report->consume_fail_status;
			consume_digest = report->consume_fail_digest;
			gta4l_emmc_block_put_string(&writer,
			    "GTA4L-EMMC P5 CONSUMEFAIL: ");
			gta4l_emmc_block_put_key(&writer, "reason");
			gta4l_emmc_block_put_u64(&writer,
			    report->consume_fail_reason);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "case");
			gta4l_emmc_block_put_u64(&writer, consume_index);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "target");
			gta4l_emmc_block_put_u64(&writer, consume_target);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "start");
			gta4l_emmc_block_put_u64(&writer, consume_start);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "length");
			gta4l_emmc_block_put_u64(&writer, consume_length);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "span");
			gta4l_emmc_block_put_u64(&writer, consume_span);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "requested");
			gta4l_emmc_block_put_u64(&writer, consume_requested);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "completed");
			gta4l_emmc_block_put_u64(&writer, consume_completed);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer, consume_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)consume_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer, consume_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "expected");
			gta4l_emmc_block_put_digest(&writer,
			    gta4l_emmc_block_zero_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "mismatch");
			gta4l_emmc_block_put_u64(&writer,
			    report->consume_mismatch);
			gta4l_emmc_block_put_char(&writer, '\n');
		} else {
			consume_index = report->consume_last_index;
			consume_target = report->consume_last_target;
			consume_start = report->consume_last_start;
			consume_length = report->consume_last_length;
			consume_span = report->consume_last_span;
			consume_actual = report->consume_last_actual;
			consume_status = report->consume_last_status;
			consume_subrequests =
			    report->consume_last_subrequests;
			consume_blocks = report->consume_last_blocks;
			consume_max = report->consume_last_max;
			consume_digest = report->consume_last_digest;
			gta4l_emmc_block_put_string(&writer,
			    "GTA4L-EMMC P5 CONSUMEREAD: ");
			gta4l_emmc_block_put_key(&writer, "index");
			gta4l_emmc_block_put_u64(&writer, consume_index);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "target");
			gta4l_emmc_block_put_u64(&writer, consume_target);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "start");
			gta4l_emmc_block_put_u64(&writer, consume_start);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "length");
			gta4l_emmc_block_put_u64(&writer, consume_length);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "span");
			gta4l_emmc_block_put_u64(&writer, consume_span);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer, consume_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)consume_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "subrequests");
			gta4l_emmc_block_put_u64(&writer, consume_subrequests);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "blocks");
			gta4l_emmc_block_put_u64(&writer, consume_blocks);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "max");
			gta4l_emmc_block_put_u64(&writer, consume_max);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer, consume_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "expected");
			gta4l_emmc_block_put_digest(&writer,
			    gta4l_emmc_block_zero_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "mismatch");
			gta4l_emmc_block_put_u64(&writer,
			    report->consume_mismatch);
			gta4l_emmc_block_put_char(&writer, '\n');
		}
	}

	if (report->width != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 WIDTH: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->width_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->width_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->width_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->width_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->width_complete);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "switch");
		gta4l_emmc_block_put_u64(&writer, report->width_switch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "verified");
		gta4l_emmc_block_put_u64(&writer, report->width_verified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hb");
		gta4l_emmc_block_put_u64(&writer, report->width_hb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ha");
		gta4l_emmc_block_put_u64(&writer, report->width_ha);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rb");
		gta4l_emmc_block_put_u64(&writer, report->width_rb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cmd6");
		gta4l_emmc_block_put_u64(&writer, report->width_cmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "revert");
		gta4l_emmc_block_put_u64(&writer, report->width_revert);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rverified");
		gta4l_emmc_block_put_u64(&writer, report->width_rverified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rhb");
		gta4l_emmc_block_put_u64(&writer, report->width_rhb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rha");
		gta4l_emmc_block_put_u64(&writer, report->width_rha);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rcmd6");
		gta4l_emmc_block_put_u64(&writer, report->width_rcmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->width_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->width_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->width_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer, report->width_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, report->width_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "match");
		gta4l_emmc_block_put_u64(&writer, report->width_match);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatches");
		gta4l_emmc_block_put_u64(&writer, report->width_mismatch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->width_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->width_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->width_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->width_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->width_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->width_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		if (report->width_failed != 0u) {
			gta4l_emmc_block_put_string(&writer,
			    "GTA4L-EMMC P5 WIDTHFAIL: ");
			gta4l_emmc_block_put_key(&writer, "stage");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_fail_stage);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)report->width_fail_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_fail_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer,
			    report->width_fail_digest, 16u);
			gta4l_emmc_block_put_char(&writer, '\n');
		} else if (report->width_reads != 0u) {
			gta4l_emmc_block_put_string(&writer,
			    "GTA4L-EMMC P5 WIDTHREAD: ");
			gta4l_emmc_block_put_key(&writer, "index");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_index);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "lba");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_lba);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "start");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_start);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "length");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_length);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "span");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_span);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)
			    report->width_last_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "subrequests");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_subrequests);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "blocks");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_blocks);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "max");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_max);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer,
			    report->width_last_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "predigest");
			gta4l_emmc_block_put_digest(&writer,
			    report->width_last_predigest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "match");
			gta4l_emmc_block_put_u64(&writer,
			    report->width_last_match);
			gta4l_emmc_block_put_char(&writer, '\n');
		}
	}

	if (report->widthhold != 0) {
		gta4l_emmc_block_put_string(&writer,
		    (report->widthpub != 0) ?
		    "GTA4L-EMMC P5 WIDTHPUB: " :
		    "GTA4L-EMMC P5 WIDTHHOLD: ");
		gta4l_emmc_block_put_key(&writer, "cases");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_cases);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "passed");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_passed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "failed");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_failed);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_complete);
		if (report->widthpub != 0) {
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "pub");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthpub_pub);
		}
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "switch");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_switch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "verified");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_verified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hb");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_hb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ha");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_ha);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rb");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_rb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cmd6");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_cmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refs");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_refs);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refbytes");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_refbytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refblocks");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_refblocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refstatus");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->widthhold_refstatus);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refchunks");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_refchunks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refstops");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_refstops);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refstopfail");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_refstopfail);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "refdigest");
		gta4l_emmc_block_put_digest(&writer,
		    report->widthhold_refdigest, 16u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reads");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_reads);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "blocks");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "subrequests");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_subrequests);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "max");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_max);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "c18");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_c18);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stops");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_stops);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stopfail");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_stopfail);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "match");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_match);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatches");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_mismatch);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "foreign");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_foreign);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "obsdigest");
		gta4l_emmc_block_put_digest(&writer,
		    report->widthhold_obsdigest, 16u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partbase");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_partbase);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partsize");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_partsize);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partro");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_partro);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partdiag");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_partdiag);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "revert");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_revert);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rverified");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_rverified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rhb");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_rhb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rha");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_rha);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rcmd6");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_rcmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rstatus");
		gta4l_emmc_block_put_u64(&writer,
		    (uint64_t)(uint32_t)report->widthhold_rstatus);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->widthhold_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer,
		    report->widthhold_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');

		if (report->widthhold_failed != 0u) {
			gta4l_emmc_block_put_string(&writer,
			    (report->widthpub != 0) ?
			    "GTA4L-EMMC P5 WIDTHPUBFAIL: " :
			    "GTA4L-EMMC P5 WIDTHHOLDFAIL: ");
			gta4l_emmc_block_put_key(&writer, "index");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_index);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)
			    report->widthhold_fail_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "start");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_start);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "length");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_length);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "span");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_span);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "blocks");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_blocks);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "max");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_fail_max);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer,
			    report->widthhold_fail_digest, 16u);
			gta4l_emmc_block_put_char(&writer, '\n');
		} else if (report->widthhold_reads != 0u) {
			gta4l_emmc_block_put_string(&writer,
			    (report->widthpub != 0) ?
			    "GTA4L-EMMC P5 WIDTHPUBREAD: " :
			    "GTA4L-EMMC P5 WIDTHHOLDREAD: ");
			gta4l_emmc_block_put_key(&writer, "index");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_index);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "lba");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_lba);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "start");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_start);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "length");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_length);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "span");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_span);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "actual");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_actual);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "status");
			gta4l_emmc_block_put_u64(&writer,
			    (uint64_t)(uint32_t)
			    report->widthhold_last_status);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "subrequests");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_subrequests);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "blocks");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_blocks);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "max");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_max);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "digest");
			gta4l_emmc_block_put_digest(&writer,
			    report->widthhold_last_digest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "predigest");
			gta4l_emmc_block_put_digest(&writer,
			    report->widthhold_last_predigest, 16u);
			gta4l_emmc_block_put_char(&writer, ' ');
			gta4l_emmc_block_put_key(&writer, "match");
			gta4l_emmc_block_put_u64(&writer,
			    report->widthhold_last_match);
			gta4l_emmc_block_put_char(&writer, '\n');
		}
	}

	if (report->speed != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 SPEED: ");
		gta4l_emmc_block_put_key(&writer, "hs");
		gta4l_emmc_block_put_u64(&writer, report->speed_hs);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hsrb");
		gta4l_emmc_block_put_u64(&writer, report->speed_hsrb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hsa");
		gta4l_emmc_block_put_u64(&writer, report->speed_hsa);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rcg");
		gta4l_emmc_block_put_hex(&writer, report->speed_rcg, 8u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hispd");
		gta4l_emmc_block_put_hex(&writer, report->speed_hispd, 2u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "wtry");
		gta4l_emmc_block_put_u64(&writer, report->speed_wtry);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "wfall");
		gta4l_emmc_block_put_u64(&writer, report->speed_wfall);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "wroll");
		gta4l_emmc_block_put_u64(&writer, report->speed_wroll);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "wid");
		gta4l_emmc_block_put_u64(&writer, report->speed_wid);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "verified");
		gta4l_emmc_block_put_u64(&writer, report->speed_verified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hb");
		gta4l_emmc_block_put_hex(&writer, report->speed_hb, 2u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ha");
		gta4l_emmc_block_put_hex(&writer, report->speed_ha, 2u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cmd6");
		gta4l_emmc_block_put_u64(&writer, report->speed_cmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "base");
		gta4l_emmc_block_put_u64(&writer, report->speed_base);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes1");
		gta4l_emmc_block_put_u64(&writer, report->speed_bytes1);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ticks1");
		gta4l_emmc_block_put_u64(&writer, report->speed_ticks1);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hz");
		gta4l_emmc_block_put_u64(&writer, report->speed_hz);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "dig1");
		gta4l_emmc_block_put_digest(&writer, report->speed_dig1, 16u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes2");
		gta4l_emmc_block_put_u64(&writer, report->speed_bytes2);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ticks2");
		gta4l_emmc_block_put_u64(&writer, report->speed_ticks2);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "dig2");
		gta4l_emmc_block_put_digest(&writer, report->speed_dig2, 16u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "req");
		gta4l_emmc_block_put_u64(&writer, report->speed_req);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "fails");
		gta4l_emmc_block_put_u64(&writer, report->speed_fails);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cancel");
		gta4l_emmc_block_put_u64(&writer, report->speed_cancel);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "budget");
		gta4l_emmc_block_put_u64(&writer, report->speed_budget);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partial");
		gta4l_emmc_block_put_u64(&writer, report->speed_partial);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "pub");
		gta4l_emmc_block_put_u64(&writer, report->speed_pub);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "partdiag");
		gta4l_emmc_block_put_u64(&writer, report->speed_partdiag);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "revert");
		gta4l_emmc_block_put_u64(&writer, report->speed_revert);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rverified");
		gta4l_emmc_block_put_u64(&writer, report->speed_rverified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rhb");
		gta4l_emmc_block_put_hex(&writer, report->speed_rhb, 2u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rha");
		gta4l_emmc_block_put_hex(&writer, report->speed_rha, 2u);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rcmd6");
		gta4l_emmc_block_put_u64(&writer, report->speed_rcmd6);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rstatus");
		gta4l_emmc_block_put_i32(&writer, report->speed_rstatus);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hsrevert");
		gta4l_emmc_block_put_u64(&writer, report->speed_hsrevert);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "hsverified");
		gta4l_emmc_block_put_u64(&writer, report->speed_hsverified);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rhsb");
		gta4l_emmc_block_put_u64(&writer, report->speed_rhsb);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rhs");
		gta4l_emmc_block_put_u64(&writer, report->speed_rhs);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "rclk");
		gta4l_emmc_block_put_u64(&writer, report->speed_rclk);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "limit");
		gta4l_emmc_block_put_u64(&writer, report->speed_limit);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "seg");
		gta4l_emmc_block_put_u64(&writer, report->speed_segments);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "cap");
		gta4l_emmc_block_put_u64(&writer, report->speed_capacity);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if (report->part_obs != 0) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 PARTOBS: ");
		gta4l_emmc_block_put_key(&writer, "samples");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_samples);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "first");
		gta4l_emmc_block_put_u64(&writer, report->part_obs_first);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "first_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_first_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "first_nz");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_first_nz);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "first_nz_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_first_nz_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "final");
		gta4l_emmc_block_put_u64(&writer, report->part_obs_final);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "final_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_final_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "changes");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_changes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stable");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_stable);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stable_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_stable_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "deadline_ms");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_deadline_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "timed_out");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_timed_out);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "none");
		gta4l_emmc_block_put_u64(&writer, report->part_obs_none);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "error");
		gta4l_emmc_block_put_u64(&writer, report->part_obs_error);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "slots");
		gta4l_emmc_block_put_u64(&writer, report->part_obs_slots);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reported");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_reported);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "complete");
		gta4l_emmc_block_put_u64(&writer,
		    report->part_obs_complete);
		gta4l_emmc_block_put_char(&writer, '\n');
	} else if ((report->widthhold == 0) && (report->speed == 0)) {
		gta4l_emmc_block_put_string(&writer,
		    "GTA4L-EMMC P5 PART: ");
		gta4l_emmc_block_put_key(&writer, "count");
		gta4l_emmc_block_put_u64(&writer, report->part_count);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "expected");
		gta4l_emmc_block_put_u64(&writer, report->part_expected);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "waited_ms");
		gta4l_emmc_block_put_u64(&writer, report->part_wait_ms);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "error");
		gta4l_emmc_block_put_u64(&writer, report->part_error);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reported");
		gta4l_emmc_block_put_u64(&writer, report->part_reported);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	for (index = 0; index < GTA4L_EMMC_BLOCK_PART_REPORT; index++) {
		if ((report->widthhold != 0) || (report->speed != 0) ||
		    (index >= report->part_reported)) {
			break;
		}
		gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 PART");
		gta4l_emmc_block_put_u64(&writer, index + 1u);
		gta4l_emmc_block_put_string(&writer, ": ");
		gta4l_emmc_block_put_key(&writer, "base");
		gta4l_emmc_block_put_u64(&writer, report->part[index].base);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "size");
		gta4l_emmc_block_put_u64(&writer, report->part[index].size);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "content");
		gta4l_emmc_block_put_content(&writer,
		    report->part[index].content);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	if ((report->consume == 0) && (report->width == 0) &&
	    (report->widthhold == 0) && (report->speed == 0)) {
		gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 READ: ");
		gta4l_emmc_block_put_key(&writer, "lba");
		gta4l_emmc_block_put_u64(&writer, GTA4L_EMMC_BLOCK_REPORT_LBA);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "attempted");
		gta4l_emmc_block_put_u64(&writer, report->read_attempted);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ok");
		gta4l_emmc_block_put_u64(&writer, report->read_ok);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->read_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "sum");
		gta4l_emmc_block_put_u64(&writer, report->read_sum);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "base");
		gta4l_emmc_block_put_u64(&writer, report->baseline_sum);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "same");
		gta4l_emmc_block_put_u64(&writer, report->same);
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 QUAL: ");
		gta4l_emmc_block_put_key(&writer, "ref");
		gta4l_emmc_block_put_u64(&writer, report->qual_ref_valid);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ref_lba");
		gta4l_emmc_block_put_u64(&writer, report->qual_ref_lba);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ref_blocks");
		gta4l_emmc_block_put_u64(&writer, report->qual_ref_blocks);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_att");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_attempted);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_ok");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_ok);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_reason");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_bytes");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_mismatch");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_two_mismatch_valid);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_off");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_two_mismatch_offset);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_match");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_matches);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_c18");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_cmd18);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_c17");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_cmd17);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_stops");
		gta4l_emmc_block_put_u64(&writer, report->qual_two_stops);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "two_sfail");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_two_stop_failures);
		gta4l_emmc_block_put_char(&writer, '\n');

		gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 QUAL3: ");
		gta4l_emmc_block_put_key(&writer, "att");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_attempted);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "ok");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_ok);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "reason");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_reason);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "bytes");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_bytes);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "mismatch");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_three_mismatch_valid);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "off");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_three_mismatch_offset);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "match");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_matches);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "c18");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_cmd18);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "c17");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_cmd17);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "stops");
		gta4l_emmc_block_put_u64(&writer, report->qual_three_stops);
		gta4l_emmc_block_put_char(&writer, ' ');
		gta4l_emmc_block_put_key(&writer, "sfail");
		gta4l_emmc_block_put_u64(&writer,
		    report->qual_three_stop_failures);
		gta4l_emmc_block_put_char(&writer, '\n');
	}

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 SNAP: ");
	gta4l_emmc_block_put_key(&writer, "locked");
	gta4l_emmc_block_put_u64(&writer, report->snapshot_locked);
	gta4l_emmc_block_put_char(&writer, '\n');

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 FAIL: ");
	gta4l_emmc_block_put_key(&writer, "valid");
	gta4l_emmc_block_put_u64(&writer, report->fail_valid);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "stage");
	gta4l_emmc_block_put_u64(&writer, report->fail_stage);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "code");
	gta4l_emmc_block_put_i32(&writer, report->fail_code);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "block");
	gta4l_emmc_block_put_u64(&writer, report->first_fail_block);
	gta4l_emmc_block_put_char(&writer, ' ');
	gta4l_emmc_block_put_key(&writer, "error");
	gta4l_emmc_block_put_i32(&writer, report->first_fail_error);
	gta4l_emmc_block_put_char(&writer, '\n');

	if (writer.ok == 0) {
		buffer[0] = 0;
		return 0;
	}
	buffer[writer.at] = 0;
	return writer.at;
}
