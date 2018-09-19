include ../../var.mk
PL_NAME=wvnc
PLUGINS=$(PL_NAME).$(EXT)
APP_DIR=$(BUILDIRD)/htdocs/

OBJS = 		$(PLUGINS_BASE)/plugin.o 

PLUGINSDEP = 	$(OBJS) \
				wvnc.o
		
PLUGINLIBS = libantd.$(EXT) -lvncclient  -lpthread -lz# -lsqlite3 
				
PCFLAGS=-W -Wall -g -D DEBUG  $(PPF_FLAG) -D USE_ZLIB
 
main:  $(PLUGINSDEP)  $(PLUGINS)  #lib

%.o: %.c
		$(CC) $(PCFLAGS) -fPIC $(INCFLAG) -c  $< -o $@

%.$(EXT):
		-ln -s $(PBUILDIRD)/libantd.$(EXT) .
		$(CC) $(PCFLAGS) $(PLUGINSDEP) $(PLUGINLIBS) -shared -o $(PBUILDIRD)/$(basename $@).$(EXT) 


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



