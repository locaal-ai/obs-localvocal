#include "custom-api-json.h"

#include <nlohmann/json.hpp>

#include <limits>
#include <sstream>
#include <vector>

namespace {

using json = nlohmann::json;

std::vector<std::string> split_response_path(const std::string &response_json_path)
{
	if (response_json_path.empty())
		throw TranslationError("Failed to resolve response JSON path: path is empty");

	std::vector<std::string> segments;
	std::size_t segment_start = 0;
	while (segment_start <= response_json_path.size()) {
		const std::size_t separator = response_json_path.find('.', segment_start);
		const std::size_t segment_end =
			separator == std::string::npos ? response_json_path.size() : separator;
		if (segment_end == segment_start) {
			std::ostringstream message;
			message << "Failed to resolve response JSON path '" << response_json_path
				<< "': empty path segment at position " << segments.size();
			throw TranslationError(message.str());
		}

		segments.emplace_back(response_json_path, segment_start,
				      segment_end - segment_start);
		if (separator == std::string::npos)
			break;
		segment_start = separator + 1;
	}

	return segments;
}

std::size_t parse_array_index(const std::string &response_json_path, const std::string &segment)
{
	const std::size_t max_index = std::numeric_limits<std::size_t>::max();
	std::size_t index = 0;
	for (const char character : segment) {
		if (character < '0' || character > '9') {
			std::ostringstream message;
			message << "Failed to resolve response JSON path '" << response_json_path
				<< "': segment '" << segment << "' is not a valid array index";
			throw TranslationError(message.str());
		}

		const std::size_t digit = static_cast<std::size_t>(character - '0');
		if (index > (max_index - digit) / 10) {
			std::ostringstream message;
			message << "Failed to resolve response JSON path '" << response_json_path
				<< "': array index '" << segment << "' overflows size_t";
			throw TranslationError(message.str());
		}
		index = index * 10 + digit;
	}

	return index;
}

[[noreturn]] void throw_non_string_value_error(const std::string &response_json_path,
					       const json &value)
{
	std::ostringstream message;
	message << "Failed to resolve response JSON path '" << response_json_path
		<< "': resolved value is not a string (JSON type " << value.type_name() << ")";
	throw TranslationError(message.str());
}

} // namespace

std::string resolve_custom_api_response(const nlohmann::json &response,
					const std::string &response_json_path)
{
	const auto segments = split_response_path(response_json_path);

	// Preserve the existing behavior for a literal dotted key at the root.
	if (response.is_object()) {
		const auto exact_match = response.find(response_json_path);
		if (exact_match != response.end()) {
			if (!exact_match->is_string())
				throw_non_string_value_error(response_json_path, *exact_match);
			return exact_match->get<std::string>();
		}
	}

	const json *current = &response;
	for (const std::string &segment : segments) {
		if (current->is_object()) {
			const auto member = current->find(segment);
			if (member == current->end()) {
				std::ostringstream message;
				message << "Failed to resolve response JSON path '"
					<< response_json_path << "': object key '" << segment
					<< "' was not found";
				throw TranslationError(message.str());
			}
			current = &*member;
			continue;
		}

		if (current->is_array()) {
			const std::size_t index = parse_array_index(response_json_path, segment);
			if (index >= current->size()) {
				std::ostringstream message;
				message << "Failed to resolve response JSON path '"
					<< response_json_path << "': array index " << index
					<< " is out of range (size " << current->size() << ")";
				throw TranslationError(message.str());
			}
			current = &(*current)[index];
			continue;
		}

		std::ostringstream message;
		message << "Failed to resolve response JSON path '" << response_json_path
			<< "': cannot traverse segment '" << segment << "' through JSON type '"
			<< current->type_name() << "'";
		throw TranslationError(message.str());
	}

	if (!current->is_string())
		throw_non_string_value_error(response_json_path, *current);

	return current->get<std::string>();
}
