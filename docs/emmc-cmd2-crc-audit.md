# gta4l eMMC CMD2 CRC audit

**Status:** read-only reference audit, updated with the 2026-10-31
current-source hardware result, the two follow-up response-window captures and
the 2026-11-01 `fmt=4` fail-closed runs. The audit itself made no source
change; the diagnostic images were built, flashed and run separately.
Comment-only corrections to `package/Gta4lEmmc.c` and `package/Gta4lEmmc.h`,
followed by the diagnostics-only captures in sections 2.7 and 10, were applied
afterwards. The `cmd2window` run reached CMD3: CMD2 completed and the failure
moved from the CMD2 `CMD_CRC` record to a CMD3 completion timeout. The later
`fmt=2` run passed CMD3 and CMD9 and stopped at a CMD7 R1b completion timeout;
see section 2.8 and `docs/emmc-cmd7-timeout-finding.md`. The `fmt=4` fail-closed
image was run on 2026-11-01 and stopped at CMD2 with the same `-8` (`CMD_CRC`)
record and response window as the section 2.6 image (sections 2.9 and 2.10); the
CMD7 capture was not exercised. A later candidate added only an 8-us delay
after an accepted ready CMD1. On 2026-11-01 it completed the full identification
sequence (`rc=0`) on the owner's SM-T505N and on two owner-reported SM-T505
units, which exercised the CMD7 capture (section 2.11). The owner then supplied
stage-1 full-probe reports from the same SM-T505N and one of those SM-T505s;
both completed identification and the read-only `CMD17` reads of blocks 0 and
1 (section 2.12). TWRP on `RECOVERY` was unchanged at
`14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae` in the
recorded flash records. The owner reports the first `fmt=4` run on an SM-T505
and a later BOOT-only run on the owner's SM-T505N; the results agree 1:1 per the
owner, and the second run's flash record, readback and photo are in section
2.10.

**Hardware-tested source (2026-10-31 image):** `Gta4lEmmc.c 722110a5…`,
`Gta4lEmmc.h 7abc7d87…`, `Gta4lEmmcProbe.c d541514b…`, simulator
`5262362d…`, probe simulator `18f2637a…`, probe test `52c94ccb…`.

**Current working tree at the 2026-11-01 `fmt=4` run:**

| Artifact | SHA-256 |
| --- | --- |
| `src/UniXNU-gta4l/package/Gta4lEmmc.c` | `e493b3eb6db421e81436ace4e3d81f0aded33e42c1713330359ab2b39b895d5e` |
| `src/UniXNU-gta4l/package/Gta4lEmmc.h` | `e0a2830cc18ec2faba2e60faa0f17e13eb2b24162e060e5f75328791c0f1e199` |
| `src/UniXNU-gta4l/package/Gta4lEmmcProbe.c` | `932552a2b79297a14bce2fb40ab0702f8696d995bafd55cdeddc2b4bc6397f4a` |
| `src/UniXNU-gta4l/package/Gta4lSdhciHostInit.c` | `3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb` |
| `src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c` | `fd585d4973a7708bf8ff13ac66bcde66162092dc019104eaff46fb261a9f129e` |
| `src/UniXNU-gta4l/scripts/sim-gta4l-emmc-probe/harness.c` | `fca9409a9c77a009e97f92004a33854153db02f69fcb098d1d5b48d0e0f6ca73` |
| `src/UniXNU-gta4l/scripts/test-gta4l-emmc.py` | `97811c8b767b58986e5b5b6bddc29d0cf627b508fb25bddcb15d3f2c33b88507` |
| `src/UniXNU-gta4l/scripts/test-gta4l-emmc-probe.py` | `da8dc01328d6fcac51a23420da363d7d79923168e0f93ab91e3b56f2a2532262` |

The 2026-11-01 `cmd1gap` candidate source is the same tree with only the delay
added: `Gta4lEmmc.c 874e6878…`, `Gta4lEmmc.h 5dd223f7…` and
`Gta4lEmmcProbe.c 932552a2…`. Its image and three-unit result are in section
2.11.

The section 2.6 image (`33311a32…`) predates the `cmd1rsp=`/`pre2rsp=` capture.
The section 2.7 `cmd2window` image is the later diagnostics-only build and is
the first hardware run of those fields. The section 2.9 image is built from the
current working-tree hashes above and is the first `fmt=4` hardware run. The
host probe simulator runs the stage-3 identification sequence through CMD2
failure and CMD3/CMD9 timeouts, and the successful identification path, and it
rejects twenty-one mutation controls for the failure-report reads, response
windows and success capture.

**Hardware artifacts:**

- Stage-2/3 hardware-tested kernel:
  `1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a`
  (`src/UniXNU-gta4l/docs/emmc-driver.md:328,360`).
- Older flashed failure-record kernel:
  `be30e9f4f5d88a87da2943a8e1c977d8b5c96151ed78051cdae35782eaed186e`
  (`src/UniXNU-gta4l/docs/emmc-driver.md:383`); that image was not booted.
- Current-source flashed and run kernel:
  `12ebe94c75803579af4536c20233f92b75fb67fcc1b4ec73c5d728b100be6ec8`;
  boot image `33311a32…` (85,688,320 bytes). The stage-3 report, transcript
  and photo are in section 2.6.
- `cmd2window` flashed and run kernel:
  `4c1cd5e1b1d8faaad2fa5f98687c103228fca595be481f8014fc1913027ebbef`;
  payload `177e5179…`; boot image `b119f780…` (85,688,320 bytes). The stage-3
  CMD3-timeout report and photo are in section 2.7.
- The current failure path snapshots raw `RESPONSE0..3`
  (`Gta4lEmmc.c:84-116`) and prints `rsp=` and `cmd1=`
  (`Gta4lEmmcProbe.c:425-456`). The older `be30e9f4` image predates those
  fields; the current `33311a32` image contains them. The working tree also
  captures `cmd1rsp=`/`pre2rsp=` (section 10); the `33311a32` image predates
  that capture, while the `cmd2window` image contains it.

---

## 1. Executive conclusion

The portable CMD2 path is semantically equivalent to mainline Linux and is
covered by the simulator. The recorded result is `-8`, which the core maps from
the SDHCI command-CRC error bit `0x0002`. CMD2 is the only CRC-checked command
in this sequence: CMD0 has no response, CMD1 is sent without CRC checking, and
CMD2 is the next command with `RESP_LONG | CRC`.

The 2026-10-31 current-source run confirms CMD2 for that image by captured
fields: `fail_index=2`, `fail_argument=0`, `fail_command=0x0209`, and
`fail_error=0x0002`. It applies the current POR/vendor/DLL normalization
(`ven=0x18A9C`, `PWRSAVE_DLL` clear, `DLL_RST|DLL_PDN`, RCG programmed for a
nominal 400 kHz), and CMD2 failed in that image. H1 (the old build's
unnormalized state) is therefore excluded as a sufficient explanation for that
run.

The later `cmd2window` run (2.7) did not reproduce the CMD2 CRC. CMD2 completed
(`step=9`; the CMD2 step is assigned only after a successful validated
response), and the failure moved to CMD3: `fail_index=3`,
`fail_command=0x031A`, `fail_argument=0x00010000`, `fail_error=0x0000`,
`fail_normal=0x0000`, `rc=-7` (`ERR_CMD_TIMEOUT`). The failed-command window
holds CID words 2..4 (`5836384d`, `42009005`, `f4661978`; CRC7 `0x3c` encoded
as `0x78`) and a plausible CMD3 R1 status in `RESPONSE0` (`0x00000500`). The
window is therefore real CMD2 data, not the stale CMD1 OCR, and the card
apparently answered CMD3 while the host's completion poll never saw `CC`.

The CMD2 CRC failure is not confined to the section 2.6 image: the 2026-11-01
`fmt=4` fail-closed run (2.9) reproduced the same `fail=`, `rsp=`, register and
`log=` record on the current working tree, and the owner reports the same 1:1
result on an SM-T505 before the SM-T505N run. It did not reproduce in
`cmd2window` or `fmt=2`, which the owner reports were run only on the SM-T505N,
so the outcome is not deterministic across diagnostic builds on that unit. For
the `cmd2window`/`fmt=2` boots, H2 (CMD-line electrical/signal integrity), H4
(card-side CID CRC) and H5 (controller R2 CRC engine) were not supported because
CMD2 delivered the CID and passed its CRC; the `fmt=4` runs restore the CMD2 CRC
record, so those hypotheses are not excluded as contributors. H1 remains
excluded as a sufficient explanation. H3 (rails) remains unmeasured but is not
indicated. H6 (host software/timing) and H7 (actual clock) remain open; the CMD3
completion path (H8) applies only when CMD2 completes and was not reached in
2.9. No cause of the across-build outcome difference is established. The failed
R2 snapshot is never promoted to a CID, and no CRC checking may be disabled.

A timing candidate is specified in `docs/emmc-cmd1-gap-candidate.md`: one
8-us post-ready-CMD1 delay, with a simulator model that is explicitly
hypothetical. It was built, flashed and run on 2026-11-01. The candidate
completed CMD2 and the full identification sequence on the owner's SM-T505N and
two owner-reported SM-T505 units (section 2.11). On the owner's unit the
fail-closed image had failed CMD2, and the delay is the only source delta
between the two images. That supports a timing-sensitive contribution for these
runs; it does not establish the root cause, does not prove the delay necessary
(the earlier `cmd2window`/`fmt=2` builds passed CMD2 without it) and is not a
reliability claim. No general fix is adopted and CRC checking remains enabled.

---

## 2. Evidence inventory

### 2.1 Stage-2 hardware result (PASS, controller-local)

Path: `out/gta4l/ram-root-emmc-probe-evidence/host-report-stage2.txt:1-3`

    GTA4L-EMMC HOST REPORT: rc=0 mci=10000072 pwr=0f clk=0007 hc2=0008
    ven=00318a0c pst=00000000 log=4/1/268435570 @Gta4lEmmcProbe.c:394

Establishes: v5 controller initialized; bus power written; no pending power/IO
request; internal clock stable and card clock enabled; 1.8 V signalling
selected; pad bits set. Does **not** establish card identification or any rail
voltage.

### 2.2 Stage-3 hardware result (FAIL, `-8`)

Path: `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`

    GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 mci=10000072 pwr=0f
    clk=0007 hc2=0008 ven=00318a0c pst=00000000 cfg=00002017 cmd=0000
    0000 log=4/1/268435570 @Gta4lEmmcProbe.c:373

`rc=4294967288` is `0xFFFFFFF8` = `-8` = `GTA4L_EMMC_ERR_CMD_CRC`
(`Gta4lEmmc.h:242`). The SDHCI error bit is
`GTA4L_EMMC_INT_ERR_CMD_CRC = 0x0002` (`Gta4lEmmc.h:129`).

The transcript contains no `fail=`, `rsp=`, `cmd1=`, DLL, vendor-spec3, DDR, or
MND fields. It bounds the error to a CRC-checked command but does not itself
name the command. The current source's richer report is not the report that
produced this transcript. A later current-source run in 2.6 does capture those
fields.

### 2.3 Failure-record image (older, flashed, not run)

Path: `out/gta4l/ram-root-emmc-probe-failrec-evidence/flash-record.json:1-26`

- Image: `boot-ram-root-emmc-probe-failrec.img`, 85,688,320 bytes, SHA-256
  `76745fe7e81ca68025ee428cc96c88f98a392b125ecd18c5cf5d0bbef57c7488`.
- BOOT-only write; pre-flash BOOT backup
  `ef2fb96f0b7e0925c157613953d8fc0cd694019232f13408239e579abf97e936`.
- `RECOVERY` before and after:
  `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`.
- EFS/SEC_EFS backups recorded and verified against `SHA256SUMS.all`.
- `"hardware_run": "pending"`.

This is flash evidence, not run evidence. It must not be cited as a captured
CMD2 failure record. The 2026-10-31 current-source image in 2.6 is the run
evidence.

### 2.4 Handoff snapshot

Path: `src/UniXNU-gta4l/docs/sdhci-handoff-audit.md:130-158`

    ps=01f800f0 clk=0003 pwr=00 hc=00 hc2=0000
    vs=00300a0c vc=742dc8b2 ddr=80040873 dll=00000000

The handoff vendor-spec value is `0x00300a0c`; the DLL config is zero. The
stage-3 `ven=00318a0c` is the handoff value plus pad bits `0x00018000`. The
stage-3 DLL state was not captured.

### 2.5 Electrical contract

Path: `src/UniXNU-gta4l/docs/sdhc-electrical-contract.md:48-52`

- VCC via L24: range 2.704–3.600 V, requested 2.960 V; measured level `[U]`.
- VCCQ/IO via L11: range 1.704–1.952 V, requested 1.800 V; measured level `[U]`.
- SDC1 pads: vendor pinctrl drive/pull values are source facts; actual handoff
  pad state and pad supply are `[U]`.

### 2.6 Current-source hardware result (2026-10-31)

Image: `out/gta4l/ram-root-emmc-probe-cmd2crc-build2-evidence/boot-ram-root-emmc-probe-cmd2crc-build2.img`,
85,688,320 bytes, SHA-256
`33311a329b38da1e0eda3736618d52b7d5922bc4b12a9b85b19316d58e148d65`.
Kernel `12ebe94c75803579af4536c20233f92b75fb67fcc1b4ec73c5d728b100be6ec8`;
probe source `Gta4lEmmcProbe.c d541514b…` and core `Gta4lEmmc.c 722110a5…`.

Flash and report: `flash-record.json`, `stage3-failure-report.txt`; photo
`stage3-cmd2-crc-failure-photo-2026-10-31.jpg` SHA-256
`39f03d3f11b1dfa8a76ad88d944e75c6abdf2ad6f70dfecb2c5edf2d9a4397c6`.
BOOT-only write; pre-flash BOOT backup `28e2e7db…`; RECOVERY unchanged at
`14e11370…`.

    GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 fail=1/2/0/00000209
    /0002/8000/03F800f0/8 rsp=00000000,00000000,80000000,00c0ff80 cmd1
    =3/0/c0ff8080 clocks=1 mci=10000072 pwr=0f clk=0007 hc=00 hc2=0008
    ven=00018a9c v3=02626040 dll=6000642c dst=00000000 ddr=80040873 p
    st=00000000 cfg=00002017 mnd=00000001/000000fc/000000fb cmd=000000
    00 bcr=00000000 apps=00004221 ahb=00000001 step=8 log=5/8/2/163840

Decoding:

- `rc=4294967288` is `-8` (`GTA4L_EMMC_ERR_CMD_CRC`); `fail_error=0x0002` is
  `CMD_CRC`.
- `fail=1/2/0/0x0209/0x0002/0x8000/0x03F800F0/8`: valid=1, index=2,
  argument=0, command=0x0209 (CMD2 index `0x0200`, `RESP_LONG` `0x0001`, CRC
  `0x0008`), normal=0x8000,
  present=0x03F800F0, step=8. `step=8` is the completed CMD1 readiness step.
- `cmd1=3/0/0xC0FF8080`: three attempts, no timeouts, OCR `0xC0FF8080`.
- `ven=0x00018A9C` is the current POR `0xA9C` plus pad bits; `v3=0x02626040`
  has `PWRSAVE_DLL` (bit 3) clear; `dll=0x6000642C` has `DLL_RST|DLL_PDN` set;
  `ddr=0x80040873` remains the POR value; the RCG is programmed for a nominal
  400 kHz (`cfg=0x2017`, `mnd=1/252/251`). This is a register state, not a
  measured clock.
- `rsp=00000000,00000000,80000000,00C0FF80`: a real register snapshot, not an
  image-text-recognition artifact (the 2.9 rerun reproduced it word for word).
  The two nonzero words contain the bytes `80 00 00 00 00 C0 FF 80`, a one-byte
  rotation of `00 00 00 00 C0 FF 80 80`, whose low 32 bits are the recorded CMD1
  response `0xC0FF8080`. This is consistent with a retained or rotated CMD1 window, not
  a plain logical shift; it does not prove retention. The words do not match
  the known CID (redacted here as `1501…1900`) or a byte/word rotation of it,
  and they identify no corrupted CID bits. A failed R2 register snapshot is
  never a valid CID.

**Update (cmd2window and fmt=4 runs):** the later diagnostics-only image in 2.7
did not reproduce this CMD2 CRC failure. CMD2 completed and the failure moved
to a CMD3 timeout; the `fmt=2` run also passed CMD2. The 2026-11-01 `fmt=4`
runs in 2.9 reproduced this exact failure record and response window on current
source, so the CMD2 attribution applies to sections 2.6 and 2.9. The diagnostic
builds differ in outcome and the cause is not established.

### 2.7 cmd2window stage-3 result (2026-10-31)

A second diagnostics-only image added raw `RESPONSE0..3` captures after the
final accepted CMD1 (`cmd1rsp=`) and immediately before CMD2 (`pre2rsp=`),
without changing command semantics, retrying commands, writing response
registers or disabling CRC checking (section 10). Image:
`out/gta4l/ram-root-emmc-probe-cmd2window-evidence/boot-ram-root-emmc-probe-cmd2window.img`,
85,688,320 bytes, SHA-256
`b119f78010bb8e50f319d871f05d94c29b57010757b2a32176c0d9ff778ff19b`;
payload `177e5179…`; kernel `4c1cd5e1…`. Flash and report:
`flash-record.json` in the same directory. BOOT-only write; pre-flash BOOT
backup `99a6cab4…` (the section 2.6 image); RECOVERY unchanged at
`14e11370…` before and after. Photo
`stage3-cmd2window-photo-2026-10-31.jpg` SHA-256
`a9656fc934f1fec9417633054441e28d192d93be3864ee8bf83a377560634305`.

The photographed panic:

    GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967289 fail=1/3/65536/0000
    031a/0000/0000/03f800f0/9 rsp=00000500,5836384d,42009005,f4661978
    cmd1rsp=1/c0ff8080,00000000,00000000,00000000 pre2rsp=1/c0ff8080,0
    0000000,00000000,00000000 cmd1=3/0/c0ff8080 clocks=1 mci=10000072
    pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c v3=02626040 dll=600064
    2c dst=00000000 ddr=80040873 pst=00000000 cfg=00002017 mnd=00000000
    1/000000fc/000000fb cmd=00000000 bcr=00000000 apps=00004221 ahb=00
    000001 step=9 log=5/8/3/0 @Gta41EmmcProbe.c:456

Decoding:

- `rc=4294967289` is `-7` (`GTA4L_EMMC_ERR_CMD_TIMEOUT`), not the section
  2.6 `-8` CRC error.
