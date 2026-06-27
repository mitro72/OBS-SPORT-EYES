#pragma once

#include <string>
#include <vector>

struct detect_filter;

// A profile can be one of the built-in basketball presets or a JSON file saved
// in the module configuration directory. The id is intentionally opaque to the
// UI: it is used only to decide how the profile is loaded.
struct SportEyesProfileEntry {
	std::string id;
	std::string label;
	bool builtIn = false;
};

std::vector<SportEyesProfileEntry> sport_eyes_profile_list();

// The actions below read the relevant paths/name/selection from the filter's
// OBS settings. They always update filter->profileStatus with a human-readable
// result so the properties dialog can refresh after a button click.
bool sport_eyes_profile_apply_selected(struct detect_filter *filter);
bool sport_eyes_profile_save_current(struct detect_filter *filter);
bool sport_eyes_profile_delete_selected(struct detect_filter *filter);
bool sport_eyes_profile_export_current(struct detect_filter *filter);
bool sport_eyes_profile_import_and_apply(struct detect_filter *filter);
