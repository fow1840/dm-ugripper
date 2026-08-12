cmake_minimum_required(VERSION 3.10)
project(utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules(FMT REQUIRED IMPORTED_TARGET fmt)

if(NOT TARGET PP::utils)
    if(NOT DEFINED PP_MAIN_ROOT)
        set(_pp_main_candidates
            "${CMAKE_SOURCE_DIR}/../pp-main"
            "${CMAKE_SOURCE_DIR}/../.."
        )
        foreach(_candidate IN LISTS _pp_main_candidates)
            get_filename_component(_candidate_abs "${_candidate}" ABSOLUTE)
            if(EXISTS "${_candidate_abs}/src/utils/logger.h")
                set(PP_MAIN_ROOT "${_candidate_abs}" CACHE PATH
                    "Path to the immutable pp-main source tree used by the logger target")
                break()
            endif()
        endforeach()
    endif()

    if(NOT DEFINED PP_MAIN_ROOT)
        message(FATAL_ERROR
            "Unable to locate pp-main. Set -DPP_MAIN_ROOT=/path/to/pp-main")
    endif()

    set(PP_MAIN_UTILS_DIR "${PP_MAIN_ROOT}/src/utils")
    foreach(required_file IN ITEMS spd_logger.cc spd_logger.h log_fmtter.cc log_fmtter.h logger.h)
        if(NOT EXISTS "${PP_MAIN_UTILS_DIR}/${required_file}")
            message(FATAL_ERROR
                "PP_MAIN_ROOT does not contain src/utils/${required_file}: ${PP_MAIN_ROOT}")
        endif()
    endforeach()

    add_library(pp_main_logger STATIC
        "${PP_MAIN_UTILS_DIR}/log_fmtter.cc"
        "${PP_MAIN_UTILS_DIR}/spd_logger.cc"
    )
    set_target_properties(pp_main_logger PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_include_directories(pp_main_logger
        PUBLIC
            "${PP_MAIN_UTILS_DIR}"
    )
    target_link_libraries(pp_main_logger
        PUBLIC
            PkgConfig::SPDLOG
            PkgConfig::FMT
    )
    add_library(PP::utils ALIAS pp_main_logger)
endif()

add_library(utils
    src/env_utils.cc
    src/fifo_utils.cc
    src/file_logger.cc
    src/time_utils.cc
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(utils
    PUBLIC
        PP::utils
        PkgConfig::SPDLOG
        PkgConfig::FMT
)
add_library(UGripper::utils ALIAS utils)

add_executable(utils_smoke_test
    src/utils_smoke_test.cc
)

target_link_libraries(utils_smoke_test
    PRIVATE
        utils
)
