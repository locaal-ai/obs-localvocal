#pragma once

#include <string>

struct CloudTranslatorConfig {
	std::string provider;
	std::string access_key;         // Main API key/Client ID
	std::string secret_key;         // Secret key/Client secret
	std::string region;             // For AWS / Azure
	std::string model;              // For Claude
	bool free;                      // For Deepl
	std::string endpoint;           // For Custom API
	std::string body;               // For Custom API
	std::string response_json_path; // For Custom API
};

std::string translate_cloud(const CloudTranslatorConfig &config, const std::string &text,
			    const std::string &target_lang, const std::string &source_lang);
