#include "sport-eyes-filter-internal.h"

namespace {
void worker(detect_filter *tf)
{
	for (;;) {
		cv::Mat frame;
		float scale = 1.0f;
		uint64_t captureNs = 0;
		uint64_t generation = 0;
		cv::Point cropOrigin(0, 0);
		{
			std::unique_lock<std::mutex> lock(tf->asyncInferenceMutex);
			tf->asyncInferenceCv.wait(lock, [tf] {
				return tf->asyncInferenceStop || tf->asyncInferenceTaskPending;
			});
			if (tf->asyncInferenceStop)
				return;
			frame = std::move(tf->asyncInferencePendingFrame);
			scale = tf->asyncInferencePendingScale;
			captureNs = tf->asyncInferencePendingCaptureNs;
			generation = tf->asyncInferencePendingGeneration;
			cropOrigin = tf->asyncInferencePendingCropOrigin;
			tf->asyncInferenceTaskPending = false;
			tf->asyncInferenceWorkerBusy = true;
		}

		std::vector<Object> objects;
		const uint64_t startNs = os_gettime_ns();
		try {
			std::unique_lock<std::mutex> lock(tf->modelMutex);
			if (tf->model)
				objects = tf->model->inference(frame);
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "OBS Sport Eyes async inference failed: %s", e.what());
		}
		const uint64_t completedNs = os_gettime_ns();

		if (scale < 0.999f && scale > 0.0f) {
			const float inv = 1.0f / scale;
			for (Object &obj : objects) {
				obj.rect.x *= inv;
				obj.rect.y *= inv;
				obj.rect.width *= inv;
				obj.rect.height *= inv;
			}
		}

		std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
		tf->asyncInferenceWorkerBusy = false;
		if (!tf->asyncInferenceStop && generation == tf->asyncInferenceGeneration) {
			tf->asyncInferenceResultObjects = std::move(objects);
			tf->asyncInferenceResultCaptureNs = captureNs;
			tf->asyncInferenceResultCompletedNs = completedNs;
			tf->asyncInferenceResultCropOrigin = cropOrigin;
			tf->asyncInferenceLastMs = (float)(completedNs - startNs) / 1000000.0f;
			tf->asyncInferenceResultReady = true;
			++tf->asyncInferenceCompleted;
		}
	}
}
} // namespace

void sport_eyes_async_inference_start(detect_filter *tf)
{
	if (!tf)
		return;
	std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
	if (tf->asyncInferenceThread.joinable())
		return;
	tf->asyncInferenceStop = false;
	tf->asyncInferenceThread = std::thread(worker, tf);
	obs_log(LOG_INFO, "OBS Sport Eyes: async inference worker started");
}

void sport_eyes_async_inference_stop(detect_filter *tf)
{
	if (!tf)
		return;
	{
		std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
		tf->asyncInferenceStop = true;
		tf->asyncInferenceTaskPending = false;
		tf->asyncInferencePendingFrame.release();
	}
	tf->asyncInferenceCv.notify_all();
	if (tf->asyncInferenceThread.joinable())
		tf->asyncInferenceThread.join();
}

void sport_eyes_async_inference_reset(detect_filter *tf)
{
	if (!tf)
		return;
	std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
	tf->asyncInferenceTaskPending = false;
	tf->asyncInferencePendingFrame.release();
	++tf->asyncInferenceGeneration;
	tf->asyncInferenceResultReady = false;
	tf->asyncInferenceResultObjects.clear();
	tf->asyncInferenceResultCaptureNs = 0;
	tf->asyncInferenceResultCompletedNs = 0;
	tf->asyncInferenceLastSubmitNs = 0;
}

bool sport_eyes_async_inference_submit(detect_filter *tf, const cv::Mat &bgr,
	float scale, uint64_t captureNs, const cv::Point &cropOrigin)
{
	if (!tf || bgr.empty())
		return false;
	cv::Mat task;
	if (scale < 0.999f)
		cv::resize(bgr, task, cv::Size(), std::clamp((double)scale, 0.25, 1.0),
			std::clamp((double)scale, 0.25, 1.0), cv::INTER_LINEAR);
	else
		task = bgr.clone();

	{
		std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
		if (tf->asyncInferenceStop)
			return false;
		if (tf->asyncInferenceTaskPending)
			++tf->asyncInferenceReplaced;
		tf->asyncInferencePendingFrame = std::move(task);
		tf->asyncInferencePendingScale = scale;
		tf->asyncInferencePendingCaptureNs = captureNs;
		tf->asyncInferencePendingGeneration = tf->asyncInferenceGeneration;
		tf->asyncInferencePendingCropOrigin = cropOrigin;
		tf->asyncInferenceTaskPending = true;
		tf->asyncInferenceLastSubmitNs = captureNs;
		++tf->asyncInferenceSubmitted;
	}
	tf->asyncInferenceCv.notify_one();
	return true;
}

bool sport_eyes_async_inference_try_get(detect_filter *tf,
	std::vector<Object> &objectsOut, uint64_t &captureNsOut,
	uint64_t &completedNsOut, cv::Point &cropOriginOut)
{
	if (!tf)
		return false;
	std::lock_guard<std::mutex> lock(tf->asyncInferenceMutex);
	if (!tf->asyncInferenceResultReady)
		return false;
	objectsOut = std::move(tf->asyncInferenceResultObjects);
	captureNsOut = tf->asyncInferenceResultCaptureNs;
	completedNsOut = tf->asyncInferenceResultCompletedNs;
	cropOriginOut = tf->asyncInferenceResultCropOrigin;
	tf->asyncInferenceResultReady = false;
	return true;
}
