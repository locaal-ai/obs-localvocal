# Find LibAV from the OBS dependencies

function(find_libav TARGET)
  if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
      FFMPEG
      REQUIRED
      IMPORTED_TARGET
      libavformat
      libavcodec
      libavutil
      libswresample)
    if(FFMPEG_FOUND)
      target_link_libraries(${TARGET} PRIVATE PkgConfig::FFMPEG)
    else()
      message(FATAL_ERROR "FFMPEG not found!")
    endif()
    return()
  endif()

  if(NOT buildspec)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)
  endif()
  string(
    JSON
    version
    GET
    ${buildspec}
    dependencies
    prebuilt
    version)

  if(MSVC)
    set(arch ${CMAKE_GENERATOR_PLATFORM})
  elseif(APPLE)
    set(arch universal)
  endif()
  set(deps_root "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-${version}-${arch}")

  target_include_directories(${TARGET} PRIVATE "${deps_root}/include")
  target_link_libraries(
    ${TARGET}
    PRIVATE "${deps_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}avcodec${CMAKE_STATIC_LIBRARY_SUFFIX}"
            "${deps_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}avformat${CMAKE_STATIC_LIBRARY_SUFFIX}"
            "${deps_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}avutil${CMAKE_STATIC_LIBRARY_SUFFIX}"
            "${deps_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}swresample${CMAKE_STATIC_LIBRARY_SUFFIX}")
endfunction(find_libav)
