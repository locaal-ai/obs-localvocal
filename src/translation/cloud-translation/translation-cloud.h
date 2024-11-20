#pragma once

#include <string>

struct CloudTranslatorConfig {
	std::string provider;
	std::string access_key; // Main API key/Client ID
	std::string secret_key; // Secret key/Client secret
	std::string region;     // For AWS
	std::string location;   // For Azure
	std::string model;      // For Claude
};

std::string translate_cloud(const CloudTranslatorConfig &config, const std::string &text,
			    const std::string &target_lang, const std::string &source_lang);
