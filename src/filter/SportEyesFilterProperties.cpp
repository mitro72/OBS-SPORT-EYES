#include "sport-eyes-filter-internal.h"

static bool visible_on_bool(obs_properties_t *ppts, obs_data_t *settings, const char *bool_prop,
			    const char *prop_name)
{
	const bool enabled = obs_data_get_bool(settings, bool_prop);
	obs_property_t *p = obs_properties_get(ppts, prop_name);
	obs_property_set_visible(p, enabled);
	return true;
}

static bool enable_advanced_settings(obs_properties_t *ppts, obs_property_t *p,
				     obs_data_t *settings)
{
	const bool enabled = obs_data_get_bool(settings, "advanced");

	for (const char *prop_name :
	     {"threshold", "useGPU", "numThreads", "model_size", "detected_object", "sort_tracking",
	      "max_unseen_frames", "show_unseen_objects", "save_detections_path", "crop_group",
	      "min_size_threshold"}) {
		p = obs_properties_get(ppts, prop_name);
		obs_property_set_visible(p, enabled);
	}

	return true;
}

static bool csv_logging_enabled_modified(obs_properties_t *props, obs_property_t *,
					 obs_data_t *settings)
{
	const bool enabled = obs_data_get_bool(settings, "csv_logging_enabled");
	for (const char *name : {"csv_diagnostics_path", "csv_director_path"}) {
		if (obs_property_t *property = obs_properties_get(props, name))
			obs_property_set_visible(property, enabled);
	}
	return true;
}

static bool profile_apply_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	sport_eyes_profile_apply_selected(static_cast<detect_filter *>(data));
	return true;
}

static bool profile_save_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	sport_eyes_profile_save_current(static_cast<detect_filter *>(data));
	return true;
}

static bool profile_delete_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	sport_eyes_profile_delete_selected(static_cast<detect_filter *>(data));
	return true;
}

static bool profile_export_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	sport_eyes_profile_export_current(static_cast<detect_filter *>(data));
	return true;
}

static bool profile_import_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	sport_eyes_profile_import_and_apply(static_cast<detect_filter *>(data));
	return true;
}

