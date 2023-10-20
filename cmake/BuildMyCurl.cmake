include(FetchContent)

set(LibCurl_VERSION "8.4.0-1")
set(LibCurl_BASEURL "https://github.com/obs-ai/obs-ai-libcurl-dep/releases/download/${LibCurl_VERSION}")

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(LibCurl_BUILD_TYPE Release)
else()
  set(LibCurl_BUILD_TYPE Debug)
endif()

if(APPLE)
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-macos-master-Release.tar.gz")
    set(LibCurl_HASH MD5=C89D8BC38E221737B7D5C9E5AE18C079)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-macos-master-Debug.tar.gz")
    set(LibCurl_HASH MD5=F2A643B44D8626119DDB326C5AFFD704)
  endif()
elseif(MSVC)
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-windows-master-Release.zip")
    set(LibCurl_HASH MD5=81BC6A5004F0D52AA082A25B813B1362)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-windows-master-Debug.zip")
    set(LibCurl_HASH MD5=8DC73477E3D427E2EA5124B57985BF47)
  endif()
else()
  if(LibCurl_BUILD_TYPE STREQUAL Release)
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-linux-master-Release.tar.gz")
    set(LibCurl_HASH MD5=EA9537A0BDBA1C53DC4A54FE767E90E8)
  else()
    set(LibCurl_URL "${LibCurl_BASEURL}/libcurl-linux-master-Debug.tar.gz")
    set(LibCurl_HASH MD5=6F405CFA0398A04ADD1C2FCE9596F8B8)
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
