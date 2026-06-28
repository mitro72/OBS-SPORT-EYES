#include "sport-eyes-filter-internal.h"


namespace {

struct TrackingFilterMatches {
	const char *name = nullptr;
	std::vector<obs_source_t *> filters;
};

void collect_tracking_filter(obs_source_t *parent, obs_source_t *candidate, void *param)
{
	(void)parent;
	auto *matches = static_cast<TrackingFilterMatches *>(param);
	if (matches && candidate && matches->name &&
		strcmp(obs_source_get_name(candidate), matches->name) == 0) {
		// Keep an owned reference: duplicate removal below can otherwise invalidate
		// pointers while OBS is enumerating the parent filter list.
		obs_source_t *owned = obs_source_get_ref(candidate);
		if (owned)
			matches->filters.emplace_back(owned);
	}
}

obs_source_t *find_and_deduplicate_tracking_filter(obs_source_t *parent, const char *name)
{
	TrackingFilterMatches matches{name, {}};
	obs_source_enum_filters(parent, collect_tracking_filter, &matches);

	if (matches.filters.empty())
		return nullptr;

	// The first filter in the parent chain is the authoritative one. The old
	// startup workaround could create a second helper before OBS restored the
	// serialized helper filters; remove any exact-name duplicates deterministically.
	obs_source_t *selected = matches.filters.front();
	for (size_t i = 1; i < matches.filters.size(); ++i) {
		obs_log(LOG_WARNING, "OBS Sport Eyes: removing duplicate tracking helper '%s'", name);
		obs_source_filter_remove(parent, matches.filters[i]);
		obs_source_release(matches.filters[i]);
	}
	return selected;
}

void release_tracking_filter_ref(obs_source_t *&source)
{
	if (source) {
		obs_source_release(source);
		source = nullptr;
	}
}

void replace_tracking_filter_ref(obs_source_t *&slot, obs_source_t *source)
{
	if (slot == source) {
		// find_and_deduplicate_tracking_filter returns an owned reference even
		// when the selected OBS source did not change.
		if (source)
			obs_source_release(source);
		return;
	}
	release_tracking_filter_ref(slot);
	slot = source;
}

obs_source_t *create_tracking_helper(obs_source_t *parent, const char *sourceId,
	const char *name, obs_data_t *settings)
{
	obs_source_t *created = obs_source_create(sourceId, name, settings, nullptr);
	if (!created)
		return nullptr;

	obs_source_filter_add(parent, created);
	obs_source_release(created);
	return find_and_deduplicate_tracking_filter(parent, name);
}

} // namespace

bool sport_eyes_sync_tracking_filters(detect_filter *filter, bool allowCreate)
{
	obs_source_t *parent = obs_filter_get_parent(filter->source);
	if (!parent) {
		filter->trackingSetupPending = filter->trackingEnabled;
		obs_log(LOG_DEBUG,
			"OBS Sport Eyes: tracking setup deferred; parent source is not ready yet");
		return false;
	}

	if (!filter->trackingEnabled) {
		obs_source_t *scaleFilter =
			find_and_deduplicate_tracking_filter(parent, "Detect Tracking Scale");
		if (scaleFilter) {
			obs_source_filter_remove(parent, scaleFilter);
			obs_source_release(scaleFilter);
		}
		release_tracking_filter_ref(filter->trackingScaleFilter);

		obs_source_t *cropFilter =
			find_and_deduplicate_tracking_filter(parent, "Detect Tracking");
		if (cropFilter) {
			obs_source_filter_remove(parent, cropFilter);
			obs_source_release(cropFilter);
		}
		release_tracking_filter_ref(filter->trackingFilter);
		filter->trackingSetupPending = false;
		filter->trackingSetupGraceTicks = 0;
		return true;
	}

	obs_source_t *cropFilter =
		find_and_deduplicate_tracking_filter(parent, "Detect Tracking");
	if (!cropFilter && allowCreate)
		cropFilter = create_tracking_helper(parent, "crop_filter", "Detect Tracking", nullptr);
	replace_tracking_filter_ref(filter->trackingFilter, cropFilter);

	obs_source_t *scaleFilter =
		find_and_deduplicate_tracking_filter(parent, "Detect Tracking Scale");
	if (!scaleFilter && allowCreate) {
		obs_data_t *scaleSettings = obs_data_create();
		obs_data_set_string(scaleSettings, "resolution", "1920x1080");
		scaleFilter = create_tracking_helper(parent, "scale_filter",
			"Detect Tracking Scale", scaleSettings);
		obs_data_release(scaleSettings);
	}
	replace_tracking_filter_ref(filter->trackingScaleFilter, scaleFilter);
	if (filter->trackingScaleFilter)
		obs_source_set_enabled(filter->trackingScaleFilter, false);

	filter->trackingSetupPending = !(filter->trackingFilter && filter->trackingScaleFilter);
	if (filter->trackingSetupPending) {
		if (!allowCreate)
			obs_log(LOG_DEBUG, "OBS Sport Eyes: waiting for OBS filter restore before creating tracking helpers");
		else
			obs_log(LOG_WARNING, "OBS Sport Eyes: tracking helper setup incomplete; retrying");
	} else {
		obs_log(LOG_INFO, "OBS Sport Eyes: tracking filters ready");
	}
	return !filter->trackingSetupPending;
}


