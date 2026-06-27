#include "DirectorAIEngine.h"

#include <algorithm>
#include <cmath>

namespace director_ai {
namespace {

static cv::Point2f rect_center(const cv::Rect2f &r)
{
	return cv::Point2f(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
}

static float rect_dispersion(const cv::Rect2f &r)
{
	return std::sqrt(std::max(0.0f, r.width * r.width + r.height * r.height));
}

static float point_distance(const cv::Point2f &a, const cv::Point2f &b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}

static cv::Point2f clamp_point_lead(const cv::Point2f &origin,
				    const cv::Point2f &target,
				    float max_lead_px,
				    bool &clamped)
{
	clamped = false;
	if (max_lead_px <= 0.0f)
		return target;

	const float dx = target.x - origin.x;
	const float dy = target.y - origin.y;
	const float dist = std::sqrt(dx * dx + dy * dy);
	if (dist <= max_lead_px || dist <= 0.0001f)
		return target;

	const float scale = max_lead_px / dist;
	clamped = true;
	return cv::Point2f(origin.x + dx * scale, origin.y + dy * scale);
}

static cv::Rect2f move_rect_center_to(const cv::Rect2f &r, const cv::Point2f &center)
{
	return cv::Rect2f(center.x - r.width * 0.5f,
			  center.y - r.height * 0.5f,
			  r.width,
			  r.height);
}

} // namespace

DirectorAIEngine::DirectorAIEngine()
{
	configure(config_);
}

void DirectorAIEngine::reset()
{
	temporal_.reset();
	world_.reset();
	camera_.reset();
	court_.reset();
	last_frame_ = DirectorAIFrame();
}

void DirectorAIEngine::configure(const DirectorAIConfig &config)
{
	config_ = config;

	temporal_.configure(config_.prediction_horizon_ms,
			    config_.velocity_ema_alpha,
			    config_.history_samples);

	world_.configure(config_.history_samples,
			 config_.fast_transition_speed_px_s);

	CameraPlanConfig camera_cfg;
	camera_cfg.base_coverage = config_.base_coverage;
	camera_cfg.deadband_px = config_.deadband_px;
	camera_.configure(camera_cfg);
}

DirectorAIFrame DirectorAIEngine::update(uint64_t timestamp_ns,
					 const cv::Rect2f &action_bbox,
					 bool action_valid,
					 float confidence,
					 int frame_width,
					 int frame_height)
{
	DirectorAIFrame frame;

	CourtConfig court_cfg;
	court_cfg.frame_width = frame_width;
	court_cfg.frame_height = frame_height;
	court_.configure(court_cfg);

	const cv::Point2f current_center = rect_center(action_bbox);
	frame.prediction = temporal_.update(timestamp_ns, action_bbox, action_valid, confidence);

	frame.diagnostics.current_center = current_center;
	frame.diagnostics.confidence_gate_passed = frame.prediction.confidence >= config_.min_confidence_to_apply;

	if (frame.prediction.valid) {
		bool clamped = false;
		const cv::Point2f unclamped_center = frame.prediction.predicted_center;
		frame.prediction.predicted_center = clamp_point_lead(current_center,
							      frame.prediction.predicted_center,
							      config_.max_prediction_lead_px,
							      clamped);
		frame.prediction.predicted_bbox = move_rect_center_to(frame.prediction.predicted_bbox,
									  frame.prediction.predicted_center);
		frame.diagnostics.prediction_clamped = clamped;
		frame.diagnostics.prediction_lead_px = point_distance(current_center, frame.prediction.predicted_center);
		if (!frame.diagnostics.confidence_gate_passed)
			frame.prediction.valid = false;
		(void)unclamped_center;
	}

	ActionClusterState cluster;
	cluster.timestamp_ns = timestamp_ns;
	cluster.bbox = action_bbox;
	cluster.center_px = current_center;
	cluster.velocity_px_s = frame.prediction.velocity_px_s;
	cluster.dispersion_px = rect_dispersion(action_bbox);
	cluster.confidence = std::clamp(confidence, 0.0f, 1.0f);
	cluster.valid = action_valid;

	frame.world = world_.update(cluster);
	frame.camera = camera_.plan_from_prediction(frame.prediction, frame_width, frame_height);
	frame.court_point = court_.pixel_to_court(frame.prediction.predicted_center);
	frame.court_zone = court_.zone_for_point(frame.prediction.predicted_center);
	frame.valid = frame.prediction.valid && frame.camera.valid;

	last_frame_ = frame;
	return last_frame_;
}

DirectorAIFrame DirectorAIEngine::last_frame() const
{
	return last_frame_;
}

} // namespace director_ai
