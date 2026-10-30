# SPMIDxe `+0x28` static analysis

Status: host-only static analysis. No device connection, hardware access,
compilation, flashing or power change was made. This document resolves the
instruction-level shape of the SPMI protocol `+0x28` entry and states what that
shape does and does not establish about the PM6125 rails.

## Inputs

| Input | Path | SHA-256 |
| --- | --- | --- |
| SPMI DXE driver | `src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SPMIDxe/SPMIDxe.efi` | `50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254` |
| PMIC DXE driver | `src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi` | `db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce` |
| Arbiter cross-reference | `drivers/spmi/spmi-pmic-arb.c` at pin `cf5f763f…` | `320e71b64915eb4e32be90b49fd65f12da2241e877bf4f68f0c0c1eb4ab3e28d` |

Both images are the exact bytes recorded as booted in `firmware-handoff.md`
(boot image SHA-256 `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`).
`SPMIDxe.efi` has `.text` at VMA `0x1000`, size `0x7000`; `.data` at `0x8000`.
`PmicDxeLa.efi` has `.text` at `0x1000`, size `0x1c000`; `.data` at `0x1d000`.
For both files the PE section VMA equals the file offset, so the addresses below
are VMAs. Disassembly was checked with `llvm-objdump -d --triple=aarch64`.

This document does not re-derive how the PMIC driver reaches `+0x28`; that path is
recorded in `firmware-handoff.md`. It only analyses what `+0x28` does once called.

## Entry path

The SPMI protocol entry at offset `+0x28` resolves to `0x164c`:

```text
0x164c  cbz  x0, 0x169c            ; null handle -> 0xc
0x165c  adrp x20, 0x8000
0x1664  ldr  x8, [x20, #0x1b8]     ; boot-services table
0x1668  ldr  x8, [x8, #0x18]       ; RaiseTPL
0x166c  blr  x8                    ; raise to TPL 8
0x1674  bl   0x2d48
0x1678  ldr  x9, [x20, #0x1b8]
0x1684  ldr  x9, [x9, #0x20]       ; RestoreTPL
0x1688  blr  x9
0x1698  ret
```

`0x2d48` fetches the channel descriptor and the runtime config, then calls
`0x2e20`:

```text
0x2d58  bl   0x2ce8                ; descriptor = array[(0>>4)&0xf]
0x2d5c  mov  x19, x0
0x2d60  bl   0x2d2c                ; config pointer
0x2d64  cbz  x19, 0x2d80           ; null descriptor -> 0xc
0x2d68  cbz  x0,  0x2d80           ; null config -> 0xc
0x2d6c  ldrb w1, [x0]              ; channel byte = config[0]
0x2d70  mov  x0, x19                ; descriptor
0x2d7c  b    0x2e20                ; tail call
```

`0x2ce8` reads the flag at `[0x84f8]`, bounds `(0 >> 4) & 0xf = 0` against
`[config+0x12]`, and returns `[config+0x18] + index*0x38`. `0x2d2c` returns the
config pointer. The descriptor is passed as `x0` and the channel byte is
`config[0]`.

## What `0x2e20` does

`0x2e20` is a bounded read/modify/write loop over a bitmap-selected set of
channel positions. The verified instruction sequence is:

```text
0x2e20  ldr  x12, [x0]             ; base
0x2e24  and  w14, w1, #0xff        ; channel (low byte)
0x2e28  mov  w11, #0x104
0x2e34  movk w11, #0x20, lsl #16   ; 0x200104
0x2e2c  mov  w15, #0x108
0x2e38  movk w15, #0x20, lsl #16   ; 0x200108
0x2e30  mov  w13, #0x2300000
0x2e3c  add  x14, x12, x14, lsl #16
0x2e4c  add  x11, x12, x11         ; read base
0x2e50  add  x12, x12, x15         ; write base
0x2e54  add  x13, x14, x13         ; bitmap base
0x2e5c  b    0x2eb0

0x2eb0  ldr  x16, [x0, #0x28]      ; channel metadata pointer
0x2eb4  ldrh w15, [x16, #0xc]      ; count
0x2eb8  cmp  w10, w15
0x2ebc  b.lo 0x2e60

0x2e60  ldr  w16, [x13, w8, uxtw]  ; bitmap word, w8 = outer*4
0x2e70  and  x1, x15, #0xffffffff  ; bit
0x2e74  lsl  w2, w14, w1
0x2e78  and  w3, w2, w16
0x2e7c  cbz  w3, 0x2e90            ; skip unset bit
0x2e80  ldr  w4, [x11, w17, sxtw]  ; read  0x200104 + n*0x10000
0x2e84  ldr  w5, [x12, w17, sxtw]  ; read  0x200108 + n*0x10000
0x2e88  bfxil w5, w4, #0, #8       ; low 8 bits of w4 -> w5[7:0]
0x2e8c  str  w5, [x12, w17, sxtw]  ; write 0x200108 + n*0x10000
0x2e90  add  x15, x15, #1
0x2e94  add  w17, w17, #0x10000
0x2e98  cbz  w16, 0x2ea4           ; zero word: inner loop skipped
0x2e9c  cmp  x15, #0x1f
0x2ea0  b.ls 0x2e70                ; bits 0..31
```

