#ifndef WHISPER_PARAMS_H
#define WHISPER_PARAMS_H

#include "transcription-filter-data.h"

/**
 * @brief Prints the whisper parameters in a human-readable format.
 *
 * This function outputs the whisper parameters to the console in a formatted
 * and readable manner.
 *
 * @param params Reference to the whisper_full_params structure.
 */
void whisper_params_pretty_print(whisper_full_params &params);

/**
 * @brief Applies default whisper parameters to the given settings.
 *
 * This function sets the default values for whisper parameters on the provided
 * OBS data settings object. It ensures that all necessary parameters have
 * their default values, which can be used as a baseline for further
 * customization.
 *
 * @param s A pointer to an obs_data_t structure representing the settings
 *          where the default whisper parameters will be applied.
 */
void apply_whisper_params_defaults_on_settings(obs_data_t *s);

/**
 * @brief Applies whisper parameters from the given settings.
 *
 * This function takes a reference to a `whisper_full_params` structure and an
 * `obs_data_t` settings object, and applies the settings to the whisper parameters.
 *
 * @param params A reference to the `whisper_full_params` structure that will be modified.
 * @param settings A pointer to the `obs_data_t` settings object containing the parameters to apply.
 */
void apply_whisper_params_from_settings(whisper_full_params &params, obs_data_t *settings);

/**
 * @brief Adds whisper parameters group properties to the given OBS properties object.
 *
 * This function adds a group of properties related to whisper parameters to the
 * specified OBS properties object. These properties can be used to configure
 * whisper-related settings in the OBS application.
 *
 * @param ppts A pointer to an OBS properties object where the whisper parameters
 *             group properties will be added.
 */
void add_whisper_params_group_properties(obs_properties_t *ppts);

#endif // WHISPER_PARAMS_H
