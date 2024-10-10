include(FetchContent)

FetchContent_Declare(
  websocketpp
  URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
  URL_HASH SHA256=6ce889d85ecdc2d8fa07408d6787e7352510750daa66b5ad44aacb47bea76755
)

FetchContent_MakeAvailable(websocketpp)
