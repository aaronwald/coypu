
# Coypu Websocket LV C++ Cache

HTTP/1.1 + Websocket Client/Server
HTTP/2 + gRPC Server (Very basic)

## Crypto Feeds

 * [Coinbase Pro Market Data API](https://docs.pro.coinbase.com/)
 * [Kraken Version 0.1.1](https://www.kraken.com/features/websocket-api)

## Build

```
git clone https://github.com/Tencent/rapidjson.git
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Dependencies

 * [nghttp2](https://nghttp2.org/) - HPACk
 * [Protocol Buffers](https://developers.google.com/protocol-buffers/)
 * [RapidJSON](http://rapidjson.org/)
 * [spdlog](https://github.com/gabime/spdlog)
 * [libyaml](https://github.com/yaml/libyaml)
 * [openssl](https://www.openssl.org/) 
 * [Googletest](https://github.com/google/googletest)
