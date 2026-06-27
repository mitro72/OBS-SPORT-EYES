#pragma once

#include "TemporalDirector.h"

#include <opencv2/core.hpp>

namespace director_ai {

struct CameraPlanConfig {
	float base_coverage = 0.50f;
	float min_coverage = 0.05f;
	float max_coverage = 1.00f;
	float deadband_px = 0.0f;
	bool clamp_to_frame = true;
};

struct CameraPlan {
	cv::Rect2f crop_rect = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	cv::Point2f target_center = cv::Point2f(0.0f, 0.0f);
	float confidence = 0.0f;
	bool valid = false;
};

class CameraPlanner {
public:
	CameraPlanner() = default;

	void reset();
	void configure(const CameraPlanConfig &config);
	CameraPlan plan_from_prediction(const DirectorPrediction &prediction,
					 int frame_width,
					 int frame_height);

	const CameraPlan &last_plan() const;

private:
	CameraPlanConfig config_;
	CameraPlan last_plan_;
};

} // namespace director_ai
