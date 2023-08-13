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

ExternalProject_Add(
  Whispercpp_Build
  DOWNLOAD_EXTRACT_TIMESTAMP true
  GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
  GIT_TAG 7b374c9ac9b9861bb737eec060e4dfa29d229259
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
  BUILD_BYPRODUCTS <INSTALL_DIR>/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX}
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config ${Whispercpp_BUILD_TYPE}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
             -DCMAKE_BUILD_TYPE=${Whispercpp_BUILD_TYPE}
             -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
             -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
             -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_}
             -DCMAKE_CXX_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
             -DCMAKE_C_FLAGS=${WHISPER_EXTRA_CXX_FLAGS}
             -DBUILD_SHARED_LIBS=OFF
             -DWHISPER_BUILD_TESTS=OFF
             -DWHISPER_BUILD_EXAMPLES=OFF
             -DWHISPER_OPENBLAS=ON)

ExternalProject_Get_Property(Whispercpp_Build INSTALL_DIR)

add_library(Whispercpp::Whisper STATIC IMPORTED)
set_target_properties(
  Whispercpp::Whisper
  PROPERTIES IMPORTED_LOCATION
             ${INSTALL_DIR}/lib/static/${CMAKE_STATIC_LIBRARY_PREFIX}whisper${CMAKE_STATIC_LIBRARY_SUFFIX})

add_library(Whispercpp INTERFACE)
add_dependencies(Whispercpp Whispercpp_Build)
target_link_libraries(Whispercpp INTERFACE Whispercpp::Whisper)
set_target_properties(Whispercpp::Whisper PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include)
if(APPLE)
  target_link_libraries(Whispercpp INTERFACE "-framework Accelerate")
endif(APPLE)
