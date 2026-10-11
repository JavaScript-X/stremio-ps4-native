#!/usr/bin/env python3
"""Fail when PS5-only code leaks into sources selected for the PS4 build.

This is intentionally dependency-free so it can run in CI and before the
OpenOrbis toolchain is installed. It is a guardrail, not a console build test.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMON_DIRS = ("gl", "src", "ui")
SKIP_PATHS = {
    "gl/ime.cpp",       # PS5 IME backend; PS4 gets its own implementation.
    "gl/main.cpp",      # PS5 executable entry point.
    "src/hwdec_ps5.cpp",
    "src/hwdec_ps5.h",
}
FORBIDDEN = (
    re.compile(r"\bPLATFORM_PS5_NATIVE\b"),
    re.compile(r"\bsceVideodec2"),
    re.compile(r"platform/ps5/"),
    re.compile(r"\bPPSA\d{5}\b"),
    re.compile(r"\.ffpfsc\b"),
)


def guarded(text: str, offset: int) -> bool:
    """Accept PS5 references inside a nearby explicit preprocessor guard."""
    prefix = text[max(0, offset - 500) : text.find("\n", offset) + 1]
    return "PLATFORM_PS5" in prefix and "#if" in prefix


def main() -> int:
    failures: list[str] = []
    for dirname in COMMON_DIRS:
        for path in (ROOT / dirname).rglob("*"):
            relative = path.relative_to(ROOT).as_posix()
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"} or relative in SKIP_PATHS:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for pattern in FORBIDDEN:
                for match in pattern.finditer(text):
                    if guarded(text, match.start()):
                        continue
                    line = text.count("\n", 0, match.start()) + 1
                    failures.append(f"{path.relative_to(ROOT)}:{line}: {match.group(0)}")
    if failures:
        print("PS4 portability audit failed:")
        print("\n".join(f"  {item}" for item in failures))
        return 1
    print("PS4 portability audit passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
