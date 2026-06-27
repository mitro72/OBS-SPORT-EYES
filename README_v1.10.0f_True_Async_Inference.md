# OBS Sport Eyes v1.10.0f – True Async Inference

## What changed

- Adds a dedicated inference worker thread. With **Async Inference** enabled, `video_tick` never calls `model->inference()` directly.
- Keeps at most one pending frame. If the worker is busy, a pending frame is replaced by the newest one instead of building a queue.
- Preserves source-frame timestamp and ROI origin with the result.
- Sends only fresh detections to SORT and Director AI; cached detections remain available for crop stability but are not reused as new motion measurements.
- Adds real pipeline age to Director AI prediction horizon, so configured prediction is relative to display time rather than an old captured frame.
- Adds an **Async Inference** checkbox in Tracking settings. It defaults to enabled. Disable it only for A/B comparison with the legacy blocking path.

## Recommended test settings

- Async Inference: ON
- Infer interval: 50 ms
- Infer scale: 1.00
- Director AI: ON

## Expected behaviour

- Tracking starts after the first completed inference (normally one inference latency after scene activation).
- Under overload the tracking may sample fewer frames, but it should follow recent action rather than slowly replay old frames.
- Use the existing CSV diagnostics to compare reaction and crop quality.
