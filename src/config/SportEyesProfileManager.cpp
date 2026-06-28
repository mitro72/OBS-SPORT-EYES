#include "config/SportEyesProfileManager.h"
#include "sport-eyes-filter-internal.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char *kProfileFormat = "obs-sport-eyes-profile";
constexpr int kProfileSchemaVersion = 1;
constexpr const char *kBuiltinPrefix = "builtin:";
constexpr const char *kSavedPrefix = "saved:";

const std::vector<std::string> &profile_setting_keys()
{
	// Curated settings only. Runtime status, UI-only output and library metadata
	// are deliberately excluded so a profile remains portable and safe to import.
	static const std::vector<std::string> keys = {
		"preview",
		"threshold",
		"object_category",
		"masking_group",
		"masking_type",
		"masking_color",
		"masking_blur_radius",
		"dilation_iterations",
		"tracking_group",
		"zoom_factor",
		"zoom_speed_factor",
		"zoom_object",
		"x_pan_preset",
		"x_snap_hysteresis",
		"x_snap_transition_time",
		"x_deadband",
		"async_inference_enabled",
		"infer_interval_ms",
		"infer_scale",
		"group_min_people",
		"group_min_people_strict",
		"group_max_dist_frac",
		"group_edge_zoom_enabled",
		"group_edge_zoom_min",
		"group_edge_zoom_amount",
		"group_edge_zoom_curve",
		"group_edge_zoom_smooth",
		"safe_roi_left",
		"safe_roi_right",
		"safe_roi_top",
		"safe_roi_bottom",
		"safe_roi_hold_ms",
		"cluster_inertia_ms",
		"preview_group_clusters",
		"preview_group_cluster_label",
		"director_ai_enabled",
		"director_ai_prediction_horizon_ms",
		"director_ai_velocity_ema_alpha",
		"director_ai_history_samples",
		"director_ai_base_coverage",
		"director_ai_fast_transition_speed_px_s",
		"director_ai_max_prediction_lead_px",
		"director_ai_min_confidence_to_apply",
		"sort_tracking",
		"max_unseen_frames",
		"show_unseen_objects",
		"min_size_threshold",
		"crop_group",
		"crop_left",
		"crop_right",
		"crop_top",
		"crop_bottom",
		"useGPU",
		"numThreads",
		"model_size",
		"external_model_file",
		"csv_logging_enabled",
		"csv_diagnostics_path",
		"csv_director_path",
		"save_detections_path",
		"advanced",
	};
	return keys;
}

std::string trim_copy(std::string value)
{
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
		value.erase(value.begin());
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
		value.pop_back();
	return value;
}

std::string safe_profile_file_stem(const std::string &name)
{
	std::string result;
	for (const char rawChar : name) {
		const unsigned char c = static_cast<unsigned char>(rawChar);
		if (std::isalnum(c) || c == '-' || c == '_')
			result.push_back(static_cast<char>(c));
		else if (std::isspace(c) || c == '.')
			result.push_back('_');
	}

	while (!result.empty() && result.back() == '_')
		result.pop_back();
	if (result.empty())
		result = "sport-eyes-profile";
	if (result.size() > 64)
		result.resize(64);
	return result;
}

bool ensure_parent_directory(const fs::path &path, std::string &error)
{
	std::error_code ec;
	const fs::path parent = path.parent_path();
	if (!parent.empty())
		fs::create_directories(parent, ec);
	if (ec) {
		error = "Cannot create profile directory: " + parent.string();
		return false;
	}
	return true;
}

fs::path profile_library_directory(std::string &error)
{
	char *rawPath = obs_module_config_path("profiles");
	if (!rawPath) {
		error = "OBS module configuration directory is not available";
		return {};
	}

	fs::path result(rawPath);
	bfree(rawPath);
	std::error_code ec;
	fs::create_directories(result, ec);
	if (ec) {
		error = "Cannot create OBS Sport Eyes profile library";
		return {};
	}
	return result;
}

json snapshot_settings(obs_data_t *settings)
{
	json result = json::object();
	if (!settings)
		return result;

	// obs_data_get_json_with_defaults is not available in older OBS SDKs.
	// Build an effective settings object explicitly: defaults first, then the
	// current user values. obs_data_get_json is available across the supported
	// OBS API versions and serializes the resulting effective values.
	obs_data_t *effective = obs_data_get_defaults(settings);
	if (!effective)
		effective = obs_data_create();
	if (!effective)
		return result;

	obs_data_apply(effective, settings);
	const char *effectiveJson = obs_data_get_json(effective);
	json raw = json::parse(effectiveJson ? effectiveJson : "{}", nullptr, false);
	obs_data_release(effective);
	if (raw.is_discarded() || !raw.is_object())
		return result;

	for (const std::string &key : profile_setting_keys()) {
		auto item = raw.find(key);
		if (item != raw.end())
			result[key] = *item;
	}
	return result;
}