void sport_eyes_filter_ensure_tracking_setup(void *data)
{
	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);
	if (!tf || !tf->trackingEnabled)
		return;

	// During scene restore, bind existing helpers only. Creation is delayed to
	// video_tick after a small grace window, preventing duplicate helpers.
	sport_eyes_sync_tracking_filters(tf, false);
}

void sport_eyes_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "profile_selected", "");
	obs_data_set_default_string(settings, "profile_name", "Basket 180");
	obs_data_set_default_string(settings, "profile_export_path", "");
	obs_data_set_default_string(settings, "profile_import_path", "");

	obs_data_set_default_bool(settings, "advanced", false);
#if _WIN32
	obs_data_set_default_string(settings, "useGPU", USEGPU_DML);
#elif defined(__APPLE__)
	obs_data_set_default_string(settings, "useGPU", USEGPU_CPU);
#else
	// Linux
	obs_data_set_default_string(settings, "useGPU", USEGPU_CPU);
#endif
	obs_data_set_default_bool(settings, "sort_tracking", false);
	obs_data_set_default_int(settings, "max_unseen_frames", 10);
	obs_data_set_default_bool(settings, "show_unseen_objects", true);
	obs_data_set_default_int(settings, "numThreads", 1);
	obs_data_set_default_bool(settings, "preview", true);
	obs_data_set_default_double(settings, "threshold", 0.5);
	obs_data_set_default_string(settings, "model_size", "small");
	obs_data_set_default_int(settings, "object_category", -1);
	obs_data_set_default_bool(settings, "masking_group", false);
	obs_data_set_default_string(settings, "masking_type", "none");
	obs_data_set_default_string(settings, "masking_color", "#000000");
	obs_data_set_default_int(settings, "masking_blur_radius", 0);
	obs_data_set_default_int(settings, "dilation_iterations", 0);
	obs_data_set_default_bool(settings, "tracking_group", false);
	obs_data_set_default_double(settings, "zoom_factor", 0.0);
	obs_data_set_default_double(settings, "zoom_speed_factor", 0.05);
	obs_data_set_default_string(settings, "zoom_object", "single");
	obs_data_set_default_string(settings, "x_pan_preset", "auto");
	obs_data_set_default_bool(settings, "async_inference_enabled", true);
	obs_data_set_default_int(settings, "infer_interval_ms", 0);
	obs_data_set_default_double(settings, "infer_scale", 1.0);
	obs_data_set_default_int(settings, "group_min_people", 6);
	obs_data_set_default_bool(settings, "group_min_people_strict", false);
	obs_data_set_default_double(settings, "group_max_dist_frac", 0.15);
	obs_data_set_default_bool(settings, "group_edge_zoom_enabled", false);
	// 1.00 retains the previous U-curve: no extra 2D zoom at the center.
	obs_data_set_default_double(settings, "group_edge_zoom_min", 1.00);
	obs_data_set_default_double(settings, "group_edge_zoom_amount", 1.20);
	obs_data_set_default_double(settings, "group_edge_zoom_curve", 2.0);
	obs_data_set_default_double(settings, "group_edge_zoom_smooth", 0.60);
	obs_data_set_default_int(settings, "safe_roi_left", 10);
	obs_data_set_default_int(settings, "safe_roi_right", 10);
	obs_data_set_default_int(settings, "safe_roi_top", 0);
	obs_data_set_default_int(settings, "safe_roi_bottom", 8);
	obs_data_set_default_int(settings, "safe_roi_hold_ms", 300);
	obs_data_set_default_int(settings, "cluster_inertia_ms", 150);
	obs_data_set_default_bool(settings, "preview_group_clusters", false);
	obs_data_set_default_bool(settings, "preview_group_cluster_label", false);
	obs_data_set_default_double(settings, "x_snap_hysteresis", 0.05);
	obs_data_set_default_double(settings, "x_snap_transition_time", 0.25);
	obs_data_set_default_double(settings, "x_deadband", 0.0);
	obs_data_set_default_bool(settings, "director_ai_enabled", false);
	obs_data_set_default_double(settings, "director_ai_prediction_horizon_ms", 300.0);
	obs_data_set_default_double(settings, "director_ai_velocity_ema_alpha", 0.35);
	obs_data_set_default_int(settings, "director_ai_history_samples", 90);
	obs_data_set_default_double(settings, "director_ai_base_coverage", 0.50);
	obs_data_set_default_double(settings, "director_ai_fast_transition_speed_px_s", 1200.0);
	obs_data_set_default_double(settings, "director_ai_max_prediction_lead_px", 480.0);
	obs_data_set_default_double(settings, "director_ai_min_confidence_to_apply", 0.05);
	obs_data_set_default_bool(settings, "csv_logging_enabled", false);
	obs_data_set_default_string(settings, "csv_diagnostics_path", "obs_sport_eyes_diagnostics.csv");
	obs_data_set_default_string(settings, "csv_director_path", "obs_sport_eyes_director_ai.csv");
	obs_data_set_default_string(settings, "save_detections_path", "");
	obs_data_set_default_bool(settings, "crop_group", false);
	obs_data_set_default_int(settings, "crop_left", 0);
	obs_data_set_default_int(settings, "crop_right", 0);
	obs_data_set_default_int(settings, "crop_top", 0);
	obs_data_set_default_int(settings, "crop_bottom", 0);
}

