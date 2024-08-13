cmake_minimum_required(VERSION 3.10)
project(ac-display)

file(GLOB_RECURSE sources src/*.cpp)
file(GLOB_RECURSE sources_test src/ac_data.cpp src/ac_display.cpp src/acudp_thread.cpp src/debug_sine_wave_update_thread.cpp src/ip_address.cpp src/settings.cpp src/util.cpp src/web_server.cpp test/src/*.cpp)

# Add the sources to the target
add_executable(ac-display ${sources})

set(APP_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include)

set_property(TARGET ac-display PROPERTY INCLUDE_DIRECTORIES ${APP_INCLUDE_DIRECTORIES})

# Debug build
SET(CMAKE_BUILD_TYPE Debug)
ADD_DEFINITIONS("-DDEBUG")

# Release build
#SET(CMAKE_BUILD_TYPE Release)
#ADD_DEFINITIONS("-DNDEBUG")

# Select flags
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

IF(UNIX)
  # Add definitions, compiler switches, etc.
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -W -Wextra -Werror -Wformat -Wformat-y2k -Winit-self -Wstack-protector -Wunknown-pragmas -Wundef -Wwrite-strings -Wno-unused-parameter -Wpointer-arith -Wno-switch -Woverloaded-virtual -Wno-stack-protector -Wmissing-include-dirs -Wuninitialized -O2")

  SET(COMPILER_PLATFORM_DEFINES "-D__LINUX__")
ENDIF()

IF(WIN32)
  SET(COMPILER_PLATFORM_DEFINES "-DUNICODE -D_UNICODE -DNOMINMAX")
ENDIF()

SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}")
SET(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
SET(CMAKE_LINKER_FLAGS_RELEASE "${CMAKE_LINKER_FLAGS_RELEASE}")
SET(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG}")

IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_RELEASE}")
ELSE()
SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG}")
ENDIF()

IF(WIN32)
  target_compile_options(ac-display PUBLIC "${COMPILER_PLATFORM_DEFINES}")
ENDIF()

###############################################################################
## dependencies ###############################################################
###############################################################################

# libsecurityheaders library
set(SECURITYHEADERS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/../libsecurityheaders/include")

# System microhttpd library
#set(MICROHTTPD_LIBRARY "-lmicrohttpd")
#find_package(MICROHTTPD REQUIRED)

# Our microhttpd library with websockets enabled
set(MICROHTTPD_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/output/include")
set(MICROHTTPD_LIB_DIR "${PROJECT_SOURCE_DIR}/output/lib")

target_include_directories(ac-display SYSTEM PUBLIC ${MICROHTTPD_INCLUDE_DIR} ${SECURITYHEADERS_INCLUDE_DIR})
target_link_directories(ac-display PUBLIC ${MICROHTTPD_LIB_DIR})
target_link_libraries(ac-display PUBLIC acudp microhttpd microhttpd_ws json-c)


###############################################################################
## testing ####################################################################
###############################################################################

# this is for our testing framework
# we don't add REQUIRED because it's just for testing
find_package(GTest)

add_executable(unit_tests ${sources_test})

# we add this define to prevent collision with the main
# this might be better solved by not adding the source with the main to the
# testing target
target_compile_definitions(unit_tests PUBLIC UNIT_TESTS)

# this allows us to use our executable as a link library
# therefore we can inherit all compiler options and library dependencies
set_target_properties(ac-display PROPERTIES ENABLE_EXPORTS on)

set_property(TARGET unit_tests PROPERTY INCLUDE_DIRECTORIES ${APP_INCLUDE_DIRECTORIES} ${CMAKE_SOURCE_DIR}/test/include)

target_include_directories(unit_tests SYSTEM PUBLIC ${MICROHTTPD_INCLUDE_DIR} ${SECURITYHEADERS_INCLUDE_DIR})
target_link_directories(unit_tests PUBLIC ${MICROHTTPD_LIB_DIR})

target_link_libraries(unit_tests PUBLIC ${GTEST_BOTH_LIBRARIES} acudp gnutls gnutlsxx microhttpd microhttpd_ws json-c)

target_include_directories(unit_tests PUBLIC
  ${GTEST_INCLUDE_DIRS} # doesn't do anything on Linux
)
