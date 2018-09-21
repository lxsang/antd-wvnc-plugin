#! /bin/bash
# Make sure the emsdk is installed and worked
echo "Preparing libjpeg..."
if [ -d "libjpeg" ]; then
    rm -r libjpeg
fi
if [ -d "zlib" ]; then
    rm -r zlib
fi
wget http://www.ijg.org/files/jpegsrc.v9c.tar.gz
mkdir libjpeg
tar xvzf jpegsrc.v9c.tar.gz -C ./libjpeg --strip-components=1
rm jpegsrc.v9c.tar.gz
cd libjpeg
emconfigure ./configure
emmake make
## Using libjpeg-turbo
#git clone https://github.com/libjpeg-turbo/libjpeg-turbo
#mv libjpeg-turbo libjpeg
#cd libjpeg
#mkdir build
#cd build
#emcmake cmake ../
#emmake make
cd ../
echo "Preparinf Zlib..."
wget https://zlib.net/zlib-1.2.11.tar.gz
mkdir zlib
tar xvzf zlib-1.2.11.tar.gz -C ./zlib --strip-components=1
rm zlib-1.2.11.tar.gz
cd zlib
emconfigure ./configure
# TODO modify make file using sed if macos
emmake make