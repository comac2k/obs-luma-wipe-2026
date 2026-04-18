#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <graphics/matrix4.h>
#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>

struct luma_wipe_info {
	obs_source_t *source;
	gs_effect_t *effect;

	gs_eparam_t *param_image;
	gs_eparam_t *param_target;
	gs_eparam_t *param_mask;
	gs_eparam_t *param_progress;
	gs_eparam_t *param_invert;
	gs_eparam_t *param_bias;
	gs_eparam_t *param_viewproj;
	gs_eparam_t *param_blur_window;
	gs_eparam_t *param_flip_x;
	gs_eparam_t *param_flip_y;

	gs_texture_t *mask_texture;
	bool invert;
	bool flip_x;
	bool flip_y;
	double motion_blur_bias;
	float last_t;
	char *mask_path;
};

static const char *luma_wipe_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("LumaWipe");
}

static void luma_wipe_update(void *data, obs_data_t *settings)
{
	struct luma_wipe_info *filter = data;
	const char *path = obs_data_get_string(settings, "mask_path");

	filter->invert = obs_data_get_bool(settings, "invert");
	filter->flip_x = obs_data_get_bool(settings, "flip_x");
	filter->flip_y = obs_data_get_bool(settings, "flip_y");
	filter->motion_blur_bias = obs_data_get_double(settings, "motion_blur_bias");

	if (filter->mask_path && path && strcmp(filter->mask_path, path) == 0) {
		return;
	}

	bfree(filter->mask_path);
	filter->mask_path = (path && *path) ? bstrdup(path) : NULL;

	obs_enter_graphics();
	if (filter->mask_texture) {
		gs_texture_destroy(filter->mask_texture);
		filter->mask_texture = NULL;
	}

	if (filter->mask_path) {
		int width, height, channels;
		unsigned short *image_data = stbi_load_16(filter->mask_path, &width, &height, &channels, 1);
		if (image_data) {
			filter->mask_texture = gs_texture_create(width, height, GS_R16, 1, (const uint8_t **)&image_data, 0);
			stbi_image_free(image_data);
		} else {
			blog(LOG_WARNING, "[Luma Wipe 2026] Failed to load mask: %s", filter->mask_path);
		}
	}
	obs_leave_graphics();
}

static void luma_wipe_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "motion_blur_bias", 0.0);
	obs_data_set_default_bool(settings, "invert", false);
	obs_data_set_default_bool(settings, "flip_x", false);
	obs_data_set_default_bool(settings, "flip_y", false);
}

static void *luma_wipe_create(obs_data_t *settings, obs_source_t *source)
{
	struct luma_wipe_info *filter = bzalloc(sizeof(struct luma_wipe_info));
	filter->source = source;

	char *effect_path = obs_module_file("luma_wipe.effect");
	if (effect_path) {
		char *error_string = NULL;
		obs_enter_graphics();
		filter->effect = gs_effect_create_from_file(effect_path, &error_string);
		obs_leave_graphics();

		if (filter->effect) {
			filter->param_image = gs_effect_get_param_by_name(filter->effect, "image");
			filter->param_target = gs_effect_get_param_by_name(filter->effect, "target");
			filter->param_mask = gs_effect_get_param_by_name(filter->effect, "luma_mask");
			filter->param_progress = gs_effect_get_param_by_name(filter->effect, "progress");
			filter->param_invert = gs_effect_get_param_by_name(filter->effect, "invert");
			filter->param_bias = gs_effect_get_param_by_name(filter->effect, "motion_blur_bias");
			filter->param_viewproj = gs_effect_get_param_by_name(filter->effect, "ViewProj");
			filter->param_blur_window = gs_effect_get_param_by_name(filter->effect, "blur_window");
			filter->param_flip_x = gs_effect_get_param_by_name(filter->effect, "flip_x");
			filter->param_flip_y = gs_effect_get_param_by_name(filter->effect, "flip_y");
		} else {
			blog(LOG_ERROR, "[Luma Wipe 2026] Effect file found but could not be loaded: %s", effect_path);
			if (error_string) {
				blog(LOG_ERROR, "[Luma Wipe 2026] Shader errors:\n%s", error_string);
				bfree(error_string);
			}
		}
		bfree(effect_path);
	} else {
		blog(LOG_ERROR, "[Luma Wipe 2026] Could not find luma_wipe.effect. Please ensure it is in the plugin's "
				"data directory (usually data/obs-plugins/luma-wipe-2026/).");
	}

	luma_wipe_update(filter, settings);

	return filter;
}

