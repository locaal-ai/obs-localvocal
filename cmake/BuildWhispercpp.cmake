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
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.4/whispercpp-macos-$ENV{MACOS_ARCH}-0.0.4.tar.gz"
  )
  if($ENV{MACOS_ARCH} STREQUAL "x86_64")
    set(WHISPER_CPP_HASH "44ebaa26a2f9883b461f86875cdf00b9c538c604acc856dd968eba5bb2f18fa4")
  elseif($ENV{MACOS_ARCH} STREQUAL "arm64")
    set(WHISPER_CPP_HASH "0075aab303eb5ddc46fa8dca29ec88626710146fd0bcf03e07ac0a88c71ac197")
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
    PROPERTIES IMPORTED_LOCATION
               ${whispercpp_fetch_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/include)
  add_library(Whispercpp::GGML STATIC IMPORTED)
  set_target_properties(
    Whispercpp::GGML
    PROPERTIES IMPORTED_LOCATION
               ${whispercpp_fetch_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ggml${CMAKE_STATIC_LIBRARY_SUFFIX})

elseif(WIN32)
  if(NOT DEFINED ENV{ACCELERATION})
    message(
      FATAL_ERROR "The ACCELERATION environment variable is not set. Please set it to either `cpu`, `cuda` or `hipblas`"
    )
  endif(NOT DEFINED ENV{ACCELERATION})

  set(ARCH_PREFIX $ENV{ACCELERATION})
  set(WHISPER_CPP_URL
      "https://github.com/occ-ai/occ-ai-dep-whispercpp/releases/download/0.0.4/whispercpp-windows-${ARCH_PREFIX}-0.0.4.zip"
  )
  if($ENV{ACCELERATION} STREQUAL "cpu")
    set(WHISPER_CPP_HASH "82ca775c1de5b27aff892fb5e3ca7589218e3be1ecdbd35fc899b3f87cfa6c68")
    add_compile_definitions("LOCALVOCAL_WITH_CPU")
  elseif($ENV{ACCELERATION} STREQUAL "cuda")
    set(WHISPER_CPP_HASH "27ced6279f333953207b0c4dc2dc7bb9721790d3252f4fa8cc304fb8e4126f3e")
    add_compile_definitions("LOCALVOCAL_WITH_CUDA")
  elseif($ENV{ACCELERATION} STREQUAL "hipblas")
    set(WHISPER_CPP_HASH "af4372e9ef497a60b98de009dbae5d83ca20d10baa49f187ed7cff2a0b24110e")
    add_compile_definitions("LOCALVOCAL_WITH_HIPBLAS")
  else()
    message(
      FATAL_ERROR
        "The ACCELERATION environment variable is not set to a valid value. Please set it to either `cpu` or `cuda` or `hipblas`"
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
    PROPERTIES IMPORTED_LOCATION
               ${whispercpp_fetch_SOURCE_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_IMPLIB
               ${whispercpp_fetch_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
  set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                       ${whispercpp_fetch_SOURCE_DIR}/include)

  if($ENV{ACCELERATION} STREQUAL "cpu")
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
if(WIN32 AND "$ENV{ACCELERATION}" STREQUAL "cpu")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::OpenBLAS)
endif()
if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate -framework CoreML -framework Metal")
  target_link_libraries(Whispercpp INTERFACE Whispercpp::GGML)
endif(APPLE)
