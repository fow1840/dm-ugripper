find_package(Threads REQUIRED)
set(EPISODE_MCAP_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../SensorRecorder/third_party/mcap/include")
set(EPISODE_MCAP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../SensorRecorder/third_party/mcap/src/lib.cc")

add_library(episode_mcap_support STATIC
    src/episode_mcap_client.cc
    src/ros2_cdr_writer.cc
)
set_target_properties(episode_mcap_support PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(episode_mcap_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(episode_mcap_vendor STATIC
    ${EPISODE_MCAP_SOURCE}
)
set_target_properties(episode_mcap_vendor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(episode_mcap_vendor
    PUBLIC
        "${EPISODE_MCAP_INCLUDE_DIR}"
)
target_compile_definitions(episode_mcap_vendor PUBLIC MCAP_COMPRESSION_NO_LZ4 MCAP_COMPRESSION_NO_ZSTD)

add_executable(EpisodeMcapRecorder
    src/episode_mcap_recorder.cc
)
set_target_properties(EpisodeMcapRecorder PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(EpisodeMcapRecorder
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(EpisodeMcapRecorder
    PRIVATE
        episode_mcap_support
        episode_mcap_vendor
        Threads::Threads
)

install(TARGETS EpisodeMcapRecorder
    RUNTIME DESTINATION bin/EpisodeMcap
)