- `fail=1/3/65536/0x031A/0x0000/0x0000/0x03F800F0/9`: valid=1, index=3
  (CMD3), argument `0x00010000` (RCA 1 in bits 31:16), command `0x031A`
  (`RESP_SHORT | CRC | INDEX | CMD3`), error status `0x0000`, normal status
  `0x0000`, present state `0x03F800F0`, failure step 9.
- `step=9` is `GTA4L_EMMC_STEP_CMD2`. That step is assigned only after CMD2
  returns `GTA4L_EMMC_OK` and its response passes
  `gta4l_emmc_validate_response()` (`Gta4lEmmc.c:1097-1111`), so CMD2
  completed in this run.
- CMD3 was issued with `GTA4L_EMMC_DEFAULT_RCA << 16` and
  `RESP_SHORT | CRC | INDEX` (`Gta4lEmmc.c:1113-1116`). The driver's
  completion poll expired with neither `CC` nor an enabled error bit; the
  post-loop timeout path recorded the failure and returned
  `GTA4L_EMMC_ERR_CMD_TIMEOUT` (`Gta4lEmmc.c:346-360`). The timeout is a
  missing completion, not a latched CRC/index/timeout error bit.
- `cmd1rsp=1/c0ff8080,0,0,0` and `pre2rsp=1/c0ff8080,0,0,0`: the final CMD1
  OCR is valid and still present in `RESPONSE0` immediately before CMD2; the
  pre-command interrupt clear did not clear the response window. The build-2
  OCR-like failed window (`80000000,00c0ff80`) is not reproduced.
- `rsp=00000500,5836384d,42009005,f4661978`: `RESPONSE1..3` are
  `5836384d`, `42009005`, `f4661978`, matching the known CID words 2, 3 and 4
  (the last word carries the CRC7 trailer). `RESPONSE0` is `00000500`, a
  plausible CMD3 R1 status (CURRENT_STATE=2, Identify; READY_FOR_DATA=1),
  not the known CID word 1 (`15010044`). This is consistent with CMD3
  overwriting `RESPONSE0` after CMD2.
- The known CID is `150100445836384d42009005f4661900` (TWRP
  `out/gta4l/ram-root-fs-timer-evidence/twrp-sdhci-node.txt`). CRC7 over the
  15-byte prefix is `0x3c`; the encoded trailer is `0x79`, and the observed
  last byte `0x78` is `CRC7 << 1` with the end bit clear. The window therefore
  contains real CID bits from CMD2, not the stale CMD1 OCR (`c0ff8080`).
- The provenance caveat from section 2.6 still applies: the "known" CID came
  from Linux sysfs, so it is already normalized by the same long-response
  path; it is not an independent raw register dump. The CRC7 match and the
  word-for-word agreement with three known CID words are still evidence that
  the card transmitted the CID.

Interpretation:

- CMD2 now passes. The build-2 CMD2 `CMD_CRC` failure did not reproduce in
  this diagnostics-only build. The only source difference is the added
  response-window reads around CMD1/CMD2, so a timing or state sensitivity is
  possible; the cause of the changed outcome is not established.
- The failure moved to CMD3. `rc=-7` with `fail_error=0` and
  `fail_normal=0` means the completion poll never saw `CC` and no error bit
  was latched, while `RESPONSE0` contains a value matching an R1 card status.
  The card appears to have answered CMD3; the host did not observe completion.
- This is not evidence of CMD-line corruption, a card CID CRC fault, or a
  controller R2 CRC-engine failure. Those were the leading CMD2 explanations;
  the cmd2window run removes their premise for this boot. H2 (signal
  integrity), H4 (card CID CRC) and H5 (R2 CRC engine) are not supported by
  this result. H1 remains excluded as a sufficient explanation. H3 (rails)
  remains unmeasured but is not indicated. H6 (host software/timing) and H7
  (actual clock) remain open, with the CMD3 completion path now the leading
  concrete suspect (H8).
- The raw window is not a valid CID and is not promoted to one: `RESPONSE0`
  was overwritten by CMD3's R1 status, the end bit is clear, and the first CID
  word is absent. No card content was read.

### 2.8 fmt=2 stage-3 result (2026-10-31)

A third diagnostics-only image, `fmt=2`, added the expanded D7 capture to the
failure report and to successful identification (section 10). The owner booted
it and supplied this panic transcription (normalized to one field per line; the
focused finding preserves the extraction verbatim). No original photo or hash
was supplied in this turn.

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967289
fail=1/7/65536/0000071b/0000/0002/03f800f0/11
rsp=00000700,0f5903ff,f6dbffef,8e40400c
cmd1rsp=1/c0ff8080,00000000,00000000,00000000
pre2rsp=1/c0ff8080,00000000,00000000,00000000
c2=1/0001/0000/05f46619,4d420090,44583638,00150100
c3p=1/0000/0000/03f800f0/0023/07ff
c3t=1/7/071b/00010000/1/00000700,0f5903ff,f6dbffef,8e40400c
c3l=1/3/0000/0001/26/0000000000001557/19200000
cid=15010044,5836384d,42009005,f4661900 fmt=2 hv=7202
cmd1=3/0/c0ff8080
clocks=1 mci=10000072 pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c
v3=02626040 dll=6000642c dst=00000000 ddr=80040873 pst=00000000
cfg=00002017 mnd=00000001/000000fc/000000fb cmd=00000000 bcr=00000000
apps=00004221 ahb=00000001 step=11 log=5/8/7/2 @Gta4lEmmcProbe.c:503
```

The extracted text renders `c3l=` as `c31=` and `Gta4lEmmcProbe.c` as
`Gta41EmmcProbe.c`; the format string and source name use lowercase `l`. Image:
`out/gta4l/ram-root-emmc-probe-fmt2-evidence/boot-ram-root-emmc-probe-fmt2.img`,
85,688,320 bytes, SHA-256
`7bf3e71991f949f2da6fcff2ef388268904a512b1876062d04e9bb86b6f411b6`;
payload `ba4e88ba…`; kernel `f53860af…`; source hashes `b4461abc…`,
`c50b6467…`, `60cff259…`. Flash and report: `flash-record.json` in the same
directory. BOOT-only write; RECOVERY unchanged at `14e11370…`. The flashing
procedure did not reboot; the owner booted the image.

Decoding:

- `rc=4294967289` is `-7`, `GTA4L_EMMC_ERR_CMD_TIMEOUT`.
- `fail=1/7/65536/0x071B/0x0000/0x0002/0x03F800F0/11`: CMD7, argument
  `0x00010000` (RCA 1), command `0x071B` (`RESP_SHORT_BUSY | CRC | INDEX`),
  error `0x0000`, normal `0x0002` (TC), present `0x03F800F0`, failure step 11.
- `step=11` is `GTA4L_EMMC_STEP_CMD9`; CMD9 completed and CMD7 success would
  have set step 12. CMD2, CMD3 and CMD9 all completed in this run.
- The recorded failure sample shows no error bit and `TC` without `CC`. The
  `fmt=2` report kept no in-loop CMD7 history, so it cannot distinguish a
  never-asserted `CC` from one cleared between samples. The returned `-7` is
  the driver's software timeout, not a latched `CMD_TIMEOUT` bit.
- `c3t` is the CMD7 timeout capture despite its legacy name: command readback
  `0x071B`, argument `0x00010000`, response stable across two reads. `c3l` is
  the CMD3-only poll capture (26 iterations, last normal `CC`, 5,463 ticks at
  19.2 MHz, about 284.5 microseconds); it is not CMD7's timeout duration.
- `rsp=0x00000700,0x0F5903FF,0xF6DBFFEF,0x8E40400C`: applying the driver's
  32-bit long-response transformation gives
  `40400CF6,DBFFEF0F,5903FF00,00070000`. That arithmetic does not recover a
  coherent CMD9 CSD. The raw upper words match the recovery CSD apart from one
  nibble, which is consistent with a retained CMD9 window, but that is
  consistency only. The window is ambiguous between a plausible CMD7 R1
  (STBY, ready, no errors) and a retained value; it is not a validated CMD7
  response.
- `log=5/8/7/2` is the last log event: count 5, `ERROR` (8), index 7,
  `(error << 16) | normal = 0x0002`. It is the failure record, not a reset log.
  The live `cmd=0`/`bcr=0` fields are GCC registers, not SDHCI command state;
  the SDHCI command submission evidence is the `c3t` readback.

This run `[C]` moves the terminal failure from CMD3 to CMD7 and establishes a
completion-status mismatch on the first R1b command: TC is set while CC is
absent. It does not establish why, whether the card accepted CMD7, or that the
response window is fresh. The focused decode, candidate interpretations and
open questions are in `docs/emmc-cmd7-timeout-finding.md`. No EXT_CSD or sector
data was read; CRC checking remains enabled. Separately, the retained
pre-instrumentation source accepted an R1b command on CC alone and returned
without waiting for the busy completion; the lost `fmt=2` build's exact source
is not retained. The working tree now requires both CC and TC. That
implementation gap does not explain the absent CC in this run.

### 2.9 fmt=4 fail-closed stage-3 result (2026-11-01)

The `fmt=4` fail-closed image was built from the current working tree on
2026-11-01 (11:15Z) and is
`out/gta4l/ram-root-emmc-probe-failclosed-evidence/boot-ram-root-emmc-probe-failclosed.img`,
85,688,320 bytes, SHA-256
`4d2f7975d3bc2e26a1ab15fb4d60d765190fba2df0e6b4ee7c5226d41a63e5bf`;
payload SHA-256
`954cc0dcc2de0536c1944d73eb92aad9b45fdc67b9de494ed520cbcdf684e399`;
kernel `27cb1b5450f43d8d6c6f138b9a99ea4a8dd9e974ef112fdb19cb31e149d35777`;
source hashes `e493b3eb…` (`Gta4lEmmc.c`), `e0a2830c…` (`Gta4lEmmc.h`)
and `932552a2…` (`Gta4lEmmcProbe.c`); archive
`e9578564dd5ebb6e8015aaaf1b55090532cca8b49536acc821e092a077af34b8`. The
payload carries `gta4l-emmc-probe=3`; its contract is 7/7 and the boot-image
gate is 18/18.

The owner reports two runs of the same image on two different units: first on
an SM-T505 the owner does not own, then on the owner's SM-T505N. The earlier
run's owner-supplied text records the same eMMC failure record and
`free=0x000000000007946f`. The later run has the BOOT-only flash record below
and the supplied photo, transcribed with `free=0x000000000007944f`. The owner
reports the two results are identical 1:1. The eMMC failure record and response
window are identical in the two transcriptions; the VM page counters differ, but
that difference is not established as a run-to-run measurement (a transcription
correction is equally possible) and is not eMMC evidence. The earlier SM-T505
run has no flash record or run ID in this tree; its unit attribution is
owner-reported, not independently tied. The owner also reports that all earlier
builds (sections 2.6-2.8) were run only on the SM-T505N. The rerun flash record
is
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/flash-record.json`:
pre-flash BOOT partition `92e80609…` (the `fmt=2` image plus its unchanged
tail), post-flash full partition `b53f31fe…`, image-byte readback
`4d2f7975…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged,
readback gate 18/18 and payload contract 7/7. The photo is
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/stage3-fmt4-rerun-photo-2026-11-01.jpg`,
2560x1920, SHA-256
`c079952510039d236a705985a91d9e4a8b042a125afb81ef66bf1a4e6ddc8c76`.

The terminal rendering (and the earlier text extraction) folds lines at
arbitrary character boundaries. The block below rejoins them. `cmd1` followed
by `rsp=` is one field; `hv=7` followed by `202` is `7202`; `c31=` is the
format's `c3l=`; `Gta41` is `Gta4l`; the earlier text extraction printed
`dll` as `d11`, while the photo shows `dll`. The block restores the fixed-width
zero padding in `pre2rsp`, `c2`, `c7t`, `pages` and `free`. The current panic
renderer in this tree prints 16 hex digits for `frames`, `pages` and `free`,
and the photo's values are at that width; the earlier text's 17-digit forms are
treated here as extraction-width artifacts; the renderer is pinned in
the paragraph below. That normalization does not identify a stack
address or a linked renderer identity.

The renderer provenance: every panic screen in sections 2.9-2.12 is
drawn by the patched panic-screen renderer built from the pinned
UniXNU checkout's `scripts/xnu-panic-screen.c`; the copy inside the
gta4l kernel build tree
(`out/gta4l/ipados-17.7.11-21H461/gta4l/tree/scripts/xnu-panic-screen.c`,
byte-identical to the source) has no record of any source change after
UniXNU commit `c1f6966` (2026-10-25), which precedes all of these
diagnostic builds. The renderer prints each frame return address as
`(fp[1] & 0x0000007fffffffffUL) | 0xffffff8000000000UL`, zero-padded
to 16 hex digits; for kernel-text return addresses (for example
`0xfffffff0079d72a4`) the OR restores the real address bit-for-bit,
so the printed value equals the real kernel VA. It prints `pages`
and `free` as `0x` followed by the sampled VM page counter value
zero-padded to 16 hex digits (`hex()` builds `0x` plus
`number(..., 16, 16, 1)`). The wide reads of this text are therefore
glyph-reading artifacts; no transcribed value changes and no linked
renderer identity is claimed.

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288
fail=1/2/0/00000209/0002/8000/03f800f0/8
rsp=00000000,00000000,80000000,00c0ff80
cmd1rsp=1/c0ff8080,00000000,00000000,00000000
pre2rsp=1/c0ff8080,00000000,00000000,00000000
c2=0/0000/0000/00000000,00000000,00000000,00000000
c3p=0/0000/0000/00000000/0000/0000
c3t=0/0/0000/00000000/0/00000000,00000000,00000000,00000000
c3l=0/0/0000/0000/0/0000000000000000/19200000
cid=00000000,00000000,00000000,00000000 fmt=4 hv=7202
cmd1=3/0/c0ff8080
clocks=1 mci=10000072 pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c
v3=02626040 dll=6000642c dst=00000000 ddr=80040873 pst=00000000
cfg=00002017 mnd=00000001/000000fc/000000fb cmd=00000000 bcr=00000000
apps=00004221 ahb=00000001 step=8 log=5/8/2/163840
c7p=0/0000/0000/00000000/0000/0000/00000000,00000000,00000000,00000000
c7q=0/0000/0000/0000/0000/0/0000000000000000
c7c=0/0/0000000000000000/0/0/0000000000000000
c7t=0/0000/0000/00000000/00000000/00000000/00000000,00000000,00000000,00000000
c7x=0/0/0:0000:0000,0:0000:0000,0:0000:0000,0:0000:0000 @Gta4lEmmcProbe.c:442
frames fffffff0079ea9bc fffffff0079d6c40 fffffff0076e7eb0 fffffff0072c8864 fffffff00741a4a4
pages=0x0000000000079eab
free=0x000000000007944f
```

Decoding:

- `rc=4294967288` is `-8`, `GTA4L_EMMC_ERR_CMD_CRC` (`Gta4lEmmc.h:242`).
- `fail=1/2/0/0x0209/0x0002/0x8000/0x03F800F0/8`: valid=1, index 2, argument
  0, command `0x0209` (CMD2 index `0x0200`, `RESP_LONG` `0x0001`, CRC
  `0x0008`), error `0x0002` (`CMD_CRC`), normal `0x8000` (the SDHCI
  error-interrupt aggregate; `CC` is clear), present `0x03F800F0`, failure
  step 8.
- `step=8` is `GTA4L_EMMC_STEP_CMD1`, the last completed step. CMD1 completed
  and CMD2 was the terminal command; CMD3, CMD9 and CMD7 were never submitted.
- `cmd1=3/0/0xC0FF8080`: three attempts, no timeouts, OCR `0xC0FF8080`.
- `cmd1rsp` and `pre2rsp` are both valid and both hold `0xC0FF8080` in their
  first word; the pre-CMD2 window had not visibly changed before the failing
  command.
- `c2=0/...`: the post-success CMD2 capture is only taken after CMD2 returns
  without error, so it is invalid on this error return. The failed-command
  window is `rsp=`.
- `c3p`, `c3t`, `c3l` and `c7p`–`c7x` are all invalid/zero because CMD3 and
  CMD7 were never reached. Their presence in the report is unconditional; it
  does not mean a CMD3 or CMD7 sample was taken.
- `rsp=0x00000000,0x00000000,0x80000000,0x00C0FF80` is not CID-shaped and
  does not match the known CID (`1501…1900`). The two nonzero words contain
  the bytes `80 00 00 00 00 C0 FF 80`, a one-byte rotation of
  `00 00 00 00 C0 FF 80 80`, whose low 32 bits are the recorded CMD1 word
  `0xC0FF8080`. That is consistent with a retained or rotated CMD1 window,
  not a plain logical shift, and it identifies no corrupted CID bits. A
  failed R2 snapshot is never a valid CID.
- `ven=0x00018A9C` is the current POR `0xA9C` plus pad bits; the register state
  repeats section 2.6 and the `fmt=2` run (`mci=0x10000072`, `pwr=0x0F`,
  `clk=0x0007`, `hc=0x00`, `hc2=0x0008`, `v3=0x02626040`, DLL `0x6000642C`,
  `ddr=0x80040873`, `cfg=0x2017`, MND `1/252/251`). This is a register state,
  not a measured clock.
- `log=5/8/2/163840` is the last log event: count 5, `ERROR` (8), index 2,
  `(error << 16) | normal = 0x00028000`. It is the failure record.
- `hv=7202` is the SDHCI host version read back in the report.
- `pages=0x0000000000079eab` and `free=0x000000000007944f` are XNU VM page
  counters printed by the panic renderer. They are not part of the eMMC report.
  The earlier text reports `free=0x000000000007946f`; that difference is not
  established as a real run-to-run change (a transcription correction is
  equally possible) and is not used as evidence.

The failure record, response window, normal/error status, present state, step,
`cmd1` and `log` words are identical to the section 2.6 CMD2 CRC report and to
the earlier `fmt=4` run. The `fmt=4` report adds the `cmd1rsp=`/`pre2rsp=`
and `c2`/`c3`/`c7` fields but does not change any captured failure value. The
two `fmt=4` observations are on different units (SM-T505 and SM-T505N) per the
owner and agree 1:1; they are not a same-unit repeatability test. On the
owner's SM-T505N all earlier builds (2.6-2.8) were also run; the `fmt=4` outcome
agrees with build-2 and contrasts with `cmd2window`/`fmt=2`, where CMD2
completed. The CMD2 outcome therefore varies across diagnostic builds on that
unit without a source change that explains it. H2, H4 and H5 are not excluded as
contributors by the two-unit agreement or by the build variation; H1 remains
excluded as a sufficient explanation; H6/H7 remain open. No cause is
established.

Bounds: CMD1 OCR `0xC0FF8080` was observed and remains valid in the pre-CMD2
window, but no validated CID, CSD, EXT_CSD, capacity, partition or sector
result was obtained; no sector read was issued; CRC checking was not disabled;
no response window is promoted to a CID. The image was built for stage 3, so it
did not issue a sector read by design.

### 2.10 Run provenance and readback verification (2026-11-01)

The `fmt=4` image has two reported runs on different units. The owner states
that the first run was on an SM-T505 the owner does not own, and that the second
was on the owner's SM-T505N. The owner reports the two results are identical
1:1. The first run left an owner-supplied text extraction but no flash record or
run ID in this tree; its unit attribution is owner-reported, not independently
tied. The second run left the BOOT-only flash record, pulled readback and photo
linked above. Private device and remote-host identifiers in that flash record
are deliberately not repeated here.

The owner also states that all earlier builds (sections 2.6-2.8) were run only
on the SM-T505N. Their flash records are consistent with that attribution; this
audit does not independently authenticate the recovery session. The owner's
assessment is that the identical `fmt=4` result on SM-T505 and SM-T505N
indicates the two models use the same eMMC. This audit records that as the
owner's inference: the shared failure record and response window show identical
observed CMD2 behavior on the two reported units, not a measured part-number,
CID, firmware or board-revision identity, and a shared host-path cause is not
excluded.

The original `readback-gate-report.json` names the source image under
`failclosed-evidence/`, not the pulled readback. Byte equality of the pulled
prefix to the source image transfers the component conclusions, but only as an
inference. In build mode the gate was rerun directly on the pulled image prefix
(`boot-after-failclosed-rerun-image-bytes.img`, 85,688,320 bytes, SHA-256
`4d2f7975…`); the payload was extracted at the boot header's ramdisk offset
(`readback-payload-extracted.img`, 83,886,080 bytes, SHA-256 `954cc0dc…`); and
the payload check was run on that extraction. The results are 18/18 gate checks
and 7/7 payload checks, written to
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/readback-gate-report-pulled.json`
and `readback-payload-check-pulled.json`. The original reports remain in place.

