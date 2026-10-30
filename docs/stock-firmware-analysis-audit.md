# Supplied stock-firmware analysis audit

Status: host-only audit of an externally supplied static analysis. It adds no
measurement and closes no SDHC power, electrical or retention requirement; the
decision remains **NO-GO** (`sdhc-decision-memo.md`) `[S]`.

The analysis archive and its findings are credited to **https://github.com/NoNameBuilder**. This
document records what this repository verified, what it corrected, and what it
did not adopt. It does not reproduce the supplied text or binaries.

## 1. Artifact and provenance

| Item | Value |
| --- | --- |
| Supplied by | https://github.com/NoNameBuilder |
| URL | `https://temp.sh/FNeBq/SM-T505-firmware-analysis.zip` (temporary link) |
| Archive SHA-256 | `6d750114c4465e894a043ce190ea3a776d25a3bf862d5b50df3d95d73f2368e9` |
| Size | 256,395,350 bytes; 227 entries; 464,205,248 bytes uncompressed |
| Manifest | `MANIFEST.sha256`: 214 listed files, all SHA-256 matches; the manifest itself is the only unlisted file |
| Package target claim | SM-T505/gta4l, firmware T505XXS8CXG1, built 3 July 2024 |
| Manifest generation | `2026-10-12 13:12 UTC` |
| Audit date | 2026-10-29 |

All 214 manifest-listed files match their recorded SHA-256 values `[S]`. That
establishes internal consistency of the package, not authorship, authenticity
or that every binary came from this tablet `[S]`. The archive was downloaded to
`/tmp/opencode/` for inspection and is not vendored. The URL is temporary and
may expire.

## 2. XBL relationship and layout correction

| Image | SHA-256 | Size | ELF identity |
| --- | --- | --- | --- |
| Supplied `firmware/xbl.elf` | `a89abd4d8196374565a1eeef90cd43369deb47a634433598711b5e4cd693eb2f` | 4,194,304 bytes | ELF64 AArch64 `ET_EXEC`, entry `0x0c225908`, 17 program headers, no section headers |
| Captured `xbl.img` | `a09c7b62e639a0565c30b7a3b61e2d68f2a07bfe9c3199d38f9af17bdf9e073e` | 4,194,304 bytes | Same ELF identity and program-header layout |

The two images have the same 15 `LOAD` program headers and the same entry
branch, `0x0c225908` to `0x0c2257a8` `[S]`. They are not the same bytes: the
main executable segment at `0x0c225000` hashes `56d37d20...` in the supplied
image and `9c2f15d4...` in the captured image, and the `0x5fc00000` segment
hashes `bce4ec44...` and `efd843c2...` respectively `[S]`. The program-header
layout therefore transfers as a cross-check, but code-level conclusions from
the supplied image do not automatically describe the captured bootloader `[S]`.

The 15 `LOAD` segments span these virtual-address/memory-size extents `[S]`:

- `0x0c119000`–`0x0c372000`;
- `0x45e00000`–`0x45f14b20`;
- `0x5fc00000`–`0x5fe20400`.

An earlier summary in this repository omitted the `0x5fc00000` range. The
supplied analysis prompted the correction, which is applied in
`power-retention-evidence.md` and `evidence-baseline.md` `[S]`.

## 3. Useful material not yet adopted

- The supplied per-segment XBL layout, entropy inventory and disassembly are a
  useful cross-check for the captured image, subject to the byte difference
  above and the coverage correction in §4.5 `[S]`.
- The package includes 20 stock device-tree overlays. None is byte-identical to
  the captured live FDT (`da1cb76f...`), so they are cross-check material, not
  the captured boot state `[S]`.
- Extracted EFI applications and RPM disassembly are research inputs. The RPM
  source binary is absent from the package, so its disassembly cannot be
  regenerated from the archive alone `[S]`.
- `firmware/working-port-boot.img` is a 28,020,736-byte Android boot image
  (`112a8dd1...`; kernel 15,597,118 bytes, ramdisk 12,341,695 bytes, page size
  4096) `[S]`. It is consistent with the package's claim that a mainline port
  exists, but this audit did not boot it or verify the kernel identity, and it
  is not a rail, pad or power measurement `[U]`.

## 4. Corrections to the supplied analysis

### 4.1 LCDB voltage table and byte order

The supplied table (`docs/03-voltages.txt` lines 148–166) gives the `lcdb_bst`
maximum as 6,271,000 µV. In archive member `device-tree/fdt_14_model.dtb`, under
`/fragment@58/__overlay__/qcom,pmi632@3/qpnp-lcdb@ec00/{ldo,ncp,bst}/regulator-{min,max}-microvolt`,
the bytes are `00 3d 09 00` = 4,000,000, `00 5b 8d 80` = 6,000,000, `00 47 b7 60`
= 4,700,000 and `00 5f bf b8` = **6,275,000** `[S]`. The supplied text also
describes the minimum values as stored with anomalous endianness; both minimum
and maximum are ordinary big-endian device-tree cells `[S]`. Its recommendation
(`docs/03-voltages.txt` line 461) to write little-endian minimum and maximum
values would be wrong for DTB integer properties `[S]`. The device-tree maximum
is a software constraint; it is not evidence of a hardware absolute-maximum
rating `[S]`.

