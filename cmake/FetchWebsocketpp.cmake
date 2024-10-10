include(FetchContent)

FetchContent_Declare(
  websocketpp
  URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
  URL_HASH SHA256=6ce889d85ecdc2d8fa07408d6787e7352510750daa66b5ad44aacb47bea76755)

FetchContent_MakeAvailable(websocketpp)

# Fetch ASIO
FetchContent_Declare(
  asio
  URL https://github.com/chriskohlhoff/asio/archive/asio-1-28-0.tar.gz
  URL_HASH SHA256=1ef87b17e5e32f1a1b4cd840acac6c2a8d0dcde365dde3f9dcd5d1eae0495290)

FetchContent_MakeAvailable(websocketpp asio)
