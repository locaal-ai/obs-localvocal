include(FetchContent)
include(ExternalProject)

set(ICU_VERSION "75.1")
set(ICU_VERSION_UNDERSCORE "75_1")
set(ICU_VERSION_DASH "75-1")
set(ICU_VERSION_NO_MINOR "75")

if(WIN32)
  set(ICU_URL
      "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION_DASH}/icu4c-${ICU_VERSION_UNDERSCORE}-Win64-MSVC2022.zip"
  )
  set(ICU_HASH "SHA256=7ac9c0dc6ccc1ec809c7d5689b8d831c5b8f6b11ecf70fdccc55f7ae8731ac8f")

  FetchContent_Declare(
    ICU_build
    URL ${ICU_URL}
    URL_HASH ${ICU_HASH})

  FetchContent_MakeAvailable(ICU_build)

  # Assuming the ZIP structure, adjust paths as necessary
  set(ICU_INCLUDE_DIR "${icu_build_SOURCE_DIR}/include")
  set(ICU_LIBRARY_DIR "${icu_build_SOURCE_DIR}/lib64")
  set(ICU_BINARY_DIR "${icu_build_SOURCE_DIR}/bin64")

  # Define the library names
  set(ICU_LIBRARIES icudt icuuc icuin)

  foreach(lib ${ICU_LIBRARIES})
    # Add ICU library
    find_library(
      ICU_LIB_${lib}
      NAMES ${lib}
      PATHS ${ICU_LIBRARY_DIR}
      NO_DEFAULT_PATH REQUIRED)
    # find the dll
    find_file(
      ICU_DLL_${lib}
      NAMES ${lib}${ICU_VERSION_NO_MINOR}.dll
      PATHS ${ICU_BINARY_DIR}
      NO_DEFAULT_PATH)
    # Copy the DLLs to the output directory
    install(FILES ${ICU_DLL_${lib}} DESTINATION "obs-plugins/64bit")
    # add the library
    add_library(ICU::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ICU::${lib} PROPERTIES IMPORTED_LOCATION "${ICU_LIB_${lib}}" IMPORTED_IMPLIB
                                                                                       "${ICU_LIB_${lib}}")
  endforeach()
else()
  set(ICU_URL
      "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION_DASH}/icu4c-${ICU_VERSION_UNDERSCORE}-src.tgz"
  )
  set(ICU_HASH "SHA256=cb968df3e4d2e87e8b11c49a5d01c787bd13b9545280fc6642f826527618caef")
  if(APPLE)
    set(ICU_PLATFORM "MacOSX")
    set(TARGET_ARCH -arch\ $ENV{MACOS_ARCH})
    set(ICU_BUILD_ENV_VARS CFLAGS=${TARGET_ARCH} CXXFLAGS=${TARGET_ARCH} LDFLAGS=${TARGET_ARCH})
  else()
    set(ICU_PLATFORM "Linux")
    set(ICU_BUILD_ENV_VARS CFLAGS=-fPIC CXXFLAGS=-fPIC LDFLAGS=-fPIC)
  endif()

  ExternalProject_Add(
    ICU_build
    DOWNLOAD_EXTRACT_TIMESTAMP true
    GIT_REPOSITORY "https://github.com/unicode-org/icu.git"
    GIT_TAG "release-${ICU_VERSION_DASH}"
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env ${ICU_BUILD_ENV_VARS} <SOURCE_DIR>/icu4c/source/runConfigureICU ${ICU_PLATFORM}
      --prefix=<INSTALL_DIR> --enable-static --disable-shared --disable-tools --disable-samples --disable-layout
      --disable-layoutex --disable-tests --disable-draft --disable-extras --disable-icuio
    BUILD_COMMAND make -j4
    BUILD_BYPRODUCTS
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icudata${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icuuc${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icui18n${CMAKE_STATIC_LIBRARY_SUFFIX}
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1)

  ExternalProject_Get_Property(ICU_build INSTALL_DIR)

  set(ICU_INCLUDE_DIR "${INSTALL_DIR}/include")
  set(ICU_LIBRARY_DIR "${INSTALL_DIR}/lib")

  set(ICU_LIBRARIES icudata icuuc icui18n)

  foreach(lib ${ICU_LIBRARIES})
    add_library(ICU::${lib} STATIC IMPORTED GLOBAL)
    add_dependencies(ICU::${lib} ICU_build)
    set(ICU_LIBRARY "${ICU_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set_target_properties(ICU::${lib} PROPERTIES IMPORTED_LOCATION "${ICU_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES
                                                                                    "${ICU_INCLUDE_DIR}")
  endforeach(lib ${ICU_LIBRARIES})
endif()

# Create an interface target for ICU
add_library(ICU INTERFACE)
add_dependencies(ICU ICU_build)
foreach(lib ${ICU_LIBRARIES})
  target_link_libraries(ICU INTERFACE ICU::${lib})
endforeach()
target_include_directories(ICU SYSTEM INTERFACE $<BUILD_INTERFACE:${ICU_INCLUDE_DIR}>)
