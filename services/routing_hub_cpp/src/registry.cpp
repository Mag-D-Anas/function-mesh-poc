#include "registry.hpp"
#include "policy.hpp"

#include <sstream>

void Registry::add(const std::string &function_name, const std::string &base_url, bool async_flag)
{
	function_backends_[function_name].push_back(base_url);
	function_async_[function_name] = async_flag;

	if (rr_index_.find(function_name) == rr_index_.end())
	{
		rr_index_[function_name] = 0;
	}
}

std::string Registry::next_backend(const std::string &function_name)
{
	auto it = function_backends_.find(function_name);
	if (it == function_backends_.end() || it->second.empty())
	{
		return "";
	}

	size_t idx = rr_index_[function_name];
	if (idx >= it->second.size())
	{
		idx = 0;
	}

	std::string selected = it->second[idx];
	rr_index_[function_name] = next_round_robin_index(idx, it->second.size());
	return selected;
}

std::string Registry::inspect_payload() const
{
	std::ostringstream oss;
	oss << "{\"py\":[{\"name\":\"routing_hub.py\",\"scope\":{\"name\":\"global_namespace\",\"funcs\":[";

	bool first = true;
	for (const auto &entry : function_backends_)
	{
		const std::string &name = entry.first;
		bool async_flag = false;
		auto ait = function_async_.find(name);
		if (ait != function_async_.end())
		{
			async_flag = ait->second;
		}

		if (!first)
		{
			oss << ",";
		}
		first = false;

		oss << "{\"name\":\"" << name << "\","
		    << "\"signature\":{\"ret\":{\"type\":{\"name\":\"float\",\"id\":6}},"
		    << "\"args\":[{\"name\":\"a\",\"type\":{\"name\":\"float\",\"id\":6}},"
		    << "{\"name\":\"b\",\"type\":{\"name\":\"float\",\"id\":6}}]},"
		    << "\"async\":" << (async_flag ? "true" : "false") << "}";
	}

	oss << "],\"classes\":[],\"objects\":[]}}]}";
	return oss.str();
}
