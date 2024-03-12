## About



## Data Flow


## Requirements

- [libjson-c](https://github.com/json-c/json-c)  
- [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)  
- [acudp](https://github.com/vpicon/acudp)  

## Build

Install dependencies:

Ubuntu:
```bash
sudo apt install automake autoconf libtool texinfo gcc-c++ cmake json-c-dev gtest-dev
```

Fedora:
```bash
sudo dnf install automake autoconf libtool texinfo gcc-c++ cmake json-c-devel gtest-devel
```

Build:
```bash
$ (cd acudp && ./build.sh)
$ (cd libmicrohttpd && ./build.sh)
$ cmake .
$ make -j
```

## Run the Unit Tests

```bash
$ ./unit_tests
```

## Usage
