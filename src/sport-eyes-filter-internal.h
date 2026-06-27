#pragma once

#include <obs-module.h>
#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#include <util/platform.h>
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>
#include <plugin-support.h>
#include "FilterData.h"
#include "consts.h"
#include "obs-utils/obs-utils.h"
#include "ort-model/utils.hpp"
#include "detect-filter-utils.h"
#include "models/OpenVINOAdapters.h"
#include "edgeyolo/coco_names.hpp"
#include "yunet/YuNetOpenVINO.h"
#include "director_ai/DirectorAIIntegration.h"

#define EXTERNAL_MODEL_SIZE "!!!EXTERNAL_MODEL!!!"
#define FACE_DETECT_MODEL_SIZE "!!!FACE_DETECT!!!"

#ifdef _WIN32
inline std::string sport_eyes_wide_to_utf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), size, nullptr, nullptr);
    return s;
}
#endif

inline float smooth_damp_critically_damped(float current, float target, float &currentVelocity,
                                           float smoothTime, float deltaTime)
{
    smoothTime = std::max(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * deltaTime;
    const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    return target + (change + temp) * exp;
}

inline float smooth_time_from_alpha60(float alpha_per_frame_60fps)
{
    const float a = std::clamp(alpha_per_frame_60fps, 0.0f, 0.999999f);
    if (a <= 0.0f)
        return 1000000.0f;
    const float dt_ref = 1.0f / 60.0f;
    const float k = -logf(1.0f - a) / dt_ref;
    return std::max(0.0001f, 2.0f / k);
}

struct RectI { int x, y, w, h; };
inline RectI make_safe_roi(int w, int h, int lPct, int rPct, int tPct, int bPct)
{
    const int l = (w * lPct) / 100;
    const int r = (w * rPct) / 100;
    const int t = (h * tPct) / 100;
    const int b = (h * bPct) / 100;
    return {l, t, std::max(1, w - l - r), std::max(1, h - t - b)};
}
inline bool point_in_rect(float px, float py, const RectI &r)
{
    return px >= (float)r.x && py >= (float)r.y && px < (float)(r.x + r.w) && py < (float)(r.y + r.h);
}
inline bool obj_center_in_safe(const Object &obj, const RectI &safe)
{
    const float cx = obj.rect.x + obj.rect.width * 0.5f;
    const float cy = obj.rect.y + obj.rect.height * 0.5f;
    return point_in_rect(cx, cy, safe);
}
inline bool rect_valid(const cv::Rect2f &r) { return r.width > 1.0f && r.height > 1.0f; }
inline float rect_center_dist(const cv::Rect2f &a, const cv::Rect2f &b)
{
    const float ax = a.x + a.width * 0.5f;
    const float ay = a.y + a.height * 0.5f;
    const float bx = b.x + b.width * 0.5f;
    const float by = b.y + b.height * 0.5f;
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

struct detect_filter : public filter_data {
    float trackVelX = 0.0f;
    float trackVelY = 0.0f;
    float trackVelW = 0.0f;
    float trackVelH = 0.0f;
    int groupMinPeople = 6;
    bool groupMinPeopleStrict = false;
    float groupMaxDistFrac = 0.15f;
    bool previewGroupClusters = false;
    bool previewGroupClusterLabel = false;
    bool groupEdgeZoomEnabled = false;
    float groupEdgeZoomAmount = 1.0f;
    float groupEdgeZoomCurve = 2.0f;
    float groupEdgeZoomSmooth = 0.60f;
    float groupEdgeZoomValue = 1.0f;
    float groupEdgeZoomVel = 0.0f;
    bool lastGroupBoxIsTrueCluster = false;
    obs_source_t *trackingScaleFilter = nullptr;
    int safe_roi_left = 10;
    int safe_roi_right = 10;
    int safe_roi_top = 0;
    int safe_roi_bottom = 8;
    int safe_roi_hold_ms = 300;
    int safe_roi_hold_timer_ms = 0;
    bool safe_roi_holding = false;
    uint64_t safe_roi_hold_until_ns = 0;
    cv::Rect2f safe_roi_last_good_bbox = cv::Rect2f(0, 0, 0, 0);
    cv::Rect2f safe_roi_decision_bbox = cv::Rect2f(0, 0, 0, 0);
    bool safe_roi_decision_from_safe = false;
    bool safe_roi_decision_is_hold = false;
    int cluster_inertia_ms = 150;
    uint64_t cluster_pending_since_ns = 0;
    cv::Rect2f cluster_pending_box = cv::Rect2f(0, 0, 0, 0);
    cv::Rect2f cluster_active_box = cv::Rect2f(0, 0, 0, 0);
    bool cluster_active_valid = false;
    bool cluster_inertia_pending = false;
    uint64_t lastGroupBoxTsNs = 0;
    cv::Rect2f lastGroupBox;
    int lastGroupCount = 0;
    bool lastGroupBoxValid = false;
    int infer_interval_ms = 0;
    float infer_scale = 1.0f;
    uint64_t last_infer_ts_ns = 0;
    bool cached_objects_valid = false;
    std::vector<Object> cached_objects;
    uint64_t cachedObjectsCaptureNs = 0;
    uint64_t cachedObjectsCompletedNs = 0;
    uint64_t cachedObjectsSequence = 0;
    float cachedObjectsInferenceMs = 0.0f;
    std::string x_pan_preset = "auto";
    int x_snap_state = 1;
    float x_snap_transition_time = 0.25f;
    float x_snap_hysteresis = 0.05f;
    float x_deadband = 0.0f;
    float last_target_zx = 0.0f;
    bool has_last_target_zx = false;
};

// Extracted components. They retain the current public callback names to keep OBS integration stable.
obs_properties_t *sport_eyes_filter_properties(void *data);
void sport_eyes_filter_defaults(obs_data_t *settings);
void sport_eyes_filter_update(void *data, obs_data_t *settings);
void *sport_eyes_filter_create(obs_data_t *settings, obs_source_t *source);
void sport_eyes_filter_destroy(void *data);
void sport_eyes_filter_activate(void *data);
void sport_eyes_filter_deactivate(void *data);
constexpr uint32_t TRACKING_SETUP_STARTUP_GRACE_TICKS = 15;
bool sport_eyes_sync_tracking_filters(detect_filter *filter, bool allowCreate);
void sport_eyes_filter_ensure_tracking_setup(void *data);
void sport_eyes_filter_video_tick(void *data, float seconds);
void sport_eyes_filter_video_render(void *data, gs_effect_t *_effect);

void sport_eyes_async_inference_start(struct detect_filter *tf);
void sport_eyes_async_inference_stop(struct detect_filter *tf);
void sport_eyes_async_inference_reset(struct detect_filter *tf);
bool sport_eyes_async_inference_submit(struct detect_filter *tf, const cv::Mat &bgr,
	float scale, uint64_t captureNs, const cv::Point &cropOrigin);
bool sport_eyes_async_inference_try_get(struct detect_filter *tf,
	std::vector<Object> &objectsOut, uint64_t &captureNsOut,
	uint64_t &completedNsOut, cv::Point &cropOriginOut,
	uint64_t &sequenceOut, float &inferenceMsOut);

struct SportEyesAsyncTelemetry {
	bool workerBusy = false;
	bool taskPending = false;
	uint64_t lastSubmitNs = 0;
	uint64_t submittedCount = 0;
	uint64_t completedCount = 0;
	uint64_t replacedCount = 0;
	uint64_t resultOverwrittenCount = 0;
	uint64_t pendingSequence = 0;
	uint64_t resultSequence = 0;
	float lastInferenceMs = 0.0f;
};

void sport_eyes_async_inference_snapshot(struct detect_filter *tf,
	SportEyesAsyncTelemetry &snapshotOut);

struct SportEyesCsvSample {
	uint64_t timestampNs = 0;
	int frameWidth = 0;
	int frameHeight = 0;
	int objectCount = 0;
	int visibleObjectCount = 0;
	bool groupClusterValid = false;
	bool lostTracking = false;
	bool safeRoiHolding = false;
	cv::Rect2f actionBox;
	cv::Rect2f cropBox;
	bool directorApplied = false;
	bool asyncEnabled = false;
	bool resultFresh = false;
	float resultAgeMs = 0.0f;
	float resultCompletionAgeMs = 0.0f;
	float inferenceMs = 0.0f;
	bool asyncWorkerBusy = false;
	bool asyncTaskPending = false;
	uint64_t asyncReplacedCount = 0;
	uint64_t asyncResultOverwrittenCount = 0;
	uint64_t asyncSubmittedCount = 0;
	uint64_t asyncCompletedCount = 0;
	uint64_t asyncPendingSequence = 0;
	uint64_t resultSequence = 0;
	uint64_t appliedSequence = 0;
	std::string directorMeasurementState = "fallback_center";
	float directorMeasurementAgeMs = 0.0f;
};

void sport_eyes_csv_logging_reconfigure(struct detect_filter *tf, bool enabled,
	const std::string &diagnosticsPath, const std::string &directorPath);
void sport_eyes_csv_logging_close(struct detect_filter *tf);
void sport_eyes_write_csv_logs(struct detect_filter *tf, const SportEyesCsvSample &sample);

bool sport_eyes_build_group_bbox(const std::vector<Object> &objects, cv::Rect2f &outBox,
                                 int minPeople, float maxDist);

// A connected component of visible player detections. Kept in the public
// sport-facing header because the crop pipeline needs both its bounds and count.
struct GroupCluster {
    cv::Rect2f box;
    int count = 0;
};

bool sport_eyes_select_best_group_cluster(const std::vector<Object> &objects,
                                          int minPeople, float maxDist,
                                          GroupCluster &bestOut);
void sport_eyes_draw_group_clusters(cv::Mat &frame, const std::vector<Object> &objects,
                                    int minPeople, float maxDist, bool showLabel);