### 2.11 cmd1gap candidate stage-3 result (2026-11-01)

The `cmd1-crc-retire-8us` candidate descends from the section 2.9 source. Its
parent `aaaae41` carries the same three source hashes as the fail-closed build
(`e493b3eb…`, `e0a2830c…`, `932552a2…`), and commit `2a3268c7` adds only the
8-us post-ready-CMD1 delay and its constant to the core. The built image is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/boot-ram-root-emmc-probe-cmd1gap.img`,
85,688,320 bytes, SHA-256
`8a463dae3ff120213046558d89374316e0a51fadb845247a60f822fad9e227ee`;
source hashes `874e6878…` (`Gta4lEmmc.c`), `5dd223f7…` (`Gta4lEmmc.h`) and
`932552a2…` (`Gta4lEmmcProbe.c`); archive `22e6186f…`; payload kernel
`ee33719e…`; payload `6b6f5212…`, 83,886,080 bytes, `gta4l-emmc-probe=3`,
contract 7/7; boot-image gate 18/18 against the pinned v5 baseline
`ce4d76e8…`. Only the ramdisk differs from that baseline. The flash record is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/flash-record.json`: BOOT only
(`/dev/block/mmcblk0p69`), pre-flash partition `b53f31fe…` (the section 2.9
image plus its unchanged tail), post-flash `92f4000e…`, image-byte readback
`8a463dae…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged,
pulled-image gate 18/18 and pulled payload contract 7/7. The record was written
after the BOOT write, records no reboot or boot at that time and names the
fail-closed result as the matched baseline for the owner's unit. The owner then
rebooted and ran the candidate.

All three run results are owner-supplied text transcriptions. No photo file or
photo hash from this run is in the tree, so the transcriptions are not
independently authenticated; the unit identities and the SM-T505/SM-T505N
attribution are owner-reported. All three runs reached
`GTA4L-EMMC IDENT REPORT: rc=0` and stopped at the probe's intentional report
panic (`Gta4lEmmcProbe.c:497`). `rc=0` is the success report's literal header:
the report is emitted only after `gta4l_emmc_init()` returns
`GTA4L_EMMC_OK`, and before any sector read (`Gta4lEmmcProbe.c:784-788`). The
owner's SM-T505N transcription, with the extraction's line folding and hex-width
artifacts rejoined as in section 2.9 (`c31=` is the format's `c3l=`, `Gta41` is
`Gta4l`, and the 17-digit frame/page forms are reduced to the renderer's
16-digit width), is:

```text
GTA4L-EMMC IDENT REPORT: rc=0 mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 ext=000009cb hc2=0008 ven=00018a9c dll=6000642c
dst=00000000 pst=00000000 step=15 log=5
GTA4L-EMMC IDENT CAPTURE fmt=4 hv=7202
c2=1/0001/0000/05f46619,4d420090,44583638,00150100
c3p=1/0000/0000/03f800f0/0023/07ff
c3t=0/0/0000/00000000/0/00000000,00000000,00000000,00000000
c3l=1/3/0000/0001/26/0000000000001560/19200000
cid=15010044,5836384d,42009005,f4661900
c7p=1/0000/0000/03f800f0/0023/07ff/ef8e4040,fff6dbff,320f5903,00d02701
c7q=1/0003/0000/0000/0003/26/0000000000001582
c7c=1/25/000000000001573/1/25/0000000000001573
c7t=1/0003/0000/03f800f0/0000071b/00010000/00000700,0f5903ff,f6dbffef,8e40400c
c7x=2/0/0:0000:0000,25:0003:0000,0:0000:0000,0:0000:0000 @Gta4lEmmcProbe.c:497
frames fffffff0079eaefc fffffff0079d6bb0 fffffff0076e7eb0 fffffff0072c8864 fffffff00741a4a4
pages=0x0000000000079eae
free=0x0000000000079472
```

The fields that differ across the three units are:

| Field | Owner's SM-T505N | SM-T505 (first) | SM-T505 (second) |
| --- | --- | --- | --- |
| `ext=` (plain EXT_CSD byte sum) | `000009cb` | `000009c7` | `000009cb` |
| `c2` raw window | `05f46619,4d420090,44583638,00150100` | `3cce1e18,4d420005,44583638,00150100` | `ddca77a7,4d42001f,44583638,00150100` |
| `cid` | `15010044,5836384d,42009005,f4661900` | `15010044,5836384d,4200053c,ce1e1800` | `15010044,5836384d,42001fdd,ca77a700` |
| `c3l` ticks | `…1560` | `…155c` | `…155f` |
| `c7q` ticks | `…1582` | `…1580` | `…1585` |
| `c7c` CC/TC ticks | `…1573`/`…1573` | `…1572`/`…1572` | `…1576`/`…1577` |
| `pages` | `0x0000000000079eae` | `0x0000000000079eac` | `0x0000000000079ead` |
| `free` | `0x0000000000079472` | `0x0000000000079450` | `0x0000000000079471` |

Every field not listed is identical in the three transcriptions, including
`mci=10000072`, `rca=1`, `cap=61071360`, `rev=8`, `bw=0`, `ocr=c0ff8080`,
`hc2=0008`, `ven=00018a9c`, `dll=6000642c`, `dst=00000000`, `pst=00000000`,
`step=15`, `log=5`, the `c3p` and `c7p` status/enable/present captures, the
`c7p` raw CSD window, the `c7t` terminal state and window, the `c7x` trace, all
frame addresses and the report location.

Decoding:

- `step=15` is `GTA4L_EMMC_STEP_READY` (`Gta4lEmmc.h:295`). The last completed
  step is set only after the EXT_CSD read, `data_sector_size_code` 0
  (512-byte sectors), the capacity floor and the partition-access check pass
  (`Gta4lEmmc.c:1570-1604`). The report is therefore a successful
  identification, not a partial one.
- `cap=61071360` is `capacity_blocks`, read from EXT_CSD `SEC_COUNT`
  (`Gta4lEmmc.c:1470-1474`): 61,071,360 × 512-byte sectors, the same user
  capacity as the recovery inventory. `rev=8` is the EXT_CSD revision byte
  (1.8) and `bw=0` is the decoded bus-width field; the port does not change
  bus width.
- `c2=1/0001/0000/…` is a valid CMD2 capture: normal status `0x0001` (CC),
  error 0, and four raw response words. The normalized `cid` is separate from
  that raw window. All three CIDs share manufacturer `0x15` (Samsung), OEM
  `0x0100`, product-name bytes `DX68M` and revision `0x42`; the serial and
  date bytes differ by unit. The report does not include the CSD words
  directly, but the `c7p` raw window is identical across the three units and is
  CSD-shaped; the report does not decode it, so it is not treated here as a
  second CSD source.
- `c3p` is a valid pre-CMD3 sample with no error; `c3t` is invalid, so no
  timeout was captured. `c3l=1/3/0000/0001/26/…` is the CMD3 poll history:
  first normal 0, last normal `0x0001` (CC), 38 iterations, and a short tick
  count at the reported 19,200,000 Hz counter.
- `c7p` is a valid pre-CMD7 sample with no error; present state
  `0x03F800F0` and enables `0x0023`/`0x07FF` match the CMD3 sample. `c7q`
  records OR normal `0x0003` (CC and TC both observed), OR error 0, 38
  iterations; `c7c` records CC and TC valid at iteration `0x25` (37) with
  essentially the same tick count; `c7t` records terminal normal `0x0003`,
  error 0, command `0x071B` (CMD7 with CRC, index and R1b flags), argument
  `0x00010000` (RCA 1) and the same CSD-shaped window. `c7x=2/0/…` is a
  two-entry trace with no overflow: the first sample has normal 0 and the
  observed change at iteration 37 already carries both CC and TC. This is the
  first hardware exercise of the committed CMD7 R1b CC+TC lifecycle on this
  board; it succeeded on all three units.
- `ext=` is the plain 512-byte sum of the EXT_CSD (`Gta4lEmmc.c:1458-1461`), not
  a checksum. A one-bit-sum difference of `0x9CB` versus `0x9C7` is consistent
  with unit-specific EXT_CSD bytes and is not evidence of corruption. The
  `pages`/`free` and frame values are XNU VM/stack counters from the panic
  renderer and are not eMMC evidence.
- `dll=6000642c`, `ven=00018a9c`, `hc2=0008`, `dst=0` and `pst=0` repeat the
  section 2.9 register state. They are register state, not measured clock or
  rail values.

The owner states that the three units are the owner's SM-T505N and two
SM-T505s, and that experts confirmed SM-T505, SM-T505N and SM-T500 are
identical for eMMC with only LTE/Wi-Fi/regional differences. That statement is
owner/expert-provided, not an independent part-number or board-revision
identity. The three transcripts are consistent with it: the capacity, CID
product name, CSD-shaped window, register state and CMD7 timing agree, while
the serial/date and EXT_CSD byte sum differ as expected for different units.
The three full CIDs and their raw `c2` CMD2 windows in the transcription and
table are unique device identifiers. They are reproduced verbatim from the
owner-supplied transcripts with the owner's explicit approval for this record;
the repository privacy rule otherwise treats device identifiers as private.

Attribution: on the owner's unit the fail-closed image failed CMD2 (section
2.9) and the candidate completed identification. The two images are built from
the same parent source with only the 8-us post-ready-CMD1 delay added, so this
is a same-unit, cross-session comparison with a single source delta. It
supports a timing-sensitive contribution at the CMD1-to-CMD2 boundary for these
runs. It does not establish the CMD2 CRC cause: the earlier `cmd2window` and
`fmt=2` builds also passed CMD2 on the same unit without the delay, so the
delay is not proven necessary, and the fail-closed extra success-path samples
remain a possible timing contributor. The result is not a repeatability or
reliability test, no general fix is adopted, and CRC checking remains enabled.

Bounds: identification only. No sector read (`CMD17`), block-device
publication, bus-width change, high-speed mode, DMA, CQE, ICE, write, mount or
root switch was issued. The image was written to BOOT only; the flash record
shows `RECOVERY` unchanged and no other partition written. The report panic is
intentional and stops the boot. No rail voltage, pad level or clock frequency
was measured.

### 2.12 Stage-1 full-probe result (2026-11-02)

The stage-1 image is the full probe (`gta4l-emmc-probe=1`) built from commit
`417acc4d89ab72491cf769bb8ed1131c79e1bb15` (clean) with the committed
`cmd1gap` source: core/header/probe `874e6878…`, `5dd223f7…`, `932552a2…`;
archive `e2a013e2…`, payload kernel `194c909f…`, payload `42e7a518…`
(83,886,080 bytes, `gta4l-emmc-probe=1`, contract 7/7) and boot image
`bf73a39b…` (85,688,320 bytes, gate 18/18 against the v5 baseline
`ce4d76e8…`). Only the ramdisk differs from the pinned v5 baseline. The build
provenance is
`out/gta4l/ram-root-emmc-probe-stage1-evidence/build-provenance.json`.

The BOOT-only flash record for the owner's SM-T505N is
`out/gta4l/ram-root-emmc-probe-stage1-evidence/flash-record.json`: pre-flash
partition `92f4000e…` (the section 2.11 image plus its unchanged tail),
post-flash `421e8848…`, image-byte readback `bf73a39b…`, tail `a28b4ea2…`
unchanged, `RECOVERY` `14e11370…` unchanged, pulled-image gate 18/18 and
pulled payload contract 7/7. The record was written after the BOOT write and
before the boot; it records no boot at that time. The owner then booted the
image on that unit and supplied a screen photograph. The same
85,688,320-byte image (SHA-256 `bf73a39b…`) was shared for the second unit;
that unit has no flash record or device-side readback in this tree.

The second run is on an SM-T505 whose CID matches the first SM-T505 column in
section 2.11, so the report links that unit to the earlier identification run;
the SM-T505N CID matches the owner's unit in section 2.11. Both runs reached
`GTA4L-EMMC PROBE REPORT: rc=0` and stopped at the probe's intentional report
panic after both `CMD17` reads. The report is emitted only after
`gta4l_emmc_init()` returns `GTA4L_EMMC_OK`, both `gta4l_emmc_read_block()`
calls succeed, and before any later operation (`Gta4lEmmcProbe.c:788-809`).
The owner-supplied SM-T505N transcription, with the photographed line folding
rejoined as in section 2.11 (`Gta41` is `Gta4l` and the 17-digit frame/page
forms are reduced to the renderer's 16-digit width), is:

```text
GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 ext=000009cb hc2=0008 ven=00018a9c dll=6000642c
dst=00000000 pst=00000000 step=15 log=7
GTA4L-EMMC PROBE CID 15010044 5836384d 42009005 f4661900
GTA4L-EMMC PROBE CSD d0270132 0f5903ff f6dbffef 8e404000
GTA4L-EMMC PROBE BLOCK0 sum=62084b66 words=00000000 00000000 000000ee 00000001
GTA4L-EMMC PROBE BLOCK1 sum=84d0543e words=20494645 54524150 00010000 0000005c
GTA4L-EMMC PROBE CAPTURE fmt=4 hv=7202
c2=1/0001/0000/05f46619,4d420090,44583638,00150100
c3p=1/0000/0000/03f800f0/0023/07ff
c3t=0/0/0000/00000000/0/00000000,00000000,00000000,00000000
c3l=1/3/0000/0001/26/0000000000001560/19200000
cid=15010044,5836384d,42009005,f4661900
c7p=1/0000/0000/03f800f0/0023/07ff/ef8e4040,fff6dbff,320f5903,00d02701
c7q=1/0003/0000/0000/0003/26/0000000000001587
c7c=1/25/0000000000001578/1/25/0000000000001579
c7t=1/0003/0000/03f800f0/0000071b/00010000/00000700,0f5903ff,f6dbffef,8e40400c
c7x=2/0/0:0000:0000,25:0003:0000,0:0000:0000,0:0000:0000 @Gta4lEmmcProbe.c:564
frames fffffff0079d72a4 fffffff0076e7eb0 fffffff0072c8864 fffffff00741a4a4
pages=0x0000000000079eae
free=0x0000000000079472
```

The SM-T505 transcription is:

```text
GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 ext=000009c7 hc2=0008 ven=00018a9c dll=6000642c
dst=00000000 pst=00000000 step=15 log=7
GTA4L-EMMC PROBE CID 15010044 5836384d 4200053c ce1e1800
GTA4L-EMMC PROBE CSD d0270132 0f5903ff f6dbffef 8e404000
GTA4L-EMMC PROBE BLOCK0 sum=62084b66 words=00000000 00000000 000000ee 00000001
GTA4L-EMMC PROBE BLOCK1 sum=425cd1ab words=20494645 54524150 00010000 0000005c
GTA4L-EMMC PROBE CAPTURE fmt=4 hv=7202
c2=1/0001/0000/3cce1e18,4d420005,44583638,00150100
c3p=1/0000/0000/03f800f0/0023/07ff
c3t=0/0/0000/00000000/0/00000000,00000000,00000000,00000000
c3l=1/3/0000/0001/26/000000000000155b/19200000
cid=15010044,5836384d,4200053c,ce1e1800
c7p=1/0000/0000/03f800f0/0023/07ff/ef8e4040,fff6dbff,320f5903,00d02701
c7q=1/0003/0000/0000/0003/26/0000000000001586
c7c=1/25/0000000000001577/1/25/0000000000001578
c7t=1/0003/0000/03f800f0/0000071b/00010000/00000700,0f5903ff,f6dbffef,8e40400c
c7x=2/0/0:0000:0000,25:0003:0000,0:0000:0000,0:0000:0000 @Gta4lEmmcProbe.c:564
frames fffffff0079d72a4 fffffff0076e7eb0 fffffff0072c8864 fffffff00741a4a4
pages=0x0000000000079eab
free=0x000000000007944f
```

The fields that differ across the two units are limited to:

| Field | SM-T505N | SM-T505 |
| --- | --- | --- |
| `ext=` (plain EXT_CSD byte sum) | `000009cb` | `000009c7` |
| `CID` | `15010044 5836384d 42009005 f4661900` | `15010044 5836384d 4200053c ce1e1800` |
| `c2` raw window | `05f46619,4d420090,44583638,00150100` | `3cce1e18,4d420005,44583638,00150100` |
| `c3l` ticks | `…1560` | `…155b` |
| `c7q` ticks | `…1587` | `…1586` |
| `c7c` CC/TC ticks | `…1578`/`…1579` | `…1577`/`…1578` |
| `BLOCK1` sum | `84d0543e` | `425cd1ab` |
| `pages` | `0x0000000000079eae` | `0x0000000000079eab` |
| `free` | `0x0000000000079472` | `0x000000000007944f` |

Every other field is identical, including `mci=10000072`, `rca=1`,
`cap=61071360`, `rev=8`, `bw=0`, `ocr=c0ff8080`, `hc2=0008`, `ven=00018a9c`,
`dll=6000642c`, `dst=00000000`, `pst=00000000`, `step=15`, `log=7`, the CSD,
`BLOCK0 sum=62084b66` and its four words, the `BLOCK1` four words, the `c3p`/
`c7p` status/enable/present captures, the `c7t` terminal state and window, the
`c7x` trace and the frame addresses.

Decoding:

- `step=15` is `GTA4L_EMMC_STEP_READY`; `log=7` adds two `READ` events to the
  stage-3 count of five. The report is therefore a successful identification
  followed by two successful single-block reads, not a partial run.
- `cap=61071360` is the EXT_CSD `SEC_COUNT` value and matches the recovery
  inventory: 61,071,360 × 512-byte sectors. The `CSD` line
  `d0270132 0f5903ff f6dbffef 8e404000` matches the recovery-known CSD
  `d02701320f5903fff6dbffef8e404000`. On the owner's unit the `CID` also
  matches the recovery-known CID `150100445836384d42009005f4661900`; the
  SM-T505 CID is the one recorded for that unit in section 2.11. The `c2` raw
  window is consistent with the same CID bytes.
- `BLOCK0 words=00000000 00000000 000000ee 00000001` are the selected words at
  offsets 0, 446, 450 and 454. They show a zero word at offset 446, a
  protective-MBR partition type `0xEE` at offset 450 and a partition start LBA
  of 1 at offset 454. The selected words do not include the `0x55AA` MBR
  signature at offset 510, so that signature is covered by the full-sector sum
  but not printed by the report itself; the recovery-side readback below reads
  it directly.
- `BLOCK1 words=20494645 54524150 00010000 0000005c` are the words at offsets
  0, 4, 8 and 12: ASCII `EFI `, ASCII `PART`, GPT revision 1.0 and a 92-byte
  GPT header. The selected words do not include the GPT header CRC at offset
  16 or the disk GUID at offset 56.
- `sum=` is the probe's rolling content check, `sum = sum*31 + byte` over all
  512 bytes (`Gta4lEmmcProbe.c:350-358`), not a CRC and not a cross-unit
  constant. `BLOCK0` is identical on both units because the protective MBR is a
  shared layout artifact; `BLOCK1` differs because the GPT header contains a
  per-device disk GUID, header CRC and related fields. A differing `BLOCK1`
  sum is therefore expected and is not evidence of corruption. The two sums are
  content fingerprints, not themselves verification of the MBR/GPT CRC fields;
  the recovery-side readback below performs that check.
- The `c3l`/`c7q`/`c7c`/`c7t` captures repeat section 2.11's CMD3 and CMD7
  poll/completion evidence: 38 iterations, CC and TC both observed at
  iteration `0x25` (37), terminal normal `0x0003`, command `0x071B`, argument
  `0x00010000`. The few changed tick values are per-run poll variation.
- `dll=6000642c`, `ven=00018a9c`, `hc2=0008`, `dst=0` and `pst=0` are register
  state, not measured clock or rail values. `pages`/`free` and the frames are
  XNU VM/stack counters from the panic renderer, not eMMC evidence.

Bounds and attribution: both reports are owner-supplied screen photographs,
transcribed here; no photo file or hash from either run is in the tree. The
SM-T505 flash procedure and unit attribution are owner-reported, and its image
identity rests on the shared `bf73a39b…` copy rather than a device-side
readback. The owner's SM-T505N has the BOOT-only flash record and readback
described above. The runs are read-only: `CMD0`/`CMD1`/`CMD2`/`CMD3`/`CMD9`/
`CMD7`/`CMD13`/`CMD8`/`CMD17` only, at the fixed identification clock, with CRC
checking enabled. No write, bus-width change, high-speed mode, DMA, CQE, ICE,
mount, root switch, block-device publication or repeated-read reliability test
was issued. The two reads establish sector-read and MBR/GPT content evidence
for blocks 0 and 1; they do not establish a filesystem, partition-table
integrity beyond those selected words and sums, broader media reliability or
the correctness of the GPT CRC fields; the separate recovery-side readback
below checks the MBR signature and GPT header CRC.

**Recovery-side readback (2026-11-02, read-only):** with owner approval, the
owner's SM-T505N in TWRP read `/dev/block/mmcblk0` sectors 0 and 1
(`dd bs=512 count=1`, LBA 0 and LBA 1; no write, operator-issued register
access or partition change was issued) and hashed each sector on the device. Both captured sectors
matched their on-device SHA-256 values. Offline, block 0 reproduces the
report's probe sum `62084b66`, the `0x55AA` MBR signature at offset 510, the
`0xEE` partition type at offset 450 and start LBA 1 at offset 454; block 1
reproduces the report's probe sum `84d0543e`, the `EFI PART` signature, the
92-byte header size and the GPT header CRC at offset 16 (computed over the
header with its CRC field zeroed). The readback therefore independently
prints the MBR signature and GPT header CRC that the stage-1 reports cover
only through their full-sector sums, and, on the owner-attested session, ties
the two block contents to the owner's unit. It is a single read-only pass on one unit, not a reliability
test, and the SM-T505 run is not re-verified this way. The sector images and
both SHA-256 values remain in the gitignored
`out/gta4l/ram-root-emmc-probe-stage1-evidence/recovery-readback/` record and
are not published; the disk GUID and partition GUIDs were not extracted or
printed.

The two full CIDs and `c2` windows are the same identifiers already reproduced
with the owner's explicit approval in section 2.11; they are repeated here for
the same units and add no new device identifier.

### 2.13 Stage-A extended content-read result (2026-11-02)

The stage-A image is the same `emmc-probe-1` diagnostic rebuilt from the
working tree on top of `53753df` with the extended read phase. The build
provenance is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/build-provenance.json`:
`Gta4lEmmc.c 874e6878…`, `Gta4lEmmc.h 7e063c08…`, `Gta4lEmmcProbe.c
034571f1…`, probe archive `57ed9995…`, payload `fdd85d25…` (83,886,080 bytes,
`gta4l-emmc-probe=1`, contract 7/7) and boot image `721e5c87…` (85,688,320
bytes, gate 18/18 against the v5 baseline `ce4d76e8…`). The BOOT-only flash
record is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/flash-record.json`: pre-flash
`421e8848…` (the §2.12 stage-1 image plus its unchanged tail), post-flash
`d55c09bf…`, image-byte readback `721e5c87…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, and the EFS/sec_efs backups unchanged; no
other partition was written.