With `n = outer*32 + bit`, the touched pair is:

- read `base + 0x200104 + n*0x10000`
- write `base + 0x200108 + n*0x10000`

The loop bound is `count = u16 [channel_meta + 0xc]`, and each outer iteration
consumes one 32-bit bitmap word at `base + 0x2300000 + channel*0x10000 + outer*4`.
A zero word skips all 32 inner iterations. There is no retry and no queue or
commit step in this function.

## Runtime base and configuration

`[S]` `0x2e20` reads `base` from `[descriptor + 0x00]`. The descriptor array is
`[config + 0x18]` with a `0x38`-byte stride; `0x2ce8` selects entry
`(w0 >> 4) & 0xf` after bounds-checking it against `config[0x12]`. The `+0x28`
entry calls `0x2ce8(0)`, so it always uses entry 0. `0x164c` checks its handle
only for null (returning `0xc`) and then calls `0x2d48()` with no argument, so
the operation targets the global configuration, not a per-handle object.

`[S]` The image's `.data` initially contains the configuration below. `.data` is
writable (`IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE`), so these are initial
values, not immutable constants:

| Location | Value | Meaning |
| --- | --- | --- |
| `0x8198` | `0x8178` | config pointer |
| `0x8178 + 0x00` | `0x00` | channel byte (`config[0]`) |
| `0x8178 + 0x12` | `0x0001` | descriptor count |
| `0x8178 + 0x18` | `0x8140` | descriptor array |
| `0x8140 + 0x00` | `0x01C00000` | descriptor base |
| `0x8140 + 0x28` | `0x811E` | channel metadata |
| `0x811E + 0x00` | `0x0100` | APID-table entry count (256) |
| `0x811E + 0x0C` | `0x0010` | bitmap-word count (16) |

`[S]` The descriptor-base override path is small and image-local. `0x38c8`
(called once from `0x266c` via `0x26b0`) loads `[0x8198]` (`0x8178`) through
`0x38dc`, loads `[config + 0x18]` (`0x8140`) through `0x38e0`, calls
`0x38b0(0x8140)`, reloads `[0x8198]`, and stores it at `[0x8868]` (`0x38f4`).
`0x38b0` is exactly:

```text
38b0  adrp x8, 0x8000
38b4  ldr  x8, [x8, #0x878]   ; [0x8878]
38b8  cbz  x8, 0x38c0
38bc  str  x8, [x0]           ; descriptor base
38c0  mov  w0, wzr
38c4  ret
```

The `.data` layout around the globals is:

| Location | Initial value | Role |
| --- | --- | --- |
| `0x8868` | `0` | cached config pointer, written by `0x38f4` |
| `0x8870` | `0` | event handle; `0x20f0` stores one pointer here |
| `0x8878` | `0` | descriptor-base override, read only by `0x38b4` |
| `0x8880` | `0` | descriptor `+0x10` target buffer |
| `0x8a80` | `0` | descriptor `+0x18` target buffer |

