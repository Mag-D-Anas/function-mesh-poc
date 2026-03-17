#include "registry.hpp"
#include "router.hpp"

#include <metacall/metacall.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string get_env_or_default(const char *key, const char *fallback)
{
	const char *value = std::getenv(key);
	return (value == nullptr) ? std::string(fallback) : std::string(value);
}

static bool read_http_request(int fd, std::string &method, std::string &path, std::string &body)
{
	std::string raw;
	char buffer[4096];

	while (raw.find("\r\n\r\n") == std::string::npos)
	{
		ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
		if (n <= 0)
		{
			return false;
		}
		raw.append(buffer, static_cast<size_t>(n));
		if (raw.size() > 1024 * 1024)
		{
			return false;
		}
	}

	size_t headers_end = raw.find("\r\n\r\n");
	std::string headers = raw.substr(0, headers_end);
	std::string rest = raw.substr(headers_end + 4);

	std::istringstream hs(headers);
	std::string request_line;
	if (!std::getline(hs, request_line))
	{
		return false;
	}
	if (!request_line.empty() && request_line.back() == '\r')
	{
		request_line.pop_back();
	}

	std::istringstream rl(request_line);
	std::string http_version;
	rl >> method >> path >> http_version;
	if (method.empty() || path.empty())
	{
		return false;
	}

	size_t content_length = 0;
	std::string line;
	while (std::getline(hs, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		const std::string key = "Content-Length:";
		if (line.rfind(key, 0) == 0)
		{
			content_length = static_cast<size_t>(std::stoul(line.substr(key.size())));
		}
	}

	body = rest;
	while (body.size() < content_length)
	{
		ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
		if (n <= 0)
		{
			return false;
		}
		body.append(buffer, static_cast<size_t>(n));
	}

	if (body.size() > content_length)
	{
		body.resize(content_length);
	}

	return true;
}

static void write_http_response(int fd, int status, const std::string &content_type, const std::string &body)
{
	std::string status_text = "OK";
	if (status == 404)
	{
		status_text = "Not Found";
	}
	else if (status == 500)
	{
		status_text = "Internal Server Error";
	}
	else if (status == 502)
	{
		status_text = "Bad Gateway";
	}

	std::ostringstream response;
	response << "HTTP/1.1 " << status << " " << status_text << "\r\n";
	response << "Content-Type: " << content_type << "\r\n";
	response << "Content-Length: " << body.size() << "\r\n";
	response << "Connection: close\r\n\r\n";
	response << body;

	std::string out = response.str();
	send(fd, out.c_str(), out.size(), 0);
}

static std::string write_url_file(const std::string &worker_url, const std::string &name)
{
	std::string path = "/tmp/" + name + ".url";
	std::ofstream f(path);
	f << worker_url << "\n";
	f.close();
	return path;
}

static void *load_worker(const std::string &url_file_path, const std::string &worker_name)
{
	void *handle = NULL;
	const char *paths[] = { url_file_path.c_str() };

	std::cout << "[DEBUG][" << worker_name << "] Loading RPC handle from: " << url_file_path << "\n";

	if (metacall_load_from_file("rpc", paths, 1, &handle) != 0)
	{
		std::cerr << "[DEBUG][" << worker_name << "] metacall_load_from_file FAILED\n";
		return nullptr;
	}

	std::cout << "[DEBUG][" << worker_name << "] metacall_load_from_file OK, handle=" << handle << "\n";
	return handle;
}

int main()
{
	const int port = 8080;

	std::cout << "[DEBUG] Calling metacall_initialize()...\n";

	if (metacall_initialize() != 0)
	{
		std::cerr << "[DEBUG] metacall_initialize FAILED\n";
		return 1;
	}

	std::cout << "[DEBUG] metacall_initialize OK\n";
	std::cout << "[DEBUG] metacall_serial() = \"" << metacall_serial() << "\"\n";

	struct metacall_allocator_std_type std_ctx = { &std::malloc, &std::realloc, &std::free };
	void *allocator = metacall_allocator_create(METACALL_ALLOCATOR_STD, (void *)&std_ctx);

	if (allocator == nullptr)
	{
		std::cerr << "[DEBUG] metacall_allocator_create FAILED\n";
		metacall_destroy();
		return 1;
	}

	std::cout << "[DEBUG] Allocator created: " << allocator << "\n";

	const std::string worker_a_url = get_env_or_default("WORKER_A_URL", "http://worker-a:7777");
	const std::string worker_b_url = get_env_or_default("WORKER_B_URL", "http://worker-b:7778");
	const std::string worker_c_url = get_env_or_default("WORKER_C_URL", "http://worker-c:7779");

	std::string url_a = write_url_file(worker_a_url, "worker_a");
	std::string url_b = write_url_file(worker_b_url, "worker_b");
	std::string url_c = write_url_file(worker_c_url, "worker_c");

	std::cout << "[DEBUG] Loading workers via RPC loader (discover phase calls GET /inspect on each)...\n";

	void *handle_a = load_worker(url_a, "worker-a");
	void *handle_b = load_worker(url_b, "worker-b");
	void *handle_c = load_worker(url_c, "worker-c");

	if (handle_a == nullptr || handle_b == nullptr || handle_c == nullptr)
	{
		std::cerr << "[DEBUG] Failed to load one or more workers\n";
		metacall_allocator_destroy(allocator);
		metacall_destroy();
		return 1;
	}

	std::cout << "[DEBUG] All workers loaded. Handles: a=" << handle_a
	          << " b=" << handle_b << " c=" << handle_c << "\n";

	Registry registry;
	registry.add("add", handle_a, false);
	registry.add("add", handle_b, false);
	registry.add("add", handle_c, false);
	registry.add("multiply", handle_a, false);
	registry.add("multiply", handle_c, false);
	registry.add("slow_add", handle_b, true);
	registry.add("slow_add", handle_c, true);

	std::cout << "[DEBUG] Registry populated: add(3 handles), multiply(2 handles), slow_add(2 handles)\n";

	Router router(registry, allocator);
	std::cout << "[DEBUG] Router created with MetaCall allocator\n";

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		std::cerr << "socket failed\n";
		metacall_allocator_destroy(allocator);
		metacall_destroy();
		return 1;
	}

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		std::cerr << "bind failed\n";
		close(server_fd);
		metacall_allocator_destroy(allocator);
		metacall_destroy();
		return 1;
	}

	if (listen(server_fd, 64) < 0)
	{
		std::cerr << "listen failed\n";
		close(server_fd);
		metacall_allocator_destroy(allocator);
		metacall_destroy();
		return 1;
	}

	std::cout << "routing-hub listening on :" << port << "\n";

	while (true)
	{
		int client_fd = accept(server_fd, nullptr, nullptr);
		if (client_fd < 0)
		{
			continue;
		}

		std::string method, path, body;
		if (!read_http_request(client_fd, method, path, body))
		{
			write_http_response(client_fd, 500, "application/json", "{\"error\":\"bad-request\"}");
			close(client_fd);
			continue;
		}

		RouteResult result = router.handle(method, path, body);
		write_http_response(client_fd, result.status, result.content_type, result.body);
		close(client_fd);
	}

	close(server_fd);
	metacall_allocator_destroy(allocator);
	metacall_destroy();
	return 0;
}
