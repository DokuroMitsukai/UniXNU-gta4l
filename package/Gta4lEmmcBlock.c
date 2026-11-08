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

int
gta4l_emmc_block_partobs_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_PARTOBS_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0)) {
		return GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_PARTOBS_ON;
}

int
gta4l_emmc_block_partcmp_select(int token_present, int token_value,
    int duplicate_tokens, int block_selected, int partobs_selected)
{
	if (token_present == 0) {
		return GTA4L_EMMC_BLOCK_PARTCMP_OFF;
	}
	if ((duplicate_tokens != 0) || (block_selected == 0) ||
	    (partobs_selected != 0)) {
		return GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;
	}
	if (token_value != 1) {
		return GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;
	}
	return GTA4L_EMMC_BLOCK_PARTCMP_ON;
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
	} else {
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
