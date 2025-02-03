include(ExternalProject)
include(FetchContent)

set(PREBUILT_WHISPERCPP_VERSION "0.0.8")
set(PREBUILT_WHISPERCPP_URL_BASE
    "https://github.com/locaal-ai/occ-ai-dep-whispercpp/releases/download/${PREBUILT_WHISPERCPP_VERSION}")

if(APPLE)
  # check the "MACOS_ARCH" env var to figure out if this is x86 or arm64
  if($ENV{MACOS_ARCH} STREQUAL "x86_64")
    set(WHISPER_CPP_HASH "ac355e3f858c707897d8e0630ff85b3786ef76b84bbb23841561b3d26629e80a")
  elseif($ENV{MACOS_ARCH} STREQUAL "arm64")
    set(WHISPER_CPP_HASH "9e1f22a25f19be7eb370fc7264318de1d97e28e9059f115f4c8a7b0ef3e72678")
  else()
    message(
      FATAL_ERROR
        "The MACOS_ARCH environment variable is not set to a valid value. Please set it to either `x86_64` or `arm64`")
  endif()

  set(WHISPER_CPP_URL
      "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-macos-$ENV{MACOS_ARCH}-${PREBUILT_WHISPERCPP_VERSION}.tar.gz")

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH})
  FetchContent_MakeAvailable(whispercpp_fetch)

  add_library(Whispercpp::Whisper STATIC IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/release/include)
  add_library(Whispercpp::GGML STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGML
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLBase STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLBase
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-base${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLCPU STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLCPU
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-cpu${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLMetal STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLMetal
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-metal${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLBlas STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLBlas
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-blas${CMAKE_STATIC_LIBRARY_SUFFIX})

  add_library(Whispercpp::CoreML STATIC IMPORTED)
  set_target_properties(
    Whispercpp::CoreML
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper.coreml${CMAKE_STATIC_LIBRARY_SUFFIX})

elseif(WIN32)
  if(NOT DEFINED ACCELERATION)
    message(FATAL_ERROR "ACCELERATION is not set. Please set it to either `cpu`, `cuda`, `vulkan` or `hipblas`")
  endif()

  set(ARCH_PREFIX ${ACCELERATION})
  set(WHISPER_CPP_URL
      "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-windows-${ARCH_PREFIX}-${PREBUILT_WHISPERCPP_VERSION}.zip")

  if("${ACCELERATION}" STREQUAL "cpu")
    set(WHISPER_CPP_HASH "cb25c675a01f98bc1cd544187945636d9f7fbaffcfc08699d5edbd29be137e0b")
    add_compile_definitions("LOCALVOCAL_WITH_CPU")
  elseif("${ACCELERATION}" STREQUAL "cuda")
    set(WHISPER_CPP_HASH "672fd34841436261937d5701bf80945ddb8194f033768bb4d7b3becbdf1f66c0")
    add_compile_definitions("LOCALVOCAL_WITH_CUDA")
  elseif("${ACCELERATION}" STREQUAL "hipblas")
    set(WHISPER_CPP_HASH "3f4f16aa6bc9bb6326e86868603136502baef108a339bc4e42bb51654c935120")
    add_compile_definitions("LOCALVOCAL_WITH_HIPBLAS")
  elseif("${ACCELERATION}" STREQUAL "vulkan")
    set(WHISPER_CPP_HASH "46bbcf96cc20a92b36e47ebfabd6c9d581480f38bce72cee16c16e78d7e8c557")
    add_compile_definitions("LOCALVOCAL_WITH_VULKAN")
  else()
    message(
      FATAL_ERROR
        "The ACCELERATION environment variable is not set to a valid value. Please set it to either `cpu` or `cuda` or `vulkan` or `hipblas`"
    )
  endif()

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(whispercpp_fetch)

  add_library(Whispercpp::Whisper SHARED IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES
      IMPORTED_IMPLIB
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})

  add_library(Whispercpp::GGML SHARED IMPORTED)
  set_target_properties(
    Whispercpp::GGML
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}ggml${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::GGML
    PROPERTIES
      IMPORTED_IMPLIB
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml${CMAKE_STATIC_LIBRARY_SUFFIX})

  add_library(Whispercpp::GGMLBase SHARED IMPORTED)
  set_target_properties(
    Whispercpp::GGMLBase
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}ggml-base${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::GGMLBase
    PROPERTIES
      IMPORTED_IMPLIB
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-base${CMAKE_STATIC_LIBRARY_SUFFIX})

  add_library(Whispercpp::GGMLCPU SHARED IMPORTED)
  set_target_properties(
    Whispercpp::GGMLCPU
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}ggml-cpu${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::GGMLCPU
    PROPERTIES
      IMPORTED_IMPLIB
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-cpu${CMAKE_STATIC_LIBRARY_SUFFIX})

  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/include)

  if("${ACCELERATION}" STREQUAL "cpu")
    # add openblas to the link line
    add_library(Whispercpp::OpenBLAS STATIC IMPORTED)
    set_target_properties(Whispercpp::OpenBLAS PROPERTIES IMPORTED_LOCATION
                                                          ${whispercpp_fetch_SOURCE_DIR}/release/lib/libopenblas.dll.a)
  endif()

  if("${ACCELERATION}" STREQUAL "cuda")
    # add cuda to the link line
    add_library(Whispercpp::GGMLCUDA SHARED IMPORTED)
    set_target_properties(
      Whispercpp::GGMLCUDA
      PROPERTIES
        IMPORTED_LOCATION
        ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}ggml-cuda${CMAKE_SHARED_LIBRARY_SUFFIX}
    )
    set_target_properties(
      Whispercpp::GGMLCUDA
      PROPERTIES
        IMPORTED_IMPLIB
        ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-cuda${CMAKE_STATIC_LIBRARY_SUFFIX}
    )
  endif()

  if("${ACCELERATION}" STREQUAL "vulkan")
    # add cuda to the link line
    add_library(Whispercpp::GGMLVulkan SHARED IMPORTED)
    set_target_properties(
      Whispercpp::GGMLVulkan
      PROPERTIES
        IMPORTED_LOCATION
        ${whispercpp_fetch_SOURCE_DIR}/release/bin/${CMAKE_SHARED_LIBRARY_PREFIX}ggml-vulkan${CMAKE_SHARED_LIBRARY_SUFFIX}
    )
    set_target_properties(
      Whispercpp::GGMLVulkan
      PROPERTIES
        IMPORTED_IMPLIB
        ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-vulkan${CMAKE_STATIC_LIBRARY_SUFFIX}
    )
  endif()

  # glob all dlls in the bin directory and install them
  file(GLOB WHISPER_DLLS ${whispercpp_fetch_SOURCE_DIR}/bin/*.dll)
  install(FILES ${WHISPER_DLLS} DESTINATION "obs-plugins/64bit")
else()
  if(NOT DEFINED ACCELERATION)
    message(FATAL_ERROR "ACCELERATION is not set. Please set it to either `cpu`, or `vulkan`")
  endif()

  set(WHISPER_CPP_URL "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-linux-x86_64-${ACCELERATION}-Release.tar.gz")

  if("${ACCELERATION}" STREQUAL "cpu")
    set(WHISPER_CPP_HASH "b6a30f0e995070145ae10e58a656449fee00dd69c53c49ffef4597b07bcb3c2a")
    add_compile_definitions("LOCALVOCAL_WITH_CPU")
  elseif("${ACCELERATION}" STREQUAL "vulkan")
    set(WHISPER_CPP_HASH "6c5fe9c6a35b5f7f63a968b4fbbc8e05e888cc887aadbb8d82cf7e39da8ec163")
    add_compile_definitions("LOCALVOCAL_WITH_VULKAN")
  else()
    message(
      FATAL_ERROR
        "The ACCELERATION environment variable is not set to a valid value. Please set it to either `cpu` or `vulkan`")
  endif()

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(whispercpp_fetch)

  # add the static Whisper library to the link line
  add_library(Whispercpp::Whisper STATIC IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
  add_library(Whispercpp::GGML STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGML
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLBase STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLBase
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-base${CMAKE_STATIC_LIBRARY_SUFFIX})
  add_library(Whispercpp::GGMLCPU STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGMLCPU
    PROPERTIES
      IMPORTED_LOCATION
      ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-cpu${CMAKE_STATIC_LIBRARY_SUFFIX})

  if("${ACCELERATION}" STREQUAL "vulkan")
    add_library(Whispercpp::GGMLVulkan STATIC IMPORTED)
    set_target_properties(
      Whispercpp::GGMLVulkan
      PROPERTIES
        IMPORTED_LOCATION
        ${whispercpp_fetch_SOURCE_DIR}/release/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml-vulkan${CMAKE_STATIC_LIBRARY_SUFFIX}
    )
  endif()
endif()

add_library(Whispercpp INTERFACE)
add_dependencies(Whispercpp Whispercpp_Build)
target_link_libraries(Whispercpp INTERFACE Whispercpp::Whisper Whispercpp::GGML Whispercpp::GGMLBase
                                           Whispercpp::GGMLCPU)

if(WIN32 AND "${ACCELERATION}" STREQUAL "cpu")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::OpenBLAS)
endif()

if(WIN32 AND "${ACCELERATION}" STREQUAL "vulkan")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::GGMLVulkan)
endif()

if(WIN32 AND "${ACCELERATION}" STREQUAL "cuda")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::GGMLCUDA)
endif()

if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate -framework CoreML -framework Metal")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::CoreML Whispercpp::GGMLMetal Whispercpp::GGMLBlas)
endif(APPLE)

if(UNIX
   AND (NOT APPLE)
   AND "${ACCELERATION}" STREQUAL "vulkan")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::GGMLVulkan)
endif()
