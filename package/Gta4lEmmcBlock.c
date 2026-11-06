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

uint32_t
gta4l_emmc_block_report_format(char *buffer, uint32_t size,
    const struct gta4l_emmc_block_report *report)
{
	struct gta4l_emmc_block_writer writer;
	uint32_t index;

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

	gta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 PART: ");
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

	for (index = 0; index < GTA4L_EMMC_BLOCK_PART_REPORT; index++) {
		if (index >= report->part_reported) {
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