void sport_eyes_filter_update(void *data, obs_data_t *settings)
{
	obs_log(LOG_INFO, "Detect filter update");

	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);

	tf->isDisabled = true;

	tf->preview = obs_data_get_bool(settings, "preview");
	tf->conf_threshold = (float)obs_data_get_double(settings, "threshold");
	tf->objectCategory = (int)obs_data_get_int(settings, "object_category");
	tf->maskingEnabled = obs_data_get_bool(settings, "masking_group");
	tf->maskingType = obs_data_get_string(settings, "masking_type");
	tf->maskingColor = (int)obs_data_get_int(settings, "masking_color");
	tf->maskingBlurRadius = (int)obs_data_get_int(settings, "masking_blur_radius");
	tf->maskingDilateIterations = (int)obs_data_get_int(settings, "dilation_iterations");

	bool newTrackingEnabled = obs_data_get_bool(settings, "tracking_group");
	tf->zoomFactor = (float)obs_data_get_double(settings, "zoom_factor");
	tf->zoomSpeedFactor = (float)obs_data_get_double(settings, "zoom_speed_factor");
	tf->zoomObject = obs_data_get_string(settings, "zoom_object");

	tf->groupMinPeople = (int)obs_data_get_int(settings, "group_min_people");
	tf->groupMinPeople = std::max(1, tf->groupMinPeople);
	tf->groupMinPeopleStrict = obs_data_get_bool(settings, "group_min_people_strict");
	tf->groupMaxDistFrac = (float)obs_data_get_double(settings, "group_max_dist_frac");
	tf->groupMaxDistFrac = std::clamp(tf->groupMaxDistFrac, 0.05f, 0.50f);
	tf->groupEdgeZoomEnabled = obs_data_get_bool(settings, "group_edge_zoom_enabled");
	tf->groupEdgeZoomMin = (float)obs_data_get_double(settings, "group_edge_zoom_min");
	tf->groupEdgeZoomAmount = (float)obs_data_get_double(settings, "group_edge_zoom_amount");
	tf->groupEdgeZoomCurve = (float)obs_data_get_double(settings, "group_edge_zoom_curve");
	tf->groupEdgeZoomSmooth = (float)obs_data_get_double(settings, "group_edge_zoom_smooth");
	tf->groupEdgeZoomAmount = std::clamp(tf->groupEdgeZoomAmount, 1.0f, 4.0f);
	// A malformed profile must never invert the U-curve.
	tf->groupEdgeZoomMin = std::clamp(tf->groupEdgeZoomMin, 1.0f, tf->groupEdgeZoomAmount);
	tf->groupEdgeZoomCurve = std::clamp(tf->groupEdgeZoomCurve, 1.0f, 5.0f);
	tf->groupEdgeZoomSmooth = std::clamp(tf->groupEdgeZoomSmooth, 0.05f, 2.00f);

	tf->safe_roi_left = (int)obs_data_get_int(settings, "safe_roi_left");
	tf->safe_roi_right = (int)obs_data_get_int(settings, "safe_roi_right");
	tf->safe_roi_top = (int)obs_data_get_int(settings, "safe_roi_top");
	tf->safe_roi_bottom = (int)obs_data_get_int(settings, "safe_roi_bottom");
	tf->safe_roi_hold_ms = (int)obs_data_get_int(settings, "safe_roi_hold_ms");
	tf->cluster_inertia_ms = (int)obs_data_get_int(settings, "cluster_inertia_ms");

	tf->safe_roi_left = std::clamp(tf->safe_roi_left, 0, 40);
	tf->safe_roi_right = std::clamp(tf->safe_roi_right, 0, 40);
	tf->safe_roi_top = std::clamp(tf->safe_roi_top, 0, 40);
	tf->safe_roi_bottom = std::clamp(tf->safe_roi_bottom, 0, 40);
	tf->safe_roi_hold_ms = std::clamp(tf->safe_roi_hold_ms, 0, 2000);
	tf->cluster_inertia_ms = std::clamp(tf->cluster_inertia_ms, 0, 2000);
	tf->previewGroupClusters = obs_data_get_bool(settings, "preview_group_clusters");
	tf->previewGroupClusterLabel = obs_data_get_bool(settings, "preview_group_cluster_label");

	tf->x_pan_preset = obs_data_get_string(settings, "x_pan_preset");
	tf->x_snap_hysteresis = (float)obs_data_get_double(settings, "x_snap_hysteresis");
	tf->x_snap_transition_time = (float)obs_data_get_double(settings, "x_snap_transition_time");
	tf->asyncInferenceEnabled = obs_data_get_bool(settings, "async_inference_enabled");
	sport_eyes_async_inference_reset(tf);
	tf->infer_interval_ms = (int)obs_data_get_int(settings, "infer_interval_ms");
	tf->infer_interval_ms = std::clamp(tf->infer_interval_ms, 0, 200);

	tf->infer_scale = (float)obs_data_get_double(settings, "infer_scale");
	tf->infer_scale = std::clamp(tf->infer_scale, 0.25f, 1.0f);

	// Reset cached detections and their telemetry on settings change.
	tf->cached_objects_valid = false;
	tf->cached_objects.clear();
	tf->cachedObjectsCaptureNs = 0;
	tf->cachedObjectsCompletedNs = 0;
	tf->cachedObjectsSequence = 0;
	tf->cachedObjectsInferenceMs = 0.0f;
	tf->last_infer_ts_ns = 0;

	tf->x_deadband = (float)obs_data_get_double(settings, "x_deadband");
	tf->x_deadband = std::clamp(tf->x_deadband, 0.0f, 5.0f);

	const bool csvLoggingEnabled = obs_data_get_bool(settings, "csv_logging_enabled");
	std::string diagnosticsCsvPath = obs_data_get_string(settings, "csv_diagnostics_path");
	std::string directorCsvPath = obs_data_get_string(settings, "csv_director_path");
	if (diagnosticsCsvPath.empty())
		diagnosticsCsvPath = "obs_sport_eyes_diagnostics.csv";
	if (directorCsvPath.empty())
		directorCsvPath = "obs_sport_eyes_director_ai.csv";
	sport_eyes_csv_logging_reconfigure(tf, csvLoggingEnabled, diagnosticsCsvPath, directorCsvPath);

	const bool directorAIWasEnabled = tf->directorAIEnabled;
	tf->directorAIEnabled = obs_data_get_bool(settings, "director_ai_enabled");
	director_ai::DirectorAIConfig nextDirectorAIConfig;
	nextDirectorAIConfig.prediction_horizon_ms = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_prediction_horizon_ms"), 0.0f, 600.0f);
	nextDirectorAIConfig.velocity_ema_alpha = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_velocity_ema_alpha"), 0.05f, 0.95f);
	nextDirectorAIConfig.history_samples = (size_t)std::clamp(
		(int)obs_data_get_int(settings, "director_ai_history_samples"), 10, 180);
	nextDirectorAIConfig.base_coverage = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_base_coverage"), 0.25f, 0.90f);
	nextDirectorAIConfig.fast_transition_speed_px_s = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_fast_transition_speed_px_s"), 200.0f, 4000.0f);
	nextDirectorAIConfig.max_prediction_lead_px = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_max_prediction_lead_px"), 0.0f, 1200.0f);
	nextDirectorAIConfig.min_confidence_to_apply = std::clamp(
		(float)obs_data_get_double(settings, "director_ai_min_confidence_to_apply"), 0.0f, 1.0f);

	const bool directorAIConfigChanged =
		tf->directorAIConfig.prediction_horizon_ms != nextDirectorAIConfig.prediction_horizon_ms ||
		tf->directorAIConfig.velocity_ema_alpha != nextDirectorAIConfig.velocity_ema_alpha ||
		tf->directorAIConfig.history_samples != nextDirectorAIConfig.history_samples ||
		tf->directorAIConfig.base_coverage != nextDirectorAIConfig.base_coverage ||
		tf->directorAIConfig.fast_transition_speed_px_s != nextDirectorAIConfig.fast_transition_speed_px_s ||
		tf->directorAIConfig.max_prediction_lead_px != nextDirectorAIConfig.max_prediction_lead_px ||
		tf->directorAIConfig.min_confidence_to_apply != nextDirectorAIConfig.min_confidence_to_apply;
	tf->directorAIConfig = nextDirectorAIConfig;

	// A changed model, or an enable/disable transition, must not retain velocity
	// and history measured under a previous configuration.
	if (directorAIWasEnabled != tf->directorAIEnabled || directorAIConfigChanged) {
		if (tf->directorAIEngine)
			tf->directorAIEngine->reset();
		if (!tf->directorAIEnabled)
			tf->lastDirectorAIFrame = director_ai::DirectorAIFrame();
	}

	tf->has_last_target_zx = false; // reset safe when settings change


	if (tf->x_pan_preset == "left")
		tf->x_snap_state = 0;
	else if (tf->x_pan_preset == "center")
		tf->x_snap_state = 1;
	else if (tf->x_pan_preset == "right")
		tf->x_snap_state = 2;

	tf->sortTracking = obs_data_get_bool(settings, "sort_tracking");
	size_t maxUnseenFrames = (size_t)obs_data_get_int(settings, "max_unseen_frames");
	if (tf->tracker.getMaxUnseenFrames() != maxUnseenFrames) {
		tf->tracker.setMaxUnseenFrames(maxUnseenFrames);
	}
	tf->showUnseenObjects = obs_data_get_bool(settings, "show_unseen_objects");
	tf->saveDetectionsPath = obs_data_get_string(settings, "save_detections_path");

	tf->crop_enabled = obs_data_get_bool(settings, "crop_group");
	tf->crop_left = (int)obs_data_get_int(settings, "crop_left");
	tf->crop_right = (int)obs_data_get_int(settings, "crop_right");
	tf->crop_top = (int)obs_data_get_int(settings, "crop_top");
	tf->crop_bottom = (int)obs_data_get_int(settings, "crop_bottom");

	tf->minAreaThreshold = (int)obs_data_get_int(settings, "min_size_threshold");

	// Always synchronise instead of only reacting to a bool transition. This fixes
	// scene-load startup where the checkbox is already true before the parent exists.
	const bool trackingStateChanged = (tf->trackingEnabled != newTrackingEnabled);
	tf->trackingEnabled = newTrackingEnabled;
	if (trackingStateChanged)
		tf->trackingSetupGraceTicks = 0;
	// Bind restored helper filters now, but do not create new ones until the
	// source graph has had time to finish restoring.
	sport_eyes_sync_tracking_filters(tf, false);

	const std::string newUseGpu = obs_data_get_string(settings, "useGPU");
	const uint32_t newNumThreads = (uint32_t)obs_data_get_int(settings, "numThreads");
	const std::string newModelSize = obs_data_get_string(settings, "model_size");

	bool reinitialize = (tf->useGPU != newUseGpu || tf->numThreads != newNumThreads || tf->modelSize != newModelSize);

	if (reinitialize) {
		obs_log(LOG_INFO, "Reinitializing model");

		std::unique_lock<std::mutex> lock(tf->modelMutex);

		char *modelFilepath_rawPtr = nullptr;
		if (newModelSize == "small") {
			modelFilepath_rawPtr =
				obs_module_file("models/edgeyolo_tiny_lrelu_coco_256x416.onnx");
		} else if (newModelSize == "medium") {
			modelFilepath_rawPtr =
				obs_module_file("models/edgeyolo_tiny_lrelu_coco_480x800.onnx");
		} else if (newModelSize == "large") {
			modelFilepath_rawPtr =
				obs_module_file("models/edgeyolo_tiny_lrelu_coco_736x1280.onnx");
		} else if (newModelSize == FACE_DETECT_MODEL_SIZE) {
			modelFilepath_rawPtr =
				obs_module_file("models/face_detection_yunet_2023mar.onnx");
		} else if (newModelSize == EXTERNAL_MODEL_SIZE) {
			const char *external_model_file =
				obs_data_get_string(settings, "external_model_file");
			if (!external_model_file || external_model_file[0] == '\0') {
				obs_log(LOG_ERROR, "External model file path is empty");
				tf->isDisabled = true;
				return;
			}
			modelFilepath_rawPtr = bstrdup(external_model_file);
		} else {
			obs_log(LOG_ERROR, "Invalid model size: %s", newModelSize.c_str());
			tf->isDisabled = true;
			return;
		}

		if (!modelFilepath_rawPtr) {
			obs_log(LOG_ERROR, "Unable to get model filename from plugin.");
			tf->isDisabled = true;
			return;
		}

#if _WIN32
		int outLength = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr,
					    -1, nullptr, 0);
		tf->modelFilepath = std::wstring(outLength, L'\0');
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1,
				    tf->modelFilepath.data(), outLength);