The owner booted the image on the same SM-T505N and supplied a screen
photograph, stored as `probe-report-photo.jpg` (SHA-256 `663d2db3…`); the
photo and the full transcription stay in the gitignored evidence directory. The
report reached `GTA4L-EMMC PROBE REPORT: rc=0` with the §2.12 identification
fields, `step=18 log=30`, and the new content lines (CID/CSD lines repeat
§2.12 for the same unit and are omitted here):

```text
GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 ext=000009cb hc2=0008 ven=00018a9c dll=6000642c
dst=00000000 pst=00000000 step=18 log=30
GTA4L-EMMC PROBE BLOCK0 sum=62084b66 words=00000000 00000000 000000ee 00000001
GTA4L-EMMC PROBE BLOCK1 sum=84d0543e words=20494645 54524150 00010000 0000005c
GTA4L-EMMC PROBE GPT elba=2 n=84 sz=128 first=34 last=61071326 backup=61071359
GTA4L-EMMC PROBE BLOCKS 2=793877e4 3=01fcf835 4=393b7c13 5=0bfbefd5 6=57ac4bf5 7=82d4c9c9 8=2e16c705 9=adbc9085 10=2d26b02c 11=69ab5982 12=604856e0 13=7f3f196c 14=7e056798 15=d6926617 16=3d035910 17=5076cbb8 18=d40dca26 19=37f4be4f 20=057d3881 21=89457318 22=00000000
GTA4L-EMMC PROBE DATA 34=f418cd33
GTA4L-EMMC PROBE LSTLBA=61071359 sum=a89b26d5
```

The `CAPTURE fmt=4` block repeats the §2.12 field layout; the changed fields
are normal run-to-run poll counters (`c3l …155e`, `c7q …1581`, `c7c
…1572/…1573`), the first panic frame (`fffffff0079d7654` vs `fffffff0079d72a4`)
and `pages`/`free` (`…79eaa`/`…7944e` vs `…79eae`/`…79472`); `c7x=2/0` still
reports no trace overflow, and the report source location moved to
`Gta4lEmmcProbe.c:717` with the added code. The full CAPTURE block is in
`probe-report.txt`.

Decoding and bounds:

- `step=18` is `GTA4L_EMMC_STEP_PROBE_LAST`; `log=30` is the stage-1 `log=7`
  plus 23 reads (21 array + first usable + last sector).
- The gate values match the live and pinned GPT header: entry array LBA 2,
  84 entries of 128 bytes, first usable LBA 34, last usable LBA 61,071,326
  and backup LBA 61,071,359. 84 × 128 = 10,752 bytes = 21 sectors, so the
  primary array spans LBAs 2..22, not the 20 sectors the plan's draft guessed
  before the live entry count was known. The gate refuses any other layout.
- `BLOCKS 22=00000000` is expected: only entries 1..80 are populated; the
  final four 128-byte entry slots (entries 81..84) are zero, so the last array
  sector is zero. The same 20 nonzero array blocks appear in the backup array.
- `DATA 34=f418cd33` is the first usable LBA the header names; `LSTLBA=
  61071359 sum=a89b26d5` is the backup GPT header at the last sector.

**Live recovery-side binding (2026-11-02, read-only):** while the owner's
SM-T505N was in TWRP, the same session read LBAs 0..33, LBA 34 and LBAs
61,071,326..61,071,359 with `dd bs=512`; each range was hashed on the device
and captured with `adb exec-out`. No write, mount, format or partition change
was issued. All 21 primary-array sums, the first-usable-sector sum and the
last-sector sum in the report equal the live readback. The live 0..33 image
(`0c7e9c03…`) is byte-identical to the pinned recovery GPT dump
`device-dump/private/gta4l-dump/gta4l-gpt.bin`; the live backup tail
(`41912e59…`) is byte-identical to the pinned backup dump; LBA 0/1 hashes are
the §2.12 values; both the primary and backup GPT header CRCs are valid
(`41c874ee`/`fde724e6`); the backup array equals the primary array. The
sector images and hashes stay in the gitignored
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/` record.

Supported by this section: the stage-A rung / F1 content claim. The driver
issued repeated single-block CMD17 reads at the fixed identification clock
(1-bit, CRC enabled) for the GPT header, the full 84-entry primary array, the
first usable sector and the last sector, and every content sum is bound to
live recovery bytes. This is multi-sector content evidence.

Not supported: a multi-block CMD18/CMD12 transfer (F2), any bus-width change
or bus-test (F3), high-speed timing (F4), block-device publication or mount
(F5), any write (F6), filesystem or partition semantics beyond the GPT bytes,
media-reliability claims, or DMA/CQE/ICE. The `DATA 34` sum binds a content
fingerprint; it is not interpreted as a filesystem or partition object.

Host verification for this rung: `scripts/test-gta4l-emmc.py` passes 112
scenarios and 62 mutation controls; `scripts/test-gta4l-emmc-probe.py` passes
44 scenarios and 34 mutation controls, including the GPT gate, the 21-sector
array phase, the first-usable/last-sector reads, the four stage-A loop
mutation controls (duplicate LBA, continued error, block-count off-by-one,
wrong last LBA) and the optional A2/F2 multi-block cases recorded in
§2.14.

### 2.14 A2/F2 multi-block design, CMD12 response decode and attempt 1 (2026-11-02)

The optional A2/F2 rung adds one open-ended `CMD18` read of the 21 primary GPT
array sectors (LBAs 2..22) and an explicit `CMD12` stop behind
`gta4l-emmc-probe=1` plus `gta4l-emmc-mb=1`; the default image is unchanged.
The transfer-mode word is `TM_READ | TM_BLOCK_COUNT_EN | TM_MULTI`. The
Multi/Single Block Select bit is required: the pinned Samsung
`drivers/mmc/host/sdhci.c:1172-1175` programs
`SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI` whenever the request is multi-block
or has more than one block. Without it the controller performs a single-block
transfer and the helper times out after one sector. A checker audit found the
bit missing in the first working-tree implementation; the header, core, both
simulators and both suites now enforce it, including a mutation control that
removes it (core 112/62, probe 44/34).

**CMD12 response decode.** The pinned Linux header labels CMD12 as
`ac ... R1b` (`/tmp/opencode/lfg4l-audit/files/LinuxForGta4l_linux__c91d0e94385f75a8788cfc9d8a4f532b3994e6da__include_linux_mmc_mmc.h:43`).
The core recovery path sends it as `R1B` with a bounded busy timeout
(`/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_core_core.c:1457-1460`, matching
Samsung CXG1 `core.c:1457-1459`). The tuning-abort path sends CMD12 as `R1`
and explicitly notes that drivers which override R1 to R1b must set a busy
timeout (`/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_core_mmc_ops.c:702-709`),
and the downstream `sdhci_send_command()` adds `MMC_RSP_BUSY` for CMD12 under
`SDHCI_QUIRK2_STOP_WITH_TC`
(`/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_host_sdhci.c:1351-1354`). The port
therefore keeps `R1b` (`CMD_RESP_SHORT_BUSY`) with CRC and index checks,
requires both `CC` and `TC` with a bounded busy poll, and captures the
terminal state before clearing. `R1` would return without observing the
busy/TC phase; the open-ended transfer's stop needs that phase, so `R1b` is
the conservative decode and no source-supported change to it was found. CRC
checking stays enabled.

**Attempt 1 (2026-11-02).** The pre-correction image (`Gta4lEmmc.c 009f0eb9…`,
`Gta4lEmmc.h 6f3ff335…`, `Gta4lEmmcProbe.c e67ad2bc…`, payload `198dc393…`,
boot image `cac4356b…`) reached identification and then stopped before any MD
code ran:

```text
GTA4L-EMMC PROBE FAILED: stage=1 rc=4294967289
fail=1/3/65536/0000031a/0000/0000/03f800f0/9
rsp=00000500,5836384d,42009005,f4661978
c3l=1/3/0000/0000/100000/0000000014c2e0f/19200000
step=9 log=5/8/3/0
```

`stage=1` is the full-probe boot argument, `rc` is `-7`
(`GTA4L_EMMC_ERR_CMD_TIMEOUT`), `step=9` is `STEP_CMD2` (the last completed
step), `fail` names CMD3 (index 3, argument `0x00010000`, command `0x031a`)
with no error/normal bits latched, and `c3l` exhausted the 1,000,000 µs /
10 µs poll bound. RESPONSE0 `0x00000500` is a plausible CMD3 R1 status and
words 1..3 are CID words, so the command was accepted and its completion was
not observed. This is the §6 H8 CMD3-completion signature, not an MD/F2
result: no CMD18, no CMD12 and no sector read occurred. The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-evidence/stage2b-attempt1-failure.txt`;
the owner photo is still pending.

F2 remains unproven. A corrected build and a separately approved BOOT-only
attempt are required; a second pre-MD stop is still not an MD verdict and does
not trigger the MD abort condition, which applies to an actual MD report.

---

## 3. Failure attribution: the CMD2 record and the CMD3 timeout

### 3.1 Command sequence

`gta4l_emmc_init()` sends commands in this order (`Gta4lEmmc.c:1041-1163`):

1. CMD0, argument 0, flags `GTA4L_EMMC_CMD_RESP_NONE`
   (`Gta4lEmmc.c:1058-1060`). No response, no CRC check.
2. CMD1, argument `GTA4L_EMMC_OCR_VALUE`, flags
   `GTA4L_EMMC_CMD_RESP_SHORT` only (`Gta4lEmmc.c:900-934`, specifically
   `:917-919`). No CRC bit, no index bit.
3. CMD2, argument 0, flags `GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC`
   (`Gta4lEmmc.c:1073-1075`). First command with CRC checking.
4. CMD3, CMD9, CMD7, CMD13 follow only if CMD2 succeeds
   (`Gta4lEmmc.c:1088-1133`).

### 3.2 Error mapping

`gta4l_emmc_send_command()` polls `ERROR_INT_STATUS` before accepting `CC`
(`Gta4lEmmc.c:317-338`). On any enabled error bit it records the failure,
clears the latches, and returns `gta4l_emmc_map_error(error, 0)`
(`Gta4lEmmc.c:327-330`). `gta4l_emmc_map_error()` checks `CMD_TIMEOUT` first,
then `CMD_CRC` (`Gta4lEmmc.c:265-284`). Thus `error=0x0002` maps to `-8`.

### 3.3 The inference

Given SDHCI semantics, a controller raises `CMD_CRC` only for a command with
CRC checking enabled. CMD0 has no response and no CRC. CMD1 has `RESP_SHORT`
only and no CRC. The core returns immediately on the first error. Therefore,
for the recorded build, `-8` uniquely implies CMD2.

This is a code-path inference for the older stage-3 build, which did not capture
a hardware index. The 2026-10-31 current-source run does capture it
(`fail_index=2`, `fail_command=0x0209`, `fail_error=0x0002`) and confirms the
CMD2 attribution for the section 2.6 image. The later `cmd2window` run (2.7)
moves the attribution to CMD3 for that run; the 2026-11-01 `fmt=4` runs (2.9)
return it to CMD2 with the same captured fields. See 3.5 and 3.6.

### 3.4 Explicit non-supported statement

The older stage-3 transcript does **not** establish:

- the failing command index;
- the exact `ERROR_INT_STATUS` value;
- the raw `RESPONSE0..3` words;
- whether `CC` was also set;
- the DLL/vendor-spec3/DDR/MND state at the failure.

The current-source run in 2.6 does establish the command index, the error value
and the raw response words for that image. The CMD2 attribution for the
section 2.6 run is hardware-confirmed; the older transcript remains an
inference. The `cmd2window` run in 2.7 establishes a different terminal command
(CMD3) and is not a CMD2 attribution. The `fmt=4` run in 2.9 establishes CMD2
again (`fail_index=2`, `fail_command=0x0209`, `fail_error=0x0002`) on the
2026-11-01 current tree.

