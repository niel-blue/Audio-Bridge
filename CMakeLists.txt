cmake_minimum_required(VERSION 3.25)

project(AudioBridge VERSION 0.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AUDIO_BRIDGE_VST3_SDK "" CACHE PATH
    "Optional path to a local VST3 SDK checkout")
set(AUDIO_BRIDGE_VST3_SDK_GIT_TAG
    "9fad9770f2ae8542ab1a548a68c1ad1ac690abe0" CACHE STRING
    "Pinned VST3 SDK commit used when the SDK is downloaded")
mark_as_advanced(AUDIO_BRIDGE_VST3_SDK_GIT_TAG)

set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES 0 CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT 1 CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL
    "Do not create a system-wide VST3 symlink during local builds" FORCE)
set(SMTG_PLUGIN_TARGET_USER_PATH "${CMAKE_BINARY_DIR}/plugin-links" CACHE PATH
    "Local VST3 link destination" FORCE)

set(_audio_bridge_sdk_added FALSE)
if(AUDIO_BRIDGE_VST3_SDK STREQUAL "")
  set(_audio_bridge_local_sdk "${CMAKE_CURRENT_SOURCE_DIR}/lib/vst3sdk")
  if(EXISTS "${_audio_bridge_local_sdk}/CMakeLists.txt")
    set(AUDIO_BRIDGE_VST3_SDK "${_audio_bridge_local_sdk}")
  else()
    include(FetchContent)
    FetchContent_Declare(vst3sdk
      GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
      GIT_TAG "${AUDIO_BRIDGE_VST3_SDK_GIT_TAG}"
      GIT_SUBMODULES_RECURSE TRUE
      GIT_PROGRESS TRUE
    )
    message(STATUS
      "AUDIO_BRIDGE_VST3_SDK is not set; downloading the pinned VST3 SDK")
    FetchContent_MakeAvailable(vst3sdk)
    set(AUDIO_BRIDGE_VST3_SDK "${vst3sdk_SOURCE_DIR}")
    set(_audio_bridge_sdk_added TRUE)
  endif()
endif()

if(NOT EXISTS "${AUDIO_BRIDGE_VST3_SDK}/CMakeLists.txt")
  message(FATAL_ERROR
    "VST3 SDK was not found at '${AUDIO_BRIDGE_VST3_SDK}'. "
    "Set AUDIO_BRIDGE_VST3_SDK to a valid SDK checkout or leave it empty "
    "to download the pinned SDK.")
endif()

if(MSVC)
  add_compile_options(/utf-8 /permissive-)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

set(vst3sdk_SOURCE_DIR "${AUDIO_BRIDGE_VST3_SDK}")
if(NOT _audio_bridge_sdk_added)
  add_subdirectory("${AUDIO_BRIDGE_VST3_SDK}" "${CMAKE_BINARY_DIR}/vst3sdk")
endif()
smtg_enable_vst3_sdk()

configure_file(src/metadata.h.in src/metadata.h @ONLY)

set(target audio_bridge)
smtg_add_vst3plugin(${target} PACKAGE_NAME "Audio-Bridge" SOURCES_LIST
  # VST3 component and controller
  src/factory.cc
  src/processor.cc
  src/controller.cc

  # Editor and reusable UI controls
  src/editor.cc
  src/editor_widgets.cc

  # Host-owned state and controller/processor messages
  src/io_state.cc
  src/vst_messages.cc

  # Audio processing and Windows device backends
  src/output_delay.cc
  src/application_input.cc
  src/audio_device_catalog.cc
  src/audio_io.cc
  src/legacy_io.cc
  src/wasapi_io.cc
)

target_include_directories(${target} PRIVATE
  src
  "${AUDIO_BRIDGE_VST3_SDK}"
  "${CMAKE_CURRENT_BINARY_DIR}/src"
)

target_link_libraries(${target} PRIVATE sdk vstgui_support)

if(WIN32)
  target_link_libraries(${target} PRIVATE ole32 uuid winmm dsound dxguid mmdevapi)
endif()

set_property(TARGET ${target} PROPERTY CXX_EXTENSIONS OFF)

enable_testing()
add_executable(additional_routes_test tests/additional_routes_test.cc src/output_delay.cc)
target_include_directories(additional_routes_test PRIVATE src)
add_test(NAME additional_routes_test COMMAND additional_routes_test)
