#pragma once

#include "DirectorAIEngine.h"

#include <string>

namespace director_ai {

struct DirectorAILogRow {
	uint64_t timestamp_ns = 0;
	DirectorAIFrame frame;
	bool applied_to_crop = false;
};

std::string director_ai_csv_header();
std::string director_ai_csv_row(const DirectorAILogRow &row);

} // namespace director_ai