### 3.5 cmd2window attribution (2026-10-31)

The section 2.7 run changes the attribution. Its `fail_index=3`,
`fail_command=0x031A`, `fail_argument=0x00010000`, `fail_error=0x0000`,
`fail_normal=0x0000`, `rc=-7` and `step=9` show that CMD2 completed and CMD3
was the terminal command. The timeout came from the completion poll, not from
a latched error bit. The raw window retains CID words 2..4 and a plausible
CMD3 R1 status, so the CMD3 response appears to have been present while `CC`
was not observed. This is a CMD3 completion/status result, not a CMD2 CRC
result.

### 3.6 fmt=4 attribution (2026-11-01)

The section 2.9 runs record `fail_index=2`, `fail_command=0x0209`,
`fail_argument=0`, `fail_error=0x0002`, `fail_normal=0x8000`, `step=8` and
`rc=-8`. This is the CMD2 CRC attribution again, with the same captured
failure values as section 2.6. CMD3, CMD9 and CMD7 were not submitted, so the
section 2.7 CMD3 path and the section 2.8 CMD7 path were not exercised. The
outcome therefore differs from `cmd2window` and `fmt=2` without an established
cause; the runs do not select among H2/H4/H5/H6/H7.

### 3.7 cmd1gap candidate attribution (2026-11-01)

The section 2.11 candidate is the fail-closed source plus one 8-us delay after
an accepted ready CMD1. On the owner's unit the fail-closed image failed CMD2
and the candidate completed CMD2, CMD3, CMD9, CMD7 (CC and TC observed) and the
EXT_CSD read, then validated capacity, sector size and partition access. The
same candidate also completed identification on two owner-reported SM-T505
units. Because the delay is the only source delta, the comparison supports a
timing-sensitive contribution at the CMD1-to-CMD2 boundary for these runs.

It does not establish the cause of the CMD2 CRC. The `cmd2window` and `fmt=2`
builds passed CMD2 on the same owner's unit without the delay, so the delay is
not proven necessary, and the fail-closed success-path error re-reads remain a
possible timing contributor. H2, H4, H5, H6 and H7 remain open; H8 applies
only when CMD2 completes, and the candidate shows the current CMD3 and CMD7
completion paths do work on this card. The historical `fmt=2` missing-CC CMD7
timeout did not reproduce. No cause is established for either the earlier CMD2
failure or the earlier CMD7 timeout, and the candidate is not a reliability
result or a general fix.

---

## 4. Reference parity: the CMD2 software path matches mainline

### 4.1 Command encoding

`Gta4lEmmc.h:146-154`:

    RESP_NONE       0x0000
    RESP_LONG       0x0001
    RESP_SHORT      0x0002
    RESP_SHORT_BUSY 0x0003
    RESP_TYPE_MASK  0x0003
    CRC             0x0008
    INDEX           0x0010

These exactly match `/tmp/opencode/linux-pin/sdhci.h:68-71`.

The core encodes the command register as `(index << 8) | flags`
(`Gta4lEmmc.c:299`). CMD2 therefore encodes `0x0209`.

Linux sets `SDHCI_CMD_RESP_LONG` for `MMC_RSP_136`
(`/tmp/opencode/linux-pin/sdhci.c:1711-1712`) and `SDHCI_CMD_CRC` for
`MMC_RSP_CRC` (`/tmp/opencode/linux-pin/sdhci.c:1718-1719`). The core does the
same.

### 4.2 Long-response normalization

Core: `gta4l_emmc_read_long_response()` reads `RESPONSE3..RESPONSE0` in reverse
word order, shifts each word left by 8, and ORs the next word's top byte
(`Gta4lEmmc.c:202-217`).

Linux: `sdhci_read_rsp_136()` performs the identical shift
(`/tmp/opencode/linux-pin/sdhci.c:1798-1816`).

Samsung reference: the same shift
(`/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci.c:1442-1460`).

Simulator: `sim_set_response()` encodes a normalized 128-bit response into the
raw register layout (`src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c:527-543`),
and the `cmd-crc-snapshot` test verifies that the raw words are snapshotted
before the error latch is cleared (`harness.c:1209-1213,1765-1790`).

The normal SDHCI layout strips the CRC byte; the raw `RESPONSE0..3` words
reconstruct the 128-bit CID but do not directly expose the card's CRC7 byte.
The core's comment at `Gta4lEmmc.c:195-198` now states this layout correctly.
The constants and the shift behavior are correct.

### 4.3 Error mapping

Linux maps `SDHCI_INT_CRC` to `-EILSEQ` (`/tmp/opencode/linux-pin/sdhci.c:3330-3336`).
The core maps `CMD_CRC` to `-8` (`Gta4lEmmc.c:265-284`). The semantics match.

### 4.4 Send/poll sequence

Core (`Gta4lEmmc.c:287-362`):

1. Wait for command/data inhibit clear.
2. Clear normal and error latches.
3. Write `ARGUMENT`.
4. Write `COMMAND`.
5. Poll `ERROR_INT_STATUS`; if any enabled error bit, record and return.
6. Poll `NORMAL_INT_STATUS` for `CC`.
7. On `CC`, read the response (long or short).
8. Clear latches.

Linux performs the same conceptual sequence in its IRQ-driven command path; the
portable core uses polling with interrupt signalling disabled
(`Gta4lEmmc.c:230-243`). No semantic divergence was found.

The simulator covers the error-priority case where the controller reports `CC`
together with `CMD_CRC` (`harness.c:1204-1207,1754-1763`): the error must win,
and the CRC-failed CID must not be promoted to `host.cid`.

### 4.5 Clock

Core constants (`Gta4lEmmc.h:179-184`):

    RCG_400K_CFG = 0x00002017
    RCG_400K_M   = 1
    RCG_400K_N   = 0xFC
    RCG_400K_D   = 0xFB

Programming: `Gta4lEmmc.c:511-566`.

Mainline table: `/tmp/opencode/linux-verify/gcc-sm6115.c:1248-1258` contains
`F(400000, P_BI_TCXO, 12, 1, 4)`; `cmd_rcgr = 0x38028`
(`gcc-sm6115.c:1261`).

The RCG formula is `parent * 2 / (pre_div + 1) * m / n`; with
`pre_div = 2*h - 1 = 23`, `m/n = 1/4`, and `P_BI_TCXO = 19.2 MHz`, the result
is `19.2 MHz * 2/24 * 1/4 = 400 kHz`. The M/N/D encodings are `M=1`,
`N=~ (4-1)=0xFC`, `D=~4=0xFB`. The simulator asserts the same values
(`harness.c:1333-1345`).

The header comment at `Gta4lEmmc.h:179` now states the correct
`BI_TCXO * 2 / 24 * 1 / 4` arithmetic; the constants are correct.

The actual card-clock frequency at the pad is **not measured**. `cfg=00002017`
and `clk=0007` are register states, not a frequency measurement.

### 4.6 Power and IO

Core power sequence: `gta4l_emmc_power_on()` (`Gta4lEmmc.c:568-682`). It writes
`POWER_CONTROL` with bus power and 3.3 V select, then handles each observed
`CORE_PWRCTL_STATUS` request, clears only the observed bits, applies the
latest-level rule (`BUS_ON` implies IO-high; `IO_LOW` overrides; `IO_HIGH`
overrides), and requires an observed `BUS_ON` completion plus completion of the
requested IO transition.

Mainline reference: `/tmp/opencode/linux-pin/sdhci-msm.c:1718-1755` for the
power handler; `:2323-2325` for `HOST_CONTROL2 VDD_180` to
`REQ_IO_LOW`/`REQ_IO_HIGH`; `:2412-2417` for pad switch; `:2452-2480` for
voltage switch.

Core policy: `Gta4lEmmcProbe.c:345-352` sets `fixed_1v8=1` and
`pad_pwr_switch=1`; `Gta4lEmmc.c:825-864` selects `HOST_CONTROL2 VDD_180` and
the pad power switch.

POR sequence: mainline's `CORE_VENDOR_SPEC_POR_VAL` is `0xA9C`
(`sdhci-msm.c:76`). Samsung and PE write `0xA1C` initially
(`/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci-msm.c:101`) and then
immediately OR in `CORE_VNDR_SPEC_ADMA_ERR_SIZE_EN` (bit 7), producing `0xA9C`.
The core writes `0xA9C` directly (`Gta4lEmmc.h:102`, `Gta4lEmmc.c:1213`). The
earlier description of this as a `0xA1C` versus `0xA9C` divergence is
corrected; the remaining difference is the later pad policy, not the POR
value. Whether the ADMA-error-size enable is required for the port's non-ADMA
path is not established.

The power acknowledgement is controller-local. `BUS_SUCCESS` does not prove the
rail voltage. `pwr=0f` and `pst=00000000` are not electrical measurements.

### 4.7 Low-speed vendor/DLL configuration

Core: `gta4l_emmc_configure_low_speed()` (`Gta4lEmmc.c:702-736`) clears
`CORE_VENDOR_SPEC3` `PWRSAVE_DLL`, sets `DLL_RST | DLL_PDN`, selects the default
MCLK, clears `HC_SELECT_IN_EN`/`HC_SELECT_IN_MASK`, and clears
`CORE_CLK_PWRSAVE`.

Mainline: `msm_hc_select_default()` (`/tmp/opencode/linux-pin/sdhci-msm.c:803-840`)
performs the vendor-spec normalization; the <=100 MHz path sets
`DLL_RST | DLL_PDN` (`sdhci-msm.c:1397-1420`).

Simulator: `harness.c:1362-1392` asserts 1.8 V signalling, pad switch, DLL
reset/power-down, MCLK default, `HC_SELECT_IN` cleared, clock power-save
cleared, and the normalized POR value.

The current core therefore matches the mainline low-speed policy. The
hardware-tested build did not produce the normalized value (see section 5).

### 4.8 Vendor-register write comparison (v5, minor 0x72)

The v5 offset table uses `core_ddr_config = 0x25c`
(`/tmp/opencode/linux-pin/sdhci-msm.c:229`), selected because minor `0x72 >= 0x49`
(`:2827-2828`). `0x1b8`/`0x1bc` are the v4/mci offsets (`:259-260`) and do not
apply.

Linux does **not** write `DDR_CONFIG_POR_VAL 0x80040873` before the first
command at 400 kHz. The write occurs only in
`sdhci_msm_cm_dll_sdc4_calibration()` (`:1047-1051`), reached from the HS400
DLL-calibration path (`:1427-1428` → `:1345` → `:1140`) and guarded by
`host->clock > CORE_FREQ_100MHZ && (msm_host->tuning_done ||
ios->enhanced_strobe) && !msm_host->calibration_done` (`:1342-1344`).
`CORE_CMDIN_RCLK_EN` is `BIT(1)` of `core_ddr_200_cfg` (`0x224`,
`:116,225`) and is set only under `if (mmc->ios.enhanced_strobe)` inside the
same HS400 calibration (`:1053-1058`). Neither is required before CMD2 at
400 kHz.

Linux vendor writes before the first command, and the core's coverage:

| Offset | Value / action | Context | Core coverage |
| --- | --- | --- | --- |
| `0x20C` | `0x00000A9C` | probe POR (`:2779-2780`) | performed (`Gta4lEmmc.c:794-796`) |
| `0x21C` | capabilities advertisement | probe (`:2820-2824`) | **missing** |
| `0x244` | `0x0000000F` power-IRQ mask | probe (`:2861-2862`) | **missing** |
| `0x20C` | pad-switch bits 15/16 | regulator caps (`:2384-2421`) | performed (`Gta4lEmmc.c:831-841`) |
| `0x248`/`0x24C` | power status/ack | probe/power IRQ (`:1693-1694,1776-1777`) | performed in the handshake (`Gta4lEmmc.c:605-606,642-643`); probe-time zero writes skipped |
| `0x250` | clear `BIT(3)` PWRSAVE_DLL | `msm_hc_select_default()` (`:811-816`) | performed (`Gta4lEmmc.c:686-691`) |
| `0x20C` | clear `0x300`, set `0x200`, clear `BIT(18)`/`0x380000` | `msm_hc_select_default()` (`:819-834`) | performed (`Gta4lEmmc.c:705-714`) |
| `0x200` | set `BIT(30)` DLL_RST, then `BIT(29)` DLL_PDN | `<=100 MHz` (`:1404-1414`) | performed (`Gta4lEmmc.c:693-703`) |
| `0x224`/`0x25C`/`0x254`/`0x258` | not written at 400 kHz | HS400 only | not written |

The only omissions are `0x21C` (vendor capabilities advertisement) and `0x244`
(power-IRQ mask). Neither participates in command issue, response sampling, or
CRC checking. The HS400-only `0x25C` and `0x224 BIT(1)` writes are not performed
by Linux at 400 kHz either. The legacy vendor state that could affect sampling
(`0x20C` MCLK_SELECT/HC_SELECT_IN/pad bits, `0x250` PWRSAVE_DLL, and `0x200`
DLL_RST|PDN) is matched. No missing vendor write is a credible cause of a CMD2
R2 `CMD_CRC` at 400 kHz.

---

## 5. Evidence/build mismatch

### 5.1 The hardware-tested build is not current source

The stage-2/3 hardware images used kernel
`1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a`
(`src/UniXNU-gta4l/docs/emmc-driver.md:328,360`). The 2026-10-31 image
(`33311a32…`) was built from `Gta4lEmmc.c 722110a5…`,
`Gta4lEmmc.h 7abc7d87…`, `Gta4lEmmcProbe.c d541514b…`. The working tree has
since gained the response-window capture in section 10 (`Gta4lEmmc.c 7aaf0018…`,
`Gta4lEmmc.h b2df6b7d…`, `Gta4lEmmcProbe.c fc06b76c…`); that source has not
been built or run on hardware.

The `33311a32` source adds:

- raw `RESPONSE0..3` failure snapshot before clearing the error latch
  (`Gta4lEmmc.c:97-116`);
- `cmd1_attempts` / `cmd1_timeouts` accounting (`Gta4lEmmc.h:326-327`,
  `Gta4lEmmc.c:923-958`);
- a richer failure panic with `rsp=`, `cmd1=`, `v3=`, `ddr=`, `mnd=`, and
  `hc=` fields (`Gta4lEmmcProbe.c:425-456`).

The older flashed failure-record image `be30e9f4…` was built before these
additions and remains unrun. The later image built from `33311a32…` was flashed
and run on 2026-10-31; it reproduces the CMD2 CRC failure with the normalized
state (2.6). The working tree's additional `cmd1rsp=`/`pre2rsp=` capture is
documented in section 10 and ran in the section 2.7 `cmd2window` image. The
2026-11-01 failclosed image is built from the current source (`e493b3eb…`,
`e0a2830c…`, `932552a2…`) and is the first `fmt=4` hardware run; it failed at
CMD2 before the CMD7 capture (2.9).

### 5.2 The `ven` value proves the tested build lacked current normalization

Stage-3 reports `ven=00318a0c`
(`out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`).
The handoff snapshot reports `vs=00300a0c` and `dll=00000000`
(`docs/sdhci-handoff-audit.md:130-158`). The stage-3 value is the handoff value
plus pad bits `0x00018000`.

Current code writes `CORE_VENDOR_SPEC_POR = 0x00000A9C` after reset
(`Gta4lEmmc.c:814-818`) and then applies the pad policy and low-speed fields
(`Gta4lEmmc.c:850-866`). The expected current value is `0x00018A9C`
(`0xA9C | 0x18000`), with `HC_SELECT_IN` cleared. The simulator requires exactly
`SIM_VENDOR_SPEC_POR | SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH`
(`harness.c:1390-1392`).

Therefore the recorded `ven=00318a0c` is not the current core's expected
host-init state. The stage-3 transcript does not validate the current
POR/vendor/DLL code. This is an inference from the register values and the
documented build hashes, not a build-log correlation from the same run.

The 2026-10-31 current-source run reports `ven=00018a9c`, so the normalized
POR/pad policy is confirmed on hardware. CMD2 still failed in that image, so
the old normalization gap no longer explains that failure. The later
`cmd2window` run (2.7) also had the normalized state and did not reproduce the
CMD2 CRC; CMD3 timed out instead. The 2026-11-01 `fmt=4` runs report the same
normalized state and reproduces the CMD2 CRC (2.9), so the state is not a
discriminating variable between the passing and failing diagnostic runs.

### 5.3 Documentation was stale in places

All items below were corrected in the working tree after the audit; the line
numbers refer to the audit baseline.

- `src/UniXNU-gta4l/docs/emmc-driver.md:456` cites
  `package/Gta4lEmmc.h:208` for `GTA4L_EMMC_ERR_CMD_CRC`; current line is
  `Gta4lEmmc.h:242`.
- `docs/emmc-driver.md:457-461` says `ven=00318a0c` repeats the stage-2
  host-init state and that the 1.8 V signalling and pad-switch policy were
  applied. The pad bits are set, but the value is not the current normalized
  value. The bullet is stale for current code.
- `docs/emmc-driver.md:395` said the failure-report run was pending; the
  current-source run has since completed (2.6) and the line now says the older
  failure-record image was not booted.
- `docs/emmc-driver.md:502-507,542` correctly says the recorded stage-3
  transcript predates the failure record and DLL fields.
- `docs/emmc-driver.md:370-376` says the failure-record build matches the
  current source hashes. That was true when the image was built; the source has
  since changed. Re-check before citing the flashed image as current.
- `Gta4lEmmc.h:179` comment arithmetic was wrong; it is corrected in the current
  tree.
- `Gta4lEmmc.c:195-198` comment described the CRC byte layout incorrectly; it is
  corrected in the current tree.

### 5.4 Explicit non-supported statement

The following are **not** established:

- that current source fixes the stage-3 failure;
- that the old build's missing POR/vendor/DLL normalization caused the CRC
  error;
- that the older `be30e9f4` failure-record image contains the current
  raw-response and CMD1 diagnostics;
- that `rsp=` captures CMD2's CID bits rather than a stale CMD1 response;
- that `ven=00318a0c` is equivalent to the current code's host-init state.

The 2026-10-31 run establishes that current source reproduces the CMD2 CRC
failure with the normalized state (2.6).

---

## 6. Hypotheses (unranked)

The hypotheses apply to the **recorded failure** and to a failure that persists
on current source. They are not ranked: the 2026-10-31 run does not provide
discriminating evidence for an ordering. Each hypothesis has a discriminating
diagnostic.

