# build sentencepiece from "https://github.com/google/sentencepiece.git"

if(APPLE)

  include(FetchContent)

  FetchContent_Declare(
    sentencepiece_fetch
    URL https://github.com/occ-ai/obs-ai-ctranslate2-dep/releases/download/1.1.1/libsentencepiece-macos-Release-1.1.1.tar.gz
    URL_HASH SHA256=c911f1e84ea94925a8bc3fd3257185b2e18395075509c8659cc7003a979e0b32)
  FetchContent_MakeAvailable(sentencepiece_fetch)
  add_library(sentencepiece INTERFACE)
  target_link_libraries(sentencepiece INTERFACE ${sentencepiece_fetch_SOURCE_DIR}/lib/libsentencepiece.a)
  set_target_properties(sentencepiece PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                 ${sentencepiece_fetch_SOURCE_DIR}/include)
elseif(WIN32)

  FetchContent_Declare(
    sentencepiece_fetch
    URL https://github.com/occ-ai/obs-ai-ctranslate2-dep/releases/download/1.1.1/sentencepiece-windows-0.2.0-Release.zip
    URL_HASH SHA256=846699c7fa1e8918b71ed7f2bd5cd60e47e51105e1d84e3192919b4f0f10fdeb)
  FetchContent_MakeAvailable(sentencepiece_fetch)
  add_library(sentencepiece INTERFACE)
  target_link_libraries(sentencepiece INTERFACE ${sentencepiece_fetch_SOURCE_DIR}/lib/sentencepiece.lib)
  set_target_properties(sentencepiece PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                 ${sentencepiece_fetch_SOURCE_DIR}/include)

else()

  # Enable ccache if available
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  endif()

  set(SP_URL
      "https://github.com/google/sentencepiece.git"
      CACHE STRING "URL of sentencepiece repository")

  set(SP_CMAKE_OPTIONS -DSPM_ENABLE_SHARED=OFF)
  set(SENTENCEPIECE_INSTALL_LIB_LOCATION lib/${CMAKE_STATIC_LIBRARY_PREFIX}sentencepiece${CMAKE_STATIC_LIBRARY_SUFFIX})

  include(ExternalProject)

  ExternalProject_Add(
    sentencepiece_build
    GIT_REPOSITORY ${SP_URL}
    GIT_TAG v0.1.99
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
    BUILD_BYPRODUCTS <INSTALL_DIR>/${SENTENCEPIECE_INSTALL_LIB_LOCATION}
    CMAKE_ARGS -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} ${SP_CMAKE_OPTIONS})
  ExternalProject_Get_Property(sentencepiece_build INSTALL_DIR)

  add_library(libsentencepiece STATIC IMPORTED GLOBAL)
  add_dependencies(libsentencepiece sentencepiece_build)
  set_target_properties(libsentencepiece PROPERTIES IMPORTED_LOCATION
                                                    ${INSTALL_DIR}/${SENTENCEPIECE_INSTALL_LIB_LOCATION})

  add_library(sentencepiece INTERFACE)
  add_dependencies(sentencepiece libsentencepiece)
  target_link_libraries(sentencepiece INTERFACE libsentencepiece)
  target_include_directories(sentencepiece INTERFACE ${INSTALL_DIR}/include)

endif()
