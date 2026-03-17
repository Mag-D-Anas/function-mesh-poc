#include "router.hpp"

#include <curl/curl.h>

#include <sstream>
#include <vector>

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	std::string *buffer = static_cast<std::string *>(userdata);
	buffer->append(ptr, size * nmemb);
	return size * nmemb;
}

static std::vector<std::string> split_path(const std::string &path)
{
	std::vector<std::string> out;
	std::string current;
	for (char c : path)
	{
		if (c == '/')
		{
			if (!current.empty())
			{
				out.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(c);
	}
	if (!current.empty())
	{
		out.push_back(current);
	}
	return out;
}

static RouteResult forward_to_backend(const std::string &url, const std::string &body, long timeout_ms)
{
	CURL *curl = curl_easy_init();
	if (curl == nullptr)
	{
		return { 500, "{\"error\":\"curl-init-failed\"}", "application/json" };
	}

	std::string response;
	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode code = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (code != CURLE_OK)
	{
		return { 502, "{\"error\":\"upstream-unreachable\"}", "application/json" };
	}

	if (http_code != 200)
	{
		return { 502, "{\"error\":\"upstream-error\"}", "application/json" };
	}

	return { 200, response, "application/json" };
}

Router::Router(Registry &registry, long timeout_ms, int retry_count) :
	registry_(registry), timeout_ms_(timeout_ms), retry_count_(retry_count)
{
}

RouteResult Router::handle(const std::string &method, const std::string &path, const std::string &body)
{
	(void)method;

	if (path == "/health")
	{
		return { 200, "{\"status\":\"ok\",\"service\":\"routing-hub\"}", "application/json" };
	}

	if (path == "/inspect")
	{
		return { 200, registry_.inspect_payload(), "application/json" };
	}

	std::vector<std::string> parts = split_path(path);
	if (parts.size() != 2 || (parts[0] != "call" && parts[0] != "await"))
	{
		return { 404, "{\"error\":\"invalid-path\"}", "application/json" };
	}

	const std::string function_name = parts[1];
	const int attempts = retry_count_ + 1;

	for (int i = 0; i < attempts; ++i)
	{
		std::string backend = registry_.next_backend(function_name);
		if (backend.empty())
		{
			return { 404, "{\"error\":\"function-not-registered\"}", "application/json" };
		}

		std::string target_url = backend + "/" + parts[0] + "/" + function_name;
		RouteResult result = forward_to_backend(target_url, body, timeout_ms_);
		if (result.status == 200)
		{
			return result;
		}
	}

	return { 502, "{\"error\":\"all-upstreams-failed\"}", "application/json" };
}