**2026-10-31 update:** the current-source run (2.6) excludes H1 as a sufficient
explanation for that failure: the normalized state still failed CMD2. The
failed-command `rsp=` snapshot is OCR-like, but its origin is unconfirmed, so
H2 cannot be selected from `rsp=` alone. H1 remains a candidate for the old
stage-3 build only. H6 and H7 remain open. The other hypotheses are not ranked.

**cmd2window update (2026-10-31):** the section 2.7 run did not reproduce the
CMD2 CRC failure. CMD2 completed; CMD3 timed out with no error bit while the
window held real CID words and a plausible CMD3 R1 status. H2, H4 and H5 lose
their CMD2 premise for this boot. H1 remains excluded as a sufficient
explanation. H3 remains unmeasured but is not indicated. H6 and H7 remain
open, and the CMD3 completion path is the leading concrete suspect (H8). The
hypotheses below keep their original CMD2 framing as the historical record.

**fmt=4 update (2026-11-01):** the section 2.9 runs reproduced the CMD2 CRC
record on current source, matching section 2.6 word for word. `cmd2window` and
`fmt=2` had passed CMD2, so the outcome varies across diagnostic builds. H2,
H4 and H5 cannot be excluded as intermittent contributors; H1 remains excluded
as a sufficient explanation; H3 remains unmeasured but is not indicated; H6
and H7 remain open; H8 applies only to a boot that completes CMD2 and was not
reached.

### H1 — the recorded failure is an artifact of the old build's unnormalized host-init state

**Statement:** The hardware-tested build `1a471089…` did not apply the current
POR/vendor/DLL low-speed normalization. The resulting controller state may have
been unsuitable for the CRC-checked 136-bit R2 response.

**Why:**

- Stage-3 `ven=00318a0c` equals handoff `vs=00300a0c` plus pad bits; current
  code writes POR `0xA9C` and normalizes to `0x18A9C`
  (`Gta4lEmmc.c:814-818,850-866,702-736`; `harness.c:1079,1362-1392`).
- The handoff DLL config was zero (`docs/sdhci-handoff-audit.md:130-158`), but
  the stage-3 DLL state was not captured.
- The stage-3 build predates the current raw-response snapshot and CMD1 counters
  (`docs/emmc-driver.md:328,360`; current hashes in the baseline table).

**Discriminating diagnostic:**

1. Boot the already-flashed failure-record image (`be30e9f4…`) with owner
   approval. It captures `fail=` but not `rsp=`/`cmd1=`.
2. Build current source (`722110a5…`, `d541514b…`, `5262362d…`) into a new
   failure-record image; with separate owner approval, write it to BOOT only and
   boot it.
3. Compare `ven`, `v3`, `dll`, `dst`, `ddr`, and `mnd` at failure. If
   `ven=0x18A9C` and the DLL bits are `DLL_RST|DLL_PDN`, current normalization
   was applied. If CMD2 then passes, H1 is confirmed for the old failure. If
   CMD2 still fails, H1 is excluded as a sufficient explanation.

**Not supported:** The old build's `ven` value does not prove that the missing
normalization caused the CRC error. At 400 kHz the DLL is not required, the
handoff DLL was already zero, and `HC_SELECT_IN_EN` was clear in the handoff
value. The functional impact of the missing POR bits is unproven.

**Update (2026-10-31):** excluded as a sufficient explanation for the current
failure by 2.6. It remains a candidate for the old `1a471089` build; the old
failure's cause is not established.

### H2 — CMD-line electrical/signal-integrity or pad/pull-up issue corrupts the R2 response

**Statement:** The card drives a 136-bit R2 response on CMD. A pad direction,
pull-up, drive-strength, pad-supply, or signal-integrity problem can corrupt one
or more response bits and make the controller's CRC check fail.

**Why:**

- The software path matches mainline (section 4), so a persistent failure points
  away from encoding/decoding.
- CMD1 uses the same CMD line and succeeds with a short R3 response; R2 is
  longer and card-driven. CMD1 success does not prove R2 signal integrity.
- The core writes the UEFI-derived pad value `0x9FE4` (`Gta4lEmmc.h:187`,
  `Gta4lEmmc.c:684-700`), but the actual pad state, pad supply, and CMD pull-up
  are not measured (`docs/sdhc-electrical-contract.md:48-52`).

**Discriminating diagnostic:**

1. Capture raw `RESPONSE0..3` with the current build and reconstruct the 128-bit
   CID using the `read_long_response` shift (`Gta4lEmmc.c:202-217`):
   - `cid[0] = rsp[3] << 8 | rsp[2] >> 24`
   - `cid[1] = rsp[2] << 8 | rsp[1] >> 24`
   - `cid[2] = rsp[1] << 8 | rsp[0] >> 24`
   - `cid[3] = rsp[0] << 8`
2. Compare the reconstructed CID with a known-good CID from TWRP/Linux or
   another host. If the reconstructed CID differs, the response bits were
   corrupted → signal/electrical.
3. Scope CMD and CLK at the pad during CMD2 at 400 kHz, if equipment is
   available. Measure L11/pad supply and the CMD pull-up rail.
4. Read the TLMM SDC1 QDSD pad register directly; `hc=` (SDHCI host control)
   and `ven=` (SDHCI vendor spec) do not read back the TLMM `0x9FE4` pad value.

**Not supported:** No CMD/CLK scope, pad voltage, pull-up, drive-strength, or
signal-integrity measurement exists. CMD1 success does not prove R2 integrity.
A failed `rsp=` snapshot that differs from the known CID is not proof of
electrical corruption: stale, partial or differently packed response registers
remain possible. The section 2.9 snapshot is consistent with a retained or
rotated CMD1 window, not with identified corrupted CID bits.

### H3 — rail/voltage/power state is wrong or droops during CMD2

**Statement:** VCC (L24) or VCCQ/IO (L11) is not at the intended level, or the
card is underpowered during the R2 response, causing a CRC error.

**Why:**

- The core selects 1.8 V signalling and the pad switch based on board policy
  (`Gta4lEmmcProbe.c:345-352`, `Gta4lEmmc.c:825-864`), but the actual L11 level
  is unmeasured.
- The power acknowledgement is controller-local; `BUS_SUCCESS` does not prove
  the rail (`Gta4lEmmc.c:568-682`; `docs/sdhc-electrical-contract.md:48-52`).
- Stage-3 `pwr=0f` and `pst=00000000` are register states, not voltage
  measurements.

**Discriminating diagnostic:**

1. Measure L24 and L11 at the eMMC/pads before and during the probe. Check PMIC
   state and any shared consumers.
2. Compare with TWRP/Linux boot state on the same tablet.
3. Capture `pwr`, `pst`, and the power log event with the current build.
4. If a rail is wrong or drooping, correct the power path only with owner
   approval; do not change CRC checking.

**Not supported:** No rail measurement exists. The `pwr=0f`/`pst=0` values do not
prove voltage or current. No droop evidence exists.

### H4 — card-side CID/R2 CRC error or card state

**Statement:** The eMMC responds to CMD1 but transmits an invalid CID CRC, or is
in a state where CMD2's R2 response is not reliable.

**Why:**

- CMD1 returns a valid R3 response with bit 31 set, proving the card is present
  and responding (`Gta4lEmmc.c:900-934`).
- CMD2's CID is fixed; a damaged, refurbished, or nonconforming card could have
  a bad CID CRC.
- The board's populated eMMC is identified as `DX68MB`
  (`docs/sdhc-electrical-contract.md:48-52`). The CID/CSD were later recorded
  from the recovery sysfs (section 2.7): CID
  `150100445836384d42009005f4661900` and CSD
  `d02701320f5903fff6dbffef8e404000`, and the CID CRC7 prefix check is
  consistent. Those are Linux-normalized values, not an independent raw
  capture, so they do not by themselves validate the card's transmitted CRC.
- TWRP boots from eMMC, which suggests the card works under another software
  stack, but that access was not re-verified in this audit.

**Discriminating diagnostic:**

1. Read the CID/CSD via TWRP/Linux (`/sys/block/mmcblk0/device/cid`, `csd`, or
   `mmc-utils`) if accessible. Record the exact command and output.
2. Compare the known CID with the reconstructed CID from the current build's
   `rsp=` snapshot.
3. If the reconstructed CID matches the known CID but the controller still
   reports CRC, the data bits are intact; the fault is in the CRC byte/check path
   (card or controller).
4. Try another known-good eMMC/board if available. Do not alter or wipe the card.

**Not supported:** At the original audit time the CID/CSD contents and the
card's CRC7 were unknown. Recovery sysfs values are now recorded (section 2.7),
but they come through the same Linux long-response normalization and are not an
independent raw capture; CMD1 success does not prove the CID CRC is valid. This
audit did not re-verify TWRP eMMC access.

### H5 — controller R2 CRC-engine/quirk/silicon behavior

**Statement:** The SDHCI v5 controller's CRC engine or R2 handling has a
vendor-specific behavior that makes CMD2 fail even with a valid card response.

**Why:**

- The controller is the same IP family as Linux's `sdhci-msm`. The pinned
  `sdhci_msm_pdata` sets `SDHCI_QUIRK2_PRESET_VALUE_BROKEN` as its only
  `quirks2` value; it does not set `SDHCI_QUIRK2_RSP_136_HAS_CRC`
  (`/tmp/opencode/linux-pin/sdhci-msm.c:2579-2586`).
- `SDHCI_QUIRK2_RSP_136_HAS_CRC` affects response layout, not the hardware CRC
  check; the core always applies the non-quirk shift (`Gta4lEmmc.c:202-217`).
- No direct observation of the controller's CRC computation exists.
- If Linux/TWRP reads the CID on the same device, the controller/card path is
  capable under Linux's configuration, which would weaken H5. It would not by
  itself select another hypothesis.

**Discriminating diagnostic:**

1. Check the pinned `sdhci-msm` quirks and DT compatible for this SoC/board.
   Record whether `SDHCI_QUIRK2_RSP_136_HAS_CRC` or a vendor CRC quirk applies.
   The pinned data checked here sets only `SDHCI_QUIRK2_PRESET_VALUE_BROKEN`
   plus the dynamic broken-64-bit-DMA flag; recheck for the exact compatible.
2. Compare Linux/TWRP CID reads on the same tablet with the core's CMD2 result.
3. Capture `ERROR_INT_STATUS`, `NORMAL_INT_STATUS`, and raw `RESPONSE0..3` with
   the current build.
4. Do not add a quirk or change CRC handling without evidence and owner
   approval.

**Not supported:** No direct controller CRC observation exists. The pinned
mainline data does not select `SDHCI_QUIRK2_RSP_136_HAS_CRC`, but that only
bounds a reference quirk; it does not exclude a vendor-specific behavior. The
effect of any quirk on the CRC error bit is not established.

### H6 — host software bug in the CMD2 path

**Statement:** The core's CMD2 encoding, response handling, or error mapping is
wrong.

**Why this is considered less likely:**

- Encoding matches mainline (`Gta4lEmmc.c:299`; `sdhci.h:68-71`).
- Long-response normalization matches Linux and Samsung (`Gta4lEmmc.c:202-217`;
  `sdhci.c:1798-1816`; Samsung `sdhci.c:1442-1460`).
- Error mapping matches Linux (`Gta4lEmmc.c:265-284`; `sdhci.c:3330-3336`).
- The simulator covers CMD2 CRC, CC+CRC priority, and raw-response snapshot
  (`harness.c:1202-1213,1742-1790`).

**Discriminating diagnostic:**

- Confirm the captured failure fields are `fail_index=2`, `fail_command=0x0209`,
  `fail_error=0x0002`, and `fail_step=GTA4L_EMMC_STEP_CMD1`. If they are not,
  revisit the software path.
- The simulator is not silicon; it cannot prove the hardware's exact behavior,
  but it excludes the obvious software defects.

**Not supported:** The simulator is a model. It does not prove the silicon
behaves identically. The software path is not the leading explanation, but it is
not proven perfect.

### H7 — clock frequency/programming is not actually 400 kHz

**Statement:** The RCG programming or parent rate is wrong, so CMD2 runs above
the intended identification clock and fails CRC.

**Why this is considered less likely:**

- The RCG values match the mainline table (`Gta4lEmmc.h:179-184`;
  `gcc-sm6115.c:1248-1258,1261`).
- The formula yields 400 kHz from 19.2 MHz (`pre_div=23`, dual-edge, `m/n=1/4`).
- The simulator asserts `0x2017/1/0xFC/0xFB` (`harness.c:1333-1345`).
- Stage-3 `cfg=00002017` matches the programmed value.

**Discriminating diagnostic:**

1. Measure CLK at the pad or read the actual parent rate; verify the RCG update
   completed.
2. If the actual frequency is 400 kHz, reject H7.
3. If it is 800 kHz or 1.6 MHz, investigate the RCG update/parent.

**Not supported:** The actual card-clock frequency is not measured.
`cfg=00002017` and `clk=0007` are register states, not a measurement. The parent
rate is assumed from the mainline table.

### H8 — CMD3 completion is not observed (host completion path/timing)

**Statement:** CMD2 succeeds, the card answers CMD3, but the controller's
command-complete (`CC`) status is not observed by the polling path, so the
driver reports a timeout even though the R1 response is present.

**Why:**

- Section 2.7: `rc=-7`, `fail_error=0x0000`, `fail_normal=0x0000`,
  `step=9`; `RESPONSE0=0x00000500` is a plausible CMD3 R1 status, while
  `RESPONSE1..3` retain the CMD2 CID words.
- CMD3 is the first command in the sequence with `CRC | INDEX` and a short
  response after a long response. CMD1 (`RESP_SHORT` only) completes with the
  same polling loop, so the loop and the `CC` bit work for CMD1.
- The build-2 CMD2 CRC did not reproduce in the diagnostics-only build, so
  timing/state sensitivity around the CMD1→CMD2→CMD3 transition is possible.
- At the time of the `cmd2window` run the driver polled `NORMAL_INT_STATUS` and
  did not read the command register or the response after the timeout; a missed
  `CC`, a masked status bit, or a command-register/response-type issue could not
  be distinguished from that record. The current `fmt=4` tree adds fresh
  post-timeout status/command/argument/response samples
  (`Gta4lEmmc.c:450-466`), but the `fmt=4` runs stopped at CMD2, so that CMD7
  capture remains unexercised.

**Discriminating diagnostic:**

1. Capture `NORMAL_INT_STATUS`, `ERROR_INT_STATUS`, `PRESENT_STATE`,
   `COMMAND`, and `RESPONSE0..3` immediately after the CMD3 completion poll
   expires, before the interrupt clear.
2. Add a post-CMD3 raw-window capture, and log the first and last observed
   `NORMAL_INT_STATUS` values during the poll.
3. Compare the CMD3 command encoding and completion path with the pinned
   Linux `sdhci_send_command()`/`sdhci_finish_command()` path and with the
   CMD1 path that succeeds.
4. Check whether the controller requires a different response-type or
   interrupt-enable setting for a CRC+index short response; do not change CRC
   checking or add a quirk without evidence and owner approval.

**Not supported:** At the time of the `cmd2window` run no post-timeout
command/status capture existed. The current tree adds those samples, but they
have not produced a CMD3/CMD7 hardware sample because the `fmt=4` runs stopped
at CMD2. The CMD3 R1 value is plausible but not independently confirmed as the
CMD3 response. The cause of the missing completion is not established.

**fmt=2 update (2026-10-31):** H8 as stated is CMD3-specific and is not the
terminal issue in the section 2.8 run. CMD3 completed (the `c3l` poll saw
`CC`) and CMD9 completed (`step=11`). The same completion-observation pattern
then appeared at CMD7, the first R1b command: `error=0`, `normal=0x0002` (TC),
`CC` absent, and the driver returned its software timeout. The broader
"completion is not observed" question therefore moves to the CMD7/R1b path; it
is not confirmed for CMD3. See `docs/emmc-cmd7-timeout-finding.md`.

---

## 7. Discriminating diagnostic plan (owner approval required)

**2026-10-31 cmd2window update:** D2/D3 have produced a CMD2 pass and a CMD3
timeout (2.7). The next discriminating step is D7, not another CMD2 CRC run.

### D0 — Preconditions

- Preserve TWRP on `RECOVERY`; verify the recorded hash `14e11370…` before and
  after any BOOT write.
- Verify EFS/SEC_EFS backups before any flash.
- Do not disable CRC checking.
- Do not write any partition without explicit owner approval.

### D1 — Boot the older failure-record image

Image: `out/gta4l/ram-root-emmc-probe-failrec-evidence/boot-ram-root-emmc-probe-failrec.img`
(`be30e9f4…`).

This requires no new flash. It captures the failure-record fields present in that
build (`fail_valid/index/argument/command/error/normal/present/step`) but not the
current `rsp=`/`cmd1=` fields.

Expected if the failure persists:

    fail=1/2/0/0x0209/0x0002/<normal>/<present>/<fail_step>

This confirms CMD2 and the CRC error bit for that build.

**Status:** superseded, not completed. D2 already captured the richer fields on
2026-10-31; this image was not booted.

### D2 — Build and run the current failure-record image

Build from current source (`Gta4lEmmc.c 722110a5…`, `Gta4lEmmcProbe.c d541514b…`).
The current failure path captures:

- `fail=` valid/index/argument/command/error/normal/present/step;
- `rsp=` raw `RESPONSE0..3` before clear;
- `cmd1=` attempts/timeouts/OCR;
- `v3=`, `dll=`, `dst=`, `ddr=`, `mnd=`, `hc=`.

**Completed 2026-10-31.** Image `33311a32…` (kernel `12ebe94c…`) was written to
BOOT only and run. The report is in 2.6. `ven=0x18A9C`, `PWRSAVE_DLL` clear,
`DLL_RST|DLL_PDN`, and the RCG programmed for a nominal 400 kHz all match the
expected current values. CMD2 failed with `CMD_CRC` in that image. The later
`cmd2window` image (2.7) did not reproduce that failure; CMD2 completed and
CMD3 timed out.

### D3 — Analyze the raw response

Reconstruct the CID with the shift from `Gta4lEmmc.c:202-217`:

    cid[0] = rsp[3] << 8 | rsp[2] >> 24
    cid[1] = rsp[2] << 8 | rsp[1] >> 24
    cid[2] = rsp[1] << 8 | rsp[0] >> 24
    cid[3] = rsp[0] << 8

Compare with a known-good CID from TWRP/Linux. Classification:

