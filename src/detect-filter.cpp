#include "detect-filter.h"
#include "sport-eyes-filter-internal.h"

const char *detect_filter_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("SportEyesFilter");
}

void *detect_filter_create(obs_data_t *settings, obs_source_t *source)
{
    return sport_eyes_filter_create(settings, source);
}
void detect_filter_destroy(void *data) { sport_eyes_filter_destroy(data); }
void detect_filter_defaults(obs_data_t *settings) { sport_eyes_filter_defaults(settings); }
obs_properties_t *detect_filter_properties(void *data) { return sport_eyes_filter_properties(data); }
void detect_filter_update(void *data, obs_data_t *settings) { sport_eyes_filter_update(data, settings); }
void detect_filter_activate(void *data) { sport_eyes_filter_activate(data); }
void detect_filter_deactivate(void *data) { sport_eyes_filter_deactivate(data); }
void detect_filter_video_tick(void *data, float seconds) { sport_eyes_filter_video_tick(data, seconds); }
void detect_filter_video_render(void *data, gs_effect_t *effect) { sport_eyes_filter_video_render(data, effect); }
