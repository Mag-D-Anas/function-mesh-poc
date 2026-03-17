cmake_minimum_required(VERSION 3.15)
project(routing_hub_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_executable(routing_hub
	src/main.cpp
	src/router.cpp
	src/registry.cpp
	src/policy_round_robin.cpp
)

target_include_directories(routing_hub PRIVATE include)
target_link_libraries(routing_hub PRIVATE CURL::libcurl)
