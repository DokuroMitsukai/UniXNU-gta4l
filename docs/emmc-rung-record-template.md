# gta4l eMMC rung record template

Copy this file to `docs/emmc-<id>-<name>-<date>.md` and fill every section.
Keep it factual: measured values and committed paths only. Never include a
device serial, unique ID, account, host name, SSH target, IP address, e-mail
address or absolute home path. Raw transcripts and logs stay in the
gitignored `out/` tree; the record carries normalized lines, hashes and
counts.

## Header

- Rung:
- Date:
- Risk class: R0 / R1 / R2
- Selector token:
- Parent commit:
- Status: design / host-only / flashed / verdict

## 1. Question and scope

One paragraph: what single question this rung answers, and what it does not
qualify.

## 2. Design

- Source seams: file:line of every changed entry point.
- Card and host sequence: command order, timing and width changes.
- Report fields: exact label and field list.
- Success predicate: every term, with the host source of each expected value.
- Failure classes: one line per class and the next action.
- Timing budget: every watchdog term in milliseconds; total must stay below
  the 30,000 ms watchdog.
- Host mutation list: each scenario and mutation control.
- Revert: the verified path back to 1-bit / 400 kHz, and what failure leaves
  the driver degraded or offline.

## 3. Host validation

| Gate | Result |
| --- | --- |
| board userdata GPT check | |
| eMMC core test | |
| eMMC probe test | |
| block driver test | |
| payload and package tests | |
| evidence-count tests | |
| TOML parse and `git diff --check` | |
| privacy grep | |

## 4. Candidate

- Source commit:
- Image path (gitignored):
- Image bytes:
- Image SHA-256:
- Package gate result: (18/18, `only_ramdisk_differs`)
- Bound report digest source:

## 5. Run

- Boots attempted:
- Pre-identification stops:
- Owner report (normalized, hash):
- Post-boot protected-partition comparison:
- Raw evidence path (gitignored):

## 6. Verdict

- Result: PASS / FAIL / NO-VERDICT
- Qualified claim: exactly what this run proves.
- Unqualified: what remains `[U]`.
- Failure class, if any:

## 7. Follow-up

- Next rung and why.
- Source or documentation updates owed.
- Baseline hashes to refresh.