json make_profile_document(obs_data_t *settings, const std::string &profileName)
{
	return json{
		{"format", kProfileFormat},
		{"schema_version", kProfileSchemaVersion},
		{"profile_name", profileName},
		{"settings", snapshot_settings(settings)},
	};
}

bool write_json_document(const fs::path &path, const json &document, std::string &error)
{
	if (!ensure_parent_directory(path, error))
		return false;

	std::ofstream output(path, std::ios::out | std::ios::trunc);
	if (!output.is_open()) {
		error = "Cannot write JSON profile: " + path.string();
		return false;
	}

	output << document.dump(2) << '\n';
	if (!output.good()) {
		error = "Failed while writing JSON profile: " + path.string();
		return false;
	}
	return true;
}

bool read_json_document(const fs::path &path, json &document, std::string &error)
{
	std::ifstream input(path);
	if (!input.is_open()) {
		error = "Cannot open JSON profile: " + path.string();
		return false;
	}

	try {
		input >> document;
	} catch (const std::exception &e) {
		error = std::string("Invalid JSON profile: ") + e.what();
		return false;
	}
	return true;
}

bool validate_profile_document(const json &document, json &settingsOut,
	std::string &profileNameOut, std::string &error)
{
	if (!document.is_object()) {
		error = "Profile root must be a JSON object";
		return false;
	}
	if (document.value("format", std::string()) != kProfileFormat) {
		error = "Unsupported profile format";
		return false;
	}
	const int schemaVersion = document.value("schema_version", 0);
	if (schemaVersion <= 0 || schemaVersion > kProfileSchemaVersion) {
		error = "Unsupported profile schema version";
		return false;
	}
	if (!document.contains("settings") || !document["settings"].is_object()) {
		error = "Profile does not contain a settings object";
		return false;
	}

	settingsOut = json::object();
	for (const std::string &key : profile_setting_keys()) {
		const auto item = document["settings"].find(key);
		if (item != document["settings"].end())
			settingsOut[key] = *item;
	}
	if (settingsOut.empty()) {
		error = "Profile does not contain supported OBS Sport Eyes settings";
		return false;
	}

	profileNameOut = trim_copy(document.value("profile_name", std::string()));
	if (profileNameOut.empty())
		profileNameOut = "Imported profile";
	return true;
}

json builtin_profile_settings(const std::string &id)
{
	// Built-ins intentionally contain only sport-framing tuning. They preserve
	// the operator's model, device, model file and local CSV paths.
	json base = {
		{"tracking_group", true},
		{"zoom_factor", 0.50},
		{"zoom_object", "group"},
		{"x_pan_preset", "auto"},
		{"async_inference_enabled", true},
		{"infer_interval_ms", 50},
		{"infer_scale", 1.00},
		{"group_min_people", 4},
		{"group_min_people_strict", false},
		{"group_max_dist_frac", 0.15},
		{"group_edge_zoom_min", 1.00},
		{"safe_roi_left", 10},
		{"safe_roi_right", 10},
		{"safe_roi_top", 0},
		{"safe_roi_bottom", 8},
		{"director_ai_enabled", true},
		{"director_ai_base_coverage", 0.50},
		{"director_ai_max_prediction_lead_px", 480.0},
		{"director_ai_min_confidence_to_apply", 0.05},
	};

	if (id == "builtin:basket-180-reactive") {
		base["zoom_speed_factor"] = 0.040;
		base["x_snap_hysteresis"] = 0.070;
		base["x_snap_transition_time"] = 0.18;
		base["safe_roi_hold_ms"] = 180;
		base["cluster_inertia_ms"] = 80;
		base["director_ai_prediction_horizon_ms"] = 260.0;
		base["director_ai_velocity_ema_alpha"] = 0.50;
		base["director_ai_history_samples"] = 70;
		base["director_ai_fast_transition_speed_px_s"] = 1400.0;
	} else if (id == "builtin:basket-180-conservative") {
		base["zoom_speed_factor"] = 0.020;
		base["x_snap_hysteresis"] = 0.120;
		base["x_snap_transition_time"] = 0.35;
		base["safe_roi_hold_ms"] = 450;
		base["cluster_inertia_ms"] = 250;
		base["director_ai_prediction_horizon_ms"] = 340.0;
		base["director_ai_velocity_ema_alpha"] = 0.30;
		base["director_ai_history_samples"] = 110;
		base["director_ai_fast_transition_speed_px_s"] = 1000.0;
	} else {
		base["zoom_speed_factor"] = 0.030;
		base["x_snap_hysteresis"] = 0.100;
		base["x_snap_transition_time"] = 0.25;
		base["safe_roi_hold_ms"] = 300;
		base["cluster_inertia_ms"] = 150;
		base["director_ai_prediction_horizon_ms"] = 300.0;
		base["director_ai_velocity_ema_alpha"] = 0.35;
		base["director_ai_history_samples"] = 90;
		base["director_ai_fast_transition_speed_px_s"] = 1200.0;
	}
	return base;
}

