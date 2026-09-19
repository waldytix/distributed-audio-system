cmake_minimum_required(VERSION 3.16)

project(DistributedAudioSystem
    VERSION 0.1.0
    DESCRIPTION "Foundation for a distributed network audio system"
    LANGUAGES CXX
)

add_executable(distributed_audio_system
    src/main.cpp
)

target_compile_features(distributed_audio_system PRIVATE cxx_std_17)

target_compile_options(distributed_audio_system PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_include_directories(distributed_audio_system PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
