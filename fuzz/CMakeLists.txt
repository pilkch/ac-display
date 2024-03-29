# Set the minimum cmake version
CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

SET(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

project(fuzz_ac_display)

add_compile_options(-std=c++20 -Wall -W -Wextra -Werror -Wformat -Wformat-y2k -Winit-self -Wstack-protector -Wunknown-pragmas -Wundef -Wwrite-strings -Wno-unused-parameter -Wpointer-arith -Wno-switch -Woverloaded-virtual -Wno-stack-protector -Wmissing-include-dirs -Wuninitialized -O2)

INCLUDE(FindPkgConfig)


INCLUDE_DIRECTORIES(../include/)
link_directories(../)

file(GLOB_RECURSE ac_display_sources ../src/ac_data.cpp ../src/ac_display.cpp ../src/acudp_thread.cpp ../src/debug_sine_wave_update_thread.cpp ../src/ip_address.cpp ../src/settings.cpp ../src/util.cpp ../src/web_server.cpp)

###############################################################################
## dependencies ###############################################################
###############################################################################

# System microhttpd library
#set(MICROHTTPD_LIBRARY "-lmicrohttpd")
#find_package(MICROHTTPD REQUIRED)

# Our microhttpd library with websockets enabled
set(MICROHTTPD_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/../output/include")
set(MICROHTTPD_LIB_DIR "${PROJECT_SOURCE_DIR}/../output/lib")

# Fuzz Web Server HTTPS

ADD_EXECUTABLE(fuzz_web_server_https ${ac_display_sources} ./src/fuzz_webserver_https.cpp)

target_compile_options(fuzz_web_server_https PRIVATE -fsanitize=address,fuzzer)
target_link_options(fuzz_web_server_https PRIVATE -fsanitize=address,fuzzer)

target_include_directories(fuzz_web_server_https SYSTEM PUBLIC include ${MICROHTTPD_INCLUDE_DIR})
target_link_directories(fuzz_web_server_https PUBLIC ${MICROHTTPD_LIB_DIR})
target_link_libraries(fuzz_web_server_https PRIVATE -fsanitize=address,fuzzer acudp microhttpd microhttpd_ws json-c)