### 4.2 PMIC interrupt specifier

`interrupts = <0x03 0xec 0x01 0x01>` (`docs/03-voltages.txt` lines 110–130 and
`docs/07-dtb-device-tree.txt` lines 500–512; property
`/fragment@58/__overlay__/qcom,pmi632@3/qpnp-lcdb@ec00/interrupts` in
`device-tree/fdt_14_model.dtb`) is the four-cell QPNP form: SID 3, peripheral
`0xec`, IRQ index 1 and trigger 1, matching `qpnpint_irq_domain_translate` in
`spmi-pmic-arb.c` `[S]`. It is not a GIC SPI tuple, and the third cell is not a
GIC type `[S]`. The inspected LCDB node is under `pmi632@3`, not `pmi632@2`
`[S]`.

### 4.3 XBL stack and BSS addresses

The raw listing (`analysis/disassembly/xbl.asm.zst`, addresses `0xc22596c` and
`0xc225978`) is `mov x0,#0xb000` followed by `movk x0,#0xc27,lsl #16`, and the
same pattern with `0x8000`; these produce `0x0c27b000` and `0x0c278000` `[S]`.
The supplied rendition (`docs/16-XBL-UEFI-BOOTLOADER.txt` lines 116–138)
normalizes each pair into a single `mov` and writes `0xc27b0000` and
`0xc2780000`, the raw values shifted one hex digit left (×16), repeating them
at lines 132, 137 and 138 `[S]`.
The supplied program-header table (`docs/16-XBL-UEFI-BOOTLOADER.txt` line 32)
lists the correct `0x0c27b000` as the RW load at file offset `0x051880` `[S]`.
Its row for `0x0c278000` (line 31) annotates the segment as `.bss (3 KB,
zeroed)`, but the prose, the zeroing-loop constant `#0x3000`, the
`0x0c27b000`−`0x0c278000` difference and the supplied text's own footnote at
line 139 ("exactly the 12 KB being zeroed") are all 12 KB, and `readelf -l`
reports `memsz 0x003000` for that LOAD; the 3 KB annotation is another
supplied inconsistency `[S]`.
The corrected addresses are consistent with the layout in §2: `0x0c27b000` and
`0x0c278000` lie in the `0x0c119000`–`0x0c372000` extent, while `0xc27b0000`
is outside every LOAD range `[S]`.

### 4.4 RPM reproducibility and build mismatch

`analysis/disassembly/rpm.asm.zst` is included, but the archive has no
`rpm.mbn` or `rpm.img` member. The disassembly cannot be regenerated from the
archive alone `[S]`.

The listing is also a different RPM build from the captured image: its build
metadata is T505XXS8CXG1 (2024-07-03) while the captured `rpm.img` is
T505NDXS6CXB1 (2024-03-05); the entry differs (`0x4623e41` versus
`0x4623e51`), code is displaced by 8/16 bytes in many regions, and the listing
decodes everything in ARM mode although the captured entry code is Thumb `[S]`.
It is usable only as a rough function-layout map; any byte-level claim about the
captured image must be re-disassembled from the captured bytes `[S]`.

### 4.5 XBL disassembly coverage

The supplied report (`docs/16-XBL-UEFI-BOOTLOADER.txt` lines 63–73) states
792,829 address lines total, 57,418 from the `0x5fc00000` blob and about
735,400 remaining. The same passage lists only 15,536 lines for `PT_LOAD#6`,
31,760 for `PT_LOAD#13` and 57,418 for `PT_LOAD#16`; those three numbers sum to
104,714 and reconcile with neither the 792,829 total nor the 735,400 remainder
(792,829 − 57,418 = 735,411) `[S]`. Re-counting the packaged
`analysis/disassembly/xbl.asm.zst` by address lines gives 792,829 total but
544,346 in `PT_LOAD#16`, leaving 248,483 `[S]`. The packaged listing also
assigns 2,707 lines to `PT_LOAD#2`, 79,637 to `PT_LOAD#6`, 14,004 to
`PT_LOAD#10`, 28,942 to `PT_LOAD#11` and 123,193 to `PT_LOAD#13` `[S]`. Even
the 248,483 remainder is not established as real code by line counting `[S]`.

## 5. Effect on the decision

No SDHC power, electrical, retention or authorization gate closes. The supplied
archive is a lead and cross-check source, not a measurement: its XBL executable
bytes differ from the captured image, its device-tree overlays are not the live
FDT, and it contains no rail, pad or power-good observation. **NO-GO** remains
`[S]`.

