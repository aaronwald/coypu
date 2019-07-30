
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

# Protobuf
```bash
sudo apt install libprotobuf-c-dev protobuf-compiler-grpc protobuf-compiler
```

# Rust
```bash
curl https://sh.rustup.rs -sSf | sh
```

# DPDK Build

```bash
mkdir -p lib
cd lib
git clone https://github.com/linux-rdma/rdma-core.git
cd rdma-core
mkdir build
cd build
CFLAGS=-fPIC cmake -DIN_PLACE=1 -DENABLE_STATIC=1 -GNinja ..
ninja
cd ..
cd ..
git clone https://github.com/DPDK/dpdk.git
cd dpdk
git checkout v19.02
sed -i 's/CONFIG_RTE_LIBRTE_PMD_PCAP=n/CONFIG_RTE_LIBRTE_PMD_PCAP=y/' config/common_base
sed -i 's/CONFIG_RTE_LIBRTE_IEEE1588=n/CONFIG_RTE_LIBRTE_IEEE1588=y/' config/common_base
sed -i 's/CONFIG_RTE_LIBRTE_MLX5_PMD=n/CONFIG_RTE_LIBRTE_MLX5_PMD=y/' config/common_base
sed -i 's/CONFIG_RTE_ETHDEV_RXTX_CALLBACKS=n/CONFIG_RTE_ETHDEV_RXTX_CALLBACKS=y/' config/common_base
#sed -i 's/CONFIG_RTE_ETHDEV_PROFILE_WITH_VTUNE=n/CONFIG_RTE_ETHDEV_PROFILE_WITH_VTUNE=y/' config/common_base
#  -O0 -g"
make config T=x86_64-native-linuxapp-gcc
make EXTRA_CFLAGS="-Wno-error=missing-prototypes -I$HOME/dev/coypu/lib/rdma-core/build/include" EXTRA_LDFLAGS="-L$HOME/dev/coypu/lib/rdma-core/build/lib" PKG_CONFIG_PATH="$HOME/dev/coypu/lib//rdma-core/build/lib/pkgconfig" -j 4
sudo make install prefix=../3rd/dpdk
cd ../..
```

# SPDK build
```bash
cd lib
git clone https://github.com/spdk/spdk.git
cd spdk
git checkout v19.04
git submodule update --init
#./configure --with-dpdk=/usr/local/share/dpdk/x86_64-native-linuxapp-gcc
./configure --with-dpdk=../3rd/dpdk/
make -j 4
sudo make install prefix=$HOME/dev/coypu/lib/3rd/spdk
cd ../..
```

# Ninja build

```bash
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
```
