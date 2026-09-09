include(ExternalProject)
include(FetchContent)

set(PREBUILT_WHISPERCPP_VERSION "0.0.13")
set(PREBUILT_WHISPERCPP_URL_BASE
    "https://github.com/locaal-ai/occ-ai-dep-whispercpp/releases/download/${PREBUILT_WHISPERCPP_VERSION}")

add_library(Whispercpp INTERFACE)

include(cmake/BuildWhispercppHelpers.cmake)

if(APPLE)
  add_compile_definitions(WHISPER_DYNAMIC_BACKENDS)

  # check the "MACOS_ARCH" env var to figure out if this is x86 or arm64
  if($ENV{MACOS_ARCH} STREQUAL "x86_64")
    if($ENV{MACOS_VERSION} STREQUAL "embedded")
      set(METAL_STD EMBEDDED)
      set(WHISPER_CPP_HASH "27e506e0c473de33ccfd55b99364d1f829f4958f1443cbdd6fab33b0e10cf555")
    elseif($ENV{MACOS_VERSION} STREQUAL "12")
      set(METAL_STD 2.4)
      set(WHISPER_CPP_HASH "58a938440e2c1d955e829daee9858ef683b7cf9afd6edd22a3f8b812d8c31492")
    elseif($ENV{MACOS_VERSION} STREQUAL "13")
      set(METAL_STD 3.0)
      set(WHISPER_CPP_HASH "c6cdb11289415babba4b3379a84086f98f93e5f7fd60f0528e060a9af4009c8c")
    elseif($ENV{MACOS_VERSION} STREQUAL "14")
      set(METAL_STD 3.1)
      set(WHISPER_CPP_HASH "1786c38df8d12cde1f364f0049f9c88e3b5dcb1a867e7d884fed4be250a10fb3")
    elseif($ENV{MACOS_VERSION} STREQUAL "15")
      set(METAL_STD 3.2)
      set(WHISPER_CPP_HASH "91e406e3dfd36a33bd3b5ae0b68edd0019475ccda41428415908bb0ff0effdb1")
    endif()

    list(
      APPEND
      WHISPER_RUNTIME_MODULES
      GGMLCPU-X64
      GGMLCPU-SSE42
      GGMLCPU-SANDYBRIDGE
      GGMLCPU-HASWELL
      GGMLCPU-SKYLAKEX
      GGMLCPU-ICELAKE
      GGMLCPU-ALDERLAKE
      GGMLCPU-SAPPHIRERAPIDS)
  elseif($ENV{MACOS_ARCH} STREQUAL "arm64")
    if($ENV{MACOS_VERSION} STREQUAL "embedded")
      set(METAL_STD EMBEDDED)
      set(WHISPER_CPP_HASH "5ef495c799ef7c2aa0aa7b096b2bc8aa812717f036b6cf2e76db445a2a74899f")
    elseif($ENV{MACOS_VERSION} STREQUAL "12")
      set(METAL_STD 2.4)
      set(WHISPER_CPP_HASH "89d19b1d270b6084f8455f26c52e59e8ef2ca0e6b1353bfc2aaadb0d5fdcb9a1")
    elseif($ENV{MACOS_VERSION} STREQUAL "13")
      set(METAL_STD 3.0)
      set(WHISPER_CPP_HASH "29a79af5e918d5f531c99f66b66d9043814bc1250a836029706217ccf736f492")
    elseif($ENV{MACOS_VERSION} STREQUAL "14")
      set(METAL_STD 3.1)
      set(WHISPER_CPP_HASH "c0759798ba927f2803161d70ba9769b817cd87aaf1cbaaf8247496a8cc939699")
    elseif($ENV{MACOS_VERSION} STREQUAL "15")
      set(METAL_STD 3.2)
      set(WHISPER_CPP_HASH "5957c41f41621db03e33c6d12c5eeda735fb361f6b4128aa077da2b74548f8bb")
    endif()
    list(APPEND WHISPER_RUNTIME_MODULES GGMLCPU-APPLE_M1 GGMLCPU-APPLE_M2_M3 GGMLCPU-APPLE_M4)
  else()
    message(
      FATAL_ERROR
        "The MACOS_ARCH environment variable is not set to a valid value. Please set it to either `x86_64` or `arm64`")
  endif()
  set(WHISPER_CPP_URL
      "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-macos-$ENV{MACOS_ARCH}-metal${METAL_STD}-${PREBUILT_WHISPERCPP_VERSION}.tar.gz"
  )

  set(WHISPER_LIBRARIES Whisper Whisper_1 WhisperCoreML GGML GGMLBase)
  list(APPEND WHISPER_RUNTIME_MODULES GGMLMetal GGMLBlas GGMLVulkan)
  set(WHISPER_DEPENDENCY_LIBRARIES "-framework Accelerate" "-framework CoreML" "-framework Metal" ${BLAS_LIBRARIES})
  set(WHISPER_LIBRARY_TYPE SHARED)

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH})
  FetchContent_MakeAvailable(whispercpp_fetch)

  add_compile_definitions(LOCALVOCAL_WITH_COREML)

  set(WHISPER_SOURCE_DIR ${whispercpp_fetch_SOURCE_DIR})
  set(WHISPER_LIB_DIR ${whispercpp_fetch_SOURCE_DIR})

  install_library_to_bundle(${whispercpp_fetch_SOURCE_DIR} libomp.dylib)
  install_library_to_bundle(${whispercpp_fetch_SOURCE_DIR} libvulkan.1.dylib)
  if(NOT $ENV{MACOS_VERSION} STREQUAL "embedded")
    target_add_resource(${CMAKE_PROJECT_NAME} ${whispercpp_fetch_SOURCE_DIR}/bin/default.metallib)
  endif()
