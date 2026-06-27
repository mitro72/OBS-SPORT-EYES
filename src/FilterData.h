#ifndef FILTERDATA_H
#define FILTERDATA_H

#include <obs-module.h>
#include "ort-model/types.hpp"
#include "sort/Sort.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <fstream>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "models/IDetectorModel.h"
#include "director_ai/DirectorAIEngine.h"


/**
  * @brief The filter_data struct
  *
  * This struct is used to store the base data needed for ORT filters.
  *
*/
struct filter_data {
	std::string useGPU;
	uint32_t numThreads;
	float conf_threshold;
	std::string modelSize;

	int minAreaThreshold;
	int objectCategory;
	bool maskingEnabled;
	std::string maskingType;
	int maskingColor;
	int maskingBlurRadius;
	int maskingDilateIterations;
	bool trackingEnabled;
	float zoomFactor;
	float zoomSpeedFactor;
	std::string zoomObject;
	obs_source_t *trackingFilter;
	// Tracking configuration can be loaded before OBS attaches this filter to its parent.
	// Keep this flag until Crop/Scale filters are actually attached.
	bool trackingSetupPending = false;
	// Delay helper creation briefly while OBS restores serialized source filters.
	uint32_t trackingSetupGraceTicks = 0;
	cv::Rect2f trackingRect;
	int lastDetectedObjectId;
	bool sortTracking;
	bool showUnseenObjects;
	std::string saveDetectionsPath;
	bool crop_enabled;
	int crop_left;
	int crop_right;
	int crop_top;
	int crop_bottom;

	// create SORT tracker
	Sort tracker;

	obs_source_t *source;
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurface;
	gs_effect_t *kawaseBlurEffect;
	gs_effect_t *maskingEffect;
	gs_effect_t *pixelateEffect;

	cv::Mat inputBGRA;
	cv::Mat outputPreviewBGRA;
	cv::Mat outputMask;

	bool isDisabled;
	bool preview;

	std::mutex inputBGRALock;
	std::mutex outputLock;
	std::mutex modelMutex;

	// Latest-frame asynchronous inference: at most one pending frame is kept.
	bool asyncInferenceEnabled = true;
	std::mutex asyncInferenceMutex;
	std::condition_variable asyncInferenceCv;
	std::thread asyncInferenceThread;
	bool asyncInferenceStop = false;
	bool asyncInferenceWorkerBusy = false;
	bool asyncInferenceTaskPending = false;
	bool asyncInferenceResultReady = false;
	cv::Mat asyncInferencePendingFrame;
	float asyncInferencePendingScale = 1.0f;
	uint64_t asyncInferencePendingCaptureNs = 0;
	cv::Point asyncInferencePendingCropOrigin = cv::Point(0, 0);
	std::vector<Object> asyncInferenceResultObjects;
	uint64_t asyncInferenceResultCaptureNs = 0;
	uint64_t asyncInferenceResultCompletedNs = 0;
	cv::Point asyncInferenceResultCropOrigin = cv::Point(0, 0);
	uint64_t asyncInferenceLastSubmitNs = 0;
	uint64_t asyncInferenceGeneration = 0;
	uint64_t asyncInferencePendingGeneration = 0;
	// Monotonic task sequence numbers make it possible to distinguish a fresh
	// detection from a reused/cached result in diagnostics.
	uint64_t asyncInferenceNextSequence = 0;
	uint64_t asyncInferencePendingSequence = 0;
	uint64_t asyncInferenceResultSequence = 0;
	float asyncInferenceLastMs = 0.0f;
	uint64_t asyncInferenceSubmitted = 0;
	uint64_t asyncInferenceCompleted = 0;
	uint64_t asyncInferenceReplaced = 0;
	uint64_t asyncInferenceResultOverwritten = 0;
	// openvino
	std::unique_ptr<IDetectorModel> model;
	//std::unique_ptr<ONNXRuntimeModel> onnxruntimemodel;
	std::vector<std::string> classNames;

	// Director AI v2 alpha state. Kept disabled by default until detect-filter.cpp wires it in.
	bool directorAIEnabled = false;
	director_ai::DirectorAIConfig directorAIConfig;
	std::unique_ptr<director_ai::DirectorAIEngine> directorAIEngine;
	director_ai::DirectorAIFrame lastDirectorAIFrame;

	// Optional operator CSV diagnostics. Both streams are owned by the filter
	// instance and are written from the OBS video tick only.
	bool csvLoggingEnabled = false;
	std::string diagnosticsCsvPath;
	std::string directorCsvPath;
	std::ofstream diagnosticsCsv;
	std::ofstream directorCsv;
	uint64_t csvLastWriteNs = 0;
	bool diagnosticsCsvHeaderWritten = false;
	bool directorCsvHeaderWritten = false;
	bool diagnosticsCsvOpenErrorLogged = false;
	bool directorCsvOpenErrorLogged = false;

	// Profile library UI status. Stored in memory so transient success/error messages
	// do not become part of an exported configuration.
	std::string profileStatus;

#if _WIN32
	std::wstring modelFilepath;
#else
	std::string modelFilepath;
#endif
};

#endif /* FILTERDATA_H */
