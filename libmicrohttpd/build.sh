#!/bin/bash -xe

# Validate the libmicrohttpd checksum
MICROHTTPD_VERSION="1.0.0"
MICROHTTPD_SHA256_CHECKSUM="14ae571ca35b839e9039f26782d84d6d5fd872414863db8c01694ef9c189e9aa"
echo "$MICROHTTPD_SHA256_CHECKSUM libmicrohttpd-$MICROHTTPD_VERSION.tar.gz" | sha256sum --check --status

# Unpack libmicrohttpd
rm -rf "./libmicrohttpd-$MICROHTTPD_VERSION/"
tar -xvf libmicrohttpd-$MICROHTTPD_VERSION.tar.gz

# We want to install to the parent directory
OUTPUT_DIR=$(realpath ../output)
mkdir -p "$OUTPUT_DIR"

# Configure libmicrohttpd
cd "./libmicrohttpd-$MICROHTTPD_VERSION"
./bootstrap
# Enable experimental for websocket support
./configure --enable-experimental --prefix=$OUTPUT_DIR

# Build and install libmicrohttpd
make
make install
