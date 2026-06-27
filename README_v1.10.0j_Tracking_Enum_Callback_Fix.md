# v1.10.0j — OBS tracking enumeration callback fix

Fixes `obs_source_enum_filters` callback to match the OBS API used by this build:

```cpp
typedef void (*obs_source_enum_proc_t)(
    obs_source_t *parent,
    obs_source_t *child,
    void *param);
```

The previous patch incorrectly used a `bool` return type. This update changes the callback to `void collect_tracking_filter(obs_source_t*, obs_source_t*, void*)`.
