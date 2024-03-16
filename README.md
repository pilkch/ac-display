## About

1. This is just a proof of concept to show an RPM gauge and some stats from Assetto Corsa, on a second monitor, another computer, a phone, or tablet. The idea would be that the other device sits above your wheel and then you can turn off the in game dash or change the position of the camera to show less dash and more road.
2. I am not a web designer. I like minimal web pages, but I am no web expert, the web interface is usable but ugly and janky, I know just enough to be dangerous.
3. Getting this up and running is a bit of a pain and you'll need to know a little bit about networking, good luck!


![RPM and Speedometer Gauges](/images/latest_rpm_preview.png)

![F1 Style Dash](/images/latest_f1_style_preview.png)


## Third Party Libraries and Resources

- [libjson-c](https://github.com/json-c/json-c)  
- [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)  
- [acudp](https://github.com/vpicon/acudp)  
- [pyproxy](https://github.com/rsc-dev/pyproxy/)
- [Javascript RPM gauge](https://geeksretreat.wordpress.com/2012/04/13/making-a-speedometer-using-html5s-canvas/)
- [Disconnection icon](https://www.svgrepo.com/svg/332225/api)
- [Fullscreen icon by Automattic, SVG Repo, GPL](https://www.svgrepo.com/svg/335073/fullscreen)
- [Gauge icon by romzicon, Noun Project, CC BY 3.0](https://thenounproject.com/browse/icons/term/gauge/)

## Build

Install dependencies:

Ubuntu:
```bash
sudo apt install automake autoconf libtool texinfo gcc-c++ cmake json-c-dev libxml2-dev gtest-dev
```

Fedora:
```bash
sudo dnf install automake autoconf libtool texinfo gcc-c++ cmake json-c-devel libxml2-devel gtest-devel
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


## IP Addresses

| Machine        | OS           | IP Address  |
| ------------- | ------------- | ------------- |
| Assetto Corsa | Windows | 192.168.0.2 |
| ac-display | Linux | 192.168.0.3 |
| Monitor or Device | Web Browser (Second monitor/computer/phone/tablet) | 192.168.0.x |

## Data Flow

```mermaid
flowchart TD
    AC(Windows Assetto Corsa acs.exe UDP Server 127.0.0.1:9996) --> PY(Windows pyproxy-forward-ac-udp-to-linux.py 192.168.0.2:9997)
    PY --> AC
    PY --> DISP(Linux ac-display https://192.168.0.3:8443/)
    DISP --> PY
    DISP <--> |Visits https://192.168.0.3:8443/| PHONE(Phone web browser)
    PHONE <--> |websocket| DISP
```

## Usage

### On Assetto Corsa Machine (Windows)

On the computer running :
1. Open file `C:\Program Files (x86)\Steam\steamapps\common\assettocorsa\system\cfg\assetto_corsa.ini` with notepad.
2. Find line `ENABLE_DEV_APPS=0` and change it to `ENABLE_DEV_APPS=1`
3. Save the file
4. Open UDP port 9996 on the Windows firewall, or change the network profile to Private
5. Forward the AC UDP traffic to the rest of the network, in a git-bash, VSCode terminal, or similar (Change the IP and port):
```bash
python .\pyproxy-forward-ac-udp-to-linux.py -d 192.168.0.2:9997 -v
```

### On ac-display Machine (Linux)

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

### On a Monitor or Device on the Same Network (Second monitor/computer/phone/tablet)

1. Go to the address in a browser (Replace the address and port):  
`https://192.168.0.3:7080/`
2. If you are seeing a "Disconnected" message on the page then press F12 and click on "Console" to check if there are any useful error messages


<details>
<summary>Development History in Images</summary>

![](/images/1.png)

![](/images/2.png)

![](/images/3.png)

![](/images/4.png)

![](/images/5.png)

![](/images/6.png)

![](/images/7.png)

![](/images/8.png)

![](/images/9.png)

![](/images/10.png)

![](/images/11.png)

![](/images/12.png)

![](/images/13.png)

![](/images/14.png)

![](/images/15.png)

![](/images/16.png)

</details>
