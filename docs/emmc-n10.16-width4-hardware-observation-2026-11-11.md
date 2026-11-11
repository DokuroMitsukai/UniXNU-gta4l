# gta4l N10.16 width4 hardware observation (2026-11-11)

Status: one owner-controlled cold boot of the gate-fix candidate. Bounded
**PASS** for the 4-bit SDR read and comparison, the verified return to 1-bit
and the read-only boot continuation. The pre-flash BOOT image was restored and
verified afterwards. This is not a reliability, repeatability or publication
result.

## 1. Identity and preflight

| Item | Value |
| --- | --- |
| Candidate record | `docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md` |
| BOOT image | `58352cd37f8643ba89c07c4911914fd50b4728c0333602aa4ca7840633c8aba7` (85,688,320 bytes) |
| Payload | `10ff7ee86d82c7d7838b4d37c8ed426f2b56b9c23b926a51e0f0102bfbade79f` |
| Prelinked kernel | `2be2eb0605b25758c57dcc3a80e631a76225dc975ffc4ac15aa8320e71cd7005` |
| Final-image first-1,024-byte prefix | `a6da82e356863d1ab7e31c21a8fd85fc` |

The device is a gta4l / SM-T505N in TWRP 3.7.1 (`p69` = BOOT, p70 = RECOVERY,
p64 = EFS, p63 = sec_efs; 100,663,296 / 103,546,880 / 16,777,216 /
16,777,216 bytes). The pre-flash p69 full image is
`6fa2b7e9890c0e5971e8d0d5e90cf8d96e595b6e2e92cc748f34ec4556269671`
(prefix `97479ae8…`, tail `d737414c…`). The pre-flash RECOVERY, EFS and
sec_efs hashes are `14e11370…`, `51fa824c…` and `3a265d58…`.

## 2. BOOT-only write and readback

Only p69 was written, with the candidate image through the established
4,096-byte record path and `sync`. The post-flash p69 full hash was
`cb1576264de256e1328d2a2828396ebf5da10186d70d5200db149796c4fed60e`; the
85,688,320-byte candidate prefix matched `58352cd3…`, the first 1,024 bytes
matched `a6da82e356863d1ab7e31c21a8fd85fc…`, and the p69 bytes beyond the
image were unchanged (`a28b4ea2…`, equal to the pre-flash tail beyond the
candidate length). RECOVERY, EFS and sec_efs matched their pre-flash hashes.

## 3. One cold boot

The boot budget was one attempt; the owner confirmed a single cold boot and a
return to TWRP. The report below is an **owner transcription** from the device,
not a machine-captured log or screenshot. Its raw form, including the original
line wrapping, is retained privately; the normalized content is:

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=2 blocks=4 fails=0 max=2
GTA4L-EMMC P5 XFER: req=4 c17=0 c18=2 c18b=4 cfail=0 stops=2 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=0 base=0 size=0 content=other writable=0
GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 complete=1 switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1 revert=1 rverified=1 rhb=2 rha=0 rcmd6=1 reads=2 bytes=2048 blocks=4 subrequests=2 max=2 match=1 mismatches=0 fails=0 cancel=0 budget=0 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 WIDTHREAD: index=0 lba=2255360 start=0 length=1024 span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2 digest=a6da82e356863d1ab7e31c21a8fd85fc predigest=a6da82e356863d1ab7e31c21a8fd85fc match=1
GTA4L-EMMC P5 PART: count=0 expected=0 waited_ms=0 error=0 reported=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

The terminal report stop is the intentional panic report site
(`@Gta41EmmcBlock.cpp:6429` in the transcript). No `WIDTHFAIL` line and no
watchdog report appear.

The driver's own completion guard computed `complete=1`
(`package/Gta4lEmmcBlock.cpp:6052-6091`). That guard requires the 4-bit switch
proof, both 1,024-byte reads (2,048 bytes, four blocks, two subrequests,
maximum two blocks per request), a full match, no failure/cancel/budget
condition, a verified revert, `limit=64`, `seg=1` and the measured capacity.
The report satisfies every condition.

The WIDTHREAD content clause was checked host-side:

```
python3 -B scripts/check-gta4l-width4-evidence.py \
  --image <candidate.img> --widthread <normalized-WIDTHREAD-line>
PASS: WIDTHREAD digest and predigest match a6da82e356863d1ab7e31c21a8fd85fc
```

Both digest fields are the 16-byte/32-hex-character SHA-256 prefix of the
pinned 1,024-byte window, not the literal first 16 data bytes.

## 4. Protocol deviations

The written protocol and the readiness sample carried two simulated values
that do not hold on hardware. Neither is a run failure or a completion
condition.

- `rb=0`, not `rb=1`. `rb` is the diagnostic EXT_CSD byte-183 readback. Byte
  183 is W/E_P and not readable; the switch records it only as a diagnostic
  (`package/Gta4lEmmc.c:2243-2267`) and the completion guard does not test it.
  The host adapter model fills `1`, which produced the sample value.
- `rcmd6=1`, not `rcmd6=2`. The revert succeeded on its first CMD6 attempt.
  The source accepts one or two attempts (`package/Gta4lEmmcBlock.cpp:6082`);
  the failure of the first attempt is what produces `2`.

MEDIA `whole=0` and PART `count=0` are not failure flags and do not establish
media or partition publication. The success path keeps `online=1` and the
failure latch clear.

## 5. Bounded verdict

Measured on one cold boot, read-only, at 400 kHz SDR with two-block chunks:

- the verified 4-bit `BUS_WIDTH` switch (`verified=1`, host `HOST_CONTROL`
  1-bit → 4-bit, one CMD6);
- two 1,024-byte reads of the pinned boot window (one 1-bit baseline, one
  4-bit) with byte-exact comparison and the expected window digest;
- the verified return to 1-bit (`rverified=1`, host 4-bit → 1-bit, one CMD6);
- no width failure, no watchdog report, no first-failure latch, media online.

Not established: cold-boot identification reliability, repeatability or a
failure rate; arbitrary or 64-block hardware traffic; write support; higher
clock, HS, DDR, 8-bit; DMA/CQE/ICE; mount or root switching; publication of
the media at 4-bit. F5 remains measured at 1-bit and F3 at 4-bit for one boot
each; initialization reliability remains `[U]`.

## 6. Restore and evidence

The exact pre-flash p69 image was written back with the same record path,
`sync`, and a full readback: p69 =
`6fa2b7e9890c0e5971e8d0d5e90cf8d96e595b6e2e92cc748f34ec4556269671`, prefix
`97479ae8…`, tail `d737414c…`. RECOVERY, EFS and sec_efs remained
`14e11370…`, `51fa824c…` and `3a265d58…`. No reboot, retry, repair or extra
boot was performed during the restore.

Private evidence is under
`out/gta4l/ram-root-emmc-n10.16-width4-gatefix-hardware-evidence/`
(preflight, post-flash and restore records, the owner transcript, the
normalized WIDTHREAD line, the host verifier log and the retained pre-flash
BOOT image). Public records use hashes and relative paths only.
