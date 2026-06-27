#pragma once

#include "DirectorAIEngine.h"

#include <opencv2/core.hpp>

#include <cstdint>

namespace director_ai {

struct IntegrationInput {
	uint64_t timestamp_ns = 0;
	cv::Rect2f action_bbox = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	bool action_valid = false;
	float confidence = 1.0f;
	int frame_width = 0;
	int frame_height = 0;
	float base_coverage = 0.50f;
	float deadband_px = 0.0f;
	// Time elapsed between source-frame capture and the current video tick.
	float pipeline_latency_ms = 0.0f;
};

struct IntegrationOutput {
	cv::Rect2f crop_rect = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	DirectorAIFrame frame;
	bool valid = false;
};

IntegrationOutput update_director_ai(DirectorAIEngine &engine,
					    const DirectorAIConfig &base_config,
					    const IntegrationInput &input);

} // namespace director_ai
