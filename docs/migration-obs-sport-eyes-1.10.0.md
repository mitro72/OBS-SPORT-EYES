# Migration to OBS Sport Eyes 1.10.0

## Windows installation

1. Close OBS completely.
2. Replace the previous `obs-detect.dll` with `obs-sport-eyes.dll` in `obs-plugins/64bit`.
3. Copy the accompanying `data/obs-plugins/obs-sport-eyes/` folder.
4. Do not leave the old `obs-detect.dll` installed: both modules would register the legacy source ID and OBS could load the wrong implementation.

## Scene collection compatibility

The new module registers two filter source IDs:

- `obs-sport-eyes`: used by newly added Sport Eyes filters.
- `detect-filter`: legacy compatibility ID for existing scene collections.

Existing filters therefore retain their saved settings. After confirming a scene works, a user may remove and re-add its filter to make the source ID explicitly `obs-sport-eyes`; this is optional and should only be done after a backup of the scene collection.

## First-run verification

Open OBS and confirm the log contains `OBS Sport Eyes loaded successfully (version 1.10.0)`. Then verify that the existing filter has its prior model, preset and crop settings.