| Observation | Interpretation | Next |
| --- | --- | --- |
| Reconstructed response matches the known CID, controller reports CRC | Response data bits intact; fault is in the CRC byte/check path (card or controller) | H4/H5 candidate |
| Reconstructed response differs from the known CID | Not a valid CID; mismatch alone does not prove electrical corruption | Establish freshness/layout first; H2 candidate |
| Reconstructed response matches, controller does not report CRC | Failure not reproduced on current build | H1 candidate for the old build; investigate the difference |
| Raw response is OCR-like and not a CID layout | OCR-like failed-command snapshot; origin unconfirmed; not proof of stale CMD1 data or corrupted CID bits | Compare with the pre-CMD2 window (section 10), then investigate layout/response-type behavior |
| `fail_index != 2` or `fail_command != 0x0209` | CMD2 attribution wrong for this run | Revisit H6 |

**2026-10-31 result:** `rsp = 0x00000000, 0x00000000, 0x80000000, 0x00C0FF80`.
`0x00C0FF80` is the CMD1 OCR `0xC0FF8080` shifted right by 8; the words are
consistent with a left-aligned CMD1 short response and do not match the known
CID (redacted here as `1501…1900`). The run is OCR-like, so the CID
reconstruction classification cannot be applied. This is not proof that the
register window retained the previous CMD1 response. A response snapshot after
the final CMD1 and immediately before CMD2 was added to the working tree
(section 10) and ran in the `cmd2window` image (2.7).

**cmd2window result:** the pre-CMD2 window still held the CMD1 OCR
(`c0ff8080,0,0,0`), so the pre-command clear did not empty it. After CMD2
succeeded, the failed-command window (taken during the CMD3 timeout) held
`00000500,5836384d,42009005,f4661978`: CID words 2..4 plus the CRC7 trailer,
with `RESPONSE0` overwritten by a plausible CMD3 R1 status. The window is real
CMD2 data, not the stale CMD1 OCR. The `read_long_response` reconstruction
formula above cannot be applied to this window because `RESPONSE0` is not the
CID's first word; a post-CMD3 capture with the CMD3 response separated from the
CMD2 window is needed before using the formula as a CID check.

The build-2 raw snapshot does not expose the card's CRC7 byte in the normal
stripped-CRC layout. Do not claim a CRC7 recomputation from that `rsp=` alone;
the `cmd2window` trailer match (`0x78`) is a consistency check against the
known CID, not a CRC7 recomputation from a validated CID.

### D4 — Electrical measurements

Measure and record:

- L24 (VCC) at the eMMC/pads, before and during CMD2.
- L11 (VCCQ/IO) at the eMMC/pads, before and during CMD2.
- CMD pull-up rail and pad supply.
- CMD and CLK at the pad, if a scope is available; record frequency, amplitude,
  edges, and any contention.
- PMIC state and shared consumers (L24/L11 are shared with other declarations).

### D5 — Cross-check with TWRP/Linux

If TWRP/Linux can read the eMMC CID/CSD, record the exact command and output. If
Linux reads the CID successfully on the same tablet, the card/rail path is
capable under Linux's configuration; focus on host configuration or timing/pad
differences (H2, H6, H7). If Linux also fails, focus on card/electrical
(H2/H3/H4).

### D6 — Only then consider code changes

D1 and D2 are recorded (D2 completed 2026-10-31). The `cmd2window` run (2.7)
completed D3's freshness check: the pre-CMD2 window held the CMD1 OCR, and the
post-CMD2 window held real CID words. D4 and D5 remain. The D7 capture is
implemented and host-tested in the working tree, ran on hardware in the section
2.8 `fmt=2` image, and its expanded CMD7 pre-command/poll/terminal/trace form
was exercised by the 2026-11-01 `cmd1gap` candidate on three units (2.11). Do
not make a code change from the `cmd2window` run alone. Any code change requires
owner approval and must not disable CRC checking.

### D7 — Capture the CMD3 completion state

1. Extend the diagnostics-only capture to snapshot `NORMAL_INT_STATUS`,
   `ERROR_INT_STATUS`, `PRESENT_STATE`, `COMMAND` and `RESPONSE0..3`
   immediately after the CMD3 poll expires, before the interrupt clear.
2. Record the first and last `NORMAL_INT_STATUS` values seen during the poll,
   and the number of iterations completed.
3. If `CC` is present in the snapshot, the polling path missed it; if it is
   absent, compare the CMD3 command encoding and response type with the CMD1
   path and the pinned Linux reference.
4. Do not disable CRC checking or add a quirk without evidence and owner
   approval.

**Status:** implemented as the `fmt=2` capture, exercised by the probe
simulator (`cmd3-no-cc`, `cmd3-late-cc`, `cmd9-timeout`, `cmd3-identify`) and
run on hardware in the section 2.8 image. The success report also carries the
capture, so identification that advances past CMD3 does not lose the evidence.
The hardware run passed CMD2, CMD3 and CMD9 and stopped at CMD7; the D7 capture
shows TC set and CC absent at the CMD7 timeout (2.8). The capture did not record
CMD7 poll history because `c3l` is deliberately CMD3-only, and it did not
record a pre-CMD7 response window. Those gaps were closed by the later
`c7p`/`c7q`/`c7t`/`c7x` capture: the 2026-11-01 `cmd1gap` candidate recorded a
pre-CMD7 sample, the poll OR mask (CC and TC observed), the first CC/TC
iteration and the terminal window on three units (2.11). Simulator agreement
does not prove hardware register behavior. D4 and D5 remain, and any further
CMD7 diagnostic run needs explicit owner approval.

---

## 8. Explicit non-supported statements

The following statements are **not supported** by the current evidence and must
not be presented as established:

1. The older stage-3 transcript does not contain the failing command index,
   error status, or response words. The 2026-10-31 section 2.6 run does
   contain them (`fail_index=2`, `fail_error=0x0002`, `rsp=`), and the
   section 2.7 run contains them for CMD3 (`fail_index=3`,
   `fail_error=0x0000`, `rsp=`).
2. The older `be30e9f4` failure-record image is flashed but not run. The
   2026-10-31 `33311a32` image is the section 2.6 run evidence; the
   `cmd2window` image (`b119f780…`) is the section 2.7 run evidence.
3. The hardware-tested old build `1a471089…` is not current source. The
   2026-10-31 `33311a32` image was built from the source then in the tree and
   failed CMD2; the later `cmd2window` image includes the working tree's
   capture and did not reproduce that failure.
4. `ven=00318a0c` does not prove the current host-init policy was applied. It is
   the pre-POR handoff value plus pad bits.
5. The old build's missing POR/vendor/DLL normalization is not proven to be the
   cause of the old CRC error, and is excluded as a sufficient explanation for
   the 2.6 and 2.7 runs.
6. No rail voltage, pad voltage, pull-up, drive-strength, or signal-integrity
   measurement exists. `pwr=0f`, `clk=0007`, and `cfg=00002017` are register
   states, not measurements.
7. The actual card-clock frequency is not measured. The nominal 400 kHz value is
   derived from the RCG constants and the mainline parent rate.
8. The CID/CSD/EXT_CSD contents are not fully known. The `cmd2window` window
   contains CID words 2..4 and a CRC7 trailer consistent with the known CID,
   but the first CID word is absent and the window is not a validated CID.
   CMD1 success proves the card responded to R3, not that its CID CRC is valid
   in isolation.
9. The controller's internal CRC computation and the card's transmitted CRC byte
   are not observed directly. The section 2.6 raw snapshot is OCR-like; the
   section 2.7 window contains real CID words and a CRC7 trailer, but
   `RESPONSE0` was overwritten by the CMD3 R1 status. Neither window is a
   validated CID.
10. The card is not proven absent, unsupported, faulty, counterfeit, or damaged.
11. The failure is not proven to be electrical, card-side, or controller-side.
    The hypotheses are not conclusions. The section 2.7 result points at the
    CMD3 completion path (H8) but does not establish it.
12. The stage-2 `rc=0` proves controller-local host init, not card communication
    or identification.
13. TWRP/Linux eMMC access is not re-verified in this audit. If used as a
    comparator, record the exact command and output.
14. No evidence supports disabling CRC checking. CRC checking must remain
    enabled.
15. No further partition, flash, or wipe action is authorized by this audit.
    The 2026-10-31 BOOT writes were separately owner-approved and RECOVERY
    remained unchanged; TWRP on `RECOVERY` must remain preserved.
16. The `cmd2window` capture ran on hardware in the section 2.7 image. It
    establishes that the pre-CMD2 window still held the CMD1 OCR and that the
    post-CMD2 window changed to CID words; it does not establish why CMD2
    failed in the section 2.6 image or why CMD3 completion was missed.
17. The CMD2 CRC failure did not reproduce in the `cmd2window` image. This does
    not prove the section 2.6 failure was electrical, timing-related, or a
    one-off; the cause of the difference is not established.
18. The CMD3 R1 value `0x00000500` is plausible but not independently confirmed
    as the CMD3 response; no post-CMD3 status/command capture exists yet.
19. The `read_long_response` reconstruction formula was not applied successfully
    to the section 2.7 window because `RESPONSE0` was overwritten. The window is
    evidence of real CID bits, not a validated CID.
20. No card content was read. The probe remains identification-only.
21. The two `fmt=4` observations are not a same-unit repeatability test. The
    owner reports the first run on an SM-T505 and the second on the owner's
    SM-T505N; only the second has a flash record in this tree. The first run's
    unit attribution is owner-reported, not independently tied.
22. The `free`-counter difference (`…7946f` versus `…7944f`) is not established
    as a run-to-run change; a transcription correction is equally possible. It
    is not eMMC evidence.
23. The owner's assessment that the identical `fmt=4` result on SM-T505 and
    SM-T505N indicates the same eMMC is an inference, not a measured
    part-number, CID, firmware or board-revision identity. Identical CMD2
    behavior on two units does not exclude a shared host-path cause.
24. The pulled-prefix gate rerun verifies the boot-image structure and the
    extracted payload. It does not independently verify the photo, the boot
    event, the recovery session or the unit attribution.

**2026-10-31 fmt=2 update:** the later hardware run passed CMD3 and CMD9 and
stopped at CMD7. Statements above that describe the CMD3 timeout as the latest
result remain historical for the `cmd2window` image. The new report does not
change the non-supported statements about CMD2 or CMD3, and it does not
establish a CMD7 root cause. See section 2.8 and
`docs/emmc-cmd7-timeout-finding.md`.

**2026-11-01 cmd1gap update:** the candidate completed identification on the
owner's SM-T505N and two owner-reported SM-T505 units (section 2.11). The
statements above that describe identification as incomplete remain historical
for the fail-closed image. The candidate result is owner-supplied text without
a photo file or hash in this tree; it establishes a successful identification
run sequence, not the CMD2 or CMD7 root cause, reliability, or a sector read.
The 8-us delay is not proven necessary because earlier builds passed CMD2
without it. CRC checking stays enabled and no snapshot is promoted to a CID.

---

## 9. Documentation corrections

The corrections below were applied in the working tree after the audit. The
current-source hardware runs and the follow-up response-window captures are
reflected in the current text and in sections 2.7 and 10.

| Location | Current text/issue | Correction |
| --- | --- | --- |
| `src/UniXNU-gta4l/docs/emmc-driver.md:456` | Cites `package/Gta4lEmmc.h:208` for `GTA4L_EMMC_ERR_CMD_CRC` | Current line is `Gta4lEmmc.h:242` |
| `src/UniXNU-gta4l/docs/emmc-driver.md:457-461` | Says `ven=00318a0c` repeats the stage-2 host-init state and the policy was applied | The pad bits are set, but `ven` is not the current normalized value (`0x18A9C`); the bullet is stale for current code |
| `src/UniXNU-gta4l/docs/emmc-driver.md:370-376` | Says the failure-record build matches the current source hashes | True at build time; current source has changed. Re-check before citing the flashed image as current |
| `src/UniXNU-gta4l/docs/emmc-driver.md:469-471` | Says the transcript does not establish which command failed | Add: code-path inference narrows it to CMD2 because CMD0 has no response and CMD1 has no CRC; the 2026-10-31 run captures `fail_index=2` and `fail_command=0x0209` |
| `src/UniXNU-gta4l/package/Gta4lEmmc.h:179` | Comment says "BI_TCXO / 24 * 1 / 4" | Corrected to the actual "BI_TCXO * 2 / 24 * 1 / 4" (= /48) in the current tree |
| `src/UniXNU-gta4l/package/Gta4lEmmc.c:195-198` | Comment says the CRC byte is in the low byte of `RESPONSE0` | Corrected: the normal stripped-CRC layout does not expose the CRC byte there |
| `src/UniXNU-gta4l/docs/emmc-driver.md` §5.3 | Says stage 3 stopped at a CMD2 CRC error | Add the `cmd2window` result: CMD2 passed and CMD3 timed out (2.7, §5.3) |

The following documentation statements are correct and should be retained:

- `src/UniXNU-gta4l/docs/emmc-driver.md:395`: the older failure-record image
  was flashed but not booted. The text now distinguishes it from the completed
  2026-10-31 current-source run (2.6, §5.3).
- `src/UniXNU-gta4l/docs/emmc-driver.md:502-507,542`: the recorded stage-3
  transcript predates the failure record and DLL fields.
- `src/UniXNU-gta4l/docs/emmc-driver.md:469-471`: the transcript does not
  establish the failing command by itself.

---

## 10. Post-run response-window capture (working tree)

The section 2.6 run could not distinguish a failed CMD2 response from an
unchanged register window. The working tree adds diagnostics-only capture around
identification; it does not change successful CMD1 semantics, add a second
command attempt, write the response registers or disable CRC checking. The
first capture layer ran on hardware in the section 2.7 `cmd2window` image; its
result is in 2.7. The expanded D7 layer (`fmt=2`) ran on hardware in the
section 2.8 image; its result is in 2.8 and
`docs/emmc-cmd7-timeout-finding.md`.

### 10.1 Capture points and fields

The `cmd2window` layer:

- `cmd1_response[4]` / `cmd1_response_valid`: raw `RESPONSE0..3` read after the
  final accepted ready CMD1 and before the status clear
  (`Gta4lEmmc.c:1067-1075`). `card_ready()` sets the valid flag only for the
  ready response it accepts.
- `pre_cmd2_response[4]` / `pre_cmd2_response_valid`: raw `RESPONSE0..3` read
  after the pre-command interrupt clear and immediately before the `ARGUMENT`
  and `COMMAND` writes, only for CMD2 (`Gta4lEmmc.c:359-365`). This separates a
  W1C side effect from the command issue.
- `fail_response[4]`: the existing failed-command snapshot, still taken before
  the error latch is cleared (`Gta4lEmmc.c:97-116`).

The expanded D7 layer:

- `cmd2_normal` / `cmd2_error` / `cmd2_raw_response[4]` /
  `cmd2_capture_valid`: status and a fresh raw `RESPONSE0..3` window taken
  after CMD2 reports `CC` and before the post-command status clear
  (`Gta4lEmmc.c:479-490`). The existing `read_long_response()` still produces
  the normalized `host.cid[]` separately, so raw evidence and decoded CID can
  be compared.
- `pre_cmd3_normal/error/present/normal_enable/error_enable` /
  `pre_cmd3_capture_valid`: samples taken after the pre-CMD3 W1C clear and
  before the CMD3 `ARGUMENT`/`COMMAND` writes (`Gta4lEmmc.c:372-386`). A stale
  completion or a lost status enable is visible here.
- `poll_first_normal`, `poll_last_normal`, `poll_iterations`, `poll_ticks`,
  `poll_index`, `poll_capture_valid`: CMD3-only poll history
  (`Gta4lEmmc.c:396-447`). `poll_ticks` is the free-running counter delta
  across the poll; the probe reports the counter frequency separately. The
  capture is deliberately CMD3-only so later CMD9/CMD7 polls do not overwrite
  it.
- `timeout_normal/error/present/command/argument/response[4]`,
  `timeout_response_stable` / `timeout_capture_valid`: fresh samples taken
  after the poll expires and before the interrupt clear
  (`Gta4lEmmc.c:450-466`). `fail_normal` is now this fresh read;
  `poll_last_normal` keeps the last in-loop sample. `timeout_command` is the
  SDHCI COMMAND readback, distinct from the GCC `cmd=` field.

`gta4l_emmc_clear_failure()` zeroes every window, validity flag and expanded
sample on controller init and before each CMD1 attempt
(`Gta4lEmmc.c:128-182`), so an unreached checkpoint is explicitly invalid
rather than zero-valued data.

The probe report appends (format identifier `fmt=2`):

    cmd1rsp=<valid>/<w0>,<w1>,<w2>,<w3>
    pre2rsp=<valid>/<w0>,<w1>,<w2>,<w3>
    c2=<valid>/<normal>/<error>/<w0>,<w1>,<w2>,<w3>
    c3p=<valid>/<normal>/<error>/<present>/<normal_en>/<error_en>
    c3t=<valid>/<command>/<argument>/<response-stable>
    c3l=<valid>/<index>/<first-normal>/<last-normal>/<iterations>/<ticks>/<hz>
    cid=<w0>,<w1>,<w2>,<w3> fmt=2 hv=<HOST_VERSION>

`c3l` prints the 64-bit tick count as two adjacent 32-bit halves. `hv` is the
SDHCI HOST_VERSION at offset `0xFE`, kept distinct from `mci=`. These are
sequential register samples, not an atomic snapshot; added reads and timestamps
perturb timing. The stage-3 success report (`GTA4L-EMMC IDENT CAPTURE`) and the
post-read success report (`GTA4L-EMMC PROBE CAPTURE`) print the same `fmt=2`
capture block, including the counter frequency, so a successful identification
does not discard the D7 evidence.

### 10.2 Simulator and test coverage

The core simulator (`scripts/sim-gta4l-emmc/harness.c`) records every
`RESPONSE0..3` read with the last command, offset and error-clear generation
(`:409-419`), and invalidates the response on an error-status W1C clear only
when a CMD_CRC bit was actually pending before that clear (`:943-951`); a
normal-status W1C clear can also drop the window (`:913-928`). This enforces the
snapshot-before-clear ordering. `response_window_at()` /
`response_window_find()` (`:1295-1331`) identify complete four-word windows,
and `cmd_issue_clear[]` (`:182,740`) records the clear generation at each
command issue. The model also clears HOST_CONTROL2 on `RESET_ALL`
(`:479-490`), so a second `controller_init()` raises the IO request again; the
`power-unchanged-1v8-bus-only` scenario (`:1384-1391`) keeps the preserved-bit
quirk behind an explicit flag. The new D7 flags model a normalized CMD2 raw
layout (`:655-666`), a missing/late/stale CMD3 completion (`:300-307,812-816,
915-924`) and a mismatched status-enable readback (`:314-320`).

