#!/usr/bin/env python3
"""Expose Director AI alpha controls in the OBS filter GUI.

The patch is guarded and idempotent. It adds advanced/group-only controls for:
- Enable Director AI
- Prediction horizon
- Velocity EMA alpha
- Max prediction lead
- Min confidence to apply
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "src" / "detect-filter.cpp"
BACKUP = ROOT / "src" / "detect-filter.cpp.bak-director-ai-gui"

PROP_LIST_MARKER = '"group_min_people", "group_min_people_strict", "group_edge_zoom_enabled", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth", "safe_roi_left", "safe_roi_right", "safe_roi_top", "safe_roi_bottom", "safe_roi_hold_ms", "cluster_inertia_ms"}) {'
PROP_LIST_REPLACEMENT = '"group_min_people", "group_min_people_strict", "group_edge_zoom_enabled", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth", "safe_roi_left", "safe_roi_right", "safe_roi_top", "safe_roi_bottom", "safe_roi_hold_ms", "cluster_inertia_ms", "director_ai_enabled", "director_prediction_horizon_ms", "director_velocity_ema_alpha", "director_max_prediction_lead_px", "director_min_confidence_to_apply"}) {'

VISIBILITY_BLOCK_MARKER = '''\t\tobs_property_t *pgc = obs_properties_get(props_, "preview_group_clusters");
\t\tif (pgc)
\t\t\tobs_property_set_visible(pgc, enabled && is_group);
'''
VISIBILITY_BLOCK_REPLACEMENT = '''\t\tconst bool advanced_on = obs_data_get_bool(settings, "advanced");
\t\tconst bool director_on = obs_data_get_bool(settings, "director_ai_enabled");
\t\tobs_property_t *director_enable = obs_properties_get(props_, "director_ai_enabled");
\t\tif (director_enable)
\t\t\tobs_property_set_visible(director_enable, enabled && is_group && advanced_on);
\t\tfor (const char *director_prop_name : {"director_prediction_horizon_ms", "director_velocity_ema_alpha", "director_max_prediction_lead_px", "director_min_confidence_to_apply"}) {
\t\t\tobs_property_t *dp = obs_properties_get(props_, director_prop_name);
\t\t\tif (dp)
\t\t\t\tobs_property_set_visible(dp, enabled && is_group && advanced_on && director_on);
\t\t}

\t\tobs_property_t *pgc = obs_properties_get(props_, "preview_group_clusters");
\t\tif (pgc)
\t\t\tobs_property_set_visible(pgc, enabled && is_group);
'''

ZOOM_CALLBACK_MARKER = '''\t\tobs_property_t *pgc = obs_properties_get(props_, "preview_group_clusters");
\t\tif (pgc)
\t\t\tobs_property_set_visible(pgc, enabled && is_group);
'''
ZOOM_CALLBACK_REPLACEMENT = VISIBILITY_BLOCK_REPLACEMENT

DIRECTOR_PROPS_MARKER = '''\tobs_properties_add_int_slider(tracking_group_props, "cluster_inertia_ms", "Cluster inertia (ms)", 0, 2000, 25);
'''
DIRECTOR_PROPS_INSERT = '''\tobs_properties_add_int_slider(tracking_group_props, "cluster_inertia_ms", "Cluster inertia (ms)", 0, 2000, 25);

\tobs_property_t *director_ai_enabled =
\t\tobs_properties_add_bool(tracking_group_props, "director_ai_enabled", "Enable Director AI (alpha)");
\tobs_properties_add_int_slider(tracking_group_props, "director_prediction_horizon_ms",
\t\t\t\t      "Director prediction horizon (ms)", 50, 500, 10);
\tobs_properties_add_float_slider(tracking_group_props, "director_velocity_ema_alpha",
\t\t\t\t       "Director velocity EMA alpha", 0.05, 1.00, 0.05);
\tobs_properties_add_int_slider(tracking_group_props, "director_max_prediction_lead_px",
\t\t\t\t      "Director max lead (px)", 0, 1200, 10);
\tobs_properties_add_float_slider(tracking_group_props, "director_min_confidence_to_apply",
\t\t\t\t       "Director min confidence", 0.00, 1.00, 0.01);
\tobs_property_set_modified_callback(director_ai_enabled, [](obs_properties_t *props_, obs_property_t *, obs_data_t *settings) {
\t\tconst bool enabled = obs_data_get_bool(settings, "tracking_group");
\t\tconst bool advanced_on = obs_data_get_bool(settings, "advanced");
\t\tconst char *zo = obs_data_get_string(settings, "zoom_object");
\t\tconst bool is_group = (zo && strcmp(zo, "group") == 0);
\t\tconst bool director_on = obs_data_get_bool(settings, "director_ai_enabled");
\t\tfor (const char *director_prop_name : {"director_prediction_horizon_ms", "director_velocity_ema_alpha", "director_max_prediction_lead_px", "director_min_confidence_to_apply"}) {
\t\t\tobs_property_t *dp = obs_properties_get(props_, director_prop_name);
\t\t\tif (dp)
\t\t\t\tobs_property_set_visible(dp, enabled && is_group && advanced_on && director_on);
\t\t}
\t\treturn true;
\t});
\tobs_property_set_visible(director_ai_enabled, false);
\tfor (const char *director_prop_name : {"director_prediction_horizon_ms", "director_velocity_ema_alpha", "director_max_prediction_lead_px", "director_min_confidence_to_apply"}) {
\t\tobs_property_t *dp = obs_properties_get(tracking_group_props, director_prop_name);
\t\tif (dp)
\t\t\tobs_property_set_visible(dp, false);
\t}
'''

DEFAULTS_MARKER = '''\tobs_data_set_default_int(settings, "cluster_inertia_ms", 150);
'''
DEFAULTS_INSERT = '''\tobs_data_set_default_int(settings, "cluster_inertia_ms", 150);
\tobs_data_set_default_bool(settings, "director_ai_enabled", false);
\tobs_data_set_default_int(settings, "director_prediction_horizon_ms", 200);
\tobs_data_set_default_double(settings, "director_velocity_ema_alpha", 0.25);
\tobs_data_set_default_int(settings, "director_max_prediction_lead_px", 300);
\tobs_data_set_default_double(settings, "director_min_confidence_to_apply", 0.05);
'''

UPDATE_MARKER = '''\ttf->cluster_inertia_ms = std::clamp(tf->cluster_inertia_ms, 0, 2000);
'''
UPDATE_INSERT = '''\ttf->cluster_inertia_ms = std::clamp(tf->cluster_inertia_ms, 0, 2000);

\ttf->directorAIEnabled = obs_data_get_bool(settings, "director_ai_enabled");
\ttf->directorAIConfig.prediction_horizon_ms = (float)obs_data_get_int(settings, "director_prediction_horizon_ms");
\ttf->directorAIConfig.velocity_ema_alpha = (float)obs_data_get_double(settings, "director_velocity_ema_alpha");
\ttf->directorAIConfig.max_prediction_lead_px = (float)obs_data_get_int(settings, "director_max_prediction_lead_px");
\ttf->directorAIConfig.min_confidence_to_apply = (float)obs_data_get_double(settings, "director_min_confidence_to_apply");
\ttf->directorAIConfig.prediction_horizon_ms = std::clamp(tf->directorAIConfig.prediction_horizon_ms, 50.0f, 500.0f);
\ttf->directorAIConfig.velocity_ema_alpha = std::clamp(tf->directorAIConfig.velocity_ema_alpha, 0.05f, 1.0f);
\ttf->directorAIConfig.max_prediction_lead_px = std::clamp(tf->directorAIConfig.max_prediction_lead_px, 0.0f, 1200.0f);
\ttf->directorAIConfig.min_confidence_to_apply = std::clamp(tf->directorAIConfig.min_confidence_to_apply, 0.0f, 1.0f);
'''


def replace_once(text: str, marker: str, replacement: str, label: str) -> str:
    if replacement in text:
        return text
    if marker not in text:
        raise RuntimeError(f"marker not found: {label}")
    return text.replace(marker, replacement, 1)


def main() -> int:
    if not TARGET.exists():
        print(f"missing file: {TARGET}", file=sys.stderr)
        return 1

    text = TARGET.read_text(encoding="utf-8")
    original = text

    try:
        text = replace_once(text, PROP_LIST_MARKER, PROP_LIST_REPLACEMENT, "tracking visibility property list")
        text = replace_once(text, VISIBILITY_BLOCK_MARKER, VISIBILITY_BLOCK_REPLACEMENT, "tracking callback director visibility")
        text = replace_once(text, DIRECTOR_PROPS_MARKER, DIRECTOR_PROPS_INSERT, "director properties")
        text = replace_once(text, DEFAULTS_MARKER, DEFAULTS_INSERT, "director defaults")
        text = replace_once(text, UPDATE_MARKER, UPDATE_INSERT, "director update")
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if text == original:
        print("No GUI changes needed")
        return 0

    if not BACKUP.exists():
        BACKUP.write_text(original, encoding="utf-8")

    TARGET.write_text(text, encoding="utf-8")
    print("Director AI GUI controls added")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
