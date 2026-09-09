cmake_minimum_required(VERSION 3.28...4.12)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTS "Enable tests" OFF)
option(ENABLE_WEBVTT "Enable WebVTT embedding" ON)

option(USE_SYSTEM_CURL "Use system cURL" OFF)
option(USE_SYSTEM_ICU "Use system ICU" OFF)
option(USE_SYSTEM_ONNXRUNTIME "Use system ONNX Runtime" OFF)
option(USE_SYSTEM_WHISPERCPP "Use system Whisper.cpp library" OFF)
option(USE_SYSTEM_CTRANSLATE2 "Use system CTranslate2 library" OFF)
option(USE_SYSTEM_SENTENCEPIECE "Use system sentencepiece library" OFF)
option(LINUX_SOURCE_BUILD
       "Build Whispercpp dependency from source on Linux instead of using one of the precompiled variants" OFF)
option(
  WHISPER_DYNAMIC_BACKENDS
  "Build backends for all CPU variants when building Whispercpp from source, instead of only building one CPU backend optimised for the host system"
  OFF)
option(
  WHISPER_BUILD_ALL_CUDA_ARCHITECTURES
  "Build CUDA backend for all NVidia architectures when building Whispercpp from source, instead of only building for the host system's GPU"
  OFF)

# Doesn't seem to do anything set(DISABLE_ONNXRUNTIME_GPU OFF CACHE STRING "Disables GPU support of ONNX Runtime (Only
# valid on Linux)")

if(CMAKE_MAJOR_VERSION EQUAL 4)
  message(STATUS "CMake 4 detected")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

if(USE_SYSTEM_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${CURL_LIBRARIES}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${CURL_INCLUDE_DIRS}")
else()
  include(cmake/BuildMyCurl.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libcurl)
endif()

if(DEFINED ENV{ACCELERATION})
  set(ACCELERATION
      $ENV{ACCELERATION}
      CACHE STRING "Acceleration to use" FORCE)
endif()
if(NOT DEFINED ACCELERATION)
  set(ACCELERATION
      "generic"
      CACHE STRING "Acceleration to use")
endif()
set_property(CACHE ACCELERATION PROPERTY STRINGS "generic" "nvidia" "amd")

if(UNIX AND NOT APPLE)
  set(BLA_VENDOR OpenBLAS)
  set(BLA_PREFER_PKGCONFIG ON)
  set(BLA_PKGCONFIG_BLAS openblas)
  find_package(BLAS REQUIRED)
endif()

include(cmake/BuildWhispercpp.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Whispercpp)

include(cmake/BuildCTranslate2.cmake)
include(cmake/BuildSentencepiece.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ct2 sentencepiece)

# Doesn't seem to do anything if(DISABLE_ONNXRUNTIME_GPU) target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
# DISABLE_ONNXRUNTIME_GPU) endif()

if(USE_SYSTEM_ONNXRUNTIME)
  if(OS_LINUX)
    find_package(Onnxruntime 1.20.1 REQUIRED)
    set(Onnxruntime_INCLUDE_PATH
        ${Onnxruntime_INCLUDE_DIR} ${Onnxruntime_INCLUDE_DIR}/onnxruntime
        ${Onnxruntime_INCLUDE_DIR}/onnxruntime/core/session ${Onnxruntime_INCLUDE_DIR}/onnxruntime/core/providers/cpu)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${Onnxruntime_LIBRARIES}")
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${Onnxruntime_INCLUDE_PATH}")
  else()
    message(FATAL_ERROR "System ONNX Runtime is only supported on Linux!")
  endif()
else()
  include(cmake/FetchOnnxruntime.cmake)
endif()

if(USE_SYSTEM_ICU)
  find_package(
    ICU 70.1
    COMPONENTS data uc i18n
    REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${ICU_LIBRARIES}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${ICU_INCLUDE_DIRS}")
else()
  find_library(ICU_DATA_LIB icudata OPTIONAL)
  find_library(ICU_UC_LIB icuuc OPTIONAL)
  find_library(ICU_I18N_LIB icui18n OPTIONAL)

  if(ICU_DATA_LIB
     AND ICU_UC_LIB
     AND ICU_I18N_LIB)
    list(APPEND ICU_LIBRARIES ${ICU_DATA_LIB} ${ICU_UC_LIB} ${ICU_I18N_LIB})
    message(STATUS "Cached ICU libs: " ${ICU_LIBRARIES})
    set(ICU_INCLUDE_DIR "${CMAKE_BINARY_DIR}/ICU_build-prefix/include")
    message(STATUS "Cached ICU include dir: " ${ICU_INCLUDE_DIR})

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${ICU_LIBRARIES}")
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC ${ICU_INCLUDE_DIR})
  else()
    include(cmake/BuildICU.cmake)
    # Add ICU to the target
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ICU)
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC ${ICU_INCLUDE_DIR})
  endif()
endif()

# check env var for extra verbose logging
if(DEFINED ENV{LOCALVOCAL_EXTRA_VERBOSE})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE LOCALVOCAL_EXTRA_VERBOSE)
endif()

if(ENABLE_WEBVTT)
  include(cmake/BuildWebVTT.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE c_webvtt_in_video_stream)
  target_compile_definitions(c_webvtt_in_video_stream INTERFACE ENABLE_WEBVTT)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.c
          src/transcription-filter.cpp
          src/transcription-filter.c
          src/transcription-filter-callbacks.cpp
          src/transcription-filter-properties.cpp
          src/transcription-filter-utils.cpp
          src/transcription-utils.cpp
          src/model-utils/model-downloader.cpp
          src/model-utils/model-downloader-ui.cpp
          src/model-utils/model-infos.cpp
          src/model-utils/model-find-utils.cpp
          src/model-utils/sha256.cpp
          src/whisper-utils/whisper-processing.cpp
          src/whisper-utils/whisper-utils.cpp
          src/whisper-utils/whisper-model-utils.cpp
          src/whisper-utils/whisper-params.cpp
          src/whisper-utils/silero-vad-onnx.cpp
          src/whisper-utils/token-buffer-thread.cpp
          src/whisper-utils/vad-processing.cpp
          src/translation/language_codes.cpp
          src/translation/translation.cpp
          src/translation/translation-utils.cpp
          src/ui/filter-replace-utils.cpp
          src/translation/translation-language-utils.cpp
          src/ui/filter-replace-dialog.cpp)

add_subdirectory(src/translation/cloud-translation)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(APPLE)
  # Brute force ftw
  add_custom_command(
    TARGET "${CMAKE_PROJECT_NAME}"
    POST_BUILD VERBATIM
    COMMAND /usr/bin/codesign --force --deep --verify --verbose --sign "${CODESIGN_IDENTITY}"
            "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.plugin/Contents/MacOS/${PROJECT_NAME}")
endif()

if(ENABLE_TESTS)
  add_subdirectory(src/tests)
endif()
