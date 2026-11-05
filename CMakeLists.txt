cmake_minimum_required(VERSION 3.28)
project(juce_webgpu LANGUAGES CXX)

set(JUCE_WEBGPU_WGPU_TAG "v29.0.1.1" CACHE STRING "wgpu-native release tag")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/WgpuNative.cmake)
wgpu_native_fetch(${JUCE_WEBGPU_WGPU_TAG})

add_library(juce_webgpu INTERFACE)
add_library(juce_webgpu::juce_webgpu ALIAS juce_webgpu)

set(_juce_webgpu_dir ${CMAKE_CURRENT_LIST_DIR}/WebGPU)

target_sources(juce_webgpu INTERFACE
        ${_juce_webgpu_dir}/WebGPUDevice.cpp
        ${_juce_webgpu_dir}/WebGPUDevice.h
        ${_juce_webgpu_dir}/WebGPUContext.cpp
        ${_juce_webgpu_dir}/WebGPUContext.h
        ${_juce_webgpu_dir}/WebGPUNativeSurface.h
)

if(WIN32)
    target_sources(juce_webgpu INTERFACE ${_juce_webgpu_dir}/WebGPUNativeSurface_win.cpp)
elseif(APPLE)
    target_sources(juce_webgpu INTERFACE ${_juce_webgpu_dir}/WebGPUNativeSurface_mac.mm)
endif()

target_include_directories(juce_webgpu INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_compile_features(juce_webgpu INTERFACE cxx_std_20)
target_link_libraries(juce_webgpu INTERFACE wgpu_native)
