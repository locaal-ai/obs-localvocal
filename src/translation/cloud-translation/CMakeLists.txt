# add source files
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE # ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/aws.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/azure.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/claude.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/curl-helper.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/deepl.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/google-cloud.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/openai.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/papago.cpp
          ${CMAKE_SOURCE_DIR}/src/translation/cloud-translation/translation-cloud.cpp)
