set(LIBCURL_SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/curl)

find_package(Git QUIET)
execute_process(
  COMMAND ${GIT_EXECUTABLE} checkout curl-8_2_0
  WORKING_DIRECTORY ${LIBCURL_SOURCE_DIR}
  RESULT_VARIABLE GIT_SUBMOD_RESULT)

if(OS_MACOS)
  set(CURL_USE_OPENSSL OFF)
  set(CURL_USE_SECTRANSP ON)
elseif(OS_WINDOWS)
  set(CURL_USE_OPENSSL OFF)
  set(CURL_USE_SCHANNEL ON)
elseif(OS_LINUX)
  add_compile_options(-fPIC)
  set(CURL_USE_OPENSSL ON)
endif()
set(BUILD_CURL_EXE OFF)
set(BUILD_SHARED_LIBS OFF)
set(HTTP_ONLY OFF)
set(CURL_USE_LIBSSH2 OFF)
add_subdirectory(${LIBCURL_SOURCE_DIR} EXCLUDE_FROM_ALL)
if(OS_MACOS)
  target_compile_options(
    libcurl PRIVATE -Wno-error=ambiguous-macro -Wno-error=deprecated-declarations -Wno-error=unreachable-code
                    -Wno-error=unused-parameter -Wno-error=unused-variable)
endif()
include_directories(SYSTEM ${LIBCURL_SOURCE_DIR}/include)
