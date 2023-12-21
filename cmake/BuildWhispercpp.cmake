include(ExternalProject)

set(CMAKE_OSX_ARCHITECTURES_ "arm64$<SEMICOLON>x86_64")

set(Whispercpp_Build_GIT_TAG "8986690c2a7b81b2b5d79cdc186b5aa672311740")

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(Whispercpp_BUILD_TYPE Release)
else()
  set(Whispercpp_BUILD_TYPE Debug)
endif()

if(UNIX AND NOT APPLE)
  # On linux add the `-fPIC` flag to the compiler
  set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
  set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_BLAS=OFF -DWHISPER_CUBLAS=OFF -DWHISPER_OPENBLAS=OFF -DWHISPER_NO_AVX=ON
                                    -DWHISPER_NO_AVX2=ON)
endif()
if(APPLE)
  # disable Metal on MacOS as it hurts performance right now
  set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_METAL=OFF)
endif()

if(WIN32)
  if(LOCALVOCAL_WITH_CUDA)
    # Build with CUDA Check that CUDA_TOOLKIT_ROOT_DIR is set
    if(NOT DEFINED CUDA_TOOLKIT_ROOT_DIR)
      message(FATAL_ERROR "CUDA_TOOLKIT_ROOT_DIR is not set. Please set it to the root directory of your CUDA "
                          "installation, e.g. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.4`")
    endif(NOT DEFINED CUDA_TOOLKIT_ROOT_DIR)

    set(WHISPER_ADDITIONAL_ENV "CUDAToolkit_ROOT=${CUDA_TOOLKIT_ROOT_DIR}")
    set(WHISPER_ADDITIONAL_CMAKE_ARGS -DWHISPER_CUBLAS=ON -DWHISPER_OPENBLAS=OFF
                                      -DCMAKE_GENERATOR_TOOLSET=cuda=${CUDA_TOOLKIT_ROOT_DIR})
  else()
    # Build with OpenBLAS
    set(OpenBLAS_URL "https://github.com/xianyi/OpenBLAS/releases/download/v0.3.24/OpenBLAS-0.3.24-x64.zip")
    set(OpenBLAS_SHA256 "6335128ee7117ea2dd2f5f96f76dafc17256c85992637189a2d5f6da0c608163")
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
  endif()

  ExternalProject_Add(
    Whispercpp_Build
    DOWNLOAD_EXTRACT_TIMESTAMP true
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG ${Whispercpp_Build_GIT_TAG}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
    BUILD_BYPRODUCTS
      <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/bin/${CMAKE_SHARED_LIBRARY_PREFIX}whisper${CMAKE_SHARED_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}whisper${CMAKE_IMPORT_LIBRARY_SUFFIX}
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    INSTALL_COMMAND
      ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE} && ${CMAKE_COMMAND} -E copy
      <BINARY_DIR>/${Whispercpp_BUILD_TYPE}/whisper.lib <INSTALL_DIR>/lib && ${CMAKE_COMMAND} -E copy
      <SOURCE_DIR>/ggml.h <INSTALL_DIR>/include
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
  else(NOT LOCALVOCAL_WITH_CUDA)
    # normalize CUDA path with file(TO_CMAKE_PATH)
    file(TO_CMAKE_PATH ${CUDA_TOOLKIT_ROOT_DIR} CUDA_TOOLKIT_ROOT_DIR)
    # find the CUDA DLLs for cuBLAS in the bin directory of the CUDA installation e.g. cublas64_NN.dll
    file(GLOB CUBLAS_DLLS "${CUDA_TOOLKIT_ROOT_DIR}/bin/cublas64_*.dll")
    # find cublasLt DLL, e.g. cublasLt64_11.dll
    file(GLOB CUBLASLT_DLLS "${CUDA_TOOLKIT_ROOT_DIR}/bin/cublasLt64_*.dll")
    # find cudart DLL, e.g. cudart64_110.dll
    file(GLOB CUDART_DLLS "${CUDA_TOOLKIT_ROOT_DIR}/bin/cudart64_*.dll")
    # if any of the files cannot be found, abort
    if(NOT CUBLAS_DLLS
       OR NOT CUBLASLT_DLLS
       OR NOT CUDART_DLLS)
      message(FATAL_ERROR "Could not find cuBLAS, cuBLASLt or cuDART DLLs in ${CUDA_TOOLKIT_ROOT_DIR}/bin")
    endif()
    # copy the DLLs to the OBS plugin directory
    install(FILES ${CUBLAS_DLLS} ${CUBLASLT_DLLS} ${CUDART_DLLS} DESTINATION "obs-plugins/64bit")
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
