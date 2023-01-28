cmake_minimum_required(VERSION 3.20)
project(single_tuner_recorder LANGUAGES C VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LibSDRplay)

if (NOT LIBSDRPLAY_FOUND)
    message(FATAL_ERROR "SDRPlay development files not found...")
endif ()
message(STATUS "LIBSDRPLAY_INCLUDE_DIRS - ${LIBSDRPLAY_INCLUDE_DIRS}")
message(STATUS "LIBSDRPLAY_LIBRARIES - ${LIBSDRPLAY_LIBRARIES}")

set(CMAKE_BUILD_TYPE Release)
add_compile_options(-Wall -Wextra -pedantic -Werror)

set(SOURCE_FILES single_tuner_recorder.c)
include_directories(${LIBSDRPLAY_INCLUDE_DIRS})

add_executable(single_tuner_recorder ${SOURCE_FILES})
target_link_libraries(single_tuner_recorder ${LIBSDRPLAY_LIBRARIES})