`[S]` A host-only bounded scan of the pinned image (SHA-256 `50be2e1a…`)
decodes every `ADRP`/`ADR` site and follows direct page-relative arithmetic,
general-purpose loads and stores with immediate or pre/post-index addressing,
pointer values loaded from the initial `.data` image, direct conditional and
unconditional branches, caller-saved clobbers at calls, and bounded direct
`BL` recursion into `.text` (depth 2). It models a conservative whitelist of
register-only instructions; any other instruction stops the path and is
reported. Stores invalidate the `.data` pointer slots they overlap, modeled
calls propagate that invalidation to the caller, and indirect or truncated
calls conservatively invalidate every `.data` pointer slot; revisited states
are reported and conservatively invalidate every `.data` pointer slot as
well. The scan found 64
`adrp ..., 0x8000` sites and exactly one reference to `0x8878`: the 8-byte
load at `0x38b4`. It found no store that intersects `[0x8878, 0x8880)`. The
scan resolves the caller's descriptor-base store at `0x38bc` to `0x8140`, the
cached-config store at `0x38f4` to `0x8868`, the event-handle stores at
`0x210c`/`0x2188` to `0x8870`, and a disjoint store at `0x4168` to `0x9080`;
the last is above the override but does not overlap it. It also found no 4- or
8-byte literal `0x8878`, no MOVZ/MOVK immediate building `0x8878`, and no
`.reloc` entry targeting `0x8878`. The scan reports 114 accesses, 22 resolved
stores, 317 calls, 469 boundaries (157 revisited-state, 113 step, 90
unsupported, 81 call-depth, 28 indirect-branch), 150 unresolved non-stack loads
and 36 unresolved non-stack stores. It treats base registers 29/31 as stack/frame and does not
model SIMD/FP accesses, register-offset addressing, indirect branches, calls
beyond the modeled depth, or whether a decoded site is reachable code.
Reproduce with `scripts/test-gta4l-spmidxe-descriptor-base.py`.

`[S]` Manual instruction inspection, separate from the automated scan, of the
two bounded copy paths shows writes ending at most at `0x8824`: `0x2a78` copies
`0x30` bytes from slot starts `0x8718`–`0x8818` to `0x86e8`–`0x87e8`, and
`0x3490` writes 12 bytes at slot starts `0x8718`–`0x8818`. Both are below
`0x8878`. The only writer near the override is `0x20f0`, which receives
`0x8870` from `0x2704`/`0x270c` and stores one pointer there (`0x210c`,
`0x2188`), not at `0x8878`.

`[C]` In the pinned image's initial state the override is zero, so `0x38b0`
leaves the descriptor base at `0x01C00000`; the override path is armed only if
`0x8878` becomes non-zero. `0x8878` is in writable `.data`, so this is a static
image-local result, not immutability by construction. The scan stops at
instructions outside its register-only whitelist and reports them; it
invalidates `.data` pointer slots written by modeled stores and calls and
conservatively invalidates all `.data` pointer slots after indirect or
truncated calls and on revisited states. Internal indirect/computed/aliased stores, runtime agents,
corrupted pointers, DMA and different firmware revisions are still not
excluded, and the scan does not establish reachability. The descriptor-base
question therefore narrows to "the bounded scan and manual copy-path
inspection found no write to `[0x8878, 0x8880)`, but internal
indirect/computed and runtime/external writes remain unestablished" `[U]`.

`[S]` `0x01C00000` is 64 KiB aligned, so the 256-byte-alignment condition for
the direct-address exclusion holds. It is the SPMI PMIC-arbiter register block:
`base + 0x200000 = 0x01E00000` (`chnls`), `base + 0x2300000 = 0x03F00000`
(`intr`), `base + 0x40900 = core + 0x900`, and `base + 0xa700 = cnfg + 0x700`.
The initial config selects channel 0, one descriptor, a 256-entry APID table and
a 16-word owner-status bitmap.

## What the shape establishes

The low 16 bits of the status/clear pair are `0x0104` (read) and `0x0108` (write);
the `n*0x10000` term cannot change them. The bitmap word adds `0x2300000` plus a
word index, so the whole set stays inside the arbiter block. With the base at
the arbiter block, the routine's only store is to offset `0x108` in each channel
window. It does not write the command register at offset `0x00`, so it does not
issue an SPMI transaction. This is a direct-address exclusion, not a rail-state
guarantee.

The offsets and stride match the pinned Linux PMIC-arbiter v5 accessors
(`drivers/spmi/spmi-pmic-arb.c`, SHA-256 `320e71b6…`):

