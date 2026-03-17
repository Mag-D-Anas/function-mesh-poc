cmake_minimum_required(VERSION 3.15)
project(mesh_client C)

add_executable(mesh_client ../mesh_client.c)

target_include_directories(mesh_client PRIVATE /usr/local/include)
target_link_directories(mesh_client PRIVATE /usr/local/lib)
target_link_libraries(mesh_client PRIVATE metacall pthread)
