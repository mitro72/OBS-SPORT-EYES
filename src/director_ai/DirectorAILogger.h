#pragma once

#include "DirectorAIEngine.h"

#include <string>

namespace director_ai {

struct DirectorAILogRow {
	uint64_t timestamp_ns = 0;
	DirectorAIFrame frame;
	bool applied_to_crop = false;
	std::string measurement_state = "fallback_center";
	float measurement_age_ms = 0.0f;
	bool result_fresh = false;
	uint64_t result_sequence = 0;
	float async_inference_ms = 0.0f;
	bool async_worker_busy = false;
	uint64_t pending_replaced_count = 0;
};

std::string director_ai_csv_header();
std::string director_ai_csv_row(const DirectorAILogRow &row);

} // namespace director_ai
