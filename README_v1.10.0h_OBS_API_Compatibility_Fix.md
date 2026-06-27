# v1.10.0h — OBS callback API compatibility fix

Fixes compilation against recent OBS API headers:

- `obs_source_enum_filters` callback uses `bool (*)(void *param, obs_source_t *source)`.
- Uses `obs_source_get_ref()` instead of deprecated `obs_source_addref()`.

This is a source-only compatibility correction for the tracking helper deduplication introduced in v1.10.0g.
