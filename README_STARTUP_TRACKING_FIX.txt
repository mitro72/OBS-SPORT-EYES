OBS SPORT EYES v1.10.0e — Tracking startup lifecycle fix

Problem fixed
-------------
When OBS restored a saved scene, tracking_group could already be enabled while
obs_filter_get_parent() still returned null during create/update. The old code
recorded the checkbox state as enabled and returned, so it never created the
"Detect Tracking" Crop/Pad filter and "Detect Tracking Scale" filter unless
the user toggled Tracking manually.

What changed
------------
1. trackingSetupPending records a requested-but-not-yet-attached state.
2. Tracking setup is idempotent and always synchronised from update.
3. It retries from activate, when OBS normally has attached the parent source.
4. A final safe retry runs from video_tick only while the two child filters are
   still missing.

Files to overwrite
------------------
src/FilterData.h
src/sport-eyes-filter-internal.h
src/filter/SportEyesFilterLifecycle.cpp
src/pipeline/SportEyesVideoTick.cpp

Build
-----
Close OBS, extract this archive over the existing source folder, then run:

  pwsh .\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo

Validation after installation
-----------------------------
1. In OBS leave Tracking enabled.
2. Close OBS completely and reopen the same scene collection.
3. Verify that the source has filters named "Detect Tracking" and
   "Detect Tracking Scale" without toggling the checkbox.
4. In obs-studio\logs, look for:
   "OBS Sport Eyes: tracking filters ready"

If the parent is not ready during initial restore, a debug line says that setup
was deferred; activation/video tick should then create the filters automatically.
