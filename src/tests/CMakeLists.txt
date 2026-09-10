set(TEST_EXEC_NAME ${CMAKE_PROJECT_NAME}-tests)

set(CUSTOM_API_JSON_TEST_EXEC_NAME ${CMAKE_PROJECT_NAME}-custom-api-json-tests)

add_executable(
  ${CUSTOM_API_JSON_TEST_EXEC_NAME}
  ${CMAKE_SOURCE_DIR}/src/tests/custom-api-json-path-test.cpp
  ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/custom-api-json.cpp)
target_include_directories(${CUSTOM_API_JSON_TEST_EXEC_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${CUSTOM_API_JSON_TEST_EXEC_NAME} PRIVATE OBS::libobs)
add_test(NAME custom-api-json-path COMMAND ${CUSTOM_API_JSON_TEST_EXEC_NAME})

add_executable(${TEST_EXEC_NAME})

target_sources(
  ${TEST_EXEC_NAME}
  PRIVATE ${CMAKE_SOURCE_DIR}/src/tests/localvocal-offline-test.cpp
          ${CMAKE_SOURCE_DIR}/src/tests/audio-file-utils.cpp
          ${CMAKE_SOURCE_DIR}/src/transcription-utils.cpp
          ${CMAKE_SOURCE_DIR}/src/model-utils/model-find-utils.cpp
          ${CMAKE_SOURCE_DIR}/src/whisper-utils/whisper-processing.cpp
          ${CMAKE_SOURCE_DIR}/src/whisper-utils/whisper-utils.cpp
          ${CMAKE_SOURCE_DIR}/src/whisper-utils/silero-vad-onnx.cpp
          ${CMAKE_SOURCE_DIR}/src/whisper-utils/token-buffer-thread.cpp
          ${CMAKE_SOURCE_DIR}/src/whisper-utils/vad-processing.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/language_codes.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/translation.cpp
          ${CMAKE_SOURCE_DIR}/src/ui/filter-replace-utils.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/translation-language-utils.cpp)

include(${CMAKE_SOURCE_DIR}/cmake/FindLibAvObs.cmake)
find_libav(${TEST_EXEC_NAME})

target_link_libraries(${TEST_EXEC_NAME} PRIVATE ct2 sentencepiece Whispercpp Ort OBS::libobs ICU)
target_include_directories(${TEST_EXEC_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# install the tests to the release/test directory
install(TARGETS ${TEST_EXEC_NAME} DESTINATION test)
