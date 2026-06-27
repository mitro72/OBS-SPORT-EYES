# v1.10.0m — Configuration profiles and JSON portability

OBS Sport Eyes now includes a profile manager in the filter properties. It is
intended for switching quickly between proven basketball configurations without
editing dozens of sliders at every venue.

## Operator workflow

The **Configuration Profiles** group appears at the top of the filter
properties.

- **Profile** lists `Custom / current settings`, three built-in Basket 180
  presets, and every saved JSON profile in the local profile library.
- **Profile name** is used by **Save / update profile**. Saving the same name
  replaces that library profile.
- **Apply selected profile** changes the active filter configuration.
- **Delete selected saved profile** deletes only library profiles. Built-in
  presets and the current configuration cannot be deleted.
- **Export JSON file** plus **Export current configuration** writes a portable
  `.json` file anywhere selected by the operator.
- **Import JSON file** plus **Import and apply JSON** validates and applies a
  portable profile. It does not automatically add the file to the local
  library: use **Save / update profile** afterwards when it should appear in
  the Profile list.

Saved profiles are stored in the OBS Sport Eyes module configuration directory,
inside a `profiles` folder. They are independent of an OBS scene collection.

## Built-in Basket 180 profiles

| Profile | Intended use | Characteristics |
|---|---|---|
| Basket 180 - Balanced | Normal starting point | 50 ms async inference, 300 ms Safe ROI hold, 150 ms cluster inertia, moderate Director prediction |
| Basket 180 - Reactive | Fast transitions and counterattacks | Shorter hold/inertia, faster zoom and Director response |
| Basket 180 - Conservative | Unstable detections or crowded scenes | Longer hold/inertia and smoother, less aggressive Director response |

Built-in profiles adjust sports-framing controls only. They deliberately preserve
the current detector model, inference device, external model path and local CSV
paths.

## JSON format

Exported profiles use a versioned wrapper. Example:

```json
{
  "format": "obs-sport-eyes-profile",
  "schema_version": 1,
  "profile_name": "Basket 180 - My Gym",
  "settings": {
    "tracking_group": true,
    "zoom_object": "group",
    "async_inference_enabled": true,
    "infer_interval_ms": 50,
    "safe_roi_hold_ms": 300,
    "director_ai_enabled": true
  }
}
```

Only supported configuration fields are imported. Unknown fields are ignored,
and runtime state such as detected-object labels, filter helper objects and CSV
content is never imported.

## What is included

Saved and exported profiles include detector, tracking, crop, Safe ROI, cluster,
Director AI, CSV logging and advanced controls. This includes paths for external
models and CSV outputs, so after import on a different PC verify those paths
before enabling an external model or logging.

Applying a profile deliberately goes through normal OBS filter update logic.
That resets cached detections, async inference state and Director AI history,
which prevents values from a previous profile affecting the next one.
