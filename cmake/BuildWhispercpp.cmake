include(ExternalProject)
include(FetchContent)

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(Whispercpp_BUILD_TYPE Release)
else()
  set(Whispercpp_BUILD_TYPE Debug)
endif()

if(APPLE)
  # check the "MACOS_ARCH" env var to figure out if this is x86 or arm64
  if(NOT DEFINED ENV{MACOS_ARCH})
    message(FATAL_ERROR "The MACOS_ARCH environment variable is not set. Please set it to either `x86_64` or `arm64`")
  endif(NOT DEFINED ENV{MACOS_ARCH})

  set(WHISPER_CPP_URL
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.2/whispercpp-macos-$ENV{MACOS_ARCH}-0.0.2.tar.gz"
  )
  if($ENV{MACOS_ARCH} STREQUAL "x86_64")
    set(WHISPER_CPP_HASH "00C308AF0BFFF7619934403A8080CC9AFC4EDAA328D7587E617150A2C6A33313")
  elseif($ENV{MACOS_ARCH} STREQUAL "arm64")
    set(WHISPER_CPP_HASH "0478E2079E07FA81BEE77506101003F4A4C8F0DF9E23757BD7E1D25DCBD1DB30")
  else()
    message(
      FATAL_ERROR
        "The MACOS_ARCH environment variable is not set to a valid value. Please set it to either `x86_64` or `arm64`")
  endif()

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
      ${whispercpp_fetch_SOURCE_DIR}/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/include)

elseif(WIN32)
  if(NOT DEFINED ENV{CPU_OR_CUDA})
    message(
      FATAL_ERROR
        "The CPU_OR_CUDA environment variable is not set. Please set it to either `cpu`, `clblast` or `11.8.0` or `12.2.0`"
    )
  endif(NOT DEFINED ENV{CPU_OR_CUDA})

  set(ARCH_PREFIX $ENV{CPU_OR_CUDA})
  if(NOT $ENV{CPU_OR_CUDA} STREQUAL "cpu" AND NOT $ENV{CPU_OR_CUDA} STREQUAL "clblast")
    set(ARCH_PREFIX "cuda$ENV{CPU_OR_CUDA}")
    add_compile_definitions("LOCALVOCAL_WITH_CUDA")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "cpu")
    add_compile_definitions("LOCALVOCAL_WITH_CPU")
  else()
    add_compile_definitions("LOCALVOCAL_WITH_CLBLAST")
  endif()

  set(WHISPER_CPP_URL
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.2/whispercpp-windows-${ARCH_PREFIX}-0.0.2.zip"
  )
  if($ENV{CPU_OR_CUDA} STREQUAL "cpu")
    set(WHISPER_CPP_HASH "6DE628A51B9352624A1EC397231591FA3370E6BB42D9364F4F91F11DD18F77D2")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "clblast")
    set(WHISPER_CPP_HASH "97BF58520F1818B7C9F4E996197F3097934E5E0BBA92B0B016C6B28BE9FF1642")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "12.2.0")
    set(WHISPER_CPP_HASH "48C059A3364E0AAD9FB0D4194BA554865928D22A27ECE5E3C116DC672D5D6EDE")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "11.8.0")
    set(WHISPER_CPP_HASH "29A5530E83896DE207F0199535CBBB24DF0D63B1373BA66139AD240BA67120EB")
  else()
    message(
      FATAL_ERROR
        "The CPU_OR_CUDA environment variable is not set to a valid value. Please set it to either `cpu` or `11.8.0` or `12.2.0`"
    )
  endif()

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH})
  FetchContent_MakeAvailable(whispercpp_fetch)

  add_library(Whispercpp::Whisper SHARED IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION
               ${whispercpp_fetch_SOURCE_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_IMPLIB
               ${whispercpp_fetch_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/include)

  if($ENV{CPU_OR_CUDA} STREQUAL "cpu")
    # add openblas to the link line
    add_library(Whispercpp::OpenBLAS STATIC IMPORTED)
    set_target_properties(Whispercpp::OpenBLAS PROPERTIES IMPORTED_LOCATION
                                                          ${whispercpp_fetch_SOURCE_DIR}/lib/libopenblas.dll.a)
  endif()

  # glob all dlls in the bin directory and install them
  file(GLOB WHISPER_DLLS ${whispercpp_fetch_SOURCE_DIR}/bin/*.dll)
  install(FILES ${WHISPER_DLLS} DESTINATION "obs-plugins/64bit")

else()
  set(Whispercpp_Build_GIT_TAG "7395c70a748753e3800b63e3422a2b558a097c80")
  set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
  set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_BLAS=OFF -DWHISPER_CUBLAS=OFF -DWHISPER_OPENBLAS=OFF -DWHISPER_NO_AVX=ON
                                    -DWHISPER_NO_AVX2=ON)

  # On Linux build a static Whisper library
  ExternalProject_Add(
    Whispercpp_Build
    DOWNLOAD_EXTRACT_TIMESTAMP true
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG ${Whispercpp_Build_GIT_TAG}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE} && ${CMAKE_COMMAND} -E
                    copy <SOURCE_DIR>/ggml.h <INSTALL_DIR>/include
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env ${WHISPER_ADDITIONAL_ENV} ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
      ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
      -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
      -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_} -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
      -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=OFF -DWHISPER_BUILD_TESTS=OFF
      -DWHISPER_BUILD_EXAMPLES=OFF ${WHISPER_ADDITIONAL_CMAKE_ARGS})

  ExternalProject_Get_Property(Whispercpp_Build INSTALL_DIR)

  # add the static Whisper library to the link line
  add_library(Whispercpp::Whisper STATIC IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION
               ${INSTALL_DIR}/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
endif()

add_library(Whispercpp INTERFACE)
add_dependencies(Whispercpp Whispercpp_Build)
target_link_libraries(Whispercpp INTERFACE Whispercpp::Whisper)
if(WIN32 AND "$ENV{CPU_OR_CUDA}" STREQUAL "cpu")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::OpenBLAS)
endif()
if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate")
endif(APPLE)
