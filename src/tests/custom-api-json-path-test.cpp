#include "translation/cloud-translation/custom-api-json.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using json = nlohmann::json;

void expect_equal(const std::string &actual, const std::string &expected,
		  const std::string &test_name)
{
	if (actual != expected)
		throw std::runtime_error(test_name + ": expected '" + expected + "', got '" +
					 actual + "'");
}

void expect_translation_error(const json &response, const std::string &path,
			      const std::string &message_fragment, const std::string &test_name)
{
	try {
		resolve_custom_api_response(response, path);
	} catch (const TranslationError &error) {
		if (std::string(error.what()).find(message_fragment) == std::string::npos) {
			throw std::runtime_error(test_name + ": unexpected error: " + error.what());
		}
		return;
	}
	throw std::runtime_error(test_name + ": expected TranslationError");
}

void run_tests()
{
	expect_equal(resolve_custom_api_response({{"translation", "hello"}}, "translation"),
		     "hello", "simple top-level key");
	expect_equal(resolve_custom_api_response({{"data", {{"translation", {{"text", "hello"}}}}}},
						 "data.translation.text"),
		     "hello", "nested object path");
	expect_equal(resolve_custom_api_response(
			     {{"choices", {{{"message", {{"content", "现在就先保密吧"}}}}}}},
			     "choices.0.message.content"),
		     "现在就先保密吧", "OpenAI-compatible response");
	expect_equal(resolve_custom_api_response({{"translations", {{{"text", "hello"}}}}},
						 "translations.0.text"),
		     "hello", "translation-style array path");
	expect_equal(resolve_custom_api_response({{"groups", {{{"items", {{{"text", "hello"}}}}}}}},
						 "groups.0.items.0.text"),
		     "hello", "multiple array indices");
	expect_equal(resolve_custom_api_response({{"result.text", "legacy"}}, "result.text"),
		     "legacy", "legacy literal dotted key");
	expect_equal(resolve_custom_api_response({{"result.text", "legacy"},
						  {"result", {{"text", "nested"}}}},
						 "result.text"),
		     "legacy", "legacy dotted key precedence");
	expect_equal(resolve_custom_api_response({{"0", {{"text", "hello"}}}}, "0.text"), "hello",
		     "numeric object key");

	expect_translation_error({{"translation", "hello"}}, "missing", "object key 'missing'",
				 "missing object key");
	expect_translation_error({{"choices", {{{"message", {{"content", "hello"}}}}}}},
				 "choices.5.message.content", "array index 5 is out of range",
				 "out-of-range array index");
	expect_translation_error({{"choices", {{{"message", {{"content", "hello"}}}}}}},
				 "choices.first.message.content", "not a valid array index",
				 "non-numeric array index");
	expect_translation_error({{"choices", json::array()}},
				 "choices.999999999999999999999999999999999999999999",
				 "overflows size_t", "overflowing array index");
	expect_translation_error({{"data", "hello"}}, "data.text", "cannot traverse segment 'text'",
				 "scalar traversal");
	expect_translation_error({{"translation", nullptr}}, "translation", "not a string",
				 "final null value");
	expect_translation_error({{"translation", {{"text", "hello"}}}}, "translation",
				 "JSON type object", "final object value");
	expect_translation_error({{"translation", json::array({"hello"})}}, "translation",
				 "JSON type array", "final array value");
	expect_translation_error({{"translation", 42}}, "translation", "JSON type number",
				 "final number value");
	expect_translation_error({{"translation", true}}, "translation", "JSON type boolean",
				 "final boolean value");
	expect_translation_error({{"choices", json::array()}}, "choices..message.content",
				 "empty path segment", "empty middle segment");
	expect_translation_error({{"choices", json::array()}}, ".choices.0", "empty path segment",
				 "empty leading segment");
	expect_translation_error({{"choices", json::array()}}, "choices.0.", "empty path segment",
				 "empty trailing segment");
	expect_translation_error({{"translation", "hello"}}, "", "path is empty", "empty path");
}

} // namespace

int main()
{
	try {
		run_tests();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
