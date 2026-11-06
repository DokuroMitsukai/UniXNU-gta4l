# gta4l D12.6 panic-format candidate observation: clean CMD3 control with rendered kind column

**Status:** focused finding, 2026-11-06. Host-only closeout of one
owner-supplied hardware report; this batch performed no device access, boot,
flash, candidate build, driver/transport change, commit or push. It follows
`docs/emmc-d12.5-closeout-2026-11-06.md` and
`docs/emmc-panic-format-fix-2026-11-06.md` and does not rewrite either record
or any earlier verdict.

## 1. Observation and provenance

The owner supplied one H9T report after the D12.6 candidate was flashed, then
confirmed:

- the boot was a **cold** start (power off then on); no stronger physical
  rail-reset claim is made;
- there were **no intervening boot attempts** between the flash and this
  report;
- the tablet was returned to **TWRP**.

No exact boot timestamp was recorded and none is invented. No original
photograph bytes were supplied in this turn, so no photo hash is claimed. The
report is ordinal 1 of the supplied observation and no further attempt is
authorized by this closeout.

The candidate is the existing D12.6 evidence set under the ignored
`out/gta4l/ram-root-emmc-d12.6-panic-format-evidence/`:

- BOOT image `boot-ram-root-emmc-d12.6-probe9.img`, 85,688,320 bytes, SHA-256
  `7c76c7ebd3a14c343bbf0556f4dd1aa943ee2ee10f80a274a3962d54d2b2d44e`,
  18/18 boot gate.
- Payload `e384cc0618af5422a01bc0ea23f5b4c1e443cda1051a08f8c4236745f5bf96d0`,
  83,886,080 bytes; prelinked kernel `b1871d40`, packaged kernel `12385bc7`.
- Diagnostic `gta4l-emmc-probe=9` with kernel arguments `rd=md0 sheng-pmgr=2`;
  identification only, one CMD3 attempt, no retry, then the intentional report
  panic.
- Checkout anchors: device HEAD `1ee1f4f9` and shared HEAD `0a1cc714`. The
  successful build used an rsync-staged device tree outside the checkout
  (excluding `.git`, `out`, `build`, `__pycache__` and `*.pyc`) and a Python
  3.11 shim. Earlier failed attempts (a relative `--unixnu` path and a stale
  broken symlink in the build tree) remain in the evidence logs.
- Flashed to BOOT/p69 only. The post-flash image-length prefix matches the
  candidate hash; the full p69 hash is `b19c59b8`, the recorded protected
  p70/p64/p63 hashes match the pre-flash values, and the post-flash tail hash
  is `a28b4ea2`. The evidence set has no pre-flash tail hash. The previous p69
  is saved as `boot-before-d12.6.img.xz`.

The report itself does not embed the BOOT hash. Association with this candidate
uses the conversational sequence and the existing flash record; it is not an
independent cryptographic measurement of the running image.

## 2. Report decode

The header is:

```
GTA4L-EMMC H9T REPORT ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0 step=13 log=4 hz=19200000
```

CMD3 was submitted once (`sub=1`, `att=1`) with retry disabled and not engaged
(`retry=0/0`). Transport and the R1 error flag are zero. The poll history is
valid: 25 iterations, zero-based terminal iteration 24, first NORMAL `0000`,
last NORMAL `0001`. The reserved terminal slot 19 is kind SUCCESS (`C`), NORMAL
`0001` (command complete), ERROR `0000`, RESPONSE0 `00000500`, valid 1 and ticks
`0x14f2`. No timeout capture exists (`C3T v=0`).

The pre-command capture is valid (`v=1`) with NORMAL `0000`, ERROR `0000`,
PRESENT `03f800f0`, normal-enable `0033`, error-enable `07ff` and response
valid 1. Its four response words can contain CID fragments and are omitted from
this public record; they remain in the private transcript.

`TRC n=6 ov=0` reports six scheduled entries at iterations 0, 1, 2, 4, 8 and
16. All six sampled the same private RESPONSE0 word with valid 1. Slots 6-18
are unused (`?`, valid 0); slot 19 is reserved for the terminal entry. The
`FAIL` line is `fail=0/0/0/00000000/0000/0000/00000000/0`: the panic is the
intentional report stop, not a recorded hardware failure. Its source suffix
points at `Gta4lEmmcProbe.c:1579`.

