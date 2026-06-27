#pragma once

#include <opencv2/core.hpp>

namespace director_ai {

struct CourtConfig {
	int frame_width = 0;
	int frame_height = 0;
	float left_margin_norm = 0.0f;
	float right_margin_norm = 0.0f;
	float top_margin_norm = 0.0f;
	float bottom_margin_norm = 0.0f;
};

struct CourtPoint {
	float x_norm = 0.5f;
	float y_norm = 0.5f;
	bool valid = false;
};

struct CourtZone {
	int horizontal_third = 1; // 0 left, 1 middle, 2 right
	bool near_left_edge = false;
	bool near_right_edge = false;
	bool valid = false;
};

class CourtModel {
public:
	CourtModel() = default;

	void reset();
	void configure(const CourtConfig &config);
	CourtPoint pixel_to_court(const cv::Point2f &point_px) const;
	CourtZone zone_for_point(const cv::Point2f &point_px) const;
	bool configured() const;

private:
	CourtConfig config_;
	bool configured_ = false;
};

} // namespace director_ai