static void add_configuration_profiles_group(obs_properties_t *props, detect_filter *tf)
{
	obs_properties_t *profileProps = obs_properties_create();
	obs_properties_add_group(props, "configuration_profiles", "Configuration Profiles",
		OBS_GROUP_NORMAL, profileProps);

	obs_property_t *profileSelector = obs_properties_add_list(
		profileProps, "profile_selected", "Profile", OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	for (const SportEyesProfileEntry &entry : sport_eyes_profile_list())
		obs_property_list_add_string(profileSelector, entry.label.c_str(), entry.id.c_str());

	obs_properties_add_text(profileProps, "profile_name", "Profile name", OBS_TEXT_DEFAULT);
	obs_properties_add_button2(profileProps, "profile_apply_selected", "Apply selected profile",
		profile_apply_clicked, tf);
	obs_properties_add_button2(profileProps, "profile_save_current", "Save / update profile",
		profile_save_clicked, tf);
	obs_properties_add_button2(profileProps, "profile_delete_selected", "Delete selected saved profile",
		profile_delete_clicked, tf);

	obs_properties_add_text(profileProps, "profile_json_export_info",
		"Export writes the current supported configuration to a portable JSON file.",
		OBS_TEXT_INFO);
	obs_properties_add_path(profileProps, "profile_export_path", "Export JSON file",
		OBS_PATH_FILE_SAVE, "JSON (*.json)", nullptr);
	obs_properties_add_button2(profileProps, "profile_export_json", "Export current configuration",
		profile_export_clicked, tf);

	obs_properties_add_text(profileProps, "profile_json_import_info",
		"Import applies a JSON profile immediately. Use Save / update profile afterwards to keep it in the local library.",
		OBS_TEXT_INFO);
	obs_properties_add_path(profileProps, "profile_import_path", "Import JSON file",
		OBS_PATH_FILE, "JSON (*.json)", nullptr);
	obs_properties_add_button2(profileProps, "profile_import_json", "Import and apply JSON",
		profile_import_clicked, tf);

	const std::string status = tf && !tf->profileStatus.empty()
		? tf->profileStatus
		: "Saved profiles are stored in the OBS Sport Eyes module configuration folder.";
	obs_properties_add_text(profileProps, "profile_status_info", status.c_str(), OBS_TEXT_INFO);
}

void set_class_names_on_object_category(obs_property_t *object_category,
					std::vector<std::string> class_names)
{
	std::vector<std::pair<size_t, std::string>> indexed_classes;
	for (size_t i = 0; i < class_names.size(); ++i) {
		const std::string &class_name = class_names[i];
		// capitalize the first letter of the class name
		std::string class_name_cap = class_name;
		class_name_cap[0] = (char)std::toupper((int)class_name_cap[0]);
		indexed_classes.push_back({i, class_name_cap});
	}

	// sort the vector based on the class names
	std::sort(indexed_classes.begin(), indexed_classes.end(),
		  [](const std::pair<size_t, std::string> &a,
		     const std::pair<size_t, std::string> &b) { return a.second < b.second; });

	// clear the object category list
	obs_property_list_clear(object_category);

	// add the sorted classes to the property list
	obs_property_list_add_int(object_category, obs_module_text("All"), -1);

	// add the sorted classes to the property list
	for (const auto &indexed_class : indexed_classes) {
		obs_property_list_add_int(object_category, indexed_class.second.c_str(),
					  (int)indexed_class.first);
	}
}

void read_model_config_json_and_set_class_names(const char *model_file, obs_properties_t *props_,
						obs_data_t *settings, struct detect_filter *tf_)
{
	if (model_file == nullptr || model_file[0] == '\0' || strlen(model_file) == 0) {
		obs_log(LOG_ERROR, "Model file path is empty");
		return;
	}

	// read the '.json' file near the model file to find the class names
	std::string json_file = model_file;
	json_file.replace(json_file.find(".onnx"), 5, ".json");
	std::ifstream file(json_file);
	if (!file.is_open()) {
		obs_data_set_string(settings, "error", "JSON file not found");
		obs_log(LOG_ERROR, "JSON file not found: %s", json_file.c_str());
	} else {
		obs_data_set_string(settings, "error", "");
		// parse the JSON file
		nlohmann::json j;
		file >> j;
		if (j.contains("names")) {
			std::vector<std::string> labels = j["names"];
			set_class_names_on_object_category(
				obs_properties_get(props_, "object_category"), labels);
			tf_->classNames = labels;
		} else {
			obs_data_set_string(settings, "error",
					    "JSON file does not contain 'names' field");
			obs_log(LOG_ERROR, "JSON file does not contain 'names' field");
		}
	}
}

obs_properties_t *sport_eyes_filter_properties(void *data)
{
	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);

	obs_properties_t *props = obs_properties_create();

	add_configuration_profiles_group(props, tf);

	obs_properties_add_bool(props, "preview", obs_module_text("Preview"));

	// add dropdown selection for object category selection: "All", or COCO classes
	obs_property_t *object_category =
		obs_properties_add_list(props, "object_category", obs_module_text("ObjectCategory"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	set_class_names_on_object_category(object_category, edgeyolo_cpp::COCO_CLASSES);
	tf->classNames = edgeyolo_cpp::COCO_CLASSES;

	// options group for masking
	obs_properties_t *masking_group = obs_properties_create();
	obs_property_t *masking_group_prop =
		obs_properties_add_group(props, "masking_group", obs_module_text("MaskingGroup"),
					 OBS_GROUP_CHECKABLE, masking_group);

	// add callback to show/hide masking options
	obs_property_set_modified_callback(masking_group_prop, [](obs_properties_t *props_,
								  obs_property_t *,
								  obs_data_t *settings) {
		const bool enabled = obs_data_get_bool(settings, "masking_group");
		obs_property_t *prop = obs_properties_get(props_, "masking_type");
		obs_property_t *masking_color = obs_properties_get(props_, "masking_color");
		obs_property_t *masking_blur_radius =
			obs_properties_get(props_, "masking_blur_radius");
		obs_property_t *masking_dilation =
			obs_properties_get(props_, "dilation_iterations");

		obs_property_set_visible(prop, enabled);
		obs_property_set_visible(masking_color, false);
		obs_property_set_visible(masking_blur_radius, false);
		obs_property_set_visible(masking_dilation, enabled);
		std::string masking_type_value = obs_data_get_string(settings, "masking_type");
		if (masking_type_value == "solid_color") {
			obs_property_set_visible(masking_color, enabled);
		} else if (masking_type_value == "blur" || masking_type_value == "pixelate") {
			obs_property_set_visible(masking_blur_radius, enabled);
		}
		return true;
	});

	// add masking options drop down selection: "None", "Solid color", "Blur", "Transparent"
	obs_property_t *masking_type = obs_properties_add_list(masking_group, "masking_type",
							       obs_module_text("MaskingType"),
							       OBS_COMBO_TYPE_LIST,
							       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(masking_type, obs_module_text("None"), "none");
	obs_property_list_add_string(masking_type, obs_module_text("SolidColor"), "solid_color");
	obs_property_list_add_string(masking_type, obs_module_text("OutputMask"), "output_mask");
	obs_property_list_add_string(masking_type, obs_module_text("Blur"), "blur");
	obs_property_list_add_string(masking_type, obs_module_text("Pixelate"), "pixelate");
	obs_property_list_add_string(masking_type, obs_module_text("Transparent"), "transparent");

	// add color picker for solid color masking
	obs_properties_add_color(masking_group, "masking_color", obs_module_text("MaskingColor"));

	// add slider for blur radius
	obs_properties_add_int_slider(masking_group, "masking_blur_radius",
				      obs_module_text("MaskingBlurRadius"), 1, 30, 1);

	// add callback to show/hide blur radius and color picker
	obs_property_set_modified_callback(masking_type, [](obs_properties_t *props_,
							    obs_property_t *,
							    obs_data_t *settings) {
		std::string masking_type_value = obs_data_get_string(settings, "masking_type");
		obs_property_t *masking_color = obs_properties_get(props_, "masking_color");
		obs_property_t *masking_blur_radius =
			obs_properties_get(props_, "masking_blur_radius");
		obs_property_t *masking_dilation =
			obs_properties_get(props_, "dilation_iterations");
		obs_property_set_visible(masking_color, false);
		obs_property_set_visible(masking_blur_radius, false);
		const bool masking_enabled = obs_data_get_bool(settings, "masking_group");
		obs_property_set_visible(masking_dilation, masking_enabled);

		if (masking_type_value == "solid_color") {
			obs_property_set_visible(masking_color, masking_enabled);
		} else if (masking_type_value == "blur" || masking_type_value == "pixelate") {
			obs_property_set_visible(masking_blur_radius, masking_enabled);
		}
		return true;
	});

	// add slider for dilation iterations
	obs_properties_add_int_slider(masking_group, "dilation_iterations",
				      obs_module_text("DilationIterations"), 0, 20, 1);

	// add options group for tracking and zoom-follow options
	obs_properties_t *tracking_group_props = obs_properties_create();
	obs_property_t *tracking_group = obs_properties_add_group(
		props, "tracking_group", obs_module_text("TrackingZoomFollowGroup"),
		OBS_GROUP_CHECKABLE, tracking_group_props);

	// add callback to show/hide tracking options
	obs_property_set_modified_callback(tracking_group, [](obs_properties_t *props_,
							      obs_property_t *,
							      obs_data_t *settings) {
		const bool enabled = obs_data_get_bool(settings, "tracking_group");

		// Show/hide the core tracking controls when the group is enabled/disabled
		for (auto prop_name : {"zoom_factor", "zoom_object", "zoom_speed_factor", "x_pan_preset",
				       "x_snap_hysteresis", "x_snap_transition_time", "x_deadband", "async_inference_enabled", "infer_interval_ms", "infer_scale",
				       "group_min_people", "group_min_people_strict", "group_edge_zoom_enabled", "group_edge_zoom_min", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth", "safe_roi_left", "safe_roi_right", "safe_roi_top", "safe_roi_bottom", "safe_roi_hold_ms", "cluster_inertia_ms"}) {
			obs_property_t *prop = obs_properties_get(props_, prop_name);
			if (prop)
				obs_property_set_visible(prop, enabled);
		}

		// Cluster preview controls only make sense when ZoomObject == "group"
		const char *zo = obs_data_get_string(settings, "zoom_object");
		const bool is_group = (zo && strcmp(zo, "group") == 0);

		obs_property_t *gmp = obs_properties_get(props_, "group_min_people");
		if (gmp)
			obs_property_set_visible(gmp, enabled && is_group);

		obs_property_t *gms = obs_properties_get(props_, "group_min_people_strict");
		if (gms)
			obs_property_set_visible(gms, enabled && is_group);

		obs_property_t *gmd = obs_properties_get(props_, "group_max_dist_frac");
		if (gmd)
			obs_property_set_visible(gmd, enabled && is_group);

		const bool edge_on = obs_data_get_bool(settings, "group_edge_zoom_enabled");
		obs_property_t *gez = obs_properties_get(props_, "group_edge_zoom_enabled");
		if (gez)
			obs_property_set_visible(gez, enabled && is_group);
		for (const char *edge_prop_name : {"group_edge_zoom_min", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth"}) {
			obs_property_t *ep = obs_properties_get(props_, edge_prop_name);
			if (ep)
				obs_property_set_visible(ep, enabled && is_group && edge_on);
		}

		obs_property_t *pgc = obs_properties_get(props_, "preview_group_clusters");
		if (pgc)
			obs_property_set_visible(pgc, enabled && is_group);

		obs_property_t *lbl = obs_properties_get(props_, "preview_group_cluster_label");
		if (lbl) {
			const bool on = obs_data_get_bool(settings, "preview_group_clusters");
			obs_property_set_visible(lbl, enabled && is_group && on);
		}

		return true;
	});

	// add zoom factor slider
	obs_properties_add_float_slider(tracking_group_props, "zoom_factor",
					obs_module_text("ZoomFactor"), 0.0, 1.0, 0.05);

	obs_properties_add_float_slider(tracking_group_props, "zoom_speed_factor",
					obs_module_text("ZoomSpeed"), 0.0, 0.1, 0.01);

	// Group pan preset: manual end-stops left/center/right (or auto follow)
	obs_property_t *x_pan_preset = obs_properties_add_list(tracking_group_props, "x_pan_preset",
							 obs_module_text("XPosition"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(x_pan_preset, obs_module_text("Auto"), "auto");
	obs_property_list_add_string(x_pan_preset, obs_module_text("Left"), "left");
	obs_property_list_add_string(x_pan_preset, obs_module_text("Center"), "center");
	obs_property_list_add_string(x_pan_preset, obs_module_text("Right"), "right");
	obs_property_list_add_string(x_pan_preset, obs_module_text("Auto Snap"), "autosnap");
	obs_property_list_add_string(x_pan_preset, obs_module_text("Auto Snap (Smooth)"), "autosnap_smooth");

	obs_properties_add_float_slider(tracking_group_props, "x_snap_hysteresis",
					 obs_module_text("Snap Hysteresis"), 0.0, 0.20, 0.01);

	obs_properties_add_float_slider(tracking_group_props, "x_snap_transition_time",
					 obs_module_text("Snap Transition (s)"), 0.05, 1.00, 0.05);
	
	obs_properties_add_bool(tracking_group_props, "async_inference_enabled",
				obs_module_text("Async Inference"));

	obs_properties_add_int_slider(tracking_group_props, "infer_interval_ms",
			      obs_module_text("Infer Interval (ms)"), 0, 200, 5);

	obs_properties_add_float_slider(tracking_group_props, "infer_scale",
				obs_module_text("Infer Scale"), 0.25, 1.00, 0.05);

	
	obs_properties_add_float_slider(tracking_group_props, "x_deadband",
				obs_module_text("X Deadband (%)"), 0.0, 5.0, 0.1);

	// add object selection for zoom drop down: "Single", "All"
	obs_property_t *zoom_object = obs_properties_add_list(tracking_group_props, "zoom_object",
							      obs_module_text("ZoomObject"),
							      OBS_COMBO_TYPE_LIST,
							      OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(zoom_object, obs_module_text("SingleFirst"), "single");
	obs_property_list_add_string(zoom_object, obs_module_text("Biggest"), "biggest");
	obs_property_list_add_string(zoom_object, obs_module_text("Oldest"), "oldest");
	obs_property_list_add_string(zoom_object, obs_module_text("All"), "all");
	//mod x basket
	obs_property_list_add_string(zoom_object, obs_module_text("Group"), "group");
obs_property_set_modified_callback(zoom_object, [](obs_properties_t *props_, obs_property_t *, obs_data_t *settings) {
		// When switching ZoomObject, show cluster preview controls only for "group"
		const bool enabled = obs_data_get_bool(settings, "tracking_group");
		const char *zo = obs_data_get_string(settings, "zoom_object");
		const bool is_group = (zo && strcmp(zo, "group") == 0);

		obs_property_t *pgc = obs_properties_get(props_, "preview_group_clusters");
		if (pgc)
			obs_property_set_visible(pgc, enabled && is_group);

		obs_property_t *lbl = obs_properties_get(props_, "preview_group_cluster_label");
		if (lbl) {
			const bool on = obs_data_get_bool(settings, "preview_group_clusters");
			obs_property_set_visible(lbl, enabled && is_group && on);
		}

		const bool edge_on = obs_data_get_bool(settings, "group_edge_zoom_enabled");
		obs_property_t *gez = obs_properties_get(props_, "group_edge_zoom_enabled");
		if (gez)
			obs_property_set_visible(gez, enabled && is_group);
		for (const char *edge_prop_name : {"group_edge_zoom_min", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth"}) {
			obs_property_t *ep = obs_properties_get(props_, edge_prop_name);
			if (ep)
				obs_property_set_visible(ep, enabled && is_group && edge_on);
		}
		return true;
	});

	obs_properties_add_int(tracking_group_props, "group_min_people", "Group min people", 1, 15, 1);
obs_properties_add_float_slider(tracking_group_props, "group_max_dist_frac", obs_module_text("GroupMaxDistFrac"), 0.05, 0.50, 0.01);

	obs_property_t *group_edge_zoom_enabled =
		obs_properties_add_bool(tracking_group_props, "group_edge_zoom_enabled", "Group edge zoom 2D (U curve)");
	obs_properties_add_float_slider(tracking_group_props, "group_edge_zoom_min",
					"Group edge zoom min (x)", 1.00, 4.00, 0.05);
	obs_properties_add_float_slider(tracking_group_props, "group_edge_zoom_amount",
					"Group edge zoom max (x)", 1.00, 4.00, 0.05);
	obs_properties_add_float_slider(tracking_group_props, "group_edge_zoom_curve",
					"Group edge zoom curve", 1.0, 5.0, 0.10);
	obs_properties_add_float_slider(tracking_group_props, "group_edge_zoom_smooth",
					"Group edge zoom smooth (s)", 0.05, 2.00, 0.05);
	obs_property_set_modified_callback(group_edge_zoom_enabled, [](obs_properties_t *props_, obs_property_t *, obs_data_t *settings) {
		const bool enabled = obs_data_get_bool(settings, "tracking_group");
		const char *zo = obs_data_get_string(settings, "zoom_object");
		const bool is_group = (zo && strcmp(zo, "group") == 0);
		const bool edge_on = obs_data_get_bool(settings, "group_edge_zoom_enabled");
		for (const char *edge_prop_name : {"group_edge_zoom_min", "group_edge_zoom_amount", "group_edge_zoom_curve", "group_edge_zoom_smooth"}) {
			obs_property_t *ep = obs_properties_get(props_, edge_prop_name);
			if (ep)
				obs_property_set_visible(ep, enabled && is_group && edge_on);
		}
		return true;
	});
	obs_property_set_visible(group_edge_zoom_enabled, false);
	obs_property_set_visible(obs_properties_get(tracking_group_props, "group_edge_zoom_min"), false);
	obs_property_set_visible(obs_properties_get(tracking_group_props, "group_edge_zoom_amount"), false);
	obs_property_set_visible(obs_properties_get(tracking_group_props, "group_edge_zoom_curve"), false);
	obs_property_set_visible(obs_properties_get(tracking_group_props, "group_edge_zoom_smooth"), false);

	obs_properties_add_int(tracking_group_props, "safe_roi_left", "Safe ROI Left Margin (%)", 0, 40, 1);
	obs_properties_add_int(tracking_group_props, "safe_roi_right", "Safe ROI Right Margin (%)", 0, 40, 1);
	obs_properties_add_int(tracking_group_props, "safe_roi_top", "Safe ROI Top Margin (%)", 0, 40, 1);
	obs_properties_add_int(tracking_group_props, "safe_roi_bottom", "Safe ROI Bottom Margin (%)", 0, 40, 1);
	obs_properties_add_int(tracking_group_props, "safe_roi_hold_ms", "Safe ROI Hold (ms)", 0, 2000, 50);
	obs_properties_add_int_slider(tracking_group_props, "cluster_inertia_ms", "Cluster inertia (ms)", 0, 2000, 25);
		obs_properties_add_bool(tracking_group_props, "group_min_people_strict", "Strict min people");
	obs_property_t *preview_group_clusters =
		obs_properties_add_bool(tracking_group_props, "preview_group_clusters", "Preview group cluster");
	obs_property_t *preview_group_cluster_label =
		obs_properties_add_bool(tracking_group_props, "preview_group_cluster_label", "Show group cluster label");
	// Initial visibility: detect_filter_properties() has no access to current settings -> start hidden.
	obs_property_set_visible(preview_group_clusters, false);
	obs_property_set_visible(preview_group_cluster_label, false);
	// Show label option only when cluster preview is enabled
	obs_property_set_modified_callback(preview_group_clusters, [](obs_properties_t *props_, obs_property_t *, obs_data_t *settings) {
		const bool enabled = obs_data_get_bool(settings, "tracking_group");
		const bool on = obs_data_get_bool(settings, "preview_group_clusters");
		const char *zo = obs_data_get_string(settings, "zoom_object");
		const bool is_group = (zo && strcmp(zo, "group") == 0);
		obs_property_t *lbl = obs_properties_get(props_, "preview_group_cluster_label");
		if (lbl)
			obs_property_set_visible(lbl, enabled && is_group && on);
		return true;
	});



	// Director AI is a top-level, checkable group so the operator can see at a
	// glance whether the predictive controller is enabled. It must not be hidden
	// behind the generic Advanced switch.
	obs_properties_t *director_ai_props = obs_properties_create();
	obs_properties_add_group(props, "director_ai_enabled", "Director AI", OBS_GROUP_CHECKABLE,
				 director_ai_props);
	obs_properties_add_float_slider(director_ai_props, "director_ai_prediction_horizon_ms",
		"Prediction horizon (ms)", 0.0, 600.0, 5.0);
	obs_properties_add_float_slider(director_ai_props, "director_ai_velocity_ema_alpha",
		"Velocity smoothing", 0.05, 0.95, 0.05);
	obs_properties_add_int_slider(director_ai_props, "director_ai_history_samples",
		"History samples", 10, 180, 1);
	obs_properties_add_float_slider(director_ai_props, "director_ai_base_coverage",
		"Base coverage", 0.25, 0.90, 0.01);
	obs_properties_add_float_slider(director_ai_props, "director_ai_fast_transition_speed_px_s",
		"Fast transition speed (px/s)", 200.0, 4000.0, 50.0);
	obs_properties_add_float_slider(director_ai_props, "director_ai_max_prediction_lead_px",
		"Max prediction lead (px)", 0.0, 1200.0, 10.0);
	obs_properties_add_float_slider(director_ai_props, "director_ai_min_confidence_to_apply",
		"Min confidence", 0.0, 1.0, 0.01);

	// CSV logging is intentionally a compact operator control: enabling the single
	// checkbox reveals the two independent destinations without exposing extra
	// diagnostic switches.
	obs_property_t *csv_logging_enabled =
		obs_properties_add_bool(props, "csv_logging_enabled", "Abilita log CSV");
	obs_properties_add_path(props, "csv_diagnostics_path", "CSV Diagnostica unificata",
		OBS_PATH_FILE_SAVE, "CSV (*.csv)", nullptr);
	obs_properties_add_path(props, "csv_director_path", "CSV Director AI",
		OBS_PATH_FILE_SAVE, "CSV (*.csv)", nullptr);
	obs_property_set_modified_callback(csv_logging_enabled, csv_logging_enabled_modified);
	const bool csvLoggingInitiallyEnabled = tf && tf->csvLoggingEnabled;
	obs_property_set_visible(obs_properties_get(props, "csv_diagnostics_path"), csvLoggingInitiallyEnabled);
	obs_property_set_visible(obs_properties_get(props, "csv_director_path"), csvLoggingInitiallyEnabled);

	obs_property_t *advanced =
		obs_properties_add_bool(props, "advanced", obs_module_text("Advanced"));

	// If advanced is selected show the advanced settings, otherwise hide them
	obs_property_set_modified_callback(advanced, enable_advanced_settings);

	// add a checkable group for crop region settings
	obs_properties_t *crop_group_props = obs_properties_create();
	obs_property_t *crop_group =
		obs_properties_add_group(props, "crop_group", obs_module_text("CropGroup"),
					 OBS_GROUP_CHECKABLE, crop_group_props);

	// add callback to show/hide crop region options
	obs_property_set_modified_callback(crop_group, [](obs_properties_t *props_,
							  obs_property_t *, obs_data_t *settings) {
		const bool enabled = obs_data_get_bool(settings, "crop_group");
		for (auto prop_name : {"crop_left", "crop_right", "crop_top", "crop_bottom"}) {
			obs_property_t *prop = obs_properties_get(props_, prop_name);
			obs_property_set_visible(prop, enabled);
		}
		return true;
	});

	// add crop region settings
	obs_properties_add_int_slider(crop_group_props, "crop_left", obs_module_text("CropLeft"), 0,
				      1000, 1);
	obs_properties_add_int_slider(crop_group_props, "crop_right", obs_module_text("CropRight"),
				      0, 1000, 1);
	obs_properties_add_int_slider(crop_group_props, "crop_top", obs_module_text("CropTop"), 0,
				      1000, 1);
	obs_properties_add_int_slider(crop_group_props, "crop_bottom",
				      obs_module_text("CropBottom"), 0, 1000, 1);

	// add a text input for the currently detected object
	obs_property_t *detected_obj_prop = obs_properties_add_text(
		props, "detected_object", obs_module_text("DetectedObject"), OBS_TEXT_DEFAULT);
	// disable the text input by default
	obs_property_set_enabled(detected_obj_prop, false);

	// add threshold slider
	obs_properties_add_float_slider(props, "threshold", obs_module_text("ConfThreshold"), 0.0,
					1.0, 0.025);

	// add minimal size threshold slider
	obs_properties_add_int_slider(props, "min_size_threshold",
				      obs_module_text("MinSizeThreshold"), 0, 10000, 1);

	// add SORT tracking enabled checkbox
	obs_properties_add_bool(props, "sort_tracking", obs_module_text("SORTTracking"));

	// add parameter for number of missing frames before a track is considered lost
	obs_properties_add_int(props, "max_unseen_frames", obs_module_text("MaxUnseenFrames"), 1,
			       30, 1);

	// add option to show unseen objects
	obs_properties_add_bool(props, "show_unseen_objects", obs_module_text("ShowUnseenObjects"));

	// add file path for saving detections
	obs_properties_add_path(props, "save_detections_path",
				obs_module_text("SaveDetectionsPath"), OBS_PATH_FILE_SAVE,
				"JSON file (*.json);;All files (*.*)", nullptr);

	/* GPU, CPU and performance Props */
	obs_property_t *p_use_gpu =
		obs_properties_add_list(props, "useGPU", obs_module_text("InferenceDevice"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p_use_gpu, obs_module_text("CPU"), USEGPU_CPU);
	// OpenVINO
	obs_property_list_add_string(p_use_gpu, obs_module_text("OpenVINOCPU"), USEGPU_OV_CPU);
	obs_property_list_add_string(p_use_gpu, obs_module_text("OpenVINOGPU"), USEGPU_OV_GPU);
#if defined(__linux__) && defined(__x86_64__)
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUTensorRT"), USEGPU_TENSORRT);
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUCuda"), USEGPU_CUDA);
#endif
#if _WIN32
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUDirectML"), USEGPU_DML);
#endif
#if defined(__APPLE__)
	obs_property_list_add_string(p_use_gpu, obs_module_text("CoreML"), USEGPU_COREML);
#endif

	obs_properties_add_int_slider(props, "numThreads", obs_module_text("NumThreads"), 0, 16, 1);

	// add drop down option for model size: Small, Medium, Large
	obs_property_t *model_size =
		obs_properties_add_list(props, "model_size", obs_module_text("ModelSize"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(model_size, obs_module_text("SmallFast"), "small");
	obs_property_list_add_string(model_size, obs_module_text("Medium"), "medium");
	obs_property_list_add_string(model_size, obs_module_text("LargeSlow"), "large");
	obs_property_list_add_string(model_size, obs_module_text("FaceDetect"),
				     FACE_DETECT_MODEL_SIZE);
	obs_property_list_add_string(model_size, obs_module_text("ExternalModel"),
				     EXTERNAL_MODEL_SIZE);

	// add external model file path
	obs_properties_add_path(props, "external_model_file", obs_module_text("ModelPath"),
				OBS_PATH_FILE, "EdgeYOLO onnx files (*.onnx);;all files (*.*)",
				nullptr);

	// add callback to show/hide the external model file path
	obs_property_set_modified_callback2(
		model_size,
		[](void *data_, obs_properties_t *props_, obs_property_t *p, obs_data_t *settings) {
			UNUSED_PARAMETER(p);
			struct detect_filter *tf_ = reinterpret_cast<detect_filter *>(data_);
			std::string model_size_value = obs_data_get_string(settings, "model_size");
			bool is_external = model_size_value == EXTERNAL_MODEL_SIZE;
			obs_property_t *prop = obs_properties_get(props_, "external_model_file");
			obs_property_set_visible(prop, is_external);
			if (!is_external) {
				if (model_size_value == FACE_DETECT_MODEL_SIZE) {
					// set the class names to COCO classes for face detection model
					set_class_names_on_object_category(
						obs_properties_get(props_, "object_category"),
						yunet::FACE_CLASSES);
					tf_->classNames = yunet::FACE_CLASSES;
				} else {
					// reset the class names to COCO classes for default models
					set_class_names_on_object_category(
						obs_properties_get(props_, "object_category"),
						edgeyolo_cpp::COCO_CLASSES);
					tf_->classNames = edgeyolo_cpp::COCO_CLASSES;
				}
			} else {
				// if the model path is already set - update the class names
				const char *model_file =
					obs_data_get_string(settings, "external_model_file");
				read_model_config_json_and_set_class_names(model_file, props_,
									   settings, tf_);
			}
			return true;
		},
		tf);

	// add callback on the model file path to check if the file exists
	obs_property_set_modified_callback2(
		obs_properties_get(props, "external_model_file"),
		[](void *data_, obs_properties_t *props_, obs_property_t *p, obs_data_t *settings) {
			UNUSED_PARAMETER(p);
			const char *model_size_value = obs_data_get_string(settings, "model_size");
			bool is_external = strcmp(model_size_value, EXTERNAL_MODEL_SIZE) == 0;
			if (!is_external) {
				return true;
			}
			struct detect_filter *tf_ = reinterpret_cast<detect_filter *>(data_);
			const char *model_file =
				obs_data_get_string(settings, "external_model_file");
			read_model_config_json_and_set_class_names(model_file, props_, settings,
								   tf_);
			return true;
		},
		tf);

	// Add a informative text about the plugin
	std::string basic_info =
		std::regex_replace(PLUGIN_INFO_TEMPLATE, std::regex("%1"), PLUGIN_VERSION);
	obs_properties_add_text(props, "info", basic_info.c_str(), OBS_TEXT_INFO);

	UNUSED_PARAMETER(data);
	return props;
}

