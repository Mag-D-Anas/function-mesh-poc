#include "router.hpp"
#include <metacall/metacall.h>
#include <iostream>
#include <vector>


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

Router::Router(Registry &registry, void *allocator) :
	registry_(registry), allocator_(allocator)
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

	std::cout << "[DEBUG][router] " << parts[0] << "/" << function_name
	          << " | body=" << body << " | body.length()=" << body.length() << "\n";

	void *handle = registry_.next_handle(function_name);
	if (handle == nullptr)
	{
		std::cout << "[DEBUG][router] No handle found for: " << function_name << "\n";
		return { 404, "{\"error\":\"function-not-registered\"}", "application/json" };
	}

	std::cout << "[DEBUG][router] Round-robin selected handle=" << handle << "\n";

	void *func = metacall_handle_function(handle, function_name.c_str());
	if (func == nullptr)
	{
		std::cout << "[DEBUG][router] metacall_handle_function returned NULL for: " << function_name << "\n";
		return { 404, "{\"error\":\"function-not-found-in-handle\"}", "application/json" };
	}

	std::cout << "[DEBUG][router] metacall_handle_function OK, func=" << func << "\n";

	/*
	 * metacallfs flow:
	 *   1. metacall_deserialize(body) -> MetaCall value array
	 *   2. Copy args from array
	 *   3. metacallfv_s(func, args, count) -> function_rpc_interface_invoke
	 *   4. RPC loader: serial_serialize(args) -> curl POST to worker -> serial_deserialize(response)
	 *   5. Return result value
	 */
	std::cout << "[DEBUG][router] Calling metacallfs(func, body, " << body.length() + 1 << ", allocator)...\n";

	void *ret = metacallfs(func, body.c_str(), body.length() + 1, allocator_);

	if (ret == nullptr)
	{
		std::cout << "[DEBUG][router] metacallfs returned NULL (invoke failed)\n";
		return { 502, "{\"error\":\"invoke-failed\"}", "application/json" };
	}

	enum metacall_value_id ret_type = metacall_value_id(ret);
	std::cout << "[DEBUG][router] metacallfs returned value, type_id=" << ret_type << "\n";

	size_t ret_size = 0;
	char *ret_json = metacall_serialize(metacall_serial(), ret, &ret_size, allocator_);

	std::cout << "[DEBUG][router] metacall_serialize result: size=" << ret_size
	          << " json=" << (ret_json ? ret_json : "(null)") << "\n";

	metacall_value_destroy(ret);

	if (ret_json == nullptr)
	{
		std::cout << "[DEBUG][router] metacall_serialize FAILED\n";
		return { 500, "{\"error\":\"serialize-response-failed\"}", "application/json" };
	}

	std::string response(ret_json, ret_size > 0 ? ret_size - 1 : 0);
	metacall_allocator_free(allocator_, ret_json);

	std::cout << "[DEBUG][router] Response to client: " << response << "\n";

	return { 200, response, "application/json" };
}
