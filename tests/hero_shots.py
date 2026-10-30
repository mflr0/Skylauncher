#!/usr/bin/env python3
"""Capture the four game-page states without installing or starting a game."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, help="Read-only disc image for the existing art reader")
    parser.add_argument("--language", choices=("en", "fr"), default="en")
    parser.add_argument("--size", default="1600x1000")
    parser.add_argument("--pad", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    scratch = root / "build" / "scratch" / "hero"
    shots = root / "build" / "shots" / "hero"
    for folder in (scratch / "home", scratch / "collection", scratch / "data", shots):
        folder.mkdir(parents=True, exist_ok=True)
    names = [f"hero-{state}-{art}.png" for state in ("ready", "needs-disc", "installing", "running")
             for art in ("art", "noart")]
    for name in names:
        (shots / name).unlink(missing_ok=True)
    env = os.environ.copy()
    # Keep every launcher-owned folder inside this checkout, including the macOS home override.
    env.update(SDL_VIDEO_DRIVER="offscreen", HOME=str(scratch / "home"),
               CFFIXED_USER_HOME=str(scratch / "home"), SKY_LAUNCHER_INI=str(scratch / "home" / "launcher.ini"),
               SKY_COLLECTION=str(scratch / "collection"), SKYLANDERS_DATA=str(scratch / "data"),
               SKY_LAUNCHER_DATA=str(scratch / "launcher-data"), SKY_SAVE_DIR="", SKY_SETTINGS="",
               SKY_LAUNCHER_NO_UPDATE_CHECK="1", SKY_LAUNCHER_FONT_DIR=str(root / "resources" / "fonts"),
               SKY_LAUNCHER_SHOTS=str(shots), SKY_LAUNCHER_TEST_HERO="1",
               SKY_LAUNCHER_TEST_SIZE=args.size, SKY_LAUNCHER_LANGUAGE=args.language)
    if args.disc:
        env["SKY_LAUNCHER_TEST_DROP"] = str(args.disc.resolve(strict=True))
    else:
        env.pop("SKY_LAUNCHER_TEST_DROP", None)
    if args.pad:
        env["SKY_LAUNCHER_TEST_INPUT"] = "pad"
    else:
        env.pop("SKY_LAUNCHER_TEST_INPUT", None)
    binary = root / "build" / "ArkeyanLauncher.app" / "Contents" / "MacOS" / "ArkeyanLauncher"
    if not binary.exists():
        binary = root / "build" / "ArkeyanLauncher"
    log = scratch / "screenshots.log"
    with log.open("w") as output:
        process = subprocess.Popen([str(binary)], cwd=root, env=env, stdout=output, stderr=output)
        try:
            result = process.wait(timeout=150)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            print("FAIL: launcher screenshot timer expired", file=sys.stderr)
            return 1
    text = log.read_text()
    print(text, end="")
    missing = [name for name in names if not (shots / name).is_file()]
    if missing:
        print("FAIL: missing screenshots: " + ", ".join(missing), file=sys.stderr)
    # Missing art is a failure, even when the fallback layout could still be captured.
    return int(result != 0 or bool(missing) or "FAIL hero" in text
               or text.count("0 unreachable") != 8 or text.count("hero background visible:") != 4)


if __name__ == "__main__":
    sys.exit(main())
