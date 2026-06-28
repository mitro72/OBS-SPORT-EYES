#include "sport-eyes-filter-internal.h"

void sport_eyes_filter_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);

	// Let OBS finish restoring serialized source filters before creating helpers.
	// This avoids a race where create/update/activate added new helpers and OBS
	// subsequently restored the saved ones, resulting in duplicate filters.
	if (tf->trackingEnabled && (tf->trackingSetupPending || !tf->trackingFilter ||
					 !tf->trackingScaleFilter)) {
		const bool allowCreate =
			++tf->trackingSetupGraceTicks >= TRACKING_SETUP_STARTUP_GRACE_TICKS;
		sport_eyes_sync_tracking_filters(tf, allowCreate);
	}

	if (tf->isDisabled || !tf->model) {
		return;
	}

	if (!obs_source_enabled(tf->source)) {
		return;
	}

	cv::Mat imageBGRA;
	{
		std::unique_lock<std::mutex> lock(tf->inputBGRALock, std::try_to_lock);
		if (!lock.owns_lock()) {
			// No data to process
			return;
		}
		if (tf->inputBGRA.empty()) {
			// No data to process
			return;
		}
		imageBGRA = tf->inputBGRA.clone();
	}

	cv::Rect cropRect(0, 0, imageBGRA.cols, imageBGRA.rows);
	if (tf->crop_enabled) {
		cropRect = cv::Rect(tf->crop_left, tf->crop_top,
			imageBGRA.cols - tf->crop_left - tf->crop_right,
			imageBGRA.rows - tf->crop_top - tf->crop_bottom);
	}

	const uint64_t nowInferNs = os_gettime_ns();
	const uint64_t inferIntervalNs =
		(tf->infer_interval_ms > 0) ? (uint64_t)tf->infer_interval_ms * 1000000ULL : 0ULL;
	std::vector<Object> objects;
	bool detectionsFresh = false;
	uint64_t detectionCaptureNs = 0;
	uint64_t detectionCompletedNs = 0;
	uint64_t detectionSequence = 0;
	float detectionInferenceMs = 0.0f;
	cv::Point detectionCropOrigin(0, 0);
	SportEyesAsyncTelemetry asyncTelemetryBefore;
	SportEyesAsyncTelemetry asyncTelemetryAfter;

	if (tf->asyncInferenceEnabled) {
		sport_eyes_async_inference_snapshot(tf, asyncTelemetryBefore);
		// Consume a completed measurement. Between measurements, reuse the latest
		// result for framing but do not present it as a new temporal observation.
		if (sport_eyes_async_inference_try_get(tf, objects, detectionCaptureNs,
				detectionCompletedNs, detectionCropOrigin, detectionSequence, detectionInferenceMs)) {
			for (Object &obj : objects) {
				obj.rect.x += (float)detectionCropOrigin.x;
				obj.rect.y += (float)detectionCropOrigin.y;
			}
			// Cache only after the common post-processing stage below.  In
			// particular SORT initializes Object::unseenFrames; caching raw detector
			// objects here made cached async ticks look "unseen" and they were removed
			// by the hide-unseen filter.
			tf->cachedObjectsCaptureNs = detectionCaptureNs;
			tf->cachedObjectsCompletedNs = detectionCompletedNs;
			tf->cachedObjectsSequence = detectionSequence;
			tf->cachedObjectsInferenceMs = detectionInferenceMs;
			tf->last_infer_ts_ns = detectionCaptureNs;
			detectionsFresh = true;
		} else if (tf->cached_objects_valid) {
			objects = tf->cached_objects;
			detectionCaptureNs = tf->cachedObjectsCaptureNs;
			detectionCompletedNs = tf->cachedObjectsCompletedNs;
			detectionSequence = tf->cachedObjectsSequence;
			detectionInferenceMs = tf->cachedObjectsInferenceMs;
		}

		const bool due = inferIntervalNs == 0ULL ||
			(asyncTelemetryBefore.lastSubmitNs == 0ULL ||
			 (nowInferNs - asyncTelemetryBefore.lastSubmitNs) >= inferIntervalNs);
		if (due && cropRect.width > 0 && cropRect.height > 0) {
			cv::Mat bgr;
			cv::cvtColor(imageBGRA(cropRect), bgr, cv::COLOR_BGRA2BGR);
			sport_eyes_async_inference_submit(tf, bgr, tf->infer_scale, nowInferNs,
				cropRect.tl());
		}
		sport_eyes_async_inference_snapshot(tf, asyncTelemetryAfter);
	} else {
		// Explicit compatibility/debug fallback. This is intentionally blocking.
		cv::Mat inferenceFrame;
		cv::cvtColor(imageBGRA(cropRect), inferenceFrame, cv::COLOR_BGRA2BGR);
		cv::Mat inferScaled;
		cv::Mat &inferInput = inferenceFrame;
		if (tf->infer_scale < 0.999f) {
			cv::resize(inferenceFrame, inferScaled, cv::Size(), tf->infer_scale,
				tf->infer_scale, cv::INTER_LINEAR);
			inferInput = inferScaled;
		}
		try {
			std::unique_lock<std::mutex> lock(tf->modelMutex);
			objects = tf->model->inference(inferInput);
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "%s", e.what());
		}
		if (tf->infer_scale < 0.999f && tf->infer_scale > 0.0f) {
			const float inv = 1.0f / tf->infer_scale;
			for (Object &obj : objects) {
				obj.rect.x *= inv;
				obj.rect.y *= inv;
				obj.rect.width *= inv;
				obj.rect.height *= inv;
			}
		}
		for (Object &obj : objects) {
			obj.rect.x += (float)cropRect.x;
			obj.rect.y += (float)cropRect.y;
		}
		// Cache only after the shared post-processing stage below.
		tf->last_infer_ts_ns = nowInferNs;
		detectionCaptureNs = nowInferNs;
		detectionCompletedNs = os_gettime_ns();
		detectionSequence = ++tf->asyncInferenceNextSequence;
		detectionInferenceMs = (float)(detectionCompletedNs - detectionCaptureNs) / 1000000.0f;
		tf->cachedObjectsCaptureNs = detectionCaptureNs;
		tf->cachedObjectsCompletedNs = detectionCompletedNs;
		tf->cachedObjectsSequence = detectionSequence;
		tf->cachedObjectsInferenceMs = detectionInferenceMs;
		detectionsFresh = true;
		asyncTelemetryAfter.lastInferenceMs = detectionInferenceMs;
	}

	const uint64_t detectionAgeNs = detectionCaptureNs > 0 && nowInferNs >= detectionCaptureNs
		? nowInferNs - detectionCaptureNs : 0ULL;
	const uint64_t detectionCompletionAgeNs = detectionCompletedNs > 0 && nowInferNs >= detectionCompletedNs
		? nowInferNs - detectionCompletedNs : 0ULL;

	// update the detected object text input
	if (objects.size() > 0) {
		if (tf->lastDetectedObjectId != objects[0].label) {
			tf->lastDetectedObjectId = objects[0].label;
			// get source settings
			obs_data_t *source_settings = obs_source_get_settings(tf->source);
			obs_data_set_string(source_settings, "detected_object",
					    tf->classNames[objects[0].label].c_str());
			// release the source settings
			obs_data_release(source_settings);
		}
	} else {
		if (tf->lastDetectedObjectId != -1) {
			tf->lastDetectedObjectId = -1;
			// get source settings
			obs_data_t *source_settings = obs_source_get_settings(tf->source);
			obs_data_set_string(source_settings, "detected_object", "");
			// release the source settings
			obs_data_release(source_settings);
		}
	}

	if (tf->minAreaThreshold > 0) {
		objects.erase(
			std::remove_if(objects.begin(), objects.end(), [tf](const Object &obj) {
				return obj.rect.area() <= (float)tf->minAreaThreshold;
			}),
			objects.end());
	}

