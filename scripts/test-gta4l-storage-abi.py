#!/usr/bin/env python3
"""Positive and negative checks for the gta4l storage-ABI artifacts.

The test is host-only and does not run QEMU.  It requires the artifacts from
``scripts/audit-gta4l-storage-interface.py``; when they are absent it reports
SKIP instead of pretending to qualify anything.

It checks the audit itself against mutated snapshots and the harness's
``--skip-qemu`` path, including the preserved ``run.json`` provenance.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIXNU = ROOT.parent / "UniXNU"
PROFILE = "ipados-17.7.11-21H461"
KERNEL = UNIXNU / "out" / "gta4l" / PROFILE / "gta4l" / "kernel" / "kernel.macho"
PLAN = KERNEL.with_suffix(".json")
TREE = UNIXNU / "out" / "gta4l" / PROFILE / "gta4l" / "tree"
FEED = TREE / "refer" / "IOStorageFamily"
AUDIT = UNIXNU / "scripts" / "audit-storage-interface.py"
HARNESS = ROOT / "scripts" / "audit-gta4l-storage-interface.py"
METACLASS = TREE / "build" / "xnu" / "ios-obj" / "DEVELOPMENT_ARM64_BCM2837" / \
    "libkern" / "DEVELOPMENT" / "OSMetaClass.cpo"
ARTIFACTS = ROOT / "out" / "gta4l" / "storage-abi"
DERIVE = ARTIFACTS / "audit" / "derive.json"
SNAPSHOT = ARTIFACTS / "gdb-stop.json"
PROVENANCE = ARTIFACTS / "provenance.json"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(value, message):
    if not value:
        raise SystemExit(message)


def run_audit(snapshot: Path, candidate: Path, plan: Path, derive: Path,
              expect_pc: str, output: Path, work: Path):
    work.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable, "-B", str(AUDIT), "--derive", str(derive),
        "--candidate", str(candidate), "--plan", str(plan),
        "--snapshot", str(snapshot), "--expect-pc", expect_pc,
        "--metaclass-object", str(METACLASS), "--output", str(output),
        # The cached-derive path re-reads the reference object from the
        # qualification work directory; keep that directory real.
        "--work-dir", str(ARTIFACTS / "audit"), "--source", str(FEED),
        "--dwarfdump", "llvm-dwarfdump",
    ]
    return subprocess.run(command, text=True, capture_output=True)


def main() -> int:
    inputs = (KERNEL, PLAN, FEED, AUDIT, HARNESS, METACLASS, DERIVE,
              SNAPSHOT, PROVENANCE)
    missing = [path for path in inputs if not path.exists()]
    if missing:
        print("SKIP: storage-ABI artifacts are not present; run "
              "scripts/audit-gta4l-storage-interface.py first")
        return 0
    require(FEED.is_dir() and (FEED / ".git").exists(),
            f"public IOStorageFamily feed is not a git checkout: {FEED}")

    provenance = json.loads(PROVENANCE.read_text())
    expect_pc = provenance["bsd_init"]
    derive = json.loads(DERIVE.read_text())
    meta_key = f"{int(derive['metaclass'], 16):x}:50"
    service_key = f"{int(derive['service_metaclass'], 16):x}:50"
    base = json.loads(SNAPSHOT.read_text())
    require(base.get("pc") == expect_pc, "base snapshot is not at the recorded PC")
    require(meta_key in base.get("memory", {}) and
            service_key in base.get("memory", {}),
            "base snapshot is missing the live metaclass memory")

    with tempfile.TemporaryDirectory(prefix="storage-abi-test-", dir="/tmp/opencode") as raw:
        work = Path(raw)
        positive = work / "positive.json"
        result = run_audit(SNAPSHOT, KERNEL, PLAN, DERIVE, expect_pc, positive, work)
        require(result.returncode == 0,
                "positive audit failed:\n" + (result.stdout + result.stderr)[-2000:])
        audit = json.loads(positive.read_text())
        require(audit["candidate_sha256"] == sha256(KERNEL),
                "positive audit candidate mismatch")
        require(audit["method_slots"] == 190 and audit["kernel_bindings"] == 176,
                "positive audit dispatch shape changed")
        require(audit["live_native_4k"] is True, "positive audit lost native 4K")
        require(audit["live_instance_sizes"] ==
                {"IOBlockStorageDevice": 0x90, "IOService": 0x88},
                "positive audit instance sizes changed")

        cases = {}

        def mutate(name, function):
            data = json.loads(json.dumps(base))
            function(data)
            path = work / f"{name}.json"
            path.write_text(json.dumps(data, indent=2) + "\n")
            cases[name] = path

        mutate("interrupted",
               lambda data: data.__setitem__("bounded_timeout_interrupted", True))
        mutate("wrong-pc",
               lambda data: data.__setitem__("pc", "0xfffffff000000000"))
        mutate("missing-meta",
               lambda data: data["memory"].pop(meta_key))

        def class_size(data):
            raw = bytearray.fromhex(data["memory"][meta_key])
            raw[32] = 0x91
            data["memory"][meta_key] = raw.hex()

        mutate("class-size", class_size)

        def missing_sctlr(data):
            data["system_registers"].pop("SCTLR_EL1", None)
            data["system_registers"].pop("SCTLR", None)

        mutate("missing-sctlr", missing_sctlr)
        mutate("tcr",
               lambda data: data["system_registers"].__setitem__(
                   "TCR_EL1", "0x22a51a651a"))
        mutate("missing-register",
               lambda data: data["system_registers"].pop("TCR_EL1"))
        mutate("missing-system-registers",
               lambda data: data.pop("system_registers"))

        def el0_alias(data):
            sctlr = data["system_registers"].pop("SCTLR_EL1")
            data["system_registers"]["SCTLR"] = sctlr
            data["pstate"] = "0x60000200"

        mutate("el0-alias", el0_alias)

        def missing_pstate(data):
            sctlr = data["system_registers"].pop("SCTLR_EL1")
            data["system_registers"]["SCTLR"] = sctlr
            data.pop("pstate", None)

        mutate("missing-pstate", missing_pstate)

        expected = {
            "interrupted": "Snapshot was interrupted by the bounded timeout",
            "wrong-pc": "Snapshot did not stop at the expected PC",
            "missing-meta": "Live storage metadata missing",
            "class-size": "Live block-storage class/parent ABI mismatch",
            "missing-sctlr": "Live translation is not enabled",
            "tcr": "Live translation is not native 4K",
            "missing-register": "Live register missing: TCR_EL1",
            "missing-system-registers": "Snapshot has no system registers",
            "el0-alias": "QEMU SCTLR alias requires an EL1 stop",
            "missing-pstate": "QEMU SCTLR alias requires the current PSTATE",
        }
        for name, path in cases.items():
            output = work / f"{name}-audit.json"
            result = run_audit(path, KERNEL, PLAN, DERIVE, expect_pc, output, work)
            text = result.stdout + result.stderr
            require(result.returncode != 0, f"{name}: audit unexpectedly passed")
            require(expected[name] in text,
                    f"{name}: expected error not reported:\n{text[-1000:]}")

        bad_kernel = work / "kernel-bad.macho"
        raw = bytearray(KERNEL.read_bytes())
        raw[0x1000] ^= 0xff
        bad_kernel.write_bytes(raw)
        result = run_audit(SNAPSHOT, bad_kernel, PLAN, DERIVE, expect_pc,
                           work / "wrong-candidate.json", work)
        require(result.returncode != 0, "wrong candidate unexpectedly passed")
        require("Unexpected original candidate provenance" in
                result.stdout + result.stderr,
                "wrong candidate did not report provenance")

        def tampered_derive(name, function):
            data = json.loads(json.dumps(derive))
            function(data)
            path = work / f"derive-{name}.json"
            path.write_text(json.dumps(data, indent=2) + "\n")
            result = run_audit(SNAPSHOT, KERNEL, PLAN, path, expect_pc,
                               work / f"derive-{name}-audit.json", work)
            require(result.returncode != 0,
                    f"tampered derive {name} unexpectedly passed")
            return result.stdout + result.stderr

        text = tampered_derive(
            "metaclass", lambda data: data.__setitem__(
                "metaclass", "0xfffffff000000000"))
        require("Derived metaclass does not match the candidate getter" in text,
                "tampered metaclass did not report the getter mismatch")

        def method_slot(data):
            data["methods"][0]["candidate_target"] = "0xfffffff000000000"

        text = tampered_derive("method-slot", method_slot)
        require("Derived method slot 0 does not match the candidate" in text,
                "tampered method slot did not report the candidate mismatch")

        text = tampered_derive(
            "destructor", lambda data: data.__setitem__(
                "original_base_destructor", "0xfffffff000000000"))
        require("Derived base destructor does not match the candidate dispatch" in text,
                "tampered destructor did not report the dispatch mismatch")

        text = tampered_derive(
            "service", lambda data: data.__setitem__(
                "service_metaclass", "0xfffffff000000000"))
        require("Derived IOService metaclass does not match the candidate" in text,
                "tampered IOService metaclass did not report the candidate mismatch")

        text = tampered_derive(
            "vtable", lambda data: data.__setitem__(
                "vtable", "0xfffffff000000000"))
        require("Derived vtable is outside the storage __DATA_CONST extent" in text,
                "tampered vtable did not report the extent mismatch")

        def method_name(data):
            data["methods"][0]["reference_name"] = "__ZN20IOBlockStorageDeviceD2Ev"

        text = tampered_derive("method-name", method_name)
        require("Derived method names do not match the reference object" in text,
                "tampered method name did not report the reference mismatch")

        def kernel_names(data):
            data["methods"][0]["kernel_names"] = ["__ZN9IOServiceD2Ev"]

        text = tampered_derive("kernel-names", kernel_names)
        require("Derived kernel names at slot 0 do not match the candidate" in text,
                "tampered kernel names did not report the candidate mismatch")

        text = tampered_derive("missing-key", lambda data: data.pop("metaclass"))
        require("derive.json is missing metaclass" in text,
                "missing derive key did not report the incomplete record")

        # The harness --skip-qemu path must preserve the original run record
        # and refuse a snapshot or argument set that does not match it.
        copied = work / "artifacts"
        shutil.copytree(ARTIFACTS, copied)
        result = subprocess.run(
            [sys.executable, "-B", str(HARNESS), "--skip-qemu", "--out", str(copied)],
            text=True, capture_output=True)
        require(result.returncode == 0,
                "harness --skip-qemu failed:\n" + (result.stdout + result.stderr)[-2000:])
        copied_provenance = json.loads((copied / "provenance.json").read_text())
        copied_run = json.loads((copied / "run.json").read_text())
        require(copied_provenance["firmware_sha256"] == copied_run["firmware_sha256"] and
                copied_provenance["qemu_version"] == copied_run["qemu_version"] and
                copied_provenance["kernel_args"] == copied_run["kernel_args"],
                "harness --skip-qemu did not preserve the run provenance")

        copied_snapshot = copied / "gdb-stop.json"
        changed = json.loads(copied_snapshot.read_text())
        changed["pc"] = "0xfffffff000000000"
        copied_snapshot.write_text(json.dumps(changed, indent=2) + "\n")
        result = subprocess.run(
            [sys.executable, "-B", str(HARNESS), "--skip-qemu", "--out", str(copied)],
            text=True, capture_output=True)
        require(result.returncode != 0, "mutated snapshot unexpectedly passed")
        require("run.json snapshot mismatch" in result.stdout + result.stderr,
                "mutated snapshot did not fail the run-record check")

        shutil.copyfile(SNAPSHOT, copied_snapshot)
        copied_run["kernel_args"] += " extra=1"
        (copied / "run.json").write_text(json.dumps(copied_run, indent=2) + "\n")
        result = subprocess.run(
            [sys.executable, "-B", str(HARNESS), "--skip-qemu", "--out", str(copied)],
            text=True, capture_output=True)
        require(result.returncode != 0, "changed kernel arguments unexpectedly passed")
        require("kernel arguments differ from the recorded run" in
                result.stdout + result.stderr,
                "changed kernel arguments did not fail the run-record check")

    print("PASS: storage-ABI audit positive and negative checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
