LDFLAGS="-L/usr/local/opt/jpeg-turbo/lib"
CPPFLAGS="-I/usr/local/opt/jpeg-turbo/include"
main: miniviewer
	gcc viewer.c -D_REENTRANT $(CPPFLAGS) -I/usr/include/SDL2 $(LDFLAGS) -lSDL2 -lvncclient -o viewer

miniviewer:
	gcc miniviewer.c -D_REENTRANT $(CPPFLAGS) -I/usr/include/SDL2 $(LDFLAGS) -lSDL2 -lvncclient -o viewer