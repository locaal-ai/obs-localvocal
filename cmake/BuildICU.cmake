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
    ICU
    URL ${ICU_URL}
    URL_HASH ${ICU_HASH})

  FetchContent_MakeAvailable(ICU)

  # Assuming the ZIP structure, adjust paths as necessary
  set(ICU_INCLUDE_DIR "${icu_SOURCE_DIR}/include")
  set(ICU_LIBRARY_DIR "${icu_SOURCE_DIR}/lib64")
  set(ICU_BINARY_DIR "${icu_SOURCE_DIR}/bin64")

  # Add ICU libraries
  find_library(
    ICU_DATA_LIBRARY
    NAMES icudt
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)
  find_library(
    ICU_UC_LIBRARY
    NAMES icuuc
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)
  find_library(
    ICU_IN_LIBRARY
    NAMES icuin
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)

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

else() # Mac and Linux
  set(ICU_URL
      "https://github.com/unicode-org/icu/releases/download/release-${ICU_VERSION_UNDERSCORE}/icu4c-${ICU_VERSION_UNDERSCORE}-src.tgz"
  )
  set(ICU_HASH "SHA256=94bb97d88f13bb74ec0168446a845511bd92c1c49ee8e63df646a48c38dfde6d")

  ExternalProject_Add(
    ICU
    URL ${ICU_URL}
    URL_HASH ${ICU_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/source/runConfigureICU Linux --prefix=<INSTALL_DIR>
    BUILD_COMMAND make -j4
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1)

  ExternalProject_Get_Property(ICU INSTALL_DIR)

  set(ICU_INCLUDE_DIR "${INSTALL_DIR}/include")
  set(ICU_LIBRARY_DIR "${INSTALL_DIR}/lib")

  # Add ICU libraries
  find_library(
    ICU_DATA_LIBRARY
    NAMES icudata
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)
  find_library(
    ICU_UC_LIBRARY
    NAMES icuuc
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)
  find_library(
    ICU_IN_LIBRARY
    NAMES icui18n
    PATHS ${ICU_LIBRARY_DIR}
    NO_DEFAULT_PATH)
endif()

# Create an interface target for ICU
add_library(ICU::ICU INTERFACE IMPORTED GLOBAL)
add_dependencies(ICU::ICU ICU)
target_include_directories(ICU::ICU INTERFACE ${ICU_INCLUDE_DIR})
target_link_libraries(ICU::ICU INTERFACE ${ICU_DATA_LIBRARY} ${ICU_UC_LIBRARY} ${ICU_IN_LIBRARY})
