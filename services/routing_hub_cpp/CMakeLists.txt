cmake_minimum_required(VERSION 3.15)
project(routing_hub_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_library(METACALL_LIBRARY metacall HINTS ENV LOADER_LIBRARY_PATH /usr/local/lib)
find_path(METACALL_INCLUDE_DIR metacall/metacall.h HINTS /usr/local/include)

if(NOT METACALL_LIBRARY)
	message(FATAL_ERROR "libmetacall not found. Set LOADER_LIBRARY_PATH or install MetaCall.")
endif()

add_executable(routing_hub
	src/main.cpp
	src/router.cpp
	src/registry.cpp
	src/policy_round_robin.cpp
)

target_include_directories(routing_hub PRIVATE include ${METACALL_INCLUDE_DIR})
target_link_libraries(routing_hub PRIVATE CURL::libcurl ${METACALL_LIBRARY})
