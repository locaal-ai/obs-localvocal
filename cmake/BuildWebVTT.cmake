include(FetchContent)

set(Rust_RUSTUP_INSTALL_MISSING_TARGET true)

if(OS_MACOS)
  if("$ENV{MACOS_ARCH}" STREQUAL "x86_64")
    set(Rust_CARGO_TARGET "x86_64-apple-darwin")
  endif()
endif()

FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.5 # Optionally specify a commit hash, version tag or branch here
)
FetchContent_MakeAvailable(Corrosion)

# Import targets defined in a package or workspace manifest `Cargo.toml` file
corrosion_import_crate(MANIFEST_PATH "${CMAKE_SOURCE_DIR}/deps/c-webvtt-in-video-stream/Cargo.toml" CRATE_TYPES
                       "staticlib" PROFILE release)

set_target_properties(c_webvtt_in_video_stream PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                                          "${CMAKE_SOURCE_DIR}/deps/c-webvtt-in-video-stream/target/")
