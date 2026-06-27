#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <deque>

namespace director_ai {

struct DirectorSample {
	uint64_t timestamp_ns = 0;
	cv::Rect2f bbox = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	cv::Point2f center = cv::Point2f(0.0f, 0.0f);
	cv::Point2f velocity_px_s = cv::Point2f(0.0f, 0.0f);
	cv::Point2f acceleration_px_s2 = cv::Point2f(0.0f, 0.0f);
	float confidence = 0.0f;
	bool valid = false;
};

struct DirectorPrediction {
	cv::Rect2f predicted_bbox = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	cv::Point2f predicted_center = cv::Point2f(0.0f, 0.0f);
	cv::Point2f velocity_px_s = cv::Point2f(0.0f, 0.0f);
	float confidence = 0.0f;
	bool valid = false;
};

class TemporalDirector {
public:
	TemporalDirector() = default;

	void reset();
	void configure(float horizon_ms, float ema_alpha, size_t max_samples);
	DirectorPrediction update(uint64_t timestamp_ns, const cv::Rect2f &bbox, bool valid, float confidence);
	DirectorPrediction predict() const;

	const DirectorSample &last_sample() const;
	size_t sample_count() const;

private:
	float horizon_s_ = 0.30f;
	float ema_alpha_ = 0.35f;
	size_t max_samples_ = 90;
	std::deque<DirectorSample> samples_;
	DirectorSample last_sample_;
	DirectorPrediction last_prediction_;
};

} // namespace director_ai
