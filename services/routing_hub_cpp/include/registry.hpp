#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Registry {
public:
	void add(const std::string &function_name, const std::string &base_url, bool async_flag);
	std::string next_backend(const std::string &function_name);
	std::string inspect_payload() const;

private:
	std::unordered_map<std::string, std::vector<std::string>> function_backends_;
	std::unordered_map<std::string, bool> function_async_;
	std::unordered_map<std::string, size_t> rr_index_;
};