## 6. Audit method, reproduction and limits

The audit used the package's own manifest, Python `zipfile`/`hashlib` for
members and hashes, ELF header and program-header parsing, device-tree property
parsing, the archive's XBL disassembly for instruction cross-checks, and the
pinned `spmi-pmic-arb.c` for the interrupt-specifier interpretation `[S]`.

### 6.1 Reproduction

From the extracted package root (`SM-T505-analysis/`):

```sh
sha256sum -c MANIFEST.sha256
readelf -h firmware/xbl.elf
readelf -l firmware/xbl.elf
dtc -I dtb -O dts device-tree/fdt_14_model.dtb | \
  grep -A 45 'qpnp-lcdb@ec00'
python3 - <<'PY'
import collections, re, subprocess
out = subprocess.run(
    ['zstd', '-dc', 'analysis/disassembly/xbl.asm.zst'],
    capture_output=True, text=True, check=True).stdout
section = ''
counts = collections.Counter()
for line in out.splitlines():
    match = re.match(r'Disassembly of section (.+):', line)
    if match:
        section = match.group(1)
    if re.match(r'^\s*[0-9a-f]+:', line):
        counts[section] += 1
print(counts)
PY
python3 verify.py
```

### 6.2 Limits

The package's own `verify.py` exits 0, but its checks cover quoted instruction
counts, string counts, file cross-references, two hardware-part presence claims
and document counts; they do not cover the voltage, interrupt-specifier,
XBL-address or disassembly-coverage claims corrected in §4 `[S]`. The audit did
not authenticate the package, reproduce the RPM disassembly, boot the supplied
working-port image, or measure any rail, pad or power state `[U]`.

## 7. Exact-build vendor package

`[S]` A Samsung FUS package for the captured build `T505NDXS6CXB1` was
downloaded on the host with `samloader` 2.2.0. The archive is 4,385,869,977
bytes with SHA-256
`e575de6bde656dc7c99e23070906caab9d571b3f369b70e1b579cd74b135fd9c`.
The CSC/region code and the package filename are withheld at the owner's
request; they are not needed for the comparison below and must not be inferred
from the build identifier `[S]`.

`[S]` The BL members are LZ4 frames. The captured partition images are the
decompressed images padded to the partition size, so the comparison is
full-image where the sizes agree and prefix-equal otherwise. All compared BL
images match:

| Stock member | Captured partition | Result |
| --- | --- | --- |
| `xbl.elf` | `partitions/xbl.img` | Full SHA-256 match |
| `abl.elf` | `abl.img` | Full SHA-256 match |
| `tz.mbn` | `partitions/tz.img` | Full SHA-256 match |
| `hyp.mbn` | `partitions/hyp.img` | Full SHA-256 match |
| `xbl_config.elf` | `partitions/xbl_config.img` | Prefix match; captured image padded |
| `rpm.mbn` | `partitions/rpm.img` | Prefix match; captured image padded |
| `devcfg.mbn` | `partitions/devcfg.img` | Prefix match; captured image padded |
| `uefi_sec.mbn` | `partitions/uefisecapp.img` | Prefix match; captured image padded |
| `apdp.mbn` | `partitions/apdp.img` | Prefix match; captured image padded |
| `msadp.mbn` | `partitions/msadp.img` | Prefix match; captured image padded |

`[S]` In the AP member, `boot.img` (100,663,296 bytes,
`1f42eb045ba8eba194c7eb4c944fe6391c068b491003be78a3814ac07d2061c6`) and
`dtbo.img` (25,165,824 bytes,
`67546984d6be0d7bf44295dcd9a66745a4a0929993223375a403c537baf39d02`) are
byte-identical to the captured partitions. The
captured pre-test backup `boot-before-unixnu-test-20261027.img` is also
byte-identical to the stock `boot.img` `[S]`.

`[S]` The captured `recovery.img` is TWRP and therefore differs from the stock
recovery image; that is expected and is not a provenance failure `[S]`.

`[U]` The captured `vbmeta.img` does not match the AP `vbmeta.img.lz4` member,
including as a prefix. The reason is unresolved: the capture is post-unlock and
the vbmeta partition may have been rewritten, while the AP member may also be
matched with a different BL `vbmeta` image. This audit does not conclude that
the captured vbmeta is modified or that it is stock `[U]`.

`[S]` This comparison establishes that the captured BL chain and the
boot/dtbo images correspond to the `T505NDXS6CXB1` package, subject to the
unresolved vbmeta item. It does not authenticate the package beyond the FUS
download and TLS, does not cover userdata or every partition, and adds no rail,
pad or power measurement `[S]/[U]`.

`[S]` The firmware archive and all extracted images remain outside the
repository. Do not commit them, the region code, device-unique identifiers or
the capture's private partition images.
