cmake_minimum_required(VERSION 3.10)
project(utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules(FMT REQUIRED IMPORTED_TARGET fmt)
find_package(Threads REQUIRED)
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
    foreach(required_file IN ITEMS
            log_fmtter.cc
            single_process_instance.cc
            spd_logger.cc
            logger.h
            single_process_instance.h)
        if(NOT EXISTS "${PP_MAIN_UTILS_DIR}/${required_file}")
            message(FATAL_ERROR
                "PP_MAIN_ROOT does not contain src/utils/${required_file}: ${PP_MAIN_ROOT}")
        endif()
    endforeach()

    if(CMAKE_CROSSCOMPILING)
        set(PP_MAIN_BUILD_PLATFORM arm)
    else()
        set(PP_MAIN_BUILD_PLATFORM x86)
    endif()
    set(PP_MAIN_UTILS_LIBRARY
        "${PP_MAIN_ROOT}/build/${PP_MAIN_BUILD_PLATFORM}/core/utils/libutils.a")

    if(EXISTS "${PP_MAIN_UTILS_LIBRARY}")
        if(PP_MAIN_BUILD_PLATFORM STREQUAL "arm")
            set(PP_MAIN_CONAN_ARCH armv8)
        else()
            set(PP_MAIN_CONAN_ARCH x86_64)
        endif()
        set(PP_MAIN_CONAN_GENERATORS
            "${PP_MAIN_ROOT}/cmake/${PP_MAIN_BUILD_PLATFORM}/build/Release/generators")
        set(PP_MAIN_FMT_DATA
            "${PP_MAIN_CONAN_GENERATORS}/fmt-release-${PP_MAIN_CONAN_ARCH}-data.cmake")
        set(PP_MAIN_SPDLOG_DATA
            "${PP_MAIN_CONAN_GENERATORS}/spdlog-release-${PP_MAIN_CONAN_ARCH}-data.cmake")
        set(PP_MAIN_YAML_DATA
            "${PP_MAIN_CONAN_GENERATORS}/yaml-cpp-release-${PP_MAIN_CONAN_ARCH}-data.cmake")
        set(PP_MAIN_JSON_DATA
            "${PP_MAIN_CONAN_GENERATORS}/nlohmann_json-release-${PP_MAIN_CONAN_ARCH}-data.cmake")
        if(NOT EXISTS "${PP_MAIN_FMT_DATA}"
           OR NOT EXISTS "${PP_MAIN_SPDLOG_DATA}"
           OR NOT EXISTS "${PP_MAIN_YAML_DATA}"
           OR NOT EXISTS "${PP_MAIN_JSON_DATA}")
            message(FATAL_ERROR
                "pp-main PP::utils dependency metadata is missing under ${PP_MAIN_CONAN_GENERATORS}")
        endif()
        include("${PP_MAIN_FMT_DATA}")
        include("${PP_MAIN_SPDLOG_DATA}")
        include("${PP_MAIN_YAML_DATA}")
        include("${PP_MAIN_JSON_DATA}")
        set(PP_MAIN_FMT_LIBRARY "${fmt_LIB_DIRS_RELEASE}/libfmt.a")
        set(PP_MAIN_SPDLOG_LIBRARY "${spdlog_LIB_DIRS_RELEASE}/libspdlog.a")
        set(PP_MAIN_YAML_LIBRARY "${yaml-cpp_LIB_DIRS_RELEASE}/libyaml-cpp.a")
        foreach(required_path IN ITEMS
                "${fmt_INCLUDE_DIRS_RELEASE}"
                "${spdlog_INCLUDE_DIRS_RELEASE}"
                "${yaml-cpp_INCLUDE_DIRS_RELEASE}"
                "${nlohmann_json_INCLUDE_DIRS_RELEASE}"
                "${PP_MAIN_FMT_LIBRARY}"
                "${PP_MAIN_SPDLOG_LIBRARY}"
                "${PP_MAIN_YAML_LIBRARY}")
            if(NOT EXISTS "${required_path}")
                message(FATAL_ERROR
                    "pp-main PP::utils dependency path does not exist: ${required_path}")
            endif()
        endforeach()

        add_library(PP::utils STATIC IMPORTED GLOBAL)
        set_target_properties(PP::utils PROPERTIES
            IMPORTED_LOCATION "${PP_MAIN_UTILS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${PP_MAIN_UTILS_DIR};${PP_MAIN_ROOT}/src;${spdlog_INCLUDE_DIRS_RELEASE};${fmt_INCLUDE_DIRS_RELEASE};${yaml-cpp_INCLUDE_DIRS_RELEASE};${nlohmann_json_INCLUDE_DIRS_RELEASE}"
            INTERFACE_COMPILE_DEFINITIONS "SPDLOG_FMT_EXTERNAL;SPDLOG_COMPILED_LIB"
            INTERFACE_LINK_LIBRARIES
                "${PP_MAIN_SPDLOG_LIBRARY};${PP_MAIN_FMT_LIBRARY};${PP_MAIN_YAML_LIBRARY};Threads::Threads"
        )
        message(STATUS "UGripper: directly linking pp-main PP::utils at ${PP_MAIN_UTILS_LIBRARY}")
    else()
        add_library(pp_main_utils_bridge STATIC
            "${PP_MAIN_UTILS_DIR}/log_fmtter.cc"
            "${PP_MAIN_UTILS_DIR}/single_process_instance.cc"
            "${PP_MAIN_UTILS_DIR}/spd_logger.cc"
        )
        set_target_properties(pp_main_utils_bridge PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_include_directories(pp_main_utils_bridge
            PUBLIC
                "${PP_MAIN_UTILS_DIR}"
                "${PP_MAIN_ROOT}/src"
        )
        target_link_libraries(pp_main_utils_bridge
            PUBLIC
                PkgConfig::SPDLOG
                PkgConfig::FMT
        )
        add_library(PP::utils ALIAS pp_main_utils_bridge)
        message(WARNING
            "pp-main PP::utils build artifact not found at ${PP_MAIN_UTILS_LIBRARY}; "
            "using the source-identical logger/single-instance bridge for this host build")
    endif()
endif()

add_library(ugripper_utils
    src/env_utils.cc
    src/fifo_utils.cc
    src/file_logger.cc
    src/time_utils.cc
)

target_include_directories(ugripper_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ugripper_utils
    PUBLIC
        PP::utils
)
add_library(UGripper::utils ALIAS ugripper_utils)

add_executable(utils_smoke_test
    src/utils_smoke_test.cc
)

target_link_libraries(utils_smoke_test
    PRIVATE
        ugripper_utils
)
