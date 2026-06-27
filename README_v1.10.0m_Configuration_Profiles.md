# OBS Sport Eyes v1.10.0m — Configuration Profiles

This patch adds configuration profile management and portable JSON import/export.

## Included

- Three built-in Basket 180 profiles: Balanced, Reactive and Conservative.
- Local profile library stored under the OBS Sport Eyes module configuration directory.
- Save/update, apply and delete operations from the filter properties.
- Export of the current supported configuration to a versioned JSON document.
- Import and validation of portable JSON configuration files.
- Curated import rules: only supported settings are applied; runtime state, OBS
  scenes and helper filters are excluded.

## Build

Replace the supplied source files in a v1.10.0l tree and build normally:

```powershell
$env:OpenVINO_DIR = "C:\Program Files\Intel\openvino_2024\runtime\cmake"
pwsh .\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

See `docs/README_v1.10.0m_Configuration_Profiles.md` for the operator workflow
and JSON format.