| SPMIDxe address | Linux v5 accessor | Meaning |
| --- | --- | --- |
| `base + 0x200100 + n*0x10000` | `pmic_arb_acc_enable_v5()` (`:1680-1683`) | access-enable; bit 0 is `SPMI_PIC_ACC_ENABLE_BIT` |
| `base + 0x200104 + n*0x10000` | `pmic_arb_irq_status_v5()` (`:1714-1717`) | per-APID interrupt status |
| `base + 0x200108 + n*0x10000` | `pmic_arb_irq_clear_v5()` (`:1748-1751`) | per-APID interrupt clear |
| `base + 0x2300000 + ch*0x10000 + w*4` | `pmic_arb_owner_acc_status_v5()` (`:1656-1658`) | owner/access-status bitmap word |
| `base + 0x40900 + n*4` | `pmic_arb_apid_map_offset_v5()` (`:1774-1776`) | APID map (`core + 0x900`) |
| `base + 0xa700 + n*4` | `pmic_arb_apid_owner_v2()` (`:1791-1793`, used by v5) | APID owner (`cnfg + 0x700`) |

`[S]/[C]` Under the v5 interpretation, `0x2e20` is an arbiter interrupt
status-read/clear routine. For each APID whose owner-access-status bit is set it
executes:

```text
status = read32(irq_status(n));
clear  = read32(irq_clear(n));
clear  = (clear & ~0xFF) | (status & 0xFF);
write32(irq_clear(n), clear);
```

That is a read-modify-write of the whole 32-bit clear register, not a
low-byte-only write: bits 31:8 are whatever the hardware returns when
`irq_clear(n)` is read, and are written back unchanged by the software. The
routine does not establish that writing those read-back bits is harmless, and it
is not a PMIC regulator access.

`[S]` The same routine runs during initialization, not only through the protocol
entry: `0x266c` calls `0x2d90` for each configured descriptor, and `0x2d90`
calls `0x2e20` before walking the APID table. Its effects are therefore not
specific to the `ExitBootServices` callback.

`[S]/[C]` Under the v5 interpretation, `0x2ec8` writes bit 0 of the
access-enable register (`base + 0x200100 + n*0x10000`) from its fourth argument,
preserving the other 31 bits. The only call in this image is from `0x2d90`,
which passes `w3 = 0` (`0x2de8`); after its per-entry flag and channel checks it
therefore **clears** bit 0. The helper itself does not inherently enable access.

`[S]/[C]` `0x2d90` is the init-time companion: it calls `0x2e20`, then walks the
256-entry v5 APID mapping table at `base + 0x40900`. For each entry whose
mapping word has a nonzero PPID field (bits 8–19) and whose per-entry flag/channel
check passes, it calls `0x2ec8` with value 0.

The function only writes when the selected owner-status bit is set, so the
reachable APID set is the runtime contents of the `intr` bitmap, which this
static read does not resolve.

### Address envelope and bounds

`[S]` The status/clear loop bound is `[channel_meta + 0x0C]` (`0x0010`, 16
words), so `n = outer*32 + bit` ranges over 0–511. The initialization walk in
`0x2d90` instead uses `[channel_meta + 0x00]` (`0x0100`, 256) and visits APIDs
0–255. These are different fields: the 16-word bitmap can select positions above
the 256-entry table, and `0x2e20` still issues `irq_status`/`irq_clear` for them.
The 512-position upper bound matches `PMIC_ARB_MAX_PERIPHS 512` in the pinned
arbiter driver (`spmi-pmic-arb.c:99`).

`[S]` For the initial configuration (base `0x01C00000`, channel 0) the reachable
arbiter addresses are:

| Access | Range |
| --- | --- |
| status read | `base + 0x200104` … `base + 0x21F0104` (`n` = 0–511) |
| clear read-modify-write | `base + 0x200108` … `base + 0x21F0108` (`n` = 0–511) |
| bitmap word read | `base + 0x2300000` … `base + 0x230003C` (16 words) |
| APID-table walk (`0x2d90`) | `base + 0x40900` … `base + 0x40CFC` (`n` = 0–255) |
| APID owner (`0x3904` recovery) | `base + 0xa700 + n*4` |

`[S]` `0x2e20` contains exactly one store, to offset `0x108` in the selected
channel window. It does not write the command register (`PMIC_ARB_CMD` at
`chnls + offset + 0x00`), so it does not issue an SPMI transaction to a PMIC
peripheral. The direct-address exclusion therefore follows from the access type,
not only from offset alignment. It holds only while the base is the established
arbiter block at `0x01C00000`; a different base would change the destination, so
the exclusion must not be extended to an unverified mapping.