#else
		tf->modelFilepath = std::string(modelFilepath_rawPtr);
#endif
		bfree(modelFilepath_rawPtr);

		tf->useGPU = newUseGpu;
		tf->numThreads = newNumThreads;
		tf->modelSize = newModelSize;

		float nms_th_ = 0.45f;
		int num_classes_ = (int)edgeyolo_cpp::COCO_CLASSES.size();
		tf->classNames = edgeyolo_cpp::COCO_CLASSES;

		// External model labels
		if (tf->modelSize == EXTERNAL_MODEL_SIZE) {
#ifdef _WIN32
			std::wstring labelsFilepath = tf->modelFilepath;
			labelsFilepath.replace(labelsFilepath.find(L".onnx"), 5, L".json");
#else
			std::string labelsFilepath = tf->modelFilepath;
			labelsFilepath.replace(labelsFilepath.find(".onnx"), 5, ".json");
#endif
			std::ifstream labelsFile(labelsFilepath);
			if (!labelsFile.is_open()) {
				obs_log(LOG_ERROR, "Failed to open JSON file for external model labels");
				tf->isDisabled = true;
				tf->model.reset();
				return;
			}
			nlohmann::json j;
			labelsFile >> j;
			if (!j.contains("names")) {
				obs_log(LOG_ERROR, "JSON file does not contain 'names' field");
				tf->isDisabled = true;
				tf->model.reset();
				return;
			}
			std::vector<std::string> labels = j["names"];
			num_classes_ = (int)labels.size();
			tf->classNames = labels;
		} else if (tf->modelSize == FACE_DETECT_MODEL_SIZE) {
			num_classes_ = 1;
			tf->classNames = yunet::FACE_CLASSES;
		}

		try {
#ifdef _WIN32
			std::string modelPathString = sport_eyes_wide_to_utf8(tf->modelFilepath);
#else
			std::string modelPathString = tf->modelFilepath;
#endif
			const std::string device =
				(tf->useGPU == USEGPU_OV_GPU) ? "GPU" : "CPU";

			tf->model.reset();

			if (tf->modelSize == FACE_DETECT_MODEL_SIZE) {
				tf->model = std::make_unique<YuNetOpenVINOAdapter>(
					modelPathString, device, (int)tf->numThreads,
					50, nms_th_, tf->conf_threshold);
			} else {
				tf->model = std::make_unique<EdgeYOLOOpenVINOAdapter>(
					modelPathString, device, (int)tf->numThreads,
					num_classes_, nms_th_, tf->conf_threshold);
			}
			obs_data_set_string(settings, "error", "");
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "Failed to load OpenVINO model: %s", e.what());
			tf->isDisabled = true;
			tf->model.reset();
			return;
		}
	}

	if (tf->model) {
		tf->model->setBBoxConfThresh(tf->conf_threshold);
	}

	if (reinitialize) {
		obs_log(LOG_INFO, "Detect Filter Options:");
		obs_log(LOG_INFO, "  Source: %s", obs_source_get_name(tf->source));
		obs_log(LOG_INFO, "  Inference Device: %s", tf->useGPU.c_str());
		obs_log(LOG_INFO, "  Num Threads: %d", tf->numThreads);
		obs_log(LOG_INFO, "  Model Size: %s", tf->modelSize.c_str());
		obs_log(LOG_INFO, "  Preview: %s", tf->preview ? "true" : "false");
		obs_log(LOG_INFO, "  Threshold: %.2f", tf->conf_threshold);
#ifdef _WIN32
		obs_log(LOG_INFO, "  Model file path: %ls", tf->modelFilepath.c_str());
#else
		obs_log(LOG_INFO, "  Model file path: %s", tf->modelFilepath.c_str());
#endif
	}

	tf->isDisabled = false;
}

