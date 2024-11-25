#pragma once
#include <string>
#include <mutex>

#include <curl/curl.h>
#include "ITranslator.h"

class CurlHelper {
public:
	CurlHelper();
	~CurlHelper();

	// Callback for writing response data
	static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

	// URL encode a string
	static std::string urlEncode(CURL *curl, const std::string &value);

	// Common request builders
	static struct curl_slist *
	createBasicHeaders(const std::string &content_type = "application/json");

	// Verify HTTPS certificate
	static void setSSLVerification(CURL *curl, bool verify = true);

private:
	static bool is_initialized_;
	static std::mutex curl_mutex_; // For thread-safe global initialization
};
