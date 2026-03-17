#pragma once

#include "registry.hpp"

#include <string>

struct RouteResult {
	int status;
	std::string body;
	std::string content_type;
};

class Router {
public:
	Router(Registry &registry, long timeout_ms, int retry_count);
	RouteResult handle(const std::string &method, const std::string &path, const std::string &body);

private:
	Registry &registry_;
	long timeout_ms_;
	int retry_count_;
};
