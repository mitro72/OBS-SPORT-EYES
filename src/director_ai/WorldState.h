#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <deque>

namespace director_ai {

struct ActionClusterState {
	uint64_t timestamp_ns = 0;
	cv::Rect2f bbox = cv::Rect2f(0.0f, 0.0f, 0.0f, 0.0f);
	cv::Point2f center_px = cv::Point2f(0.0f, 0.0f);
	cv::Point2f velocity_px_s = cv::Point2f(0.0f, 0.0f);
	float dispersion_px = 0.0f;
	float confidence = 0.0f;
	bool valid = false;
};

struct WorldSnapshot {
	ActionClusterState action_cluster;
	float transition_probability = 0.0f;
	int direction = 0; // -1 left, 0 neutral, +1 right
	bool valid = false;
};

class WorldState {
public:
	WorldState() = default;

	void reset();
	void configure(size_t max_samples, float fast_transition_speed_px_s);
	WorldSnapshot update(const ActionClusterState &cluster);
	WorldSnapshot snapshot() const;
	size_t sample_count() const;

private:
	float estimate_transition_probability(const ActionClusterState &cluster) const;
	int estimate_direction(const ActionClusterState &cluster) const;

	size_t max_samples_ = 90;
	float fast_transition_speed_px_s_ = 1200.0f;
	std::deque<ActionClusterState> history_;
	WorldSnapshot last_snapshot_;
};

} // namespace director_ai