New scenarios and extended checks (setup `:1501-1524`; capture/window
assertions `:2051-2141,2365-2432`; CMD3 timeout assertions `:2577-2642`):

- `cmd2-normalized`: models the photographed raw layout (normalized CID words,
  CRC trailer in the low byte of `RESPONSE3`). Checks the raw capture, the
  differently decoded `host.cid[]`, the window read order and the
  capture-before-clear generation.
- `cmd3-no-cc`: CMD3 loads its R1 response but never raises `CC`; checks the
  first/last poll samples, exhaustion, fresh timeout samples, command/argument
  readback and response stability.
- `cmd3-late-cc`: `CC` appears only in the fresh post-timeout normal-status
  read; checks that the in-loop samples lack `CC` and that the timeout snapshot
  has it, without changing the returned timeout.
- `cmd3-stale-cc`: a stale `CC` survives the pre-CMD3 clear; checks the
  pre-CMD3 capture and the first poll sample.
- `cmd3-identify`: CMD3 returns the hardware-seen Identify R1 status
  (`0x00000500`); checks pre-CMD3 status enables and the non-timeout capture
  path.
- `cmd3-enable-readback`: the status-enable readback does not match the
  programmed value; checks the pre-CMD3 enable fields while the run still
  succeeds.
- `cmd1-not-ready-then-ready`: two completed not-ready CMD1 replies, then a
  ready reply; three attempts, no timeouts, valid final CMD1 window.
- `cmd1-not-ready-then-ready-crc`: the recorded 2026-10-31 sequence — two
  not-ready CMD1 replies, a ready reply, then CMD2 CRC; checks three attempts,
  no timeouts, a valid final CMD1 window and a failed window distinct from
  pre-CMD2.
- `cmd1-clear-invalidates-window`: a normal-status W1C clears `RESPONSE0..3`;
  the CMD1 snapshot must already hold the OCR while pre-CMD2 reads the cleared
  registers.
- `reinit-clears-windows`: a second `controller_init()` succeeds and clears
  both window validity flags, all eight words and the expanded D7 samples.
- `cmd2-crc-distinct`: distinct final-CMD1 and failed-CMD2 windows; checks all
  four words and that the pre-CMD2 read happens after a later status clear while
  the failed window is read before the post-failure clear.
- `cmd2-crc-unchanged`: the controller keeps the response; pre-CMD2 and failed
  windows are identical.
- `cmd2-crc-ocr-like`: an OCR-like rearranged failed-command window, matching
  the 2026-10-31 observation without treating it as proof of staleness.
- `cmd1-timeout-recovery-crc` now also requires valid windows and a failed
  window different from pre-CMD2.
- `cmd1-timeout-exhausted` and `card-not-ready` require both validity flags to
  stay zero.

The probe simulator (`scripts/sim-gta4l-emmc-probe/harness.c`) now models the
full identification sequence through EXT_CSD: CMD7/CMD13 TRAN polling, the
EXT_CSD read through `BUFFER_DATA_PORT`, block-transfer register writes, a late
CMD3 completion that appears only after the poll loop has expired, a CMD9
timeout, and the CMD7 R1b completion cases: TC only, changed and unchanged
response windows, CC-then-TC, TC-then-CC, simultaneous completion, CC without a
busy completion (a bounded timeout), a busy-phase error, a late busy-phase error
and bounded-trace overflow. Its `cmd2-crc`, `cmd3-no-cc`, `cmd3-late-cc`, `cmd9-timeout`,
`cmd3-identify` and `cmd7-*` scenarios check the actual report text:
command/argument labels, the distinct last-poll and fresh post-timeout samples,
the CMD3-only poll history, the separate CMD7 pre-command/poll/terminal/trace
captures, raw versus decoded CID, the absence of invented captures, and that
identification stops after the first timeout. `cmd3-identify` also checks that
the success report carries the CMD2, pre-CMD3, poll-history, CMD7,
counter-frequency and CID fields.

Host results at this audit snapshot: `python3 scripts/test-gta4l-emmc.py`
passes 78 scenarios and 40 mutation controls; the probe command
`python3 scripts/test-gta4l-emmc-probe.py` passes 22 scenarios and 19 mutation
controls (these are the counts at the audit snapshot; current counts are
maintained in `docs/emmc-driver.md`). New core mutations suppress the failed
snapshot, the final-CMD1 snapshot, the CMD1 valid flag or the pre-CMD2 snapshot,
point the CMD1 capture at the wrong field, retime the pre-CMD2 capture before
the status clear, retime the final-CMD1 capture after it, and drop the
response-window clear on reinit. New probe mutations insert an unguarded SDHCI
read, replace the clock guard with an unconditional branch, suppress a GCC or
SDHCI report read, the raw R2 snapshot, either identification response window,
the captured CMD2 error, the capture-format identifier, the HOST_VERSION sample,
the success-report CID or poll history, or the timeout command index; six CMD7
mutations suppress the pre-command validity, poll OR mask, TC validity, CC
validity, trace overflow count or terminal response window. Each is
rejected for its expected reason. Simulator agreement does not prove hardware
register behavior; the D7 capture was host-only at this audit snapshot. The
later 2026-11-01 `cmd1gap` candidate exercised it on hardware: CMD7 completed
with CC and TC on three units (§2.11).

### 10.3 Interpretation (unchanged policy)

| Observation | Interpretation | Hypothesis impact | Next |
| --- | --- | --- | --- |
| Pre-CMD2 and failed windows identical, OCR-like | The register window did not visibly change across CMD2; a stale/retained window is possible | H5 or layout/response-type behavior; not H2 proof | Compare with the CMD1 window and the reference response layout |
| Pre-CMD2 and failed windows identical, both zero | Window not updated; CMD2 response absent from the register file | H5/H6 candidate | Check command issue/status ordering before blaming the card |
| Pre-CMD2 and failed windows differ; failed window is not CID-shaped | CMD2 updated the window but not with valid CID data | H2 or H4 candidate; still no valid CID | Scope CMD/CLK, then compare the card's CID path |
| Failed window reconstructs the known CID but CRC is reported | Data bits intact; fault is in the CRC byte/check path | H4/H5 candidate | Card/controller CRC-path comparison |
| CMD2 succeeds, CMD3 times out; failed window has CID words 2..4 and a plausible CMD3 R1 status in `RESPONSE0` | CMD2 delivered real CID data; the CMD3 response appears present but `CC` was not observed | H5/H6/H8 candidate; CMD2 electrical/CRC explanations not supported | Capture post-CMD3 status/command and separate the CMD2 window from the CMD3 response (D7) |
| D7 `c3p` shows `CC` already set after the pre-CMD3 clear | A stale completion survived the W1C clear or the clear did not complete | W1C synchronization/stale completion candidate | Compare with the vendor's post-clear behavior; do not add a delay without evidence |
| D7 `c3p` status enables differ from the programmed values | Status-enable configuration was lost | Configuration-loss path candidate | Trace enable writes and any reset/ownership path |
| D7 `c3t`/`c3l` show `CC` only in the fresh post-timeout read | Completion arrived after the last in-loop sample | Late completion, not necessarily a missed poll | Correlate the measured ticks with the timeout; no functional change yet |
| D7 `c2` raw words and `cid=` decoded words disagree | The controller's raw response layout differs from the assumed SDHCI order | Response-layout candidate; no guessed quirk | Compare against the pinned reference and the photographed layout |
| CMD2 succeeds | Failure not reproduced in this run | H1 candidate for the old build; the failure moved to CMD3 (H8) | Continue identification only, not sector I/O |

No CRC checking may be disabled, and a failed R2 snapshot is never promoted to
a CID.

---

## 11. Anchor index

The line anchors and scenario counts in this section describe the `fmt=2`
image source snapshot. The working tree has since added the `fmt=4` CMD7
capture and its scenarios, so re-derive the anchors before reuse; current
working-tree counts are in section 10.

### 11.1 Current source

- `src/UniXNU-gta4l/package/Gta4lEmmc.c`
  - `84-95`: raw response reader
  - `97-116`: failure record (snapshot before clear)
  - `118-143`: failure clear, including both response windows and flags
  - `212-227`: long-response normalization
  - `230-253`: interrupt clear / status enables
  - `276-294`: error mapping
  - `297-386`: command send/poll/response
  - `318-325`: pre-CMD2 window capture after the status clear
  - `377-382`: final-CMD1 window capture before the status clear
  - `535-589`: 400 kHz RCG and clock enable
  - `592-705`: power-on / power acknowledgement
  - `708-719`: pad configuration
  - `726-759`: low-speed vendor/DLL normalization
  - `762-773`: card clock enable
  - `776-921`: controller init
  - `839-842`: POR write
  - `856-889`: HOST_CONTROL2 and pad switch
  - `890`: low-speed normalization call
  - `896-921`: clock, power, pads, card clock
  - `924-958`: CMD1 / card ready
  - `961-985`: response validation
  - `1066-1188`: init sequence
  - `1084-1086`: CMD0
  - `1098-1100`: CMD2
  - `1107-1110`: CID capture
- `src/UniXNU-gta4l/package/Gta4lEmmc.h`
  - `101`: vendor POR `0xA9C`
  - `128-144`: error bits/mask
  - `146-154`: command flags
  - `160-166`: power-control bits
  - `179-184`: 400 kHz constants
  - `187`: SDC1 pad value
  - `207`: CMD2
  - `242`: `GTA4L_EMMC_ERR_CMD_CRC -8`
  - `324-331`: failure response, CMD1/pre-CMD2 windows and validity flags
- `src/UniXNU-gta4l/package/Gta4lEmmcProbe.c`
  - `24-26`: SDHCI/GCC/TLMM bases
  - `345-352`: fixed 1.8 V / pad policy
  - `354-358`: stage dispatch
  - `425-456`: failure panic with `fail=`, `rsp=`, `cmd1rsp=`, `pre2rsp=`,
    `cmd1=`
  - `477`: HOST REPORT
  - `486`: IDENT REPORT

### 11.2 Simulator

- `src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c`
  - `21`: response-read log bound
  - `45`: initial vendor `0x00318A0C`
  - `116-119`: response-clear, preserved-HOST_CONTROL2 and D7 flags
  - `168-182`: capture/scenario state and `cmd_issue_clear[]`
  - `300-320`: late-CC and status-enable-readback injection
  - `409-419`: response-read tracking (command, offset, clear generation)
  - `479-490`: RESET_ALL clears HOST_CONTROL2 unless preserved
  - `593-632`: raw response encoding
  - `636-672`: CMD2 response loader, including the normalized layout
  - `727-742`: command issue / `cmd_clear_count` / `cmd_issue_clear`
  - `812-816`: CMD3 completion suppression for no-CC/late-CC runs
  - `913-928`: normal-status W1C invalidation and stale-CC injection
  - `943-951`: error-status W1C response invalidation after the pending-bit check
  - `1226-1231`: simulator init (MCI/HOST version seeds)
  - `1295-1331`: response-window helpers
  - `1384-1391`: preserved-VDD_180 scenario setup
  - `1400-1524`: CRC, CMD1-not-ready, CMD2 window and D7 scenario setup
  - `1620-1628`: RCG CFG/M/N/D checks
  - `1650-1710`: 1.8 V, pad, DLL, vendor normalization checks
  - `1818-1900`: CMD1/pre-CMD2/reinit response-window assertions
  - `2051-2141`: D7 non-timeout capture assertions
  - `2169-2210`: expected `CMD_CRC`/`CMD_TIMEOUT` scenarios
  - `2227-2290`: CRC failure capture
  - `2295-2432`: CMD1/pre-CMD2/failed-window assertions
  - `2577-2642`: CMD3 no-CC/late-CC timeout assertions
- `src/UniXNU-gta4l/scripts/test-gta4l-emmc.py`
  - scenario list, source policy, and mutation controls cover the same rules.
  - 69 scenarios, 40 mutation controls.
- `src/UniXNU-gta4l/scripts/sim-gta4l-emmc-probe/harness.c`
  - full identification sequence through EXT_CSD, CMD7/CMD13 TRAN polling,
    late-CC and CMD9-timeout injection, and the stage-3 failure/success reports,
    including `cmd1rsp=`/`pre2rsp=` and the `fmt=2` D7 fields; seeds HOST_VERSION
    `0x1234`.
- `src/UniXNU-gta4l/scripts/sim-gta4l-emmc-probe/include/kern/debug.h`
  - format-checked `panic` prototype so the report's specifiers and arguments
    are verified at compile time.
- `src/UniXNU-gta4l/scripts/test-gta4l-emmc-probe.py`
  - 13 scenarios, 13 mutation controls.

### 11.3 Linux/Samsung references

- `/tmp/opencode/linux-pin/sdhci.h:68-71`: response-type constants
- `/tmp/opencode/linux-pin/sdhci.c:1711-1712`: `RESP_LONG` for `MMC_RSP_136`
- `/tmp/opencode/linux-pin/sdhci.c:1718-1719`: `CMD_CRC`
- `/tmp/opencode/linux-pin/sdhci.c:1798-1816`: `sdhci_read_rsp_136()`
- `/tmp/opencode/linux-pin/sdhci.c:3330-3336`: CRC → `-EILSEQ`
- `/tmp/opencode/linux-pin/sdhci-msm.c:76`: `CORE_VENDOR_SPEC_POR_VAL 0xa9c`
- `/tmp/opencode/linux-pin/sdhci-msm.c:203-232`: v5 offset table, `core_ddr_config = 0x25c`
- `/tmp/opencode/linux-pin/sdhci-msm.c:803-840`: `msm_hc_select_default()`
- `/tmp/opencode/linux-pin/sdhci-msm.c:1047-1058`: DDR config and `CMDIN_RCLK_EN` under HS400
- `/tmp/opencode/linux-pin/sdhci-msm.c:1397-1420`: <=100 MHz DLL reset/power-down
- `/tmp/opencode/linux-pin/sdhci-msm.c:1718-1755`: power handler
- `/tmp/opencode/linux-pin/sdhci-msm.c:2323-2325`: HOST_CONTROL2 → REQ_IO
- `/tmp/opencode/linux-pin/sdhci-msm.c:2412-2417`: pad switch
- `/tmp/opencode/linux-pin/sdhci-msm.c:2452-2480`: voltage switch
- `/tmp/opencode/linux-pin/sdhci-msm.c:2596-2598`: `DDR_CONFIG_POR_VAL`
- `/tmp/opencode/linux-pin/sdhci-msm.c:2827-2828`: `updated_ddr_cfg` for minor `>= 0x49`
- `/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci.c:1442-1460`: R2 shift
- `/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci.c:1172-1175`: multi-block transfer-mode encoding (`BLK_CNT_EN | MULTI`)
- `/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci-msm.c:101`: Samsung POR `0xA1C`
- `/tmp/opencode/lfg4l-audit/files/LinuxForGta4l_linux__c91d0e94385f75a8788cfc9d8a4f532b3994e6da__include_linux_mmc_mmc.h:43`: CMD12 is R1b
- `/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_core_core.c:1457-1460`: CQE recovery stop is R1B
- `/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_core_mmc_ops.c:702-709`: tuning-abort CMD12 is R1 with an R1b-override note
- `/tmp/opencode/lfg4l-audit/cbd-drivers_mmc_host_sdhci.c:1351-1354`: `STOP_WITH_TC` adds `MMC_RSP_BUSY`
- `/tmp/opencode/linux-verify/gcc-sm6115.c:1248-1258`: SDCC1 apps table
- `/tmp/opencode/linux-verify/gcc-sm6115.c:1261`: `cmd_rcgr = 0x38028`

### 11.4 Hardware evidence and project docs

- `out/gta4l/ram-root-emmc-probe-evidence/host-report-stage2.txt:1-3`: stage-2 PASS
- `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`: stage-3 FAIL
- `out/gta4l/ram-root-emmc-probe-failrec-evidence/flash-record.json:1-26`: older failure-record flash, not booted
- `out/gta4l/ram-root-emmc-probe-cmd2crc-build2-evidence/flash-record.json`: current-source BOOT flash and run (2.6)
- `out/gta4l/ram-root-emmc-probe-cmd2crc-build2-evidence/stage3-failure-report.txt`: 2026-10-31 stage-3 report
- `out/gta4l/ram-root-emmc-probe-cmd2crc-build2-evidence/stage3-cmd2-crc-failure-photo-2026-10-31.jpg`: photo SHA-256 `39f03d3f…`
- `out/gta4l/ram-root-emmc-probe-cmd2window-evidence/flash-record.json`: `cmd2window` BOOT flash and run (2.7)
- `out/gta4l/ram-root-emmc-probe-cmd2window-evidence/stage3-cmd2window-report.txt`: 2026-10-31 CMD3-timeout report transcribed from the photo
- `out/gta4l/ram-root-emmc-probe-cmd2window-evidence/stage3-cmd2window-photo-2026-10-31.jpg`: photo SHA-256 `a9656fc9…`
- `out/gta4l/ram-root-emmc-probe-failclosed-evidence/flash-record.json`: `fmt=4` fail-closed BOOT flash (2.9)
- `out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/flash-record.json`: SM-T505N fail-closed rerun and matched baseline (2.9-2.10)
- `out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/stage3-failure-report.txt`: transcribed CMD2 failure
- `out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/flash-record.json`: candidate BOOT flash, unchanged recovery and pulled readback (2.11)
- `out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/build-provenance.json`: candidate source/archive/kernel/payload/image identity (2.11)
- `src/UniXNU-gta4l/docs/emmc-cmd1-gap-candidate.md`: candidate definition, build and three-unit result
- `src/UniXNU-gta4l/docs/emmc-driver.md:328,360`: stage-2/3 kernel hash
- `src/UniXNU-gta4l/docs/emmc-driver.md:383`: older failure-record kernel hash
- `src/UniXNU-gta4l/docs/emmc-driver.md:395`: older failure-record image not booted; current-source run complete (2.6)
- `src/UniXNU-gta4l/docs/emmc-driver.md:450-471`: stage-3 interpretation
- `src/UniXNU-gta4l/docs/emmc-driver.md:502-507,542`: transcript predates failure record
- `src/UniXNU-gta4l/docs/sdhci-handoff-audit.md:130-158`: handoff snapshot
- `src/UniXNU-gta4l/docs/sdhc-electrical-contract.md:48-52`: L24/L11/pad contract
