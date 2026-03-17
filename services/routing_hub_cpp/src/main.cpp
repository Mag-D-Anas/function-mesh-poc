#include "registry.hpp"
#include "router.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
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

int main()
{
	const int port = 8080;
	const long timeout_ms = std::stol(get_env_or_default("HUB_TIMEOUT_MS", "1200"));
	const int retry_count = std::stoi(get_env_or_default("HUB_RETRY_COUNT", "1"));

	const std::string worker_a = get_env_or_default("WORKER_A_URL", "http://worker-a:7777");
	const std::string worker_b = get_env_or_default("WORKER_B_URL", "http://worker-b:7778");
	const std::string worker_c = get_env_or_default("WORKER_C_URL", "http://worker-c:7779");

	Registry registry;
	registry.add("add", worker_a, false);
	registry.add("add", worker_b, false);
	registry.add("add", worker_c, false);
	registry.add("multiply", worker_a, false);
	registry.add("multiply", worker_c, false);
	registry.add("slow_add", worker_b, true);
	registry.add("slow_add", worker_c, true);

	Router router(registry, timeout_ms, retry_count);

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		std::cerr << "socket failed\n";
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
		return 1;
	}

	if (listen(server_fd, 64) < 0)
	{
		std::cerr << "listen failed\n";
		close(server_fd);
		return 1;
	}

	std::cout << "routing-hub listening on :8080\n";

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
	return 0;
}
