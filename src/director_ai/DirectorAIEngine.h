#pragma once

#include "CameraPlanner.h"
#include "CourtModel.h"
#include "TemporalDirector.h"
#include "WorldState.h"

#include <opencv2/core.hpp>

#include <cstdint>

namespace director_ai {

struct DirectorAIConfig {
	float prediction_horizon_ms = 300.0f;
	float velocity_ema_alpha = 0.35f;
	size_t history_samples = 90;
	float base_coverage = 0.50f;
	float deadband_px = 0.0f;
	float fast_transition_speed_px_s = 1200.0f;

	// Safety guardrails for alpha rollout.
	// Positive values limit how far prediction may move from current center.
	float max_prediction_lead_px = 480.0f;
	float min_confidence_to_apply = 0.05f;
};

struct DirectorAIDiagnostics {
	cv::Point2f current_center = cv::Point2f(0.0f, 0.0f);
	float prediction_lead_px = 0.0f;
	bool prediction_clamped = false;
	bool confidence_gate_passed = false;
};

struct DirectorAIFrame {
	DirectorPrediction prediction;
	WorldSnapshot world;
	CameraPlan camera;
	CourtPoint court_point;
	CourtZone court_zone;
	DirectorAIDiagnostics diagnostics;
	bool valid = false;
};

class DirectorAIEngine {
public:
	DirectorAIEngine();

	void reset();
	void configure(const DirectorAIConfig &config);
	DirectorAIFrame update(uint64_t timestamp_ns,
			       const cv::Rect2f &action_bbox,
			       bool action_valid,
			       float confidence,
			       int frame_width,
			       int frame_height);

	DirectorAIFrame last_frame() const;

private:
	DirectorAIConfig config_;
	TemporalDirector temporal_;
	WorldState world_;
	CameraPlanner camera_;
	CourtModel court_;
	DirectorAIFrame last_frame_;
};

} // namespace director_ai
