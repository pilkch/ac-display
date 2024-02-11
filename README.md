## About



## Data Flow


## Requirements

- [libjson-c](https://github.com/json-c/json-c)  
- [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)  

## Build

Install dependencies:

Ubuntu:
```bash
sudo apt install gcc-c++ cmake json-c-dev libmicrohttpd-dev gtest-dev
```

Fedora:
```bash
sudo dnf install gcc-c++ cmake json-c-devel libmicrohttpd-devel gtest-devel
```

Build:
```bash
$ cmake .
$ make -j 4
```

## Run the Unit Tests

```bash
$ ./unit_tests
```

## Usage
