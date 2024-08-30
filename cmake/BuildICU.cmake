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

  # Add ICU libraries
  find_library(
    ICU_DATA_LIBRARY
    NAMES icudt
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH REQUIRED)
  find_library(
    ICU_UC_LIBRARY
    NAMES icuuc
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH REQUIRED)
  find_library(
    ICU_IN_LIBRARY
    NAMES icuin
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH REQUIRED)

  # find the dlls
  find_file(
    ICU_DATA_DLL
    NAMES icudt${ICU_VERSION_NO_MINOR}.dll
    PATHS ${ICU_BINARY_DIR}
    NO_DEFAULT_PATH)
  find_file(
    ICU_UC_DLL
    NAMES icuuc${ICU_VERSION_NO_MINOR}.dll
    PATHS ${ICU_BINARY_DIR}
    NO_DEFAULT_PATH)
  find_file(
    ICU_IN_DLL
    NAMES icuin${ICU_VERSION_NO_MINOR}.dll
    PATHS ${ICU_BINARY_DIR}
    NO_DEFAULT_PATH)

  # Copy the DLLs to the output directory
  install(FILES ${ICU_DATA_DLL} DESTINATION "obs-plugins/64bit")
  install(FILES ${ICU_UC_DLL} DESTINATION "obs-plugins/64bit")
  install(FILES ${ICU_IN_DLL} DESTINATION "obs-plugins/64bit")

  add_library(ICU::ICU_data SHARED IMPORTED GLOBAL)
  set_target_properties(ICU::ICU_data PROPERTIES IMPORTED_LOCATION "${ICU_DATA_LIBRARY}" IMPORTED_IMPLIB
                                                                                         "${ICU_DATA_LIBRARY}")

  add_library(ICU::ICU_uc SHARED IMPORTED GLOBAL)
  set_target_properties(ICU::ICU_uc PROPERTIES IMPORTED_LOCATION "${ICU_UC_LIBRARY}" IMPORTED_IMPLIB
                                                                                     "${ICU_UC_LIBRARY}")

  add_library(ICU::ICU_in SHARED IMPORTED GLOBAL)
  set_target_properties(ICU::ICU_in PROPERTIES IMPORTED_LOCATION "${ICU_IN_LIBRARY}" IMPORTED_IMPLIB
                                                                                     "${ICU_IN_LIBRARY}")
else()
  set(ICU_URL
      "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION_DASH}/icu4c-${ICU_VERSION_UNDERSCORE}-src.tgz"
  )
  set(ICU_HASH "SHA256=cb968df3e4d2e87e8b11c49a5d01c787bd13b9545280fc6642f826527618caef")
  if(APPLE)
    set(ICU_PLATFORM "MacOSX")
    set(TARGET_ARCH -arch\ $ENV{MACOS_ARCH})
    set(ICU_ADDITIONAL_CONFIGURE_COMMAND_PREFIX CFLAGS=${TARGET_ARCH} CXXFLAGS=${TARGET_ARCH} LDFLAGS=${TARGET_ARCH})
  else()
    set(ICU_PLATFORM "Linux")
    set(ICU_ADDITIONAL_CONFIGURE_COMMAND_PREFIX "A=A")
  endif()

  ExternalProject_Add(
    ICU_build
    GIT_REPOSITORY "https://github.com/unicode-org/icu.git"
    GIT_TAG "release-${ICU_VERSION_DASH}"
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env ${ICU_ADDITIONAL_CONFIGURE_COMMAND_PREFIX} <SOURCE_DIR>/icu4c/source/runConfigureICU
      ${ICU_PLATFORM} --prefix=<INSTALL_DIR> --enable-static --disable-shared
    BUILD_COMMAND make -j4
    BUILD_BYPRODUCTS
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icudata${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icuuc${CMAKE_STATIC_LIBRARY_SUFFIX}
      <INSTALL_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icuin${CMAKE_STATIC_LIBRARY_SUFFIX}
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1)

  ExternalProject_Get_Property(ICU_build INSTALL_DIR)

  set(ICU_INCLUDE_DIR "${INSTALL_DIR}/include")
  set(ICU_LIBRARY_DIR "${INSTALL_DIR}/lib")

  add_library(ICU::ICU_data STATIC IMPORTED GLOBAL)
  add_dependencies(ICU::ICU_data ICU_build)
  set(ICU_DATA_LIBRARY "${ICU_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}icudata${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set_target_properties(ICU::ICU_data PROPERTIES IMPORTED_LOCATION "${ICU_DATA_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES
                                                                                         "${ICU_INCLUDE_DIR}")

  add_library(ICU::ICU_uc STATIC IMPORTED GLOBAL)
  add_dependencies(ICU::ICU_uc ICU_build)
  set(ICU_UC_LIBRARY "${ICU_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}icuuc${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set_target_properties(ICU::ICU_uc PROPERTIES IMPORTED_LOCATION "${ICU_UC_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES
                                                                                     "${ICU_INCLUDE_DIR}")

  add_library(ICU::ICU_in STATIC IMPORTED GLOBAL)
  add_dependencies(ICU::ICU_in ICU_build)
  set(ICU_IN_LIBRARY "${ICU_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}icui18n${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set_target_properties(ICU::ICU_in PROPERTIES IMPORTED_LOCATION "${ICU_IN_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES
                                                                                     "${ICU_INCLUDE_DIR}")
endif()

# Create an interface target for ICU
add_library(ICU INTERFACE)
add_dependencies(ICU ICU_build)
target_link_libraries(ICU INTERFACE ICU::ICU_data ICU::ICU_uc ICU::ICU_in)
target_include_directories(ICU SYSTEM INTERFACE $<BUILD_INTERFACE:${ICU_INCLUDE_DIR}>)
