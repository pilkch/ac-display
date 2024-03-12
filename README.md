## About



## Data Flow

## Third Party Libraries

- [libjson-c](https://github.com/json-c/json-c)  
- [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)  
- [acudp](https://github.com/vpicon/acudp)  
- [pyproxy](https://github.com/rsc-dev/pyproxy/)

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

## Validate Static HTML

```bash
$ xmllint --noout resources/index.html # Should be no output
$ echo $? # Should print 0
```


## Usage

### On Windows

1. Forward the AC UDP traffic to the rest of the network, in a git-bash, VSCode terminal, or similar (Change the IP and port):
```bash
python .\pyproxy-forward-ac-udp-to-linux.py -d 192.168.0.2:9997 -v
```

### On Linux

1. Optionally generate self signed certificates for TLS:
```bash
openssl genrsa -out server.key 2048
openssl rsa -in server.key -out server.key
openssl req -sha256 -new -key server.key -out server.csr -subj '/CN=localhost'
openssl x509 -req -sha256 -days 365 -in server.csr -signkey server.key -out server.crt
```
2. Set up a configuration.json file by copying the example and editing it (Set your source and destination addresses and ports, optionally set the the server.key and server.crt):
```bash
cp configuration.json.example configuration.json
vi configuration.json
```
3. Open the port in firewalld (Replace 7080 with your port):
```bash
sudo firewall-cmd --permanent --add-port=7080/udp
sudo firewall-cmd --reload
```
4. Run ac-display:
```bash
./ac-display
```
5. On any machine go to the HTTP address in a browser (Replace the address and port):  
`https://192.168.0.49:7080/`


