#!/usr/bin/env python3
import pathlib
import sys

def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    board_toml = repo_root / "board.toml"
    defconfig = repo_root / "configs" / "gta4l_defconfig"
    missing = [str(p) for p in (board_toml, defconfig) if not p.exists()]
    if missing:
        print(f"[check-gta4l] Missing: {', '.join(missing)}", file=sys.stderr)
        return 1
    print("[check-gta4l] SM6115 (gta4l) board layout verified.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
