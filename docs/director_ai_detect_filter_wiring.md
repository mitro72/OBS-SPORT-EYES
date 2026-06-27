# Director AI wiring plan for `Sport Eyes filter facade / pipeline modules`

This document describes the safe integration step for enabling Director AI in `group` mode.

The current PR intentionally keeps legacy crop behavior unchanged. The next code change should be a small patch in `src/Sport Eyes filter facade / pipeline modules` only.

## 1. Include the safe adapter

Add near the existing includes:

```cpp
#include "director_ai/DirectorAIIntegration.h"
```

## 2. Initialize the engine lazily

Inside `detect_filter_video_tick`, before using Director AI output:

```cpp
if (!tf->directorAIEngine)
    tf->directorAIEngine = std::make_unique<director_ai::DirectorAIEngine>();
```

## 3. Keep the default disabled

`FilterData.h` already defines:

```cpp
bool directorAIEnabled = false;
```

Do not enable this by default. First tests should set it internally or by a hidden/advanced setting only.

## 4. Safe call site

The best insertion point is after `boundingBox` has been selected by the existing group/safe-ROI logic and before the pan-only crop uses `targetCenterX`.

Current legacy logic uses:

```cpp
const float targetCenterX = boundingBox.x + (boundingBox.width * 0.5f);
```

Replace it with an overridable center:

```cpp
float targetCenterX = boundingBox.x + (boundingBox.width * 0.5f);

if (tf->directorAIEnabled && tf->zoomObject == "group" && rect_valid(boundingBox)) {
    if (!tf->directorAIEngine)
        tf->directorAIEngine = std::make_unique<director_ai::DirectorAIEngine>();

    director_ai::IntegrationInput input;
    input.timestamp_ns = os_gettime_ns();
    input.action_bbox = boundingBox;
    input.action_valid = true;
    input.confidence = finalGroupClusterValid ? 1.0f : 0.65f;
    input.frame_width = width;
    input.frame_height = height;
    input.base_coverage = baseCoverage;
    input.deadband_px = (tf->x_deadband / 100.0f) * (float)width;

    const auto directorOut = director_ai::update_director_ai(
        *tf->directorAIEngine,
        tf->directorAIConfig,
        input);

    if (directorOut.valid) {
        tf->lastDirectorAIFrame = directorOut.frame;
        targetCenterX = directorOut.frame.prediction.predicted_center.x;
    }
}
```

## 5. Fallback behavior

If Director AI is disabled or invalid, `targetCenterX` remains the legacy cluster center.

This preserves old behavior.

## 6. Preview overlay suggestion

After the safe-ROI/cluster overlay, draw:

- current cluster bbox in green;
- Director AI predicted bbox in cyan;
- Director camera plan in yellow.

Example:

```cpp
if (tf->preview && tf->directorAIEnabled && tf->lastDirectorAIFrame.valid) {
    const auto &pred = tf->lastDirectorAIFrame.prediction.predicted_bbox;
    const auto &plan = tf->lastDirectorAIFrame.camera.crop_rect;
    drawDashedRectangle(frame, cv::Rect((int)pred.x, (int)pred.y, (int)pred.width, (int)pred.height), cv::Scalar(255, 255, 0), 2, 6, 10);
    drawDashedRectangle(frame, cv::Rect((int)plan.x, (int)plan.y, (int)plan.width, (int)plan.height), cv::Scalar(0, 255, 255), 3, 6, 10);
}
```

## 7. Recommended rollout

1. Compile with `directorAIEnabled = false`.
2. Enable only for local debug.
3. Verify overlay only.
4. Compare legacy center vs predicted center in diagnostics.
5. Enable predicted center for group auto mode.

