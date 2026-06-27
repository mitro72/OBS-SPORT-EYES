#include "CameraPlanner.h"

#include <algorithm>
#include <cmath>

namespace director_ai {

void CameraPlanner::reset()
{
	last_plan_ = CameraPlan();
}

void CameraPlanner::configure(const CameraPlanConfig &config)
{
	config_ = config;
	config_.base_coverage = std::clamp(config_.base_coverage, config_.min_coverage, config_.max_coverage);
	config_.min_coverage = std::clamp(config_.min_coverage, 0.01f, 1.0f);
	config_.max_coverage = std::clamp(config_.max_coverage, config_.min_coverage, 1.0f);
	config_.deadband_px = std::max(0.0f, config_.deadband_px);
}

CameraPlan CameraPlanner::plan_from_prediction(const DirectorPrediction &prediction,
					       int frame_width,
					       int frame_height)
{
	CameraPlan plan;

	if (!prediction.valid || frame_width <= 0 || frame_height <= 0) {
		last_plan_ = plan;
		return last_plan_;
	}

	const float coverage = std::clamp(config_.base_coverage, config_.min_coverage, config_.max_coverage);
	const float crop_w = std::max(1.0f, (float)frame_width * coverage);
	const float crop_h = (float)frame_height;
	const float max_x = std::max(0.0f, (float)frame_width - crop_w);

	float target_x = prediction.predicted_center.x - crop_w * 0.5f;
	if (config_.clamp_to_frame)
		target_x = std::clamp(target_x, 0.0f, max_x);

	if (last_plan_.valid && config_.deadband_px > 0.0f) {
		const float dx = target_x - last_plan_.crop_rect.x;
		if (std::fabs(dx) < config_.deadband_px)
			target_x = last_plan_.crop_rect.x;
	}

	plan.crop_rect = cv::Rect2f(target_x, 0.0f, crop_w, crop_h);
	plan.target_center = prediction.predicted_center;
	plan.confidence = std::clamp(prediction.confidence, 0.0f, 1.0f);
	plan.valid = true;

	last_plan_ = plan;
	return last_plan_;
}

const CameraPlan &CameraPlanner::last_plan() const
{
	return last_plan_;
}

} // namespace director_ai
