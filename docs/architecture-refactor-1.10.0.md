# OBS Sport Eyes 1.10.0 — Refactor stage 1

This release converts `src/detect-filter.cpp` into an OBS callback facade and moves behavior-preserving code into focused components:

- `src/filter/SportEyesFilterProperties.cpp`: OBS properties and model-label UI
- `src/filter/SportEyesFilterLifecycle.cpp`: defaults, settings update, model lifecycle and OBS resource lifecycle
- `src/sport/GroupClustering.cpp`: group clustering and debug overlays
- `src/pipeline/SportEyesVideoTick.cpp`: existing tick pipeline (kept behavior-compatible)
- `src/render/SportEyesFilterRender.cpp`: OBS render path
- `src/sport-eyes-filter-internal.h`: temporary internal runtime contract

The public callbacks stay available under the legacy `detect_filter_*` symbols. Source IDs are dual registered: `obs-sport-eyes` for new scenes and `detect-filter` for migration compatibility.

Next stage: split `SportEyesVideoTick.cpp` into capture/preprocess, detection pipeline, sport decision and crop command modules, then replace cached synchronous inference with a true latest-frame async worker.
