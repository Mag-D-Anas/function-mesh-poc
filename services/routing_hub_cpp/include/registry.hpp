#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Registry {
public:
	void add(const std::string &function_name, void *handle, bool async_flag);
	void *next_handle(const std::string &function_name);
	bool has_function(const std::string &function_name) const;
	bool is_async(const std::string &function_name) const;
	std::string inspect_payload() const;

private:
	std::unordered_map<std::string, std::vector<void *>> function_handles_;
	std::unordered_map<std::string, bool> function_async_;
	std::unordered_map<std::string, size_t> rr_index_;
};
