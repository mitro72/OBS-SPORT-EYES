# OBS Sport Eyes v1.10.0k – Async telemetry and Director state logging

This patch makes the CSV diagnostics sufficient to validate the latest-frame async
pipeline in a real OBS session. It does not change the operator GUI: enable **Abilita
log CSV** as before. Existing CSV files are overwritten when a new logging session starts.

## Diagnostics CSV additions

The unified diagnostics CSV now appends these fields:

- `async_enabled`: async pipeline active for this filter.
- `result_fresh`: a newly-completed inference was consumed on this video tick.
- `result_age_ms`: age from source-frame capture to crop decision.
- `result_completion_age_ms`: age from inference completion to crop decision.
- `inference_ms`: duration of the inference that produced the applied result.
- `async_worker_busy` / `async_task_pending`: worker and one-slot pending queue state.
- `async_replaced_count`: number of pending frames replaced by newer frames.
- `async_result_overwritten_count`: completed results superseded before OBS consumed them.
- `async_submitted_count` / `async_completed_count`: worker throughput counters.
- `async_pending_sequence`, `result_sequence`, `applied_sequence`: monotonic frame IDs.
- `director_measurement_state`: `fresh_detection`, `fallback_detection`,
  `prediction_coast`, `safe_roi_hold`, or `fallback_center`.
- `director_measurement_age_ms`: age of the measurement behind the Director decision.

## Director AI CSV additions

The Director CSV now appends measurement state, age, freshness, result sequence,
inference duration, worker state, and pending-frame replacement count.

A Safe ROI hold is no longer injected into Director AI as a fresh observation. This
prevents a held box from artificially preserving confidence or flattening velocity.
The crop can still use the last Director prediction while the log explicitly says
`safe_roi_hold` or `prediction_coast`.

## Expected healthy async signature

At a 50 ms inference interval, a healthy run normally has increasing submitted and
completed counters, mostly fresh results within a few frame periods, low
`result_completion_age_ms`, and a replacement count that can increase during bursts
without causing `result_age_ms` to grow continuously.
