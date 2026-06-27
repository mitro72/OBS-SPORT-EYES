#include "CourtModel.h"

#include <algorithm>

namespace director_ai {

void CourtModel::reset()
{
	config_ = CourtConfig();
	configured_ = false;
}

void CourtModel::configure(const CourtConfig &config)
{
	config_ = config;
	config_.left_margin_norm = std::clamp(config_.left_margin_norm, 0.0f, 0.45f);
	config_.right_margin_norm = std::clamp(config_.right_margin_norm, 0.0f, 0.45f);
	config_.top_margin_norm = std::clamp(config_.top_margin_norm, 0.0f, 0.45f);
	config_.bottom_margin_norm = std::clamp(config_.bottom_margin_norm, 0.0f, 0.45f);
	configured_ = config_.frame_width > 0 && config_.frame_height > 0;
}

CourtPoint CourtModel::pixel_to_court(const cv::Point2f &point_px) const
{
	CourtPoint out;
	if (!configured_)
		return out;

	const float usable_left = config_.left_margin_norm * (float)config_.frame_width;
	const float usable_right = (1.0f - config_.right_margin_norm) * (float)config_.frame_width;
	const float usable_top = config_.top_margin_norm * (float)config_.frame_height;
	const float usable_bottom = (1.0f - config_.bottom_margin_norm) * (float)config_.frame_height;
	const float usable_w = std::max(1.0f, usable_right - usable_left);
	const float usable_h = std::max(1.0f, usable_bottom - usable_top);

	out.x_norm = std::clamp((point_px.x - usable_left) / usable_w, 0.0f, 1.0f);
	out.y_norm = std::clamp((point_px.y - usable_top) / usable_h, 0.0f, 1.0f);
	out.valid = true;
	return out;
}

CourtZone CourtModel::zone_for_point(const cv::Point2f &point_px) const
{
	CourtZone zone;
	const CourtPoint p = pixel_to_court(point_px);
	if (!p.valid)
		return zone;

	if (p.x_norm < 1.0f / 3.0f)
		zone.horizontal_third = 0;
	else if (p.x_norm > 2.0f / 3.0f)
		zone.horizontal_third = 2;
	else
		zone.horizontal_third = 1;

	zone.near_left_edge = p.x_norm < 0.12f;
	zone.near_right_edge = p.x_norm > 0.88f;
	zone.valid = true;
	return zone;
}

bool CourtModel::configured() const
{
	return configured_;
}

} // namespace director_ai
