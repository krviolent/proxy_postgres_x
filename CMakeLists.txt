cmake_minimum_required(VERSION 3.10)

#set the project name
project(proxy_postgres_x LANGUAGES C CXX)

set(CMAKE_CXX_COMPILER /usr/bin/gcc CACHE PATH "")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lstdc++ -O2")

include_directories(include)

#target_include_directories(proxy_postgres_x PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#add the executable
add_executable(proxy_postgres_x src/main.cpp include/parse_postgres_request.h include/headers.h)