std::string builtin_profile_name(const std::string &id)
{
	if (id == "builtin:basket-180-reactive")
		return "Basket 180 - Reactive";
	if (id == "builtin:basket-180-conservative")
		return "Basket 180 - Conservative";
	return "Basket 180 - Balanced";
}

bool apply_settings_to_filter(detect_filter *filter, const json &profileSettings,
	const std::string &profileName, const std::string &selectedId, std::string &error)
{
	if (!filter || !filter->source) {
		error = "Filter source is not available";
		return false;
	}

	obs_data_t *settingsToApply = obs_data_create_from_json(profileSettings.dump().c_str());
	if (!settingsToApply) {
		error = "Unable to create OBS settings from JSON profile";
		return false;
	}

	obs_data_t *currentSettings = obs_source_get_settings(filter->source);
	if (!currentSettings) {
		obs_data_release(settingsToApply);
		error = "Unable to access current OBS filter settings";
		return false;
	}

	obs_data_apply(currentSettings, settingsToApply);
	obs_data_set_string(currentSettings, "profile_name", profileName.c_str());
	obs_data_set_string(currentSettings, "profile_selected", selectedId.c_str());
	obs_source_update(filter->source, currentSettings);

	obs_data_release(currentSettings);
	obs_data_release(settingsToApply);
	return true;
}

bool read_profile_file_and_apply(detect_filter *filter, const fs::path &path,
	const std::string &selectedId, std::string &error)
{
	json document;
	if (!read_json_document(path, document, error))
		return false;

	json settings;
	std::string profileName;
	if (!validate_profile_document(document, settings, profileName, error))
		return false;
	return apply_settings_to_filter(filter, settings, profileName, selectedId, error);
}

bool is_safe_saved_filename(const std::string &filename)
{
	const fs::path path(filename);
	return !filename.empty() && path.filename() == path && path.extension() == ".json";
}

} // namespace

std::vector<SportEyesProfileEntry> sport_eyes_profile_list()
{
	std::vector<SportEyesProfileEntry> entries;
	entries.push_back({"", "Custom / current settings", false});
	entries.push_back({"builtin:basket-180-balanced", "Basket 180 - Balanced", true});
	entries.push_back({"builtin:basket-180-reactive", "Basket 180 - Reactive", true});
	entries.push_back({"builtin:basket-180-conservative", "Basket 180 - Conservative", true});

	std::string error;
	const fs::path library = profile_library_directory(error);
	if (library.empty())
		return entries;

	std::error_code ec;
	for (const fs::directory_entry &entry : fs::directory_iterator(library, ec)) {
		if (ec)
			break;
		if (!entry.is_regular_file() || entry.path().extension() != ".json")
			continue;

		json document;
		std::string readError;
		if (!read_json_document(entry.path(), document, readError))
			continue;
		json settings;
		std::string profileName;
		if (!validate_profile_document(document, settings, profileName, readError))
			continue;

		entries.push_back({std::string(kSavedPrefix) + entry.path().filename().string(),
			profileName, false});
	}

	std::sort(entries.begin() + 4, entries.end(),
		[](const SportEyesProfileEntry &a, const SportEyesProfileEntry &b) {
			return a.label < b.label;
		});
	return entries;
}