static void luma_wipe_destroy(void *data)
{
	struct luma_wipe_info *filter = data;

	obs_enter_graphics();
	if (filter->mask_texture) {
		gs_texture_destroy(filter->mask_texture);
	}
	if (filter->effect) {
		gs_effect_destroy(filter->effect);
	}
	obs_leave_graphics();

	bfree(filter->mask_path);
	bfree(filter);
}

static void luma_wipe_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct luma_wipe_info *filter = data;

	if (!filter->effect || !filter->mask_texture) {
		// Fallback: simple crossfade if no mask or no effect
		gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *param = gs_effect_get_param_by_name(default_effect, "image");

		gs_effect_set_texture(param, (t < 0.5f) ? a : b);

		while (gs_effect_loop(default_effect, "Draw")) {
			gs_draw_sprite(NULL, 0, cx, cy);
		}
		return;
	}

	struct obs_video_info ovi;
	float dt = 0.0f;

	if (t > 0.0f && t < 1.0f) {
		if (t < filter->last_t) {
			dt = t;
		} else {
			dt = t - filter->last_t;
		}
	}
	filter->last_t = t;

	if (dt <= 0.0f) {
		// Fallback for first frame or non-transitioning state
		if (obs_get_video_info(&ovi)) {
			dt = (float)ovi.fps_den / (float)ovi.fps_num;
		} else {
			dt = 0.0166f;
		}
	}

	float bias = (float)filter->motion_blur_bias;
	float blur_window_raw = dt;
	float blur_window;

	if (bias < 0.0f) {
		blur_window = blur_window_raw * (bias + 1.0f);
	} else {
		blur_window = blur_window_raw * (1.0f - bias) + 1.0f * bias;
	}

	float t2 = t * (1.0f + 2.0f * blur_window) - blur_window;

	struct matrix4 projection;
	gs_matrix_get(&projection);

	gs_effect_set_texture(filter->param_image, a);
	gs_effect_set_texture(filter->param_target, b);
	gs_effect_set_texture(filter->param_mask, filter->mask_texture);
	gs_effect_set_float(filter->param_progress, t2);
	gs_effect_set_bool(filter->param_invert, filter->invert);
	if (filter->param_bias) {
		gs_effect_set_float(filter->param_bias, (float)filter->motion_blur_bias);
	}
	if (filter->param_viewproj) {
		gs_effect_set_matrix4(filter->param_viewproj, &projection);
	}
	if (filter->param_blur_window) {
		gs_effect_set_float(filter->param_blur_window, blur_window);
	}
	if (filter->param_flip_x) {
		gs_effect_set_bool(filter->param_flip_x, filter->flip_x);
	}
	if (filter->param_flip_y) {
		gs_effect_set_bool(filter->param_flip_y, filter->flip_y);
	}

	while (gs_effect_loop(filter->effect, "LumaWipe")) {
		gs_draw_sprite(NULL, 0, cx, cy);
	}
}

static void luma_wipe_video_render(void *data, gs_effect_t *effect)
{
	struct luma_wipe_info *filter = data;
	UNUSED_PARAMETER(effect);

	obs_transition_video_render(filter->source, luma_wipe_callback);
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - t;
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t;
}

static bool luma_wipe_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
				   size_t channels, size_t sample_rate)
{
	struct luma_wipe_info *filter = data;
	return obs_transition_audio_render(filter->source, ts_out, audio, mixers, channels, sample_rate, mix_a, mix_b);
}

static obs_properties_t *luma_wipe_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	char *lumas_path = obs_module_file("lumas");
	obs_properties_add_path(props, "mask_path", obs_module_text("MaskPath"), OBS_PATH_FILE,
				obs_module_text("FilterFiles"), lumas_path);
	bfree(lumas_path);

	obs_properties_add_bool(props, "invert", obs_module_text("Invert"));
	obs_properties_add_bool(props, "flip_x", obs_module_text("FlipX"));
	obs_properties_add_bool(props, "flip_y", obs_module_text("FlipY"));
	obs_properties_add_float_slider(props, "motion_blur_bias", obs_module_text("MotionBlurBias"), -1.0, 1.0, 0.01);

	return props;
}

struct obs_source_info luma_wipe_info = {
	.id = "luma_wipe_2026",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_COMPOSITE,
	.get_name = luma_wipe_get_name,
	.create = luma_wipe_create,
	.destroy = luma_wipe_destroy,
	.update = luma_wipe_update,
	.get_defaults = luma_wipe_get_defaults,
	.video_render = luma_wipe_video_render,
	.audio_render = luma_wipe_audio_render,
	.get_properties = luma_wipe_get_properties,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("luma-wipe-2026", "en-US")

bool obs_module_load(void)
{
	obs_register_source(&luma_wipe_info);
	return true;
}
