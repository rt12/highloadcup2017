cmake_minimum_required (VERSION 2.6)
project (HighloadCup)

set (CMAKE_CXX_STANDARD 14)
set(Boost_USE_STATIC_LIBS        ON) # only find static libs
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME    OFF)

find_package(Boost 1.58 COMPONENTS system REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
set(GITHUB "~/projects/github")

include_directories("${GITHUB}/rapidjson/include")
add_executable(hlcpp main.cpp connection.cpp database.cpp loader.cpp handler.cpp server_epoll.cpp picohttpparser.c)
target_link_libraries(hlcpp ${Boost_LIBRARIES} http_parser.a pthread)

