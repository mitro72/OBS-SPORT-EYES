# v1.10.0g – Tracking helper startup deduplication

Fixes duplicate `Detect Tracking` and `Detect Tracking Scale` filters created while OBS restores a saved scene collection.

- update/activate now bind existing helper filters but do not create them immediately;
- `video_tick` waits 15 ticks before creation, allowing OBS to restore serialized helpers;
- exact-name duplicate helpers are detected and removed deterministically;
- OBS source references are released correctly when replaced, removed, or destroyed.
