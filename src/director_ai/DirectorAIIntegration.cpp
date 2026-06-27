#include "DirectorAIIntegration.h"

#include <algorithm>
#include <exception>

namespace director_ai {
namespace {

static bool rect_valid(const cv::Rect2f &r)
{
	return r.width > 1.0f && r.height > 1.0f;
}

} // namespace

IntegrationOutput update_director_ai(DirectorAIEngine &engine,
					    const DirectorAIConfig &base_config,
					    const IntegrationInput &input)
{
	IntegrationOutput out;

	if (!input.action_valid || !rect_valid(input.action_bbox) || input.frame_width <= 0 || input.frame_height <= 0)
		return out;

	try {
		DirectorAIConfig cfg = base_config;
		cfg.base_coverage = std::clamp(input.base_coverage, 0.05f, 1.0f);
		cfg.deadband_px = std::max(0.0f, input.deadband_px);
		// Director timestamps are capture timestamps. Compensate real pipeline age
		// so the configured look-ahead is relative to what the viewer sees now.
		cfg.prediction_horizon_ms = std::clamp(
			cfg.prediction_horizon_ms + std::max(0.0f, input.pipeline_latency_ms),
			0.0f, 900.0f);
		engine.configure(cfg);

		out.frame = engine.update(input.timestamp_ns,
					  input.action_bbox,
					  input.action_valid,
					  input.confidence,
					  input.frame_width,
					  input.frame_height);
		out.valid = out.frame.valid && out.frame.camera.valid;
		out.crop_rect = out.frame.camera.crop_rect;
	} catch (const std::exception &) {
		out = IntegrationOutput();
	}

	return out;
}

} // namespace director_ai
