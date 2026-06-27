#ifndef EDGEYOLO_TYPES_HPP
#define EDGEYOLO_TYPES_HPP

#include <opencv2/core/types.hpp>
#include <opencv2/video/tracking.hpp>

#ifdef _WIN32
#define file_name_t std::wstring
#else
#define file_name_t std::string
#endif

struct Object {
	// Detector backends construct Object directly before SORT assigns identity/state.
	// Initialise every scalar so cached raw detections remain safe to inspect between
	// inference results (particularly when "Hide unseen objects" is enabled).
	cv::Rect_<float> rect{};
	int label = -1;
	float prob = 0.0f;
	uint64_t id = 0;
	uint64_t unseenFrames = 0;
	cv::KalmanFilter kf;
};

struct GridAndStride {
	int grid0;
	int grid1;
	int stride;
};

#endif
