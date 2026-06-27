#include "sport-eyes-filter-internal.h"

#include "director_ai/DirectorAILogger.h"

#include <filesystem>

namespace {
constexpr uint64_t kCsvIntervalNs = 100000000ULL; // 10 Hz.

bool open_csv(std::ofstream &stream, const std::string &path, const char *header,
              bool &headerWritten, bool &errorLogged, const char *label)
{
	if (stream.is_open())
		return true;

	try {
		const std::filesystem::path outputPath(path);
		if (outputPath.has_parent_path())
			std::filesystem::create_directories(outputPath.parent_path());
	} catch (const std::exception &e) {
		if (!errorLogged) {
			obs_log(LOG_WARNING, "OBS Sport Eyes: cannot prepare %s CSV directory: %s", label, e.what());
			errorLogged = true;
		}
		return false;
	}

	stream.open(path, std::ios::out | std::ios::trunc);
	if (!stream.is_open()) {
		if (!errorLogged) {
			obs_log(LOG_WARNING, "OBS Sport Eyes: cannot open %s CSV: %s", label, path.c_str());
			errorLogged = true;
		}
		return false;
	}

	stream << header << '\n';
	stream.flush();
	headerWritten = true;
	errorLogged = false;
	return true;
}

void close_stream(std::ofstream &stream)
{
	if (stream.is_open()) {
		stream.flush();
		stream.close();
	}
}
} // namespace

void sport_eyes_csv_logging_reconfigure(struct detect_filter *tf, bool enabled,
	const std::string &diagnosticsPath, const std::string &directorPath)
{
	const bool changed = tf->csvLoggingEnabled != enabled ||
		tf->diagnosticsCsvPath != diagnosticsPath || tf->directorCsvPath != directorPath;
	if (changed)
		sport_eyes_csv_logging_close(tf);

	tf->csvLoggingEnabled = enabled;
	tf->diagnosticsCsvPath = diagnosticsPath;
	tf->directorCsvPath = directorPath;
	if (changed) {
		tf->csvLastWriteNs = 0;
		tf->diagnosticsCsvHeaderWritten = false;
		tf->directorCsvHeaderWritten = false;
		tf->diagnosticsCsvOpenErrorLogged = false;
		tf->directorCsvOpenErrorLogged = false;
	}
}

void sport_eyes_csv_logging_close(struct detect_filter *tf)
{
	if (!tf)
		return;
	close_stream(tf->diagnosticsCsv);
	close_stream(tf->directorCsv);
	tf->diagnosticsCsvHeaderWritten = false;
	tf->directorCsvHeaderWritten = false;
}

void sport_eyes_write_csv_logs(struct detect_filter *tf, const SportEyesCsvSample &sample)
{
	if (!tf || !tf->csvLoggingEnabled)
		return;
	if (tf->csvLastWriteNs && sample.timestampNs > tf->csvLastWriteNs &&
		(sample.timestampNs - tf->csvLastWriteNs) < kCsvIntervalNs)
		return;
	tf->csvLastWriteNs = sample.timestampNs;

	static const char *kDiagnosticsHeader =
		"timestamp_ns,frame_width,frame_height,objects_total,objects_visible,tracking_enabled,"
		"group_cluster_valid,lost_tracking,safe_roi_holding,action_x,action_y,action_w,action_h,"
		"crop_x,crop_y,crop_w,crop_h,director_enabled,director_applied,"
		"async_enabled,result_fresh,result_age_ms,result_completion_age_ms,inference_ms,"
		"async_worker_busy,async_task_pending,async_replaced_count,async_result_overwritten_count,async_submitted_count,"
		"async_completed_count,async_pending_sequence,result_sequence,applied_sequence,"
		"director_measurement_state,director_measurement_age_ms";
	if (open_csv(tf->diagnosticsCsv, tf->diagnosticsCsvPath, kDiagnosticsHeader,
			tf->diagnosticsCsvHeaderWritten, tf->diagnosticsCsvOpenErrorLogged, "diagnostics")) {
		tf->diagnosticsCsv << sample.timestampNs << ','
			<< sample.frameWidth << ',' << sample.frameHeight << ','
			<< sample.objectCount << ',' << sample.visibleObjectCount << ','
			<< (tf->trackingEnabled ? 1 : 0) << ','
			<< (sample.groupClusterValid ? 1 : 0) << ','
			<< (sample.lostTracking ? 1 : 0) << ','
			<< (sample.safeRoiHolding ? 1 : 0) << ','
			<< sample.actionBox.x << ',' << sample.actionBox.y << ','
			<< sample.actionBox.width << ',' << sample.actionBox.height << ','
			<< sample.cropBox.x << ',' << sample.cropBox.y << ','
			<< sample.cropBox.width << ',' << sample.cropBox.height << ','
			<< (tf->directorAIEnabled ? 1 : 0) << ','
			<< (sample.directorApplied ? 1 : 0) << ','
			<< (sample.asyncEnabled ? 1 : 0) << ','
			<< (sample.resultFresh ? 1 : 0) << ','
			<< sample.resultAgeMs << ','
			<< sample.resultCompletionAgeMs << ','
			<< sample.inferenceMs << ','
			<< (sample.asyncWorkerBusy ? 1 : 0) << ','
			<< (sample.asyncTaskPending ? 1 : 0) << ','
			<< sample.asyncReplacedCount << ','
			<< sample.asyncResultOverwrittenCount << ','
			<< sample.asyncSubmittedCount << ','
			<< sample.asyncCompletedCount << ','
			<< sample.asyncPendingSequence << ','
			<< sample.resultSequence << ','
			<< sample.appliedSequence << ','
			<< sample.directorMeasurementState << ','
			<< sample.directorMeasurementAgeMs << '\n';
		tf->diagnosticsCsv.flush();
	}

	if (!tf->directorAIEnabled)
		return;
	const std::string directorHeader = director_ai::director_ai_csv_header();
	if (open_csv(tf->directorCsv, tf->directorCsvPath, directorHeader.c_str(),
			tf->directorCsvHeaderWritten, tf->directorCsvOpenErrorLogged, "Director AI")) {
		director_ai::DirectorAILogRow row;
		row.timestamp_ns = sample.timestampNs;
		row.frame = tf->lastDirectorAIFrame;
		row.applied_to_crop = sample.directorApplied;
		row.measurement_state = sample.directorMeasurementState;
		row.measurement_age_ms = sample.directorMeasurementAgeMs;
		row.result_fresh = sample.resultFresh;
		row.result_sequence = sample.resultSequence;
		row.async_inference_ms = sample.inferenceMs;
		row.async_worker_busy = sample.asyncWorkerBusy;
		row.pending_replaced_count = sample.asyncReplacedCount;
		tf->directorCsv << director_ai::director_ai_csv_row(row) << '\n';
		tf->directorCsv.flush();
	}
}
