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
	Router(Registry &registry, void *allocator);
	RouteResult handle(const std::string &method, const std::string &path, const std::string &body);

private:
	Registry &registry_;
	void *allocator_;
};
