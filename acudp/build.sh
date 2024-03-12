#!/bin/bash -xe

# Validate the acudp checksum
ACUDP_VERSION="1.0.0"
ACUDP_SHA256_CHECKSUM="86c21b07fda2d763b2cc70c2f842500fca85ba17516c46274ea54ce466b6838f"
echo "$ACUDP_SHA256_CHECKSUM acudp-$ACUDP_VERSION.tar.gz" | sha256sum --check --status

# Unpack acudp
rm -rf "./acudp-$ACUDP_VERSION/"
tar -xvf acudp-$ACUDP_VERSION.tar.gz

# We want to install to the parent directory
OUTPUT_DIR=$(realpath ../output)
mkdir -p "$OUTPUT_DIR"

# Configure acudp
cd "./acudp-$ACUDP_VERSION"

# Build acudp (NOTE: We only build libacudp.a because the examples require python too)
make bindirs lib/libacudp.a

# Copy our build artifacts to the output directories
mkdir -p "$OUTPUT_DIR/include" "$OUTPUT_DIR/lib"
cp ./include/*.h* "$OUTPUT_DIR/include/"
cp ./lib/*.a "$OUTPUT_DIR/lib/"
