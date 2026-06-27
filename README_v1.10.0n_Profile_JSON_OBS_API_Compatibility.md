# OBS Sport Eyes v1.10.0n — Profile JSON OBS API Compatibility

## Fix

Replaces the unavailable `obs_data_get_json_with_defaults()` call in the profile snapshot path.

The replacement creates an effective OBS settings object by obtaining defaults with
`obs_data_get_defaults()`, applying the current source settings with `obs_data_apply()`,
and serializing that effective object through `obs_data_get_json()`.

## Scope

Modified file:

- `src/config/SportEyesProfileManager.cpp`

Import/apply behavior and JSON schema are unchanged.
