include ../../var.mk
PL_NAME=wvnc
PLUGINS=$(PL_NAME).$(EXT)
APP_DIR=$(BUILDIRD)/htdocs/

OBJS = 		$(PLUGINS_BASE)/plugin.o 

PLUGINSDEP = 	$(OBJS) \
				wvnc.o

DEP = 
WEB_BUILD_PATH = /home/mrsang/myws/antd-web-apps/apps/assets/scripts
ifeq ($(UNAME_S),Darwin)
	DEP= -I/usr/local/opt/jpeg-turbo/include -L/usr/local/opt/jpeg-turbo/lib
	WEB_BUILD_PATH= /Users/mrsang/Documents/workspace/antd-web-apps/apps/assets/scripts
endif
		
PLUGINLIBS = libantd.$(EXT) -lvncclient  -lpthread -lz -ljpeg# -lsqlite3 
				
PCFLAGS=-W -Wall -g -D DEBUG  $(PPF_FLAG) $(DEP) -D USE_JPEG -D USE_ZLIB
 
main:  $(PLUGINSDEP)  $(PLUGINS)  #lib

%.o: %.c
		$(CC) $(PCFLAGS) -fPIC $(INCFLAG) -c  $< -o $@

%.$(EXT):
		-ln -s $(PBUILDIRD)/libantd.$(EXT) .
		$(CC) $(PCFLAGS) $(PLUGINSDEP) $(PLUGINLIBS) -shared -o $(PBUILDIRD)/$(basename $@).$(EXT) 

web:
	emcc -o $(WEB_BUILD_PATH)/wvnc_asm.js -I wasm/libjpeg/  -I wasm/zlib wasm/decoder.c \
	wasm/libjpeg/.libs/libjpeg.a wasm/zlib/libz.a \
	-O3 -s ALLOW_MEMORY_GROWTH=1  -s WASM=1 -s NO_EXIT_RUNTIME=1 -s \
	'EXTRA_EXPORTED_RUNTIME_METHODS=["cwrap"]' 


clean: #libclean
		-rm -f *.o  *.$(EXT) $(PBUILDIRD)/$(PLUGINS) 
		-rm $(PLUGINS_BASE)/plugin.o

libclean:
	for file in lib/* ;do \
		if [ -d "$$file" ]; then \
			echo "Cleaning $$file" ;\
			make -C  "$$file" clean; \
		fi \
	done
		
	


.PRECIOUS: %.o
.PHONY: lib clean
full: clean main



