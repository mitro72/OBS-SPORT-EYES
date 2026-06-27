# v1.10.0l — Async cached-detection correctness fix

## Why this patch exists

The v1.10.0k CSV comparison isolated a logic error in the asynchronous path:

- Async ON: objects were visible in ~51% of logged samples.
- Async OFF: objects were visible in ~99.75% of logged samples.

The detector itself was not the issue. With Async ON, a completed model result was copied into
`cached_objects` before shared post-processing ran. `Object::unseenFrames` is managed by SORT,
but raw detector objects did not have a defined value for that field. On the next OBS tick, the
cached result was reused and the normal `Hide unseen objects` filter removed those objects.

## Changes

1. `Object` now initializes scalar fields, including `unseenFrames = 0`.
2. `cached_objects` is now updated only after:
   - min-area filtering;
   - object-category filtering;
   - SORT update, when SORT is enabled.
3. Cached results still never re-enter SORT on a non-fresh tick, so the fix does not create
   artificial zero-motion observations.

## Expected result

With Async Inference ON and Hide unseen objects enabled, cached frames between completed
inferences should retain the same visible objects instead of alternating with zero-object samples.

## Validation test

Use the same setup as the comparison test:

- Async Inference: ON
- Infer interval: 50 ms
- Infer scale: 1.00
- Director AI: ON
- CSV logging: ON

The key diagnostics should be:

- `objects_total > 0` in nearly all normal game samples;
- `safe_roi_hold` only during real detector misses;
- `async_submitted_count` and `async_completed_count` continue to advance normally;
- no growing `result_age_ms`.
