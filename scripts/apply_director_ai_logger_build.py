#!/usr/bin/env python3
"""Add DirectorAILogger.cpp to CMakeLists.txt in a guarded, idempotent way."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "CMakeLists.txt"
LINE = "    src/director_ai/DirectorAILogger.cpp"
MARKER = "    src/director_ai/DirectorAIIntegration.cpp"


def main() -> int:
    if not TARGET.exists():
        print(f"missing file: {TARGET}", file=sys.stderr)
        return 1

    text = TARGET.read_text(encoding="utf-8")
    if LINE in text:
        print("DirectorAILogger.cpp already present")
        return 0

    if MARKER not in text:
        print("CMake marker not found", file=sys.stderr)
        return 2

    text = text.replace(MARKER, MARKER + "\n" + LINE, 1)
    TARGET.write_text(text, encoding="utf-8")
    print("Added DirectorAILogger.cpp to CMakeLists.txt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
