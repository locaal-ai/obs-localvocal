#pragma once

#include "ITranslator.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

// Resolve a dot-separated Custom API response path and return its string value.
// Throws TranslationError when the path is malformed, cannot be resolved, or
// resolves to a non-string JSON value.
std::string resolve_custom_api_response(const nlohmann::json &response,
					const std::string &response_json_path);
