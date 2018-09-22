# antd-wvnc-plugin

An [Antd HTTP/HTTPS server's](https://github.com/lxsang/ant-http) plugin that acts as a bridge between a VNC server and web applications. It allows web application to communicate with VNC server using web socket via a predefined protocol and message format. Web application can use my dedicate javascript library called [**wvnc.js**](https://github.com/lxsang/wvnc.js) to communicate with the VNC server using the plugin.

To speed up the data transmission, **WVNC** uses **libjpeg** and **zlib** for data compression.

## Demo
A demo will soon be available

## Build from source
As **WVNC** is an **Antd's** plugin, it need to be built along with the server. This require the following application/libraries to be pre installed:

### build dep
* git
* make
* build-essential

### server dependencies
* libssl-dev
* libsqlite3-dev

### Plugin Dependencies
* Zlib
* libjpeg-turbo
* libvncserver-dev

### build
When all dependencies are installed, the build can be done with a few single command lines:

```bash
mkdir antd
cd antd
wget -O - https://apps.lxsang.me/script/antd | bash -s "wvnc"
```
The script will ask for a place to put the binaries (should be an absolute path, otherwise the build will fail) and the default HTTP port for the server config.

## Run
To run the Antd server with the **wvnc** plugin:
```sh
/path/to/your/build/antd
```

Web applications can be put on **/path/to/your/build/htdocs**, the web socket to **wvnc** is available at:
```
ws://your_host:your_port/wvnc
```
This websocket address can be used with my client side javascript library [**wvnc.js**](https://github.com/lxsang/wvnc.js) to provide web based VNC client 
