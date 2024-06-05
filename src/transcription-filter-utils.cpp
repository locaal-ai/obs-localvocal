#include "transcription-filter-utils.h"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>

void create_obs_text_source()
{
	// create a new OBS text source called "LocalVocal Subtitles"
	obs_source_t *scene_as_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_as_source);
#ifdef _WIN32
	obs_source_t *source =
		obs_source_create("text_gdiplus_v2", "LocalVocal Subtitles", nullptr, nullptr);
#else
	obs_source_t *source =
		obs_source_create("text_ft2_source_v2", "LocalVocal Subtitles", nullptr, nullptr);
#endif
	if (source) {
		// add source to the current scene
		obs_scene_add(scene, source);
		// set source settings
		obs_data_t *source_settings = obs_source_get_settings(source);
		obs_data_set_bool(source_settings, "word_wrap", true);
		obs_data_set_int(source_settings, "custom_width", 1760);
		obs_data_t *font_data = obs_data_create();
		obs_data_set_string(font_data, "face", "Arial");
		obs_data_set_string(font_data, "style", "Regular");
		obs_data_set_int(font_data, "size", 72);
		obs_data_set_int(font_data, "flags", 0);
		obs_data_set_obj(source_settings, "font", font_data);
		obs_data_release(font_data);
		obs_source_update(source, source_settings);
		obs_data_release(source_settings);

		// set transform settings
		obs_transform_info transform_info;
		transform_info.pos.x = 962.0;
		transform_info.pos.y = 959.0;
		transform_info.bounds.x = 1769.0;
		transform_info.bounds.y = 145.0;
		transform_info.bounds_type = obs_bounds_type::OBS_BOUNDS_SCALE_INNER;
		transform_info.bounds_alignment = OBS_ALIGN_CENTER;
		transform_info.alignment = OBS_ALIGN_CENTER;
		transform_info.scale.x = 1.0;
		transform_info.scale.y = 1.0;
		transform_info.rot = 0.0;
		obs_sceneitem_t *source_sceneitem = obs_scene_sceneitem_from_source(scene, source);
		obs_sceneitem_set_info(source_sceneitem, &transform_info);
		obs_sceneitem_release(source_sceneitem);

		obs_source_release(source);
	}
	obs_source_release(scene_as_source);
}
