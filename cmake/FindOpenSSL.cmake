set(OPENSSL_ROOT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows"
    CACHE STRING "Path to OpenSSL")
find_package(OpenSSL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::SSL)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::Crypto)

# copy the openssl dlls to the release directory
if(WIN32)
  set(OpenSS_LIB_NAMES "libcrypto-3-x64" "libssl-3-x64")
  foreach(lib_name IN LISTS OpenSS_LIB_NAMES)
    install(FILES ${OPENSSL_ROOT_DIR}/bin/${lib_name}.dll DESTINATION "obs-plugins/64bit")
  endforeach()
endif()
