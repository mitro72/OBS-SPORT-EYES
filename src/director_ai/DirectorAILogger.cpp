#include "DirectorAILogger.h"

#include <sstream>

namespace director_ai {

std::string director_ai_csv_header()
{
	return "timestamp_ns,valid,applied_to_crop,current_x,current_y,predicted_x,predicted_y,lead_px,clamped,confidence_gate,confidence,velocity_x,velocity_y,transition_probability,direction,court_x,court_y,court_third,crop_x,crop_y,crop_w,crop_h,measurement_state,measurement_age_ms,result_fresh,result_sequence,async_inference_ms,async_worker_busy,pending_replaced_count";
}

std::string director_ai_csv_row(const DirectorAILogRow &row)
{
	const DirectorAIFrame &f = row.frame;
	std::ostringstream out;
	out << row.timestamp_ns << ','
	    << (f.valid ? 1 : 0) << ','
	    << (row.applied_to_crop ? 1 : 0) << ','
	    << f.diagnostics.current_center.x << ','
	    << f.diagnostics.current_center.y << ','
	    << f.prediction.predicted_center.x << ','
	    << f.prediction.predicted_center.y << ','
	    << f.diagnostics.prediction_lead_px << ','
	    << (f.diagnostics.prediction_clamped ? 1 : 0) << ','
	    << (f.diagnostics.confidence_gate_passed ? 1 : 0) << ','
	    << f.prediction.confidence << ','
	    << f.prediction.velocity_px_s.x << ','
	    << f.prediction.velocity_px_s.y << ','
	    << f.world.transition_probability << ','
	    << f.world.direction << ','
	    << f.court_point.x_norm << ','
	    << f.court_point.y_norm << ','
	    << f.court_zone.horizontal_third << ','
	    << f.camera.crop_rect.x << ','
	    << f.camera.crop_rect.y << ','
	    << f.camera.crop_rect.width << ','
	    << f.camera.crop_rect.height << ','
	    << row.measurement_state << ','
	    << row.measurement_age_ms << ','
	    << (row.result_fresh ? 1 : 0) << ','
	    << row.result_sequence << ','
	    << row.async_inference_ms << ','
	    << (row.async_worker_busy ? 1 : 0) << ','
	    << row.pending_replaced_count;
	return out.str();
}

} // namespace director_ai
