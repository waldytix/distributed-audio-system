cmake_minimum_required(VERSION 3.16)

project(DistributedAudioSystem
    VERSION 0.1.0
    DESCRIPTION "Foundation for a distributed network audio system"
    LANGUAGES CXX
)

find_package(Threads REQUIRED)

function(enable_project_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endfunction()

add_library(distributed_audio_core
    src/audio_frame.cpp
    src/audio_buffer.cpp
    src/pcm_conversion.cpp
    src/gain_processor.cpp
    src/channel_mixer.cpp
    src/audio_mixer.cpp
    src/audio_meter.cpp
    src/dsp_pipeline.cpp
    src/network_protocol.cpp
    src/packet_serializer.cpp
    src/sequence_tracker.cpp
    src/udp_socket.cpp
    src/udp_sender.cpp
    src/udp_receiver.cpp
    src/clock.cpp
    src/timing_utils.cpp
    src/jitter_buffer.cpp
    src/clock_estimator.cpp
    src/playback_scheduler.cpp
    src/simulated_playback.cpp
    src/network_impairment.cpp
    src/multi_device_sync.cpp
)

target_compile_features(distributed_audio_core PUBLIC cxx_std_17)
target_include_directories(distributed_audio_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(distributed_audio_core PUBLIC Threads::Threads)
enable_project_warnings(distributed_audio_core)

add_executable(distributed_audio_system
    src/main.cpp
)
target_link_libraries(distributed_audio_system PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_system)

enable_testing()

add_executable(distributed_audio_tests
    tests/audio_tests.cpp
)
target_link_libraries(distributed_audio_tests PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_tests)

add_test(NAME distributed_audio_tests COMMAND distributed_audio_tests)

add_executable(distributed_audio_dsp_tests
    tests/dsp_tests.cpp
)
target_link_libraries(distributed_audio_dsp_tests PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_dsp_tests)

add_test(NAME distributed_audio_dsp_tests COMMAND distributed_audio_dsp_tests)

add_executable(distributed_audio_network_tests
    tests/network_tests.cpp
)
target_link_libraries(distributed_audio_network_tests PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_network_tests)

add_test(NAME distributed_audio_network_tests COMMAND distributed_audio_network_tests)

add_executable(distributed_audio_timing_tests
    tests/timing_tests.cpp
)
target_link_libraries(distributed_audio_timing_tests PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_timing_tests)

add_test(NAME distributed_audio_timing_tests COMMAND distributed_audio_timing_tests)

add_executable(distributed_audio_multi_device_tests
    tests/multi_device_tests.cpp
)
target_link_libraries(distributed_audio_multi_device_tests PRIVATE distributed_audio_core)
enable_project_warnings(distributed_audio_multi_device_tests)

add_test(NAME distributed_audio_multi_device_tests COMMAND distributed_audio_multi_device_tests)