void sport_eyes_filter_activate(void *data)
{
	obs_log(LOG_INFO, "Detect filter activated");
	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);
	tf->isDisabled = false;

	// OBS may still be restoring serialized helper filters at activation. Bind
	// only existing helpers here; creation is delayed from video_tick.
	if (tf->trackingEnabled && (tf->trackingSetupPending || !tf->trackingFilter ||
					    !tf->trackingScaleFilter)) {
		tf->trackingSetupGraceTicks = 0;
		sport_eyes_filter_ensure_tracking_setup(tf);
	}
}

void sport_eyes_filter_deactivate(void *data)
{
	obs_log(LOG_INFO, "Detect filter deactivated");
	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);
	tf->isDisabled = true;
}

/**                   FILTER CORE                     */

void *sport_eyes_filter_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "Detect filter created");
	void *data = bmalloc(sizeof(struct detect_filter));
	struct detect_filter *tf = new (data) detect_filter();

	tf->source = source;
	tf->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	tf->lastDetectedObjectId = -1;

	std::vector<std::tuple<const char *, gs_effect_t **>> effects = {
		{KAWASE_BLUR_EFFECT_PATH, &tf->kawaseBlurEffect},
		{MASKING_EFFECT_PATH, &tf->maskingEffect},
		{PIXELATE_EFFECT_PATH, &tf->pixelateEffect},
	};

	for (auto [effectPath, effect] : effects) {
		char *effectPathPtr = obs_module_file(effectPath);
		if (!effectPathPtr) {
			obs_log(LOG_ERROR, "Failed to get effect path: %s", effectPath);
			tf->isDisabled = true;
			return tf;
		}
		obs_enter_graphics();
		*effect = gs_effect_create_from_file(effectPathPtr, nullptr);
		bfree(effectPathPtr);
		if (!*effect) {
			obs_log(LOG_ERROR, "Failed to load effect: %s", effectPath);
			tf->isDisabled = true;
			return tf;
		}
		obs_leave_graphics();
	}

	sport_eyes_filter_update(tf, settings);
	sport_eyes_async_inference_start(tf);

	return tf;
}

void sport_eyes_filter_destroy(void *data)
{
	obs_log(LOG_INFO, "Detect filter destroyed");

	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);

	if (tf) {
		sport_eyes_async_inference_stop(tf);
		sport_eyes_csv_logging_close(tf);
		tf->isDisabled = true;

		release_tracking_filter_ref(tf->trackingScaleFilter);
		release_tracking_filter_ref(tf->trackingFilter);

		obs_enter_graphics();
		gs_texrender_destroy(tf->texrender);
		if (tf->stagesurface) {
			gs_stagesurface_destroy(tf->stagesurface);
		}
		gs_effect_destroy(tf->kawaseBlurEffect);
		gs_effect_destroy(tf->maskingEffect);

		gs_effect_destroy(tf->pixelateEffect);
obs_leave_graphics();
		tf->~detect_filter();
		bfree(tf);
	}
}

//MOD x basket