if (tf->objectCategory != -1) {
		objects.erase(
			std::remove_if(objects.begin(), objects.end(), [tf](const Object &obj) {
				return obj.label != tf->objectCategory;
			}),
			objects.end());
	}

if (tf->sortTracking && detectionsFresh) {
		// Cached detections are not new observations: feeding them into SORT again
		// creates artificial zero-motion samples and damages temporal tracking.
		objects = tf->tracker.update(objects);
	}

	// Store the fully normalised measurement only after min-area/category filters
	// and (when enabled) SORT have run.  Cached async frames must reuse objects
	// whose unseenFrames state is defined; otherwise the hide-unseen option
	// removes every cached detection between completed inferences.
	if (detectionsFresh) {
		tf->cached_objects = objects;
		tf->cached_objects_valid = true;
	}

	if (!tf->showUnseenObjects) {
		objects.erase(
			std::remove_if(objects.begin(), objects.end(),
				       [](const Object &obj) { return obj.unseenFrames > 0; }),
			objects.end());
	}

	if (!tf->saveDetectionsPath.empty()) {
		// Throttle disk writes a bit to reduce per-frame overhead
		static uint64_t saveCounter = 0;
		if ((++saveCounter % 5) == 0) {
			std::ofstream detectionsFile(tf->saveDetectionsPath);
			if (detectionsFile.is_open()) {
				nlohmann::json j;
				for (const Object &obj : objects) {
					nlohmann::json obj_json;
					obj_json["label"] = obj.label;
					obj_json["confidence"] = obj.prob;
					obj_json["rect"] = {{"x", obj.rect.x},
							  {"y", obj.rect.y},
							  {"width", obj.rect.width},
							  {"height", obj.rect.height}};
					obj_json["id"] = obj.id;
					j.push_back(obj_json);
				}
				// Compact JSON (no indentation) to reduce CPU + IO
				detectionsFile << j.dump();
				detectionsFile.close();
			} else {
				obs_log(LOG_ERROR, "Failed to open file for writing detections: %s",
					tf->saveDetectionsPath.c_str());
			}
		}
	}

	if (tf->preview || tf->maskingEnabled) {
		cv::Mat frame;
		cv::cvtColor(imageBGRA, frame, cv::COLOR_BGRA2BGR);

		if (tf->preview && tf->crop_enabled) {
			// draw the crop rectangle on the frame in a dashed line
			drawDashedRectangle(frame, cropRect, cv::Scalar(0, 255, 0), 5, 8, 15);
		}
// Safe ROI (decision region) overlay + current decision bbox
if (tf->preview && tf->trackingEnabled && tf->trackingFilter) {
	const RectI safe = make_safe_roi(frame.cols, frame.rows,
				 tf->safe_roi_left, tf->safe_roi_right,
				 tf->safe_roi_top, tf->safe_roi_bottom);
	cv::Rect safeRect(safe.x, safe.y, safe.w, safe.h);
	drawDashedRectangle(frame, safeRect, cv::Scalar(255, 255, 0), 3, 6, 10); // yellow

	// BBox currently driving crop decision (safe / hold / fallback)
	if (rect_valid(tf->safe_roi_decision_bbox)) {
		cv::Rect decisionRect((int)tf->safe_roi_decision_bbox.x, (int)tf->safe_roi_decision_bbox.y,
				     (int)tf->safe_roi_decision_bbox.width, (int)tf->safe_roi_decision_bbox.height);
		cv::Scalar col = tf->safe_roi_decision_from_safe ? cv::Scalar(255, 0, 255) : cv::Scalar(0, 165, 255); // magenta vs orange
		if (tf->safe_roi_decision_is_hold)
			col = cv::Scalar(255, 255, 0); // yellow for HOLD
		drawDashedRectangle(frame, decisionRect, col, 3, 6, 10);
		const char *lbl = tf->safe_roi_decision_is_hold ? "DECISION (HOLD)" :
				 (tf->safe_roi_decision_from_safe ? "DECISION (SAFE)" : "DECISION (FALLBACK)");
		cv::putText(frame, lbl,
			    cv::Point(decisionRect.x + 6, std::max(20, decisionRect.y - 8)),
			    cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
	}

	// Cluster inertia overlay (only meaningful in group mode)
	if (tf->zoomObject == "group") {
		if (tf->cluster_active_valid && rect_valid(tf->cluster_active_box)) {
			cv::Rect activeRect((int)tf->cluster_active_box.x, (int)tf->cluster_active_box.y,
					    (int)tf->cluster_active_box.width, (int)tf->cluster_active_box.height);
			drawDashedRectangle(frame, activeRect, cv::Scalar(0, 255, 0), 2, 6, 10); // green
			cv::putText(frame, "CLUSTER ACTIVE",
				    cv::Point(activeRect.x + 6, std::max(20, activeRect.y - 8)),
				    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
		}
		if (tf->cluster_pending_since_ns != 0 && rect_valid(tf->cluster_pending_box)) {
			cv::Rect pendingRect((int)tf->cluster_pending_box.x, (int)tf->cluster_pending_box.y,
					     (int)tf->cluster_pending_box.width, (int)tf->cluster_pending_box.height);
			drawDashedRectangle(frame, pendingRect, cv::Scalar(255, 0, 0), 2, 3, 6); // blue/red-ish
			cv::putText(frame, "CLUSTER PENDING",
				    cv::Point(pendingRect.x + 6, std::max(20, pendingRect.y - 8)),
				    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 0, 0), 2);
		}
	}

	if (tf->safe_roi_holding) {
		cv::putText(frame, "SAFE ROI HOLD", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX,
			    1.0, cv::Scalar(255, 255, 0), 2);
	}
}
		if (tf->preview && objects.size() > 0) {
			draw_objects(frame, objects, tf->classNames);
		}
		// Optional debug overlay: draw one bbox per detected people-cluster (group).
		// This is useful when zoomObject == "group" to understand which cluster is being selected.
		if (tf->preview && tf->zoomObject == "group" && !objects.empty()) {
			int visibleCount = 0;
			for (const Object &obj : objects) {
				if (obj.unseenFrames == 0)
					visibleCount++;
			}
			int minPeople = std::max(1, tf->groupMinPeople);
			if (!tf->groupMinPeopleStrict && visibleCount > 0)
				minPeople = std::min(minPeople, visibleCount);
			const float maxDist = static_cast<float>(frame.cols) * tf->groupMaxDistFrac;
			if (tf->previewGroupClusters)
				sport_eyes_draw_group_clusters(frame, objects, minPeople, maxDist, tf->previewGroupClusterLabel);
		}


		cv::Mat finalMask;
		if (tf->maskingEnabled) {
			finalMask = cv::Mat::zeros(frame.size(), CV_8UC1);
			for (const Object &obj : objects) {
				cv::rectangle(finalMask, obj.rect, cv::Scalar(255), -1);
			}
			if (tf->maskingDilateIterations > 0) {
				cv::dilate(finalMask, finalMask, cv::Mat(), cv::Point(-1, -1),
					   tf->maskingDilateIterations);
			}
		}

		std::lock_guard<std::mutex> lock(tf->outputLock);
		if (tf->maskingEnabled) {
			finalMask.copyTo(tf->outputMask);
		}
		cv::cvtColor(frame, tf->outputPreviewBGRA, cv::COLOR_BGR2BGRA);
	}

	if (tf->trackingEnabled && tf->trackingFilter) {
		const int width = imageBGRA.cols;
		const int height = imageBGRA.rows;

		cv::Rect2f boundingBox = cv::Rect2f(0, 0, (float)width, (float)height);
		// Safe ROI (decision region): affects ONLY crop decision, not inference
		const RectI safe = make_safe_roi(width, height,
						 tf->safe_roi_left, tf->safe_roi_right,
						 tf->safe_roi_top, tf->safe_roi_bottom);

		std::vector<Object> safeObjects;
		safeObjects.reserve(objects.size());
		for (const Object &obj : objects) {
			if (obj.unseenFrames > 0)
				continue;
			if (!obj_center_in_safe(obj, safe))
				continue;
			safeObjects.push_back(obj);
		}

		bool finalGroupClusterValid = false;

		auto compute_bbox_from_vec = [&](const std::vector<Object> &objs, bool allowCache, cv::Rect2f &outBox, bool &outTrueGroupCluster) -> bool {
			outTrueGroupCluster = false;
			outBox = cv::Rect2f(0, 0, (float)width, (float)height);

			// Count visible (objs here are already visible)
			const int visibleCount = (int)objs.size();

			if (tf->zoomObject == "single") {
				if (visibleCount > 0) {
					outBox = objs.front().rect;
					return true;
				}
				return false;

			} else if (tf->zoomObject == "group") {

				cv::Rect2f groupBox;
				const float maxDist = static_cast<float>(width) * tf->groupMaxDistFrac; // configurable

				int minPeople = std::max(1, tf->groupMinPeople);
				if (!tf->groupMinPeopleStrict && visibleCount > 0)
					minPeople = std::min(minPeople, visibleCount);

				const uint64_t nowNs = os_gettime_ns();
				const uint64_t throttleNs = 100000000ULL; // 100 ms

				if (allowCache && tf->lastGroupBoxValid && (nowNs - tf->lastGroupBoxTsNs) < throttleNs) {
					outBox = tf->lastGroupBox;
					outTrueGroupCluster = tf->lastGroupBoxIsTrueCluster;
					return true;
				}

				if (visibleCount > 0) {
					GroupCluster best;
					if (sport_eyes_select_best_group_cluster(objs, minPeople, maxDist, best)) {

						// Cluster temporal inertia: avoid micro-switching between clusters
						// Sport veloce default: 150ms
						const float switchDistPx = std::max(20.0f, 0.03f * (float)width); // ~3% of width
						tf->cluster_inertia_pending = false;

						if (!tf->cluster_active_valid) {
							tf->cluster_active_box = best.box;
							tf->cluster_active_valid = true;
							tf->cluster_pending_since_ns = 0;
							tf->cluster_pending_box = cv::Rect2f(0, 0, 0, 0);
						} else {
							const float d = rect_center_dist(best.box, tf->cluster_active_box);
							if (d <= switchDistPx) {
								// same (or very close) cluster: update immediately
								tf->cluster_active_box = best.box;
								tf->cluster_pending_since_ns = 0;
								tf->cluster_pending_box = cv::Rect2f(0, 0, 0, 0);
							} else {
								// different cluster: wait inertia before switching
								if (tf->cluster_inertia_ms <= 0) {
									tf->cluster_active_box = best.box;
									tf->cluster_pending_since_ns = 0;
									tf->cluster_pending_box = cv::Rect2f(0, 0, 0, 0);
								} else {
									if (tf->cluster_pending_since_ns == 0) {
										tf->cluster_pending_since_ns = nowNs;
										tf->cluster_pending_box = best.box;
									} else {
										const uint64_t elapsedMs = (nowNs - tf->cluster_pending_since_ns) / 1000000ULL;

										// if pending target "jumps" during waiting, restart pending (avoid flip-flop)
										const float dp = rect_center_dist(tf->cluster_pending_box, best.box);
										if (dp > switchDistPx) {
											tf->cluster_pending_since_ns = nowNs;
											tf->cluster_pending_box = best.box;
										} else if ((int)elapsedMs >= tf->cluster_inertia_ms) {
											tf->cluster_active_box = tf->cluster_pending_box;
											tf->cluster_pending_since_ns = 0;
											tf->cluster_pending_box = cv::Rect2f(0, 0, 0, 0);
										} else {
											tf->cluster_inertia_pending = true;
										}
									}
								}
							}
						}
						outBox = tf->cluster_active_box;
						outTrueGroupCluster = true;

						if (allowCache) {
							tf->lastGroupBox = best.box;
							tf->lastGroupCount = best.count;
							tf->lastGroupBoxValid = true;
							tf->lastGroupBoxIsTrueCluster = true;
							tf->lastGroupBoxTsNs = nowNs;
						}
						return true;
					} else {
						// fallback: union of all visible objects
						bool first = true;
						for (const Object &obj : objs) {
							if (first) {
								groupBox = obj.rect;
								first = false;
							} else {
								groupBox |= obj.rect;
							}
						}
						if (!first) {
							outBox = groupBox;

							if (allowCache) {
								tf->lastGroupBox = groupBox;
								tf->lastGroupCount = visibleCount;
								tf->lastGroupBoxValid = true;
								tf->lastGroupBoxIsTrueCluster = false;
								tf->lastGroupBoxTsNs = nowNs;
							}
							outTrueGroupCluster = false;
							return true;
						}
					}
				}

				if (allowCache) {
					tf->lastGroupBoxValid = false;
					tf->lastGroupBoxIsTrueCluster = false;
				}

				return false;

			} else if (tf->zoomObject == "biggest") {

				float maxArea = 0.0f;
				bool found = false;
				for (const Object &obj : objs) {
					float area = obj.rect.area();
					if (area > maxArea) {
						maxArea = area;
						outBox = obj.rect;
						found = true;
					}
				}
				return found;

			} else if (tf->zoomObject == "oldest") {

				uint64_t oldestId = UINT64_MAX;
				bool found = false;
				for (const Object &obj : objs) {
					if (obj.id < oldestId) {
						oldestId = obj.id;
						outBox = obj.rect;
						found = true;
					}
				}
				return found;

			} else { // all

				if (!objs.empty()) {
					outBox = objs.front().rect;
					for (size_t i = 1; i < objs.size(); ++i)
						outBox |= objs[i].rect;
					return true;
				}
				return false;
			}
		};

		const uint64_t nowNs = os_gettime_ns();

		// 1) Try SAFE decision set
		cv::Rect2f safeBox;
		bool safeTrueGroupCluster = false;
		const bool safeOk = (!safeObjects.empty()) && compute_bbox_from_vec(safeObjects, false, safeBox, safeTrueGroupCluster) && rect_valid(safeBox);

		if (safeOk) {
			finalGroupClusterValid = safeTrueGroupCluster;
			boundingBox = safeBox;
			tf->safe_roi_last_good_bbox = safeBox;
			tf->safe_roi_decision_bbox = safeBox;
			tf->safe_roi_decision_from_safe = true;
			tf->safe_roi_decision_is_hold = false;
			tf->safe_roi_hold_until_ns = nowNs + (uint64_t)tf->safe_roi_hold_ms * 1000000ULL;
			tf->safe_roi_holding = false;
		} else if (rect_valid(tf->safe_roi_last_good_bbox) && nowNs < tf->safe_roi_hold_until_ns) {
			// 2) HOLD last safe bbox briefly
			boundingBox = tf->safe_roi_last_good_bbox;
			tf->safe_roi_holding = true;
			tf->safe_roi_decision_bbox = tf->safe_roi_last_good_bbox;
			tf->safe_roi_decision_from_safe = true;
			tf->safe_roi_decision_is_hold = true;
		} else {
			// 3) FALLBACK: compute from full visible set (existing behavior)
			std::vector<Object> visible;
			visible.reserve(objects.size());
			for (const Object &obj : objects) {
				if (obj.unseenFrames == 0)
					visible.push_back(obj);
			}

			cv::Rect2f fullBox;
			bool fullTrueGroupCluster = false;
			const bool fullOk = (!visible.empty()) && compute_bbox_from_vec(visible, true, fullBox, fullTrueGroupCluster) && rect_valid(fullBox);
			if (fullOk) {
				boundingBox = fullBox;
				finalGroupClusterValid = fullTrueGroupCluster;
			}
			// debug decision
			tf->safe_roi_decision_bbox = fullOk ? fullBox : cv::Rect2f(0,0,0,0);
			tf->safe_roi_decision_from_safe = false;
			tf->safe_roi_decision_is_hold = false;

			tf->safe_roi_holding = false;
		}

		bool lostTracking = true;
		for (const Object &obj : objects) {
			if (obj.unseenFrames == 0) {
				lostTracking = false;
				break;
			}
		}
                // the zooming box should maintain the aspect ratio of the image
                // with the tf->zoomFactor controlling the effective buffer around the bounding box
                // the bounding box is the center of the zooming box

// Maintain the aspect ratio of the image.
// Default behaviour: dynamic zoom box (old behaviour).
// For "group": horizontal pan only (no zoom), full height, fixed window width.
float frameAspectRatio = (float)width / (float)height;

float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
bool useTrackingScaleFilter = false;
int trackingScaleOutW = 0;
int trackingScaleOutH = 0;

std::string directorMeasurementState = "fallback_center";
if (tf->safe_roi_holding)
	directorMeasurementState = "safe_roi_hold";
else if (detectionsFresh && finalGroupClusterValid)
	directorMeasurementState = "fresh_detection";
else if (detectionsFresh)
	directorMeasurementState = "fallback_detection";
else if (tf->lastDirectorAIFrame.valid)
	directorMeasurementState = "prediction_coast";

if (tf->zoomObject == "group") {
	// Group mode has TWO separate concepts:
	// 1) zoom_factor = base 32:9 -> 16:9 pan window, normally 0.50 on a 7680x2160 source.
	// 2) groupEdgeZoomValue = real 2D zoom multiplier, only when a true group cluster exists.
	//
	// IMPORTANT FIX:
	// The extra 2D zoom must be applied INSIDE the current base 16:9 pan window.
	// Do not use the YOLO/group bbox vertical center to position Y: player boxes are not a stable
	// representation of the useful court area and can make OBS cut the floor/baseline.
	// Instead, keep Y anchored to the source frame with a small downward bias, so we crop a bit
	// more from the top than from the bottom and preserve the court base.
	float baseCoverage = tf->zoomFactor;
	if (baseCoverage <= 0.0f)
		baseCoverage = 1.0f;
	baseCoverage = std::clamp(baseCoverage, 0.05f, 1.0f);

	const float baseZw = std::clamp((float)width * baseCoverage, 1.0f, (float)width);
	const float baseZh = (float)height;

	useTrackingScaleFilter = true;
	trackingScaleOutW = std::max(1, (int)std::lround(baseZw));
	trackingScaleOutH = std::max(1, (int)std::lround(baseZh));
	const float baseMaxZX = std::max(0.0f, (float)width - baseZw);
	float targetCenterX = boundingBox.x + (boundingBox.width * 0.5f);

	if (tf->directorAIEnabled && tf->zoomObject == "group" && rect_valid(boundingBox)) {
		if (!tf->directorAIEngine)
			tf->directorAIEngine = std::make_unique<director_ai::DirectorAIEngine>();

		// A cached bbox remains useful for crop stability, but it must not be
		// injected again into the temporal model as a new camera observation.
		// A Safe ROI hold is also not a new player observation: feeding it into
		// Director AI would artificially preserve confidence and damp velocity.
		if (detectionsFresh && !tf->safe_roi_holding) {
			director_ai::IntegrationInput directorInput;
			directorInput.timestamp_ns = detectionCaptureNs ? detectionCaptureNs : nowInferNs;
			directorInput.action_bbox = boundingBox;
			directorInput.action_valid = true;
			directorInput.confidence = finalGroupClusterValid ? 1.0f : 0.65f;
			directorInput.frame_width = width;
			directorInput.frame_height = height;
			directorInput.base_coverage = baseCoverage;
			directorInput.deadband_px = (tf->x_deadband / 100.0f) * (float)width;
			directorInput.pipeline_latency_ms = (float)detectionAgeNs / 1000000.0f;

			const auto directorOut = director_ai::update_director_ai(
				*tf->directorAIEngine, tf->directorAIConfig, directorInput);
			if (directorOut.valid)
				tf->lastDirectorAIFrame = directorOut.frame;
		}

		if (tf->lastDirectorAIFrame.valid)
			targetCenterX = tf->lastDirectorAIFrame.prediction.predicted_center.x;
	}

	float baseZx = 0.0f;
	if (tf->x_pan_preset == "left") {
		baseZx = 0.0f;
	} else if (tf->x_pan_preset == "right") {
		baseZx = baseMaxZX;
	} else if (tf->x_pan_preset == "center") {
		baseZx = baseMaxZX * 0.5f;
	} else if (tf->x_pan_preset == "autosnap" || tf->x_pan_preset == "autosnap_smooth") {
		float norm = (width > 0) ? (targetCenterX / (float)width) : 0.5f;
		float h = std::clamp(tf->x_snap_hysteresis, 0.0f, 0.20f);
		const float t1 = 1.0f / 3.0f;
		const float t2 = 2.0f / 3.0f;

		const int prev_state = tf->x_snap_state;
		switch (tf->x_snap_state) {
		case 0:
			if (norm > t1 + h)
				tf->x_snap_state = 1;
			break;
		case 2:
			if (norm < t2 - h)
				tf->x_snap_state = 1;
			break;
		case 1:
		default:
			if (norm < t1 - h)
				tf->x_snap_state = 0;
			else if (norm > t2 + h)
				tf->x_snap_state = 2;
			break;
		}

		baseZx = (tf->x_snap_state == 0) ? 0.0f : (tf->x_snap_state == 2) ? baseMaxZX : (baseMaxZX * 0.5f);

		if (tf->x_pan_preset == "autosnap") {
			tf->trackVelX = 0.0f;
		} else if (tf->x_snap_state != prev_state) {
			tf->trackVelX = 0.0f;
		}
	} else {
		// Auto: pan the base 16:9 window on the group's X center.
		baseZx = targetCenterX - (baseZw * 0.5f);
	}
	baseZx = std::clamp(baseZx, 0.0f, baseMaxZX);

	float targetEdgeZoom = 1.0f;
	if (tf->groupEdgeZoomEnabled && finalGroupClusterValid && width > 0) {
		const float groupCenterX = boundingBox.x + boundingBox.width * 0.5f;
		const float normX = std::clamp(groupCenterX / (float)width, 0.0f, 1.0f);
		const float edge01 = std::clamp(std::fabs(normX * 2.0f - 1.0f), 0.0f, 1.0f);
		const float curved = std::pow(edge01, tf->groupEdgeZoomCurve);

		// Group Edge Zoom Min is the minimum real 2D zoom kept while a
		// valid group drives the crop. The existing Amount remains the
		// maximum at the outer edge. With Min = 1.00 the equation is
		// identical to the pre-v1.10.0o behavior.
		const float edgeZoomMax = std::clamp(tf->groupEdgeZoomAmount, 1.0f, 4.0f);
		const float edgeZoomMin = std::clamp(tf->groupEdgeZoomMin, 1.0f, edgeZoomMax);
		targetEdgeZoom = edgeZoomMin + ((edgeZoomMax - edgeZoomMin) * curved);
		targetEdgeZoom = std::clamp(targetEdgeZoom, 1.0f, 4.0f);
	}

	// Smooth the real zoom multiplier itself so the crop size never jumps when entering/leaving the sides.
	// targetEdgeZoom/value: 1.0 = no extra zoom, 4.0 = 4x real 2D zoom.
	tf->groupEdgeZoomValue = smooth_damp_critically_damped(tf->groupEdgeZoomValue, targetEdgeZoom,
								tf->groupEdgeZoomVel, tf->groupEdgeZoomSmooth, seconds);
	tf->groupEdgeZoomValue = std::clamp(tf->groupEdgeZoomValue, 1.0f, 4.0f);

	const float zoomScale = 1.0f / tf->groupEdgeZoomValue;
	zw = std::clamp(baseZw * zoomScale, 1.0f, baseZw);
	zh = std::clamp(baseZh * zoomScale, 1.0f, baseZh);

	// Extra zoom is centered horizontally inside the current 16:9 pan window.
	// This avoids changing the meaning of left/center/right end-stops while still producing real zoom.
	zx = baseZx + (baseZw - zw) * 0.5f;

	// Vertical zoom is NOT driven by YOLO bbox Y. Keep the court/floor stable.
	// 0.50 = crop top/bottom equally. 0.65 = crop more from top and preserve more bottom/floor.
	// Preserve the court/floor: crop mostly from the top, minimally from the bottom.
	const float verticalTopShare = 0.85f;
	zy = (baseZh - zh) * verticalTopShare;

	const float maxZX = std::max(0.0f, (float)width - zw);
	const float maxZY = std::max(0.0f, (float)height - zh);
	zx = std::clamp(zx, 0.0f, maxZX);
	zy = std::clamp(zy, 0.0f, maxZY);
} else {
        // calculate an aspect ratio box around the object using its height
        float boxHeight = boundingBox.height;
        // calculate the zooming box size
        float dh = (float)height - boxHeight;
        float buffer = dh * (1.0f - tf->zoomFactor);
        zh = boxHeight + buffer;
        zw = zh * frameAspectRatio;
        // calculate the top left corner of the zooming box
        zx = boundingBox.x - (zw - boundingBox.width) / 2.0f;
        zy = boundingBox.y - (zh - boundingBox.height) / 2.0f;
}
				// --- Optional X deadband (applies to ALL zoom_object modes) ---
			if (tf->x_deadband > 0.0f) {
				const float db_px = (tf->x_deadband / 100.0f) * (float)width;

				if (tf->has_last_target_zx) {
				const float dx = zx - tf->last_target_zx;
					if (std::fabs(dx) < db_px) {
						zx = tf->last_target_zx; // ignore micro jitter
					} else {
					tf->last_target_zx = zx;
					}
			} else {
					tf->last_target_zx = zx;
					tf->has_last_target_zx = true;
			}
}
                if (tf->trackingRect.width == 0) {
                        // initialize the trackingRect
                        tf->trackingRect = cv::Rect2f(zx, zy, zw, zh);
				tf->trackVelX = tf->trackVelY = tf->trackVelW = tf->trackVelH = 0.0f;
                } else {
                        // interpolate the zooming box to tf->trackingRect (frame-rate independent, low hysteresis)
                        const float alpha60 = tf->zoomSpeedFactor * (lostTracking ? 0.2f : 1.0f);

                        if (alpha60 <= 0.0f) {
                        	// frozen
                        } else if (alpha60 >= 1.0f) {
                        	// snap
                        	tf->trackingRect = cv::Rect2f(zx, zy, zw, zh);
				tf->trackVelX = tf->trackVelY = tf->trackVelW = tf->trackVelH = 0.0f;
                        	tf->trackVelX = tf->trackVelY = tf->trackVelW = tf->trackVelH = 0.0f;
                        } else {

	const float smoothTime = smooth_time_from_alpha60(alpha60);

	if (tf->zoomObject == "group") {
		float smoothTimeX = smoothTime;
		if (tf->x_pan_preset == "autosnap_smooth")
			smoothTimeX = std::clamp(tf->x_snap_transition_time, 0.05f, 1.0f);

		// Group mode now supports real 2D edge zoom: smooth X, Y, W and H.
		tf->trackingRect.x = smooth_damp_critically_damped(tf->trackingRect.x, zx, tf->trackVelX,
							   smoothTimeX, seconds);
		tf->trackingRect.y = smooth_damp_critically_damped(tf->trackingRect.y, zy, tf->trackVelY,
							   smoothTime, seconds);
		tf->trackingRect.width =
			smooth_damp_critically_damped(tf->trackingRect.width, zw, tf->trackVelW, smoothTime, seconds);
		tf->trackingRect.height =
			smooth_damp_critically_damped(tf->trackingRect.height, zh, tf->trackVelH, smoothTime, seconds);
	} else {
		tf->trackingRect.x = smooth_damp_critically_damped(tf->trackingRect.x, zx, tf->trackVelX,
							   smoothTime, seconds);
		tf->trackingRect.y = smooth_damp_critically_damped(tf->trackingRect.y, zy, tf->trackVelY,
							   smoothTime, seconds);
		tf->trackingRect.width =
			smooth_damp_critically_damped(tf->trackingRect.width, zw, tf->trackVelW, smoothTime, seconds);
		tf->trackingRect.height =
			smooth_damp_critically_damped(tf->trackingRect.height, zh, tf->trackVelH, smoothTime, seconds);
	}
}
}

                // get the settings of the crop/pad filter
			obs_data_t *crop_pad_settings = obs_source_get_settings(tf->trackingFilter);

			// Clamp to valid crop values to avoid negative crop/pad inputs
			const float x0 = std::max(0.0f, tf->trackingRect.x);
			const float y0 = std::max(0.0f, tf->trackingRect.y);
			const float x1 = std::min((float)width, tf->trackingRect.x + tf->trackingRect.width);
			const float y1 = std::min((float)height, tf->trackingRect.y + tf->trackingRect.height);

			const int left = (int)x0;
			const int top = (int)y0;
			const int right = (int)((float)width - x1);
			const int bottom = (int)((float)height - y1);

			obs_data_set_int(crop_pad_settings, "left", left);
			obs_data_set_int(crop_pad_settings, "top", top);
			obs_data_set_int(crop_pad_settings, "right", std::max(0, right));
			obs_data_set_int(crop_pad_settings, "bottom", std::max(0, bottom));

			// apply the crop settings
                obs_source_update(tf->trackingFilter, crop_pad_settings);
                obs_data_release(crop_pad_settings);

			// Group 2D edge zoom must scale the smaller crop back to the base
			// 16:9 window. Without this filter OBS only receives a smaller source,
			// which appears as cut sides/base instead of optical zoom.
			if (tf->trackingScaleFilter) {
				obs_source_set_enabled(tf->trackingScaleFilter, useTrackingScaleFilter);
				if (useTrackingScaleFilter && trackingScaleOutW > 0 && trackingScaleOutH > 0) {
					char resolution[64];
					snprintf(resolution, sizeof(resolution), "%dx%d", trackingScaleOutW, trackingScaleOutH);
					obs_data_t *scale_settings = obs_source_get_settings(tf->trackingScaleFilter);
					obs_data_set_string(scale_settings, "resolution", resolution);
					obs_source_update(tf->trackingScaleFilter, scale_settings);
					obs_data_release(scale_settings);
				}
			}

			SportEyesCsvSample csvSample;
			csvSample.timestampNs = os_gettime_ns();
			const uint64_t csvResultAgeNs =
				(detectionCaptureNs > 0 && csvSample.timestampNs >= detectionCaptureNs)
					? csvSample.timestampNs - detectionCaptureNs : 0ULL;
			const uint64_t csvCompletionAgeNs =
				(detectionCompletedNs > 0 && csvSample.timestampNs >= detectionCompletedNs)
					? csvSample.timestampNs - detectionCompletedNs : 0ULL;
			csvSample.frameWidth = width;
			csvSample.frameHeight = height;
			csvSample.objectCount = static_cast<int>(objects.size());
			for (const Object &object : objects) {
				if (object.unseenFrames == 0)
					++csvSample.visibleObjectCount;
			}
			csvSample.groupClusterValid = finalGroupClusterValid;
			csvSample.lostTracking = lostTracking;
			csvSample.safeRoiHolding = tf->safe_roi_holding;
			csvSample.actionBox = boundingBox;
			csvSample.cropBox = tf->trackingRect;
			csvSample.directorApplied = tf->directorAIEnabled && tf->lastDirectorAIFrame.valid;
			csvSample.asyncEnabled = tf->asyncInferenceEnabled;
			csvSample.resultFresh = detectionsFresh;
			csvSample.resultAgeMs = (float)csvResultAgeNs / 1000000.0f;
			csvSample.resultCompletionAgeMs = (float)csvCompletionAgeNs / 1000000.0f;
			csvSample.inferenceMs = detectionInferenceMs;
			csvSample.asyncWorkerBusy = asyncTelemetryAfter.workerBusy;
			csvSample.asyncTaskPending = asyncTelemetryAfter.taskPending;
			csvSample.asyncReplacedCount = asyncTelemetryAfter.replacedCount;
			csvSample.asyncResultOverwrittenCount = asyncTelemetryAfter.resultOverwrittenCount;
			csvSample.asyncSubmittedCount = asyncTelemetryAfter.submittedCount;
			csvSample.asyncCompletedCount = asyncTelemetryAfter.completedCount;
			csvSample.asyncPendingSequence = asyncTelemetryAfter.pendingSequence;
			csvSample.resultSequence = detectionSequence;
			csvSample.appliedSequence = detectionSequence;
			csvSample.directorMeasurementState = directorMeasurementState;
			csvSample.directorMeasurementAgeMs = (float)csvResultAgeNs / 1000000.0f;
			sport_eyes_write_csv_logs(tf, csvSample);
        }
}

