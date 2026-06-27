#include "WorldState.h"

#include <algorithm>
#include <cmath>

namespace director_ai {

void WorldState::reset()
{
	history_.clear();
	last_snapshot_ = WorldSnapshot();
}

void WorldState::configure(size_t max_samples, float fast_transition_speed_px_s)
{
	max_samples_ = std::max<size_t>(2, max_samples);
	fast_transition_speed_px_s_ = std::max(1.0f, fast_transition_speed_px_s);
	while (history_.size() > max_samples_)
		history_.pop_front();
}

WorldSnapshot WorldState::update(const ActionClusterState &cluster)
{
	if (!cluster.valid) {
		last_snapshot_.transition_probability *= 0.85f;
		last_snapshot_.valid = false;
		return last_snapshot_;
	}

	history_.push_back(cluster);
	while (history_.size() > max_samples_)
		history_.pop_front();

	last_snapshot_.action_cluster = cluster;
	last_snapshot_.transition_probability = estimate_transition_probability(cluster);
	last_snapshot_.direction = estimate_direction(cluster);
	last_snapshot_.valid = true;
	return last_snapshot_;
}

WorldSnapshot WorldState::snapshot() const
{
	return last_snapshot_;
}

size_t WorldState::sample_count() const
{
	return history_.size();
}

float WorldState::estimate_transition_probability(const ActionClusterState &cluster) const
{
	const float speed = std::sqrt(cluster.velocity_px_s.x * cluster.velocity_px_s.x +
				      cluster.velocity_px_s.y * cluster.velocity_px_s.y);
	const float speed01 = std::clamp(speed / fast_transition_speed_px_s_, 0.0f, 1.0f);

	// A compact cluster moving fast is more likely to represent a meaningful transition.
	const float dispersion_penalty = std::clamp(cluster.dispersion_px / 1200.0f, 0.0f, 0.4f);
	const float confidence = std::clamp(cluster.confidence, 0.0f, 1.0f);
	return std::clamp((speed01 * confidence) - dispersion_penalty, 0.0f, 1.0f);
}

int WorldState::estimate_direction(const ActionClusterState &cluster) const
{
	const float vx = cluster.velocity_px_s.x;
	if (std::fabs(vx) < 80.0f)
		return 0;
	return vx < 0.0f ? -1 : 1;
}

} // namespace director_ai
