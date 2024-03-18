include(FetchContent)

set(LibCurl_VERSION "8.4.0-3")
set(LibCurl_BASEURL "https://github.com/occ-ai/obs-ai-libcurl-dep/releases/download/${LibCurl_VERSION}")

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(LibCurl_BUILD_TYPE Release)
else()
  set(LibCurl_BUILD_TYPE Debug)
endif()

if(APPLE)
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-macos-${LibCurl_VERSION}-Release.tar.gz")
    set(LibCurl_HASH SHA256=5ef7bfed2c2bca17ba562aede6a3c3eb465b8d7516cff86ca0f0d0337de951e1)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-macos-${LibCurl_VERSION}-Debug.tar.gz")
    set(LibCurl_HASH SHA256=da0801168eac5103e6b27bfd0f56f82e0617f85e4e6c69f476071dbba273403b)
  endif()
elseif(MSVC)
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-windows-${LibCurl_VERSION}-Release.zip")
    set(LibCurl_HASH SHA256=bf4d4cd7d741712a2913df0994258d11aabe22c9a305c9f336ed59e76f351adf)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-windows-${LibCurl_VERSION}-Debug.zip")
    set(LibCurl_HASH SHA256=9fe20e677ffb0d7dd927b978d532e23574cdb1923e2d2ca7c5e42f1fff2ec529)
  endif()
else()
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-linux-${LibCurl_VERSION}-Release.tar.gz")
    set(LibCurl_HASH SHA256=f2cd80b7d3288fe5b4c90833bcbf0bde7c9574bc60eddb13015df19c5a09f56b)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-linux-${LibCurl_VERSION}-Debug.tar.gz")
    set(LibCurl_HASH SHA256=6a41d3daef98acc3172b3702118dcf1cccbde923f3836ed2f4f3ed7301e47b8b)
  endif()
endif()

FetchContent_Declare(
  libcurl_fetch
  URL ${LibCurl_URL}
  URL_HASH ${LibCurl_HASH})
FetchContent_MakeAvailable(libcurl_fetch)

if(MSVC)
  set(libcurl_fetch_lib_location "${libcurl_fetch_SOURCE_DIR}/lib/libcurl.lib")
  set(libcurl_fetch_link_libs "\$<LINK_ONLY:ws2_32>;\$<LINK_ONLY:advapi32>;\$<LINK_ONLY:crypt32>;\$<LINK_ONLY:bcrypt>")
else()
  find_package(ZLIB REQUIRED)
  set(libcurl_fetch_lib_location "${libcurl_fetch_SOURCE_DIR}/lib/libcurl.a")
  if(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    set(libcurl_fetch_link_libs "\$<LINK_ONLY:OpenSSL::SSL>;\$<LINK_ONLY:OpenSSL::Crypto>;\$<LINK_ONLY:ZLIB::ZLIB>")
  else()
    set(libcurl_fetch_link_libs
        "-framework SystemConfiguration;-framework Security;-framework CoreFoundation;-framework CoreServices;ZLIB::ZLIB"
    )
  endif()
endif()

# Create imported target
add_library(libcurl STATIC IMPORTED)

set_target_properties(
  libcurl
  PROPERTIES INTERFACE_COMPILE_DEFINITIONS "CURL_STATICLIB"
             INTERFACE_INCLUDE_DIRECTORIES "${libcurl_fetch_SOURCE_DIR}/include"
             INTERFACE_LINK_LIBRARIES "${libcurl_fetch_link_libs}")
set_property(
  TARGET libcurl
  APPEND
  PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(libcurl PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C" IMPORTED_LOCATION_RELEASE
                                                                                       ${libcurl_fetch_lib_location})
