# build the CTranslate2 library from source https://github.com/OpenNMT/CTranslate2.git

include(ExternalProject)
include(FetchContent)

if(APPLE)
  message(STATUS "Using pre-compiled CTranslate2")
  FetchContent_Declare(
    ctranslate2_fetch
    URL https://github.com/occ-ai/obs-ai-ctranslate2-dep/releases/download/1.2.0/libctranslate2-macos-Release-1.2.0.tar.gz
    URL_HASH SHA256=9029F19B0F50E5EDC14473479EDF0A983F7D6FA00BE61DC1B01BF8AA7F1CDB1B)
  FetchContent_MakeAvailable(ctranslate2_fetch)

  add_library(ct2 INTERFACE)
  target_link_libraries(ct2 INTERFACE "-framework Accelerate" ${ctranslate2_fetch_SOURCE_DIR}/lib/libctranslate2.a
                                      ${ctranslate2_fetch_SOURCE_DIR}/lib/libcpu_features.a)
  set_target_properties(ct2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ctranslate2_fetch_SOURCE_DIR}/include)
  target_compile_options(ct2 INTERFACE -Wno-shorten-64-to-32 -Wno-comma)

elseif(WIN32)
  message(STATUS "Using pre-compiled CTranslate2")

  if(NOT ${ACCELERATION} STREQUAL "nvidia")
    FetchContent_Declare(
      ctranslate2_fetch
      URL https://github.com/occ-ai/obs-ai-ctranslate2-dep/releases/download/1.2.0/libctranslate2-windows-4.1.1-Release-cpu.zip
      URL_HASH SHA256=30ff8b2499b8d3b5a6c4d6f7f8ddbc89e745ff06e0050b645e3b7c9b369451a3)
  else()
    # add compile definitions for CUDA
    add_compile_definitions(POLYGLOT_WITH_CUDA)
    add_compile_definitions(POLYGLOT_CUDA_VERSION="12.8.0")

    FetchContent_Declare(
      ctranslate2_fetch
      URL https://github.com/occ-ai/obs-ai-ctranslate2-dep/releases/download/1.2.0/libctranslate2-windows-4.1.1-Release-cuda12.2.0.zip
      URL_HASH SHA256=131724d510f9f2829970953a1bc9e4e8fb7b4cbc8218e32270dcfe6172a51558)
  endif()

  FetchContent_MakeAvailable(ctranslate2_fetch)

  add_library(ct2 INTERFACE)
  target_link_libraries(ct2 INTERFACE ${ctranslate2_fetch_SOURCE_DIR}/lib/ctranslate2.lib)
  set_target_properties(ct2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ctranslate2_fetch_SOURCE_DIR}/include)
  target_compile_options(ct2 INTERFACE /wd4267 /wd4244 /wd4305 /wd4996 /wd4099)

  file(GLOB CT2_DLLS ${ctranslate2_fetch_SOURCE_DIR}/bin/*.dll)
  install(FILES ${CT2_DLLS} DESTINATION "obs-plugins/64bit")
else()
  if(USE_SYSTEM_CTRANSLATE2)
    message(STATUS "Using system CTranslate2")
    find_library(CT2_LIBRARY ctranslate2 REQUIRED)
    find_path(CT2_INCLUDE_DIR ctranslate2/translator.h REQUIRED)
    find_library(CPU_FEATURES_LIBRARY cpu_features QUIET)

    add_library(ct2 INTERFACE)
    target_link_libraries(ct2 INTERFACE ${CT2_LIBRARY})
    if(CPU_FEATURES_LIBRARY)
      target_link_libraries(ct2 INTERFACE ${CPU_FEATURES_LIBRARY})
    endif()
    target_link_libraries(ct2 INTERFACE ${BLAS_LIBRARIES})
    target_include_directories(ct2 SYSTEM INTERFACE ${CT2_INCLUDE_DIR})
  else()
    message(STATUS "Building CTranslate2 from source")
    # Enable ccache if available
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()

    # build cpu_features from source
    set(CPU_FEATURES_VERSION "0.9.0")
    set(CPU_FEATURES_URL "https://github.com/google/cpu_features.git")
    if(CMAKE_MAJOR_VERSION EQUAL 4)
      set(CPU_FEATURES_CMAKE_ARGS -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
    else()
      set(CPU_FEATURES_CMAKE_ARGS -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF)
    endif()
    ExternalProject_Add(
      cpu_features_build
      GIT_REPOSITORY ${CPU_FEATURES_URL}
      GIT_TAG v${CPU_FEATURES_VERSION}
      GIT_PROGRESS 1
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
      CMAKE_GENERATOR ${CMAKE_GENERATOR}
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
      BUILD_BYPRODUCTS
        <INSTALL_DIR>/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}cpu_features${CMAKE_STATIC_LIBRARY_SUFFIX}
      CMAKE_ARGS -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                 -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                 ${CPU_FEATURES_CMAKE_ARGS}
      LOG_CONFIGURE ON
      LOG_BUILD ON
      LOG_INSTALL ON)
    ExternalProject_Get_Property(cpu_features_build INSTALL_DIR)

    add_library(cpu_features STATIC IMPORTED GLOBAL)
    add_dependencies(cpu_features cpu_features_build)
    set_target_properties(
      cpu_features
      PROPERTIES
        IMPORTED_LOCATION
        ${INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}cpu_features${CMAKE_STATIC_LIBRARY_SUFFIX})
    set_target_properties(cpu_features PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)

    # build CTranslate2 from source
    set(CT2_VERSION "4.1.1")
    set(CT2_URL "https://github.com/OpenNMT/CTranslate2.git")

    if(CMAKE_MAJOR_VERSION EQUAL 4)
      set(CT2_CMAKE_PLATFORM_OPTIONS -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                                     -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
    else()
      set(CT2_CMAKE_PLATFORM_OPTIONS -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
    endif()
    set(CT2_LIB_INSTALL_LOCATION
        ${CMAKE_INSTALL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}ctranslate2${CMAKE_STATIC_LIBRARY_SUFFIX})

    ExternalProject_Add(
      ct2_build
      GIT_REPOSITORY ${CT2_URL}
      GIT_TAG v${CT2_VERSION}
      GIT_PROGRESS 1
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
      CMAKE_GENERATOR ${CMAKE_GENERATOR}
      INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
      BUILD_BYPRODUCTS <INSTALL_DIR>/${CT2_LIB_INSTALL_LOCATION}
      CMAKE_ARGS -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
                 -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                 -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
                 -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                 -DOPENMP_RUNTIME=COMP
                 -DWITH_MKL=OFF
                 -DWITH_DNNL=OFF
                 -DWITH_ACCELERATE=OFF
                 -DWITH_OPENBLAS=ON
                 -DWITH_RUY=OFF
                 -DWITH_CUDA=OFF
                 -DWITH_CUDNN=OFF
                 -DWITH_TENSOR_PARALLEL=ON
                 -DENABLE_CPU_DISPATCH=ON
                 -DENABLE_PROFILING=OFF
                 -DBUILD_CLI=OFF
                 -DBUILD_TESTS=OFF
                 ${CT2_CMAKE_PLATFORM_OPTIONS}
      LOG_CONFIGURE ON
      LOG_BUILD ON
      LOG_INSTALL ON)

    ExternalProject_Get_Property(ct2_build INSTALL_DIR)

    add_library(ct2::ct2 STATIC IMPORTED GLOBAL)
    add_dependencies(ct2::ct2 ct2_build cpu_features_build)
    set_target_properties(ct2::ct2 PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/${CT2_LIB_INSTALL_LOCATION})
    set_target_properties(ct2::ct2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)

    add_library(ct2 INTERFACE)
    target_link_libraries(ct2 INTERFACE ct2::ct2 cpu_features ${BLAS_LIBRARIES})

  endif()
endif()