| Slot | Kind | Iteration | NORMAL / ERROR | Response / valid | Counter ticks |
| --- | --- | --- | --- | --- | --- |
| T00 | `S` | 0 | `0000 / 0000` | private word, valid 1 | `0000000000000030` |
| T01 | `S` | 1 | `0000 / 0000` | private word, valid 1 | `0000000000000118` |
| T02 | `S` | 2 | `0000 / 0000` | private word, valid 1 | `0000000000000200` |
| T03 | `S` | 4 | `0000 / 0000` | private word, valid 1 | `00000000000003c1` |
| T04 | `S` | 8 | `0000 / 0000` | private word, valid 1 | `0000000000000737` |
| T05 | `S` | 16 | `0000 / 0000` | private word, valid 1 | `0000000000000e13` |
| T06-T18 | `?` | placeholder | `0000 / 0000` | zero, valid 0 | zero |
| T19 | `C` | 24 | `0001 / 0000` | `00000500`, valid 1 | `00000000000014f2` |

The thirteen unused scheduled slots are expected after early successful
completion; they are not missing observations or failed commands. The terminal
`C` is the successful terminal kind, not the twentieth scheduled sample.

## 3. Kind-column result

The supplied transcript renders the kind column on every trace line: scheduled
`S`, unused `?` and terminal success `C`. This is the expected output of the
guarded formatter in `scripts/xnu-panic-screen.c` (SHA-256
`8b71ed5a1fadbf3bea5839ce4b614f5b6e0c998e37131e60c1d37b3f2686ba95`) and
matches the host regression in `scripts/test-gta4l-panic-format.py`. It
contrasts with the D12.5 report, where the same line format rendered `T19  7`
without a kind.

This accepts the formatter correction for this report. It does not qualify the
framebuffer glyph path generally, other conversions, every future candidate or
a photographic record, because no original photo bytes were supplied in this
turn.

## 4. Interpretation limits

The six scheduled samples are the retained long-response window's first word
sampled while polling before completion; they are not CMD3 responses. The
terminal sample is a later sequential register read. The
terminal-versus-last-scheduled comparison bounds a register change to an
interval; it does not prove a fresh card response, an exact arrival time or a
card-state transition. The driver applies its R1 error mask to the terminal
RESPONSE0 and reported no masked error (`r1=0`); it assigns the RCA in the CMD3
argument and does not parse an RCA from the response.

Counter values are preserved as observed. The earlier conversational estimate
of about 279 µs was only arithmetic on terminal `0x14f2` at 19.2 MHz; it is
not a measured command/card latency or response-arrival time.
Poll ticks `0x14e4` and terminal ticks `0x14f2` are distinct capture fields
with different read points and must not be promoted into a hardware-event
timing claim.

Disposition: clean instrumented identification / CMD3 non-reproduction.
Initialization reliability remains unresolved. One boot does not qualify a
failure rate, repeatability or cold-start reliability. No EXT_CSD, CMD17/CMD18
sector comparison, block publication, IOMedia read, mount or root switch is
established by this identification-only report. F2 remains measured PASS for
the one D10.5 cold boot and p5b F5 is unchanged.

## 5. Evidence and validation

Private raw evidence is preserved under the ignored
`out/gta4l/ram-root-emmc-d12.6-panic-format-evidence/observation-1/`:

- `owner-transcript-raw.txt` — exact owner text, 1840 bytes, SHA-256
  `d0ad264574be70aa30ffcacc24c50aad0ffd6b29cd89e025806a93af75ce1660`;
- `observation-record.json` — structured decode and owner-confirmed facts;
- `interpretation.md` — host-side decode and limits.

A supplemental SHA-256 manifest at the evidence-directory root covers the
observation files and this document; the existing build/flash evidence and
manifests are not rewritten.

Host validation was run from `src/UniXNU-gta4l/` with `LC_ALL=C` and
`/usr/bin/python3.11 -B`:

| Check | Result |
| --- | --- |
| `tomllib` parse of `porting.toml`, `board.toml`, `feeds.toml` | exited 0; 13 porting areas |
| `../UniXNU/scripts/porting-record.py check --device .` | exited 0: 13 structured areas checked |
| `scripts/check-gta4l.py` | exited 0: `SM6115 (gta4l) board layout verified.` |
| `scripts/test-gta4l-emmc-evidence-counts.py` | exited 0: documented counts match sources (core 187/115, probe 196/100) |
| `git diff --check` in the device repository | exited 0 |
| Privacy grep over the pending tracked diff and the new document | no unexplained hits |

This is a docs/porting-record-only change; no fixture, test or source changed.
The earlier 24-script battery results (23 passed, with the pre-existing
`scripts/test-gta4l-storage-abi.py` artifact-staleness failure) remain
historical and are not re-claimed as a current full-suite acceptance.

## 6. Limits and next decisions

Additional observation requires fresh explicit approval and an explicit
remaining budget; do not boot repeatedly until a failure occurs. D13 requires
its own reliability disposition and implementation decision. No commit or push
was made in this batch.
