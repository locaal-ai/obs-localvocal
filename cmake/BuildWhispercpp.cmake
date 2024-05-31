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
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.3/whispercpp-macos-$ENV{MACOS_ARCH}-0.0.3.tar.gz"
  )
  if($ENV{MACOS_ARCH} STREQUAL "x86_64")
    set(WHISPER_CPP_HASH "94DB35C69E958C8A59F0F331734F4F4F45F4BB13D2F54D3C838457E8590874C4")
  elseif($ENV{MACOS_ARCH} STREQUAL "arm64")
    set(WHISPER_CPP_HASH "ACA1DF8F34F4946B56FEED89B7548C9AD56D1DD89615C96BDEB6E4734A946451")
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
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.3/whispercpp-windows-${ARCH_PREFIX}-0.0.3.zip"
  )
  if($ENV{CPU_OR_CUDA} STREQUAL "cpu")
    set(WHISPER_CPP_HASH "A7243E649E0B6D080AA6D2210DB0AC08C597FA11B88C3068B8A60083AD9E62EF")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "clblast")
    set(WHISPER_CPP_HASH "7957AC76A0E6517C95951B3BECCB554CD992E30DAF8716681B40F375590F69F1")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "12.2.0")
    set(WHISPER_CPP_HASH "0F6BC1F91C573A867D6972554FC29C3D8EAFD7994FA0FEBBEAFCF945DC8A9F41")
  elseif($ENV{CPU_OR_CUDA} STREQUAL "11.8.0")
    set(WHISPER_CPP_HASH "51CB6750ADDF96F38106E4E88212FCC06500999E568E5A4EDC6D42CA6D7CA99D")
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
  set(Whispercpp_Build_GIT_TAG "v1.6.2")
  set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
  set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_BLAS=OFF -DWHISPER_CUBLAS=OFF -DWHISPER_OPENBLAS=OFF)

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
