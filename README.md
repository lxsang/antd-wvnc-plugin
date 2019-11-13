# antd-wvnc-plugin

Overview about wvnc: [https://blog.lxsang.me/post/id/23](https://blog.lxsang.me/post/id/23)

An [Antd HTTP/HTTPS server's](https://github.com/lxsang/ant-http) plugin that acts as a bridge between a VNC server and web applications. It allows web application to communicate with VNC server using web socket via a predefined protocol and message format. Web application can use my dedicate javascript library called [**wvnc.js**](https://github.com/lxsang/wvnc.js) to communicate with the VNC server using the plugin.

To speed up the data transmission, **WVNC** uses **libjpeg** and **zlib** for data compression.

## Build from source
As **WVNC** is an **Antd's** plugin, the server need to be pre-installed:

### build dep
* git
* make
* build-essential


### Plugin Dependencies
* Zlib
* libjpeg-turbo
* libvncserver-dev

### build
When all dependencies are installed, the build can be done with a few single command lines:

```bash
# replace x.x.x by a version number
wget -O- https://get.bitdojo.dev/antd_plugin | bash -s "wvnc-x.x.x"

# or from the distribution tarball
tar xvzf wvnc-x.x.x.tar.gz
cd wvnc-x.x.x
./configure --prefix=/opt/www --enable-debug=yes
make
sudo make install

```

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

### Generate distribution
```sh
libtoolize
aclocal
autoconf
automake --add-missing
make distcheck
``` 