### Access phases

| Phase | Path | Established accesses |
| --- | --- | --- |
| Initialization | `0x266c` → `0x2d90` → `0x2e20`, `0x2ec8` | IRQ status/clear for bitmap-selected APIDs; clears access-enable bit 0 after checks; reads the APID mapping table |
| Normal transaction | `0x2730` → `0x27bc` → `0x34c0`/`0x3530` | channel command/status/data path; the full sequence is not re-derived here |
| Recovery | `0x27bc` → `0x3904` → `0x39ec`/`0x3350`/`0x31d0` | APID owner and mapping writes at `base+apid*4+0xa700` and `base+apid*4+0x40900` |
| Exit callback | `0x120a8` → `0x7f18` → `0x134b4` → `0x164c` → `0x2d48` → `0x2e20` | the same IRQ status/clear routine as initialization |

## Claim classes

| Class | Meaning | Examples in this document |
| --- | --- | --- |
| Confirmed | Bytes or instructions in this image | descriptor offsets, `0x38` stride, the four-step clear sequence, 512/256 bounds |
| Conditional | True only if the arbiter is v5 and the base holds | register names, IRQ status/clear meaning, owner/APID table roles |
| Unresolved | Not decidable from the pinned set | runtime bitmap, `irq_clear` read-back value, internal indirect/computed or external writer of `[0x8878]`, PM6125 layout |

The register-name mapping in the table above is conditional on the v5
interpretation; the addresses and instruction sequence are confirmed.

## Related functions

- `0x27bc` is the transaction core, not `0x2e20`. It checks state at
  `[0x84f8]`, validates the channel against `[cfg+0x12]`, uses a 10-entry
  `0x30`-byte ring, calls `0x34c0` (lookup) and `0x3530` (execute), and on
  failure enters recovery through `0x39d4`/`0x39cc` → `0x3904`. `0x3904`
  re-maps the APID through `0x39ec`, `0x3350` and `0x31d0`. That path performs
  arbiter MMIO writes, not only RAM bookkeeping: `0x3ab8` stores to
  `base+apid*4+0xa700` (`cnfg+0x700`, APID owner) and `0x3b0c` stores to
  `base+apid*4+0x40900` (`core+0x900`, APID mapping). These are arbiter
  owner/APID registers, not PMIC regulator registers. Reachability is
  conditional, so this does not establish that recovery ran in the recorded
  boot.
- `0x2730` takes a lock via `0x21ac` on `[0x8870]` and calls `0x27bc` with op
  `1`.
- `0x2ec8` is an initialization-time read/modify/write of the access-enable bit
  at `base + 0x200100 + (index << 16)`, preserving the other 31 bits. Its fourth
  argument supplies bit 0; the only call passes 0, so it clears. It is reached
  from `0x2d90`, which `0x266c` calls for each configured channel during
  initialization. It is not reached from `+0x28`.
- `0x13dc` and `0x1494` share the null-check/TPL/`0x2730`/restore shape of
  `0x164c`.

## Not established

- The runtime contents of the `intr` owner-status bitmap, and therefore which
  APIDs `0x2e20` actually processes at a given moment.
- What hardware returns when `irq_clear(n)` is read, and therefore the effect of
  writing bits 31:8 of that read value back.
- Whether an internal indirect/computed/aliased store or an external runtime
  writer populates `[0x8878]`; the bounded scan finds one read and no write
  intersecting the 8-byte override, but it does not cover every addressing mode
  or establish that every decoded site is reachable.
- Whether any run-time path outside the pinned image changes the descriptor base
  or the configuration after initialization.
- Whether the UEFI driver's arbiter block is exactly the Linux v5 revision
  (the offset/stride match is exact; the version register was not read).
- Whether PM6125 has this peripheral layout, and any live rail voltage,
  readiness or effect.
- Any PMIC-side effect of clearing arbiter interrupt status. The routine writes
  the arbiter's `irq_clear`, not the PMIC's `QPNPINT_REG_LATCHED_CLR`/`EN_CLR`.

None of this changes the disposition in `pmic-audit.md`: live PMIC/SPMI access
remains blocked. This analysis resolves the `0x0104`/`0x0108` pair, the
`0x2ec8` bit, and the image's initial base; it does not establish the callback's
complete effect set, because internal indirect/computed and runtime/external
paths remain open.
