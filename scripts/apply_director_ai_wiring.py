#!/usr/bin/env python3
"""Apply the guarded Director AI wiring patch to src/detect-filter.cpp.

The patch is intentionally conservative:
- it creates a .bak backup;
- it fails if expected markers are not found;
- it is idempotent for the include and targetCenterX hook;
- it keeps legacy crop as fallback.
"""

from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "src" / "detect-filter.cpp"
BACKUP = ROOT / "src" / "detect-filter.cpp.bak-director-ai"

INCLUDE_MARKER = '#include "yunet/YuNetOpenVINO.h"'
INCLUDE_LINE = '#include "director_ai/DirectorAIIntegration.h"'

LEGACY_CENTER = 'const float targetCenterX = boundingBox.x + (boundingBox.width * 0.5f);'
DIRECTOR_CENTER = '''float targetCenterX = boundingBox.x + (boundingBox.width * 0.5f);

	if (tf->directorAIEnabled && tf->zoomObject == "group" && rect_valid(boundingBox)) {
		if (!tf->directorAIEngine)
			tf->directorAIEngine = std::make_unique<director_ai::DirectorAIEngine>();

		director_ai::IntegrationInput directorInput;
		directorInput.timestamp_ns = os_gettime_ns();
		directorInput.action_bbox = boundingBox;
		directorInput.action_valid = true;
		directorInput.confidence = finalGroupClusterValid ? 1.0f : 0.65f;
		directorInput.frame_width = width;
		directorInput.frame_height = height;
		directorInput.base_coverage = baseCoverage;
		directorInput.deadband_px = (tf->x_deadband / 100.0f) * (float)width;

		const auto directorOut = director_ai::update_director_ai(
			*tf->directorAIEngine,
			tf->directorAIConfig,
			directorInput);

		if (directorOut.valid) {
			tf->lastDirectorAIFrame = directorOut.frame;
			targetCenterX = directorOut.frame.prediction.predicted_center.x;
		}
	}'''


def main() -> int:
    if not TARGET.exists():
        print(f"missing file: {TARGET}", file=sys.stderr)
        return 1

    text = TARGET.read_text(encoding="utf-8")
    original = text

    if INCLUDE_LINE not in text:
        if INCLUDE_MARKER not in text:
            print("include marker not found", file=sys.stderr)
            return 2
        text = text.replace(INCLUDE_MARKER, INCLUDE_MARKER + "\n" + INCLUDE_LINE, 1)

    if "director_ai::IntegrationInput directorInput;" not in text:
        if LEGACY_CENTER not in text:
            print("legacy targetCenterX marker not found", file=sys.stderr)
            return 3
        text = text.replace(LEGACY_CENTER, DIRECTOR_CENTER, 1)

    if text == original:
        print("no changes needed")
        return 0

    if not BACKUP.exists():
        BACKUP.write_text(original, encoding="utf-8")

    TARGET.write_text(text, encoding="utf-8")
    print(f"patched {TARGET}")
    print(f"backup: {BACKUP}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
