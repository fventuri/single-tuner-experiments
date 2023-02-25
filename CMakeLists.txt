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

include_directories(${LIBSDRPLAY_INCLUDE_DIRS})

set(SOURCE_FILES_RECORDER single_tuner_recorder.c)
add_executable(single_tuner_recorder ${SOURCE_FILES_RECORDER})
target_link_libraries(single_tuner_recorder ${LIBSDRPLAY_LIBRARIES})

set(SOURCE_FILES_GAIN_CHANGES single_tuner_gain_changes.c)
add_executable(single_tuner_gain_changes ${SOURCE_FILES_GAIN_CHANGES})
target_link_libraries(single_tuner_gain_changes ${LIBSDRPLAY_LIBRARIES})
