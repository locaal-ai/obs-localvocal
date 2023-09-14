include(ExternalProject)

set(CMAKE_OSX_ARCHITECTURES_ "arm64$<SEMICOLON>x86_64")

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(Whispercpp_BUILD_TYPE Release)
else()
  set(Whispercpp_BUILD_TYPE Debug)
endif()

# On linux add the `-fPIC` flag to the compiler
if(UNIX AND NOT APPLE)
  set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
endif()

# if on Windows - download OpenBLAS prebuilt binaries
if(WIN32)
  if(LOCALVOCAL_WITH_CUDA)
    # Build with CUDA Check that CUDA_TOOLKIT_ROOT_DIR is set
    if(NOT DEFINED CUDA_TOOLKIT_ROOT_DIR)
      message(FATAL_ERROR "CUDA_TOOLKIT_ROOT_DIR is not set. Please set it to the root directory of your CUDA "
                          "installation.")
    endif(NOT DEFINED CUDA_TOOLKIT_ROOT_DIR)

    set(WHISPER_ADDITIONAL_ENV "CUDAToolkit_ROOT=${CUDA_TOOLKIT_ROOT_DIR}")
    set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_CUBLAS=ON -DCMAKE_GENERATOR_TOOLSET=cuda=${CUDA_TOOLKIT_ROOT_DIR})
  else(LOCALVOCAL_WITH_CUDA)
    # Build with OpenBLAS
    set(OpenBLAS_URL "https://github.com/xianyi/OpenBLAS/releases/download/v0.3.24/OpenBLAS-0.3.24-x64.zip")
    set(OpenBLAS_SHA256 "8E777E406BA7030D21ADB18683D6175E4FA5ADACFBC09982C01E81245B348132")
    ExternalProject_Add(
      OpenBLAS
      URL ${OpenBLAS_URL}
      URL_HASH SHA256=${OpenBLAS_SHA256}
      DOWNLOAD_NO_PROGRESS true
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR> <INSTALL_DIR>)
    ExternalProject_Get_Property(OpenBLAS INSTALL_DIR)
    set(OpenBLAS_DIR ${INSTALL_DIR})
    set(WHISPER_ADDITIONAL_ENV "OPENBLAS_PATH=${OpenBLAS_DIR}")
    set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_BLAS=ON -DWHISPER_CUBLAS=OFF)
  endif(LOCALVOCAL_WITH_CUDA)

  ExternalProject_Add(
    Whispercpp_Build
    DOWNLOAD_EXTRACT_TIMESTAMP true
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG 7b374c9ac9b9861bb737eec060e4dfa29d229259
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
    BUILD_BYPRODUCTS
      <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}whisper${CMAKE_IMPORT_LIBRARY_SUFFIX}
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE} && ${CMAKE_COMMAND} -E
                    copy <BINARY_DIR>/${Whispercpp_BUILD_TYPE}/whisper.lib <INSTALL_DIR>/lib
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env ${WHISPER_ADDITIONAL_ENV} ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
      ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
      -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
      -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_} -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
      -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=ON -DWHISPER_BUILD_TESTS=OFF
      -DWHISPER_BUILD_EXAMPLES=OFF ${WHISPER_ADDITIONAL_CMAKE_ARGS})

  if(NOT LOCALVOCAL_WITH_CUDA)
    add_dependencies(Whispercpp_Build OpenBLAS)
  endif(NOT LOCALVOCAL_WITH_CUDA)
else()
  # On Linux and MacOS build a static Whisper library
  ExternalProject_Add(
    Whispercpp_Build
    DOWNLOAD_EXTRACT_TIMESTAMP true
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG 3fec2119e6b52d1381b02a0fbf281b1b34728c25
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env OPENBLAS_PATH=${OpenBLAS_DIR} ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
      ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
      -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
      -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_} -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
      -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=OFF -DWHISPER_BUILD_TESTS=OFF
      -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BLAS=OFF)
endif(WIN32)

ExternalProject_Get_Property(Whispercpp_Build INSTALL_DIR)

# add the Whisper library to the link line
if(WIN32)
  add_library(Whispercpp::Whisper SHARED IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX})
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_IMPLIB ${INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})

  install(FILES ${INSTALL_DIR}/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX}
          DESTINATION "obs-plugins/64bit")

  if(NOT LOCALVOCAL_WITH_CUDA)
    # add openblas to the link line
    add_library(Whispercpp::OpenBLAS STATIC IMPORTED)
    set_target_properties(Whispercpp::OpenBLAS PROPERTIES IMPORTED_LOCATION ${OpenBLAS_DIR}/lib/libopenblas.dll.a)
    install(FILES ${OpenBLAS_DIR}/bin/libopenblas.dll DESTINATION "obs-plugins/64bit")
  endif(NOT LOCALVOCAL_WITH_CUDA)
else()
  # on Linux and MacOS add the static Whisper library to the link line
  add_library(Whispercpp::Whisper STATIC IMPORTED)
  set_target_properties(
    Whispercpp::Whisper
    PROPERTIES IMPORTED_LOCATION
               ${INSTALL_DIR}/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})
endif(WIN32)

add_library(Whispercpp INTERFACE)
add_dependencies(Whispercpp Whispercpp_Build)
target_link_libraries(Whispercpp INTERFACE Whispercpp::Whisper)
if(WIN32 AND NOT LOCALVOCAL_WITH_CUDA)
  target_link_libraries(Whispercpp INTERFACE Whispercpp::OpenBLAS)
endif()
set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate")
endif(APPLE)