elseif(WIN32)
  add_compile_definitions(WHISPER_DYNAMIC_BACKENDS)

  if(NOT DEFINED ACCELERATION)
    message(FATAL_ERROR "ACCELERATION is not set. Please set it to either `generic`, `nvidia`, or `amd`")
  endif()

  set(WHISPER_LIBRARIES Whisper GGML GGMLBase)
  set(WHISPER_RUNTIME_MODULES
      GGMLCPU-X64
      GGMLCPU-SSE42
      GGMLCPU-SANDYBRIDGE
      GGMLCPU-HASWELL
      GGMLCPU-SKYLAKEX
      GGMLCPU-ICELAKE
      GGMLCPU-ALDERLAKE
      GGMLBlas
      GGMLVulkan)
  set(WHISPER_LIBRARY_TYPE SHARED)

  set(ARCH_PREFIX "")
  set(ACCELERATION_PREFIX "-${ACCELERATION}")
  set(WHISPER_CPP_URL
      "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-windows${ARCH_PREFIX}${ACCELERATION_PREFIX}-${PREBUILT_WHISPERCPP_VERSION}.zip"
  )
  if(${ACCELERATION} STREQUAL "amd")
    set(WHISPER_CPP_HASH "4919c0258c8b2753763062b554eaf4d99f3f8256331e398114128270786023db")
    list(APPEND WHISPER_RUNTIME_MODULES GGMLHip)
  elseif(${ACCELERATION} STREQUAL "generic")
    set(WHISPER_CPP_HASH "8653395b0ec3c9d931cf0647391850d249a67196b4f78ccf4d8dbf98ae30ed82")
  elseif(${ACCELERATION} STREQUAL "nvidia")
    set(WHISPER_CPP_HASH "534cb37a3bbfb36379dde1b6acb933990840f2e334abe2c953b07b6817983818")
    list(APPEND WHISPER_RUNTIME_MODULES GGMLCUDA)
  else()
    message(
      FATAL_ERROR
        "The ACCELERATION environment variable is not set to a valid value. Please set it to either `generic`, `nvidia` or `amd`"
    )
  endif()

  FetchContent_Declare(
    whispercpp_fetch
    URL ${WHISPER_CPP_URL}
    URL_HASH SHA256=${WHISPER_CPP_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(whispercpp_fetch)

  set(WHISPER_SOURCE_DIR ${whispercpp_fetch_SOURCE_DIR})
  set(WHISPER_LIB_DIR ${whispercpp_fetch_SOURCE_DIR})
  set(WHISPER_DEPENDENCY_LIBRARIES "${whispercpp_fetch_SOURCE_DIR}/lib/libopenblas.lib")

  # glob all dlls in the bin directory and install them
  file(GLOB WHISPER_DLLS ${whispercpp_fetch_SOURCE_DIR}/bin/*.dll)
  install(FILES ${WHISPER_DLLS} DESTINATION "obs-plugins/64bit")
  file(GLOB WHISPER_PDBS ${whispercpp_fetch_SOURCE_DIR}/bin/*.pdb)
  install(FILES ${WHISPER_PDBS} DESTINATION "obs-plugins/64bit")
else()
  # Linux

  # Enable ccache if available
  find_program(CCACHE_PROGRAM ccache QUIET)
  if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    if(APPLE)
      set(CLANG_ENABLE_EXPLICIT_MODULES_WITH_COMPILER_LAUNCHER YES)
    endif()
  endif()

  set(BLA_VENDOR OpenBLAS)
  find_package(BLAS REQUIRED)

  if(USE_SYSTEM_WHISPERCPP)
    # Use a system/prefix-installed whisper.cpp
    find_path(Whispercpp_INCLUDE_DIR NAMES whisper.h REQUIRED)
    cmake_path(GET Whispercpp_INCLUDE_DIR PARENT_PATH Whispercpp_PREFIX)
    message(STATUS "Using system whisper.cpp at ${Whispercpp_PREFIX}")

    set(WHISPER_SOURCE_DIR "${Whispercpp_PREFIX}")
    set(WHISPER_LIB_DIR "${Whispercpp_PREFIX}")
    set(WHISPER_LIBRARY_TYPE SHARED)
    set(WHISPER_LIBRARIES Whisper GGML GGMLBase)

    # GGMLBase is optional depending on the whisper.cpp build configuration
    find_library(
      GGMLBase_LIB ggml-base
      PATHS "${Whispercpp_PREFIX}/lib"
      NO_DEFAULT_PATH)
    if(NOT GGMLBase_LIB)
      list(REMOVE_ITEM WHISPER_LIBRARIES GGMLBase)
    endif()
    unset(GGMLBase_LIB CACHE)

    list(APPEND WHISPER_DEPENDENCY_LIBRARIES BLAS::BLAS)
  elseif(NOT LINUX_SOURCE_BUILD)
    add_compile_definitions(WHISPER_DYNAMIC_BACKENDS)
    set(WHISPER_LIBRARY_TYPE SHARED)
    set(WHISPER_LIBRARIES Whisper GGML GGMLBase)
    list(APPEND WHISPER_DEPENDENCY_LIBRARIES Vulkan::Vulkan BLAS::BLAS)
    if(NOT "${ACCELERATION}" STREQUAL "nvidia")
      # NVidia CUDA has its own OpenCL library
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES OpenCL::OpenCL)
    endif()
    list(
      APPEND
      WHISPER_RUNTIME_MODULES
      GGMLCPU-X64
      GGMLCPU-SSE42
      GGMLCPU-SANDYBRIDGE
      GGMLCPU-HASWELL
      GGMLCPU-SKYLAKEX
      GGMLCPU-ICELAKE
      GGMLCPU-ALDERLAKE
      GGMLCPU-SAPPHIRERAPIDS
      GGMLBlas
      GGMLVulkan
      GGMLOpenCL)

    find_package(
      Vulkan
      COMPONENTS glslc
      REQUIRED)
    if(NOT "${ACCELERATION}" STREQUAL "nvidia")
      # NVidia CUDA provides its own OpenCL; skip the standalone OpenCL search.
      find_package(OpenCL REQUIRED)
    endif()
    find_package(Python3 REQUIRED)

    set(ARCH_PREFIX "-x86_64")
    set(ACCELERATION_PREFIX "-${ACCELERATION}")
    set(WHISPER_CPP_URL
        "${PREBUILT_WHISPERCPP_URL_BASE}/whispercpp-linux${ARCH_PREFIX}${ACCELERATION_PREFIX}-Release.tar.gz")
    if(${ACCELERATION} STREQUAL "amd")
      set(WHISPER_CPP_HASH "2334e6bfc40d0fd631ee67711598ead0a7375fac3dea18529d15147770128c27")
      list(APPEND WHISPER_RUNTIME_MODULES GGMLHip)

      # Find hip libraries and link against them
      set(CMAKE_PREFIX_PATH /opt/rocm-6.4.2/lib/cmake)
      set(HIP_PLATFORM amd)
      set(CMAKE_HIP_PLATFORM amd)
      set(CMAKE_HIP_ARCHITECTURES OFF)
      find_package(hip REQUIRED)
      find_package(hipblas REQUIRED)
      find_package(rocblas REQUIRED)
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES hip::host roc::rocblas roc::hipblas)
    elseif(${ACCELERATION} STREQUAL "generic")
      set(WHISPER_CPP_HASH "5811798e245482597ad393fac7bab82f0df8664e6b82be6231d089af13de0656")
    elseif(${ACCELERATION} STREQUAL "nvidia")
      set(WHISPER_CPP_HASH "dc00f91f5ddfb8271fa177b005022dc6ccc979ab669ee8e92008a7dc694295ad")
      list(APPEND WHISPER_RUNTIME_MODULES GGMLCUDA)

      # Find CUDA libraries and link against them. In sandboxed builds (e.g. Flatpak) CUDA is absent at compile time;
      # libggml-cuda.so from the prebuilt tarball resolves its own CUDA dependencies at runtime via
      # org.freedesktop.Platform.GL.nvidia.
      if(NOT DEFINED CUDAToolkit_ROOT AND EXISTS /usr/local/cuda-12.8)
        set(CUDAToolkit_ROOT /usr/local/cuda-12.8/)
      endif()
      find_package(CUDAToolkit QUIET)
      if(CUDAToolkit_FOUND)
        list(
          APPEND
          WHISPER_DEPENDENCY_LIBRARIES
          CUDA::cudart
          CUDA::cublas
          CUDA::cublasLt
          CUDA::cuda_driver
          CUDA::OpenCL)
      else()
        message(STATUS "CUDAToolkit not found at build time – libggml-cuda.so will resolve CUDA at runtime")
      endif()
    else()
      message(
        FATAL_ERROR
          "The ACCELERATION environment variable is not set to a valid value. Please set it to either `generic`, `nvidia` or `amd`"
      )
    endif()

    FetchContent_Declare(
      whispercpp_fetch
      URL ${WHISPER_CPP_URL}
      URL_HASH SHA256=${WHISPER_CPP_HASH}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(whispercpp_fetch)

    message(STATUS "Whispercpp URL: ${WHISPER_CPP_URL}")
    message(STATUS "Whispercpp source dir: ${whispercpp_fetch_SOURCE_DIR}")

    set(WHISPER_SOURCE_DIR ${whispercpp_fetch_SOURCE_DIR})
    set(WHISPER_LIB_DIR ${whispercpp_fetch_SOURCE_DIR})

    file(GLOB WHISPER_SOS ${whispercpp_fetch_SOURCE_DIR}/lib/*${CMAKE_SHARED_LIBRARY_SUFFIX}*)
    install(FILES ${WHISPER_SOS} DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins/obs-localvocal")
    file(GLOB WHISPER_BIN_SOS ${whispercpp_fetch_SOURCE_DIR}/bin/*${CMAKE_SHARED_LIBRARY_SUFFIX}*)
    install(FILES ${WHISPER_BIN_SOS} DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins/obs-localvocal")
  else()
    # Source build
    if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
      set(Whispercpp_BUILD_TYPE RelWithDebInfo)
    else()
      set(Whispercpp_BUILD_TYPE Debug)
    endif()
    set(Whispercpp_Build_GIT_TAG "v1.8.2")
    set(WHISPER_EXTRA_CXX_FLAGS "-fPIC")
    set(WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS)
    set(WHISPER_LIBRARIES Whisper GGML GGMLBase)

    set(WHISPER_DEPENDENCY_LIBRARIES ${BLAS_LIBRARIES})
    set(WHISPER_LIBRARY_TYPE SHARED)

    if(WHISPER_DYNAMIC_BACKENDS)
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON)
      list(
        APPEND
        WHISPER_RUNTIME_MODULES
        GGMLBlas
        GGMLCPU-X64
        GGMLCPU-SSE42
        GGMLCPU-SANDYBRIDGE
        GGMLCPU-HASWELL
        GGMLCPU-SKYLAKEX
        GGMLCPU-ICELAKE
        GGMLCPU-ALDERLAKE
        GGMLCPU-SAPPHIRERAPIDS)
      add_compile_definitions(WHISPER_DYNAMIC_BACKENDS)
    else()
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_NATIVE=ON -DGGML_BACKEND_DL=OFF)
      list(APPEND WHISPER_LIBRARIES GGMLBlas GGMLCPU)
    endif()

    find_package(
      Vulkan
      COMPONENTS glslc
      QUIET)
    if(Vulkan_FOUND)
      message(STATUS "Vulkan found, Libraries: ${Vulkan_LIBRARIES}")
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_VULKAN=ON)
      if(WHISPER_DYNAMIC_BACKENDS)
        list(APPEND WHISPER_RUNTIME_MODULES GGMLVulkan)
      else()
        list(APPEND WHISPER_LIBRARIES GGMLVulkan)
      endif()
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES Vulkan::Vulkan)
    endif()

    find_package(OpenCL QUIET)
    find_package(Python3 QUIET)
    if(OpenCL_FOUND AND Python3_FOUND)
      message(STATUS "OpenCL found, Libraries: ${OpenCL_LIBRARIES}")
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_OPENCL=ON -DGGML_OPENCL_EMBED_KERNELS=ON
           -DGGML_OPENCL_USE_ADRENO_KERNELS=OFF)
      if(WHISPER_DYNAMIC_BACKENDS)
        list(APPEND WHISPER_RUNTIME_MODULES GGMLOpenCL)
      else()
        list(APPEND WHISPER_LIBRARIES GGMLOpenCL)
      endif()
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES OpenCL::OpenCL)
    endif()

    set(HIP_PLATFORM amd)
    set(CMAKE_HIP_PLATFORM amd)
    set(CMAKE_HIP_ARCHITECTURES OFF)
    find_package(hip QUIET)
    find_package(hipblas QUIET)
    find_package(rocblas QUIET)
    if(hip_FOUND
       AND hipblas_FOUND
       AND rocblas_FOUND)
      message(STATUS "hipblas found, Libraries: ${hipblas_LIBRARIES}")
      list(APPEND WHISPER_ADDITIONAL_ENV "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH};HIP_PLATFORM=${HIP_PLATFORM}")
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_HIP=ON -DGGML_HIP_ROCWMMA_FATTN=ON)
      if(WHISPER_DYNAMIC_BACKENDS)
        list(APPEND WHISPER_RUNTIME_MODULES GGMLHip)
      else()
        list(APPEND WHISPER_LIBRARIES GGMLHip)
      endif()
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES hip::host roc::rocblas roc::hipblas)
    endif()

    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
      message(STATUS "CUDA found, Libraries: ${CUDAToolkit_LIBRARIES}")
      list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DGGML_CUDA=ON)

      if(WHISPER_BUILD_ALL_CUDA_ARCHITECTURES)
        list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DCMAKE_CUDA_ARCHITECTURES=all)
      else()
        list(APPEND WHISPER_ADDITIONAL_CMAKE_ARGS -DCMAKE_CUDA_ARCHITECTURES=native)
      endif()

      if(WHISPER_DYNAMIC_BACKENDS)
        list(APPEND WHISPER_RUNTIME_MODULES GGMLCUDA)
      else()
        list(APPEND WHISPER_LIBRARIES GGMLCUDA)
      endif()
      list(APPEND WHISPER_DEPENDENCY_LIBRARIES CUDA::cudart CUDA::cublas CUDA::cublasLt CUDA::cuda_driver)
    endif()

    foreach(component ${WHISPER_LIBRARIES})
      lib_name(${component} WHISPER_COMPONENT_IMPORT_LIB)
      list(
        APPEND
        WHISPER_BYPRODUCTS
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/obs-plugins/${CMAKE_PROJECT_NAME}/${CMAKE_SHARED_LIBRARY_PREFIX}${WHISPER_COMPONENT_IMPORT_LIB}${CMAKE_SHARED_LIBRARY_SUFFIX}"
      )
    endforeach(component ${WHISPER_LIBRARIES})

    # On Linux build a shared Whisper library
    ExternalProject_Add(
      Whispercpp_Build
      DOWNLOAD_EXTRACT_TIMESTAMP true
      GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
      GIT_TAG ${Whispercpp_Build_GIT_TAG}
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
      BUILD_BYPRODUCTS ${WHISPER_BYPRODUCTS}
      CMAKE_GENERATOR ${CMAKE_GENERATOR}
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE} && ${CMAKE_COMMAND} -E
                      copy <SOURCE_DIR>/ggml/include/ggml.h <INSTALL_DIR>/include
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env ${WHISPER_ADDITIONAL_ENV} ${CMAKE_COMMAND} <SOURCE_DIR> -B <BINARY_DIR> -G
        ${CMAKE_GENERATOR} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/obs-plugins/${CMAKE_PROJECT_NAME}
        -DCMAKE_INSTALL_BINDIR=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/obs-plugins/${CMAKE_PROJECT_NAME}
        -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE} -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
        -DCMAKE_CUDA_FLAGS=${WHISPER_EXTRA_CXX_FLAGS} -DBUILD_SHARED_LIBS=ON -DWHISPER_BUILD_TESTS=OFF
        -DWHISPER_BUILD_EXAMPLES=OFF ${WHISPER_ADDITIONAL_CMAKE_ARGS})

    ExternalProject_Get_Property(Whispercpp_Build INSTALL_DIR)

    set(WHISPER_SOURCE_DIR ${INSTALL_DIR})
    set(WHISPER_LIB_DIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/obs-plugins/${CMAKE_PROJECT_NAME})

    add_dependencies(Whispercpp Whispercpp_Build)
  endif()
endif()

foreach(lib ${WHISPER_LIBRARIES})
  message(STATUS "Adding " Whispercpp::${lib} " to build")
  add_whisper_component(Whispercpp::${lib} ${WHISPER_LIBRARY_TYPE} ${WHISPER_SOURCE_DIR} ${WHISPER_LIB_DIR})
endforeach(lib ${WHISPER_LIBRARIES})

foreach(lib ${WHISPER_RUNTIME_MODULES})
  message(STATUS "Adding " Whispercpp::${lib} " to build as runtime module")
  add_whisper_runtime_module(Whispercpp::${lib} ${WHISPER_SOURCE_DIR} ${WHISPER_LIB_DIR})
endforeach(lib ${WHISPER_RUNTIME_MODULES})

foreach(lib ${WHISPER_DEPENDENCY_LIBRARIES})
  message(STATUS "Adding dependency " ${lib} " to linker")
  target_link_libraries(Whispercpp INTERFACE ${lib})
endforeach(lib ${WHISPER_DEPENDENCY_LIBRARIES})

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE Whisper)