bool sport_eyes_profile_apply_selected(struct detect_filter *filter)
{
	if (!filter || !filter->source)
		return false;

	obs_data_t *current = obs_source_get_settings(filter->source);
	if (!current) {
		filter->profileStatus = "Unable to access current filter settings";
		return false;
	}
	const std::string selection = obs_data_get_string(current, "profile_selected");
	obs_data_release(current);

	if (selection.empty()) {
		filter->profileStatus = "Select a saved or built-in profile first";
		return false;
	}

	std::string error;
	if (selection.rfind(kBuiltinPrefix, 0) == 0) {
		const json settings = builtin_profile_settings(selection);
		if (!apply_settings_to_filter(filter, settings, builtin_profile_name(selection), selection, error)) {
			filter->profileStatus = error;
			return false;
		}
		filter->profileStatus = "Applied " + builtin_profile_name(selection);
		return true;
	}

	if (selection.rfind(kSavedPrefix, 0) != 0) {
		filter->profileStatus = "Unsupported profile selection";
		return false;
	}
	const std::string filename = selection.substr(std::strlen(kSavedPrefix));
	if (!is_safe_saved_filename(filename)) {
		filter->profileStatus = "Invalid saved profile file name";
		return false;
	}
	const fs::path library = profile_library_directory(error);
	if (library.empty()) {
		filter->profileStatus = error;
		return false;
	}
	if (!read_profile_file_and_apply(filter, library / filename, selection, error)) {
		filter->profileStatus = error;
		return false;
	}
	filter->profileStatus = "Applied saved profile";
	return true;
}

bool sport_eyes_profile_save_current(struct detect_filter *filter)
{
	if (!filter || !filter->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->source);
	if (!settings) {
		filter->profileStatus = "Unable to access current filter settings";
		return false;
	}

	std::string profileName = trim_copy(obs_data_get_string(settings, "profile_name"));
	if (profileName.empty()) {
		obs_data_release(settings);
		filter->profileStatus = "Enter a profile name before saving";
		return false;
	}

	std::string error;
	const fs::path library = profile_library_directory(error);
	if (library.empty()) {
		obs_data_release(settings);
		filter->profileStatus = error;
		return false;
	}

	const std::string filename = safe_profile_file_stem(profileName) + ".json";
	const fs::path destination = library / filename;
	const json document = make_profile_document(settings, profileName);
	obs_data_release(settings);
	if (!write_json_document(destination, document, error)) {
		filter->profileStatus = error;
		return false;
	}

	filter->profileStatus = "Saved profile: " + profileName;
	return true;
}

bool sport_eyes_profile_delete_selected(struct detect_filter *filter)
{
	if (!filter || !filter->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->source);
	if (!settings) {
		filter->profileStatus = "Unable to access current filter settings";
		return false;
	}
	const std::string selection = obs_data_get_string(settings, "profile_selected");
	obs_data_release(settings);

	if (selection.rfind(kSavedPrefix, 0) != 0) {
		filter->profileStatus = "Only saved JSON profiles can be deleted";
		return false;
	}
	const std::string filename = selection.substr(std::strlen(kSavedPrefix));
	if (!is_safe_saved_filename(filename)) {
		filter->profileStatus = "Invalid saved profile file name";
		return false;
	}

	std::string error;
	const fs::path library = profile_library_directory(error);
	if (library.empty()) {
		filter->profileStatus = error;
		return false;
	}

	std::error_code ec;
	if (!fs::remove(library / filename, ec) || ec) {
		filter->profileStatus = "Unable to delete saved profile";
		return false;
	}
	filter->profileStatus = "Deleted saved profile";
	return true;
}

bool sport_eyes_profile_export_current(struct detect_filter *filter)
{
	if (!filter || !filter->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->source);
	if (!settings) {
		filter->profileStatus = "Unable to access current filter settings";
		return false;
	}
	const std::string outputPath = trim_copy(obs_data_get_string(settings, "profile_export_path"));
	std::string profileName = trim_copy(obs_data_get_string(settings, "profile_name"));
	if (profileName.empty())
		profileName = "OBS Sport Eyes export";
	const json document = make_profile_document(settings, profileName);
	obs_data_release(settings);

	if (outputPath.empty()) {
		filter->profileStatus = "Choose an export JSON file first";
		return false;
	}

	std::string error;
	if (!write_json_document(fs::path(outputPath), document, error)) {
		filter->profileStatus = error;
		return false;
	}
	filter->profileStatus = "Exported JSON configuration";
	return true;
}

bool sport_eyes_profile_import_and_apply(struct detect_filter *filter)
{
	if (!filter || !filter->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(filter->source);
	if (!settings) {
		filter->profileStatus = "Unable to access current filter settings";
		return false;
	}
	const std::string inputPath = trim_copy(obs_data_get_string(settings, "profile_import_path"));
	obs_data_release(settings);
	if (inputPath.empty()) {
		filter->profileStatus = "Choose a JSON file to import first";
		return false;
	}

	std::string error;
	if (!read_profile_file_and_apply(filter, fs::path(inputPath), "", error)) {
		filter->profileStatus = error;
		return false;
	}
	filter->profileStatus = "Imported and applied JSON configuration; save it to add it to the profile list";
	return true;
}
