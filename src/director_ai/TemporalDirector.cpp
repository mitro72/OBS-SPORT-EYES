#include "TemporalDirector.h"

#include <algorithm>
#include <cmath>

namespace director_ai {
namespace {

static bool rect_valid(const cv::Rect2f &r)
{
	return r.width > 1.0f && r.height > 1.0f;
}

static cv::Point2f rect_center(const cv::Rect2f &r)
{
	return cv::Point2f(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
}

static cv::Point2f lerp_point(const cv::Point2f &a, const cv::Point2f &b, float alpha)
{
	return cv::Point2f(a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha);
}

static cv::Rect2f move_rect_center_to(const cv::Rect2f &r, const cv::Point2f &center)
{
	return cv::Rect2f(center.x - r.width * 0.5f,
			  center.y - r.height * 0.5f,
			  r.width,
			  r.height);
}

} // namespace

void TemporalDirector::reset()
{
	samples_.clear();
	last_sample_ = DirectorSample();
	last_prediction_ = DirectorPrediction();
}

void TemporalDirector::configure(float horizon_ms, float ema_alpha, size_t max_samples)
{
	horizon_s_ = std::clamp(horizon_ms * 0.001f, 0.0f, 1.0f);
	ema_alpha_ = std::clamp(ema_alpha, 0.01f, 1.0f);
	max_samples_ = std::max<size_t>(2, max_samples);
	while (samples_.size() > max_samples_)
		samples_.pop_front();
}

DirectorPrediction TemporalDirector::update(uint64_t timestamp_ns, const cv::Rect2f &bbox, bool valid, float confidence)
{
	DirectorSample sample;
	sample.timestamp_ns = timestamp_ns;
	sample.bbox = bbox;
	sample.center = rect_center(bbox);
	sample.confidence = std::clamp(confidence, 0.0f, 1.0f);
	sample.valid = valid && rect_valid(bbox);

	if (!sample.valid) {
		last_prediction_.confidence *= 0.85f;
		last_prediction_.valid = last_prediction_.confidence > 0.05f && rect_valid(last_prediction_.predicted_bbox);
		return last_prediction_;
	}

	if (!samples_.empty() && samples_.back().valid) {
		const DirectorSample &prev = samples_.back();
		const double dt_s = std::max(0.001, (double)(sample.timestamp_ns - prev.timestamp_ns) / 1000000000.0);
		const cv::Point2f measured_velocity(
			(sample.center.x - prev.center.x) / (float)dt_s,
			(sample.center.y - prev.center.y) / (float)dt_s);

		sample.velocity_px_s = lerp_point(prev.velocity_px_s, measured_velocity, ema_alpha_);
		sample.acceleration_px_s2 = cv::Point2f(
			(sample.velocity_px_s.x - prev.velocity_px_s.x) / (float)dt_s,
			(sample.velocity_px_s.y - prev.velocity_px_s.y) / (float)dt_s);
	} else {
		sample.velocity_px_s = cv::Point2f(0.0f, 0.0f);
		sample.acceleration_px_s2 = cv::Point2f(0.0f, 0.0f);
	}

	samples_.push_back(sample);
	while (samples_.size() > max_samples_)
		samples_.pop_front();

	last_sample_ = sample;

	cv::Point2f predicted_center = sample.center;
	predicted_center.x += sample.velocity_px_s.x * horizon_s_ + 0.5f * sample.acceleration_px_s2.x * horizon_s_ * horizon_s_;
	predicted_center.y += sample.velocity_px_s.y * horizon_s_ + 0.5f * sample.acceleration_px_s2.y * horizon_s_ * horizon_s_;

	last_prediction_.predicted_center = predicted_center;
	last_prediction_.predicted_bbox = move_rect_center_to(sample.bbox, predicted_center);
	last_prediction_.velocity_px_s = sample.velocity_px_s;
	last_prediction_.confidence = sample.confidence;
	last_prediction_.valid = true;

	return last_prediction_;
}

DirectorPrediction TemporalDirector::predict() const
{
	return last_prediction_;
}

const DirectorSample &TemporalDirector::last_sample() const
{
	return last_sample_;
}

size_t TemporalDirector::sample_count() const
{
	return samples_.size();
}

} // namespace director_ai
