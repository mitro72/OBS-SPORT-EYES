#include "sport-eyes-filter-internal.h"

void sport_eyes_filter_video_render(void *data, gs_effect_t *_effect)
{
	UNUSED_PARAMETER(_effect);

	struct detect_filter *tf = reinterpret_cast<detect_filter *>(data);

	if (tf->isDisabled || !tf->model) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	uint32_t width, height;
	if (!getRGBAFromStageSurface(tf, width, height)) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	// if preview is enabled, render the image
	if (tf->preview || tf->maskingEnabled) {
		cv::Mat outputBGRA, outputMask;
		{
			// lock the outputLock mutex
			std::lock_guard<std::mutex> lock(tf->outputLock);
			if (tf->outputPreviewBGRA.empty()) {
				obs_log(LOG_ERROR, "Preview image is empty");
				if (tf->source) {
					obs_source_skip_video_filter(tf->source);
				}
				return;
			}
			if ((uint32_t)tf->outputPreviewBGRA.cols != width ||
			    (uint32_t)tf->outputPreviewBGRA.rows != height) {
				if (tf->source) {
					obs_source_skip_video_filter(tf->source);
				}
				return;
			}
			outputBGRA = tf->outputPreviewBGRA.clone();
			outputMask = tf->outputMask.clone();
		}

		gs_texture_t *tex = gs_texture_create(width, height, GS_BGRA, 1,
						      (const uint8_t **)&outputBGRA.data, 0);
		gs_texture_t *maskTexture = nullptr;
		std::string technique_name = "Draw";
		gs_eparam_t *imageParam = gs_effect_get_param_by_name(tf->maskingEffect, "image");
		gs_eparam_t *maskParam =
			gs_effect_get_param_by_name(tf->maskingEffect, "focalmask");
		gs_eparam_t *maskColorParam =
			gs_effect_get_param_by_name(tf->maskingEffect, "color");

		if (tf->maskingEnabled) {
			maskTexture = gs_texture_create(width, height, GS_R8, 1,
							(const uint8_t **)&outputMask.data, 0);
			gs_effect_set_texture(maskParam, maskTexture);
			if (tf->maskingType == "output_mask") {
				technique_name = "DrawMask";
			} else if (tf->maskingType == "blur") {
				gs_texture_destroy(tex);
				tex = blur_image(tf, width, height, maskTexture);
			} else if (tf->maskingType == "pixelate") {
				gs_texture_destroy(tex);
				tex = pixelate_image(tf, width, height, maskTexture,
						     (float)tf->maskingBlurRadius);
			} else if (tf->maskingType == "transparent") {
				technique_name = "DrawSolidColor";
				gs_effect_set_color(maskColorParam, 0);
			} else if (tf->maskingType == "solid_color") {
				technique_name = "DrawSolidColor";
				gs_effect_set_color(maskColorParam, tf->maskingColor);
			}
		}

		gs_effect_set_texture(imageParam, tex);

		while (gs_effect_loop(tf->maskingEffect, technique_name.c_str())) {
			gs_draw_sprite(tex, 0, 0, 0);
		}

		gs_texture_destroy(tex);
		gs_texture_destroy(maskTexture);
	} else {
		obs_source_skip_video_filter(tf->source);
	}
	return;
}
