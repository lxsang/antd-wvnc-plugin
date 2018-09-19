/**
 * @example SDLvncviewer.c
 */

#include <SDL2/SDL.h>
#include <signal.h>
#include <rfb/rfbclient.h>

static int enableResizable = 1, viewOnly, listenLoop, buttonMask;
int sdlFlags;
SDL_Texture *sdlTexture;
SDL_Renderer *sdlRenderer;
SDL_Window *sdlWindow;


static rfbBool resize(rfbClient* client) {
	int width=client->width,height=client->height,
		depth=client->format.bitsPerPixel;

	if (enableResizable)
		sdlFlags |= SDL_WINDOW_RESIZABLE;

	client->updateRect.x = client->updateRect.y = 0;
	client->updateRect.w = width; client->updateRect.h = height;

	/* (re)create the surface used as the client's framebuffer */
	SDL_FreeSurface(rfbClientGetClientData(client, SDL_Init));
	SDL_Surface* sdl=SDL_CreateRGBSurface(0,
					      width,
					      height,
					      depth,
					      0,0,0,0);
	if(!sdl)
	    rfbClientErr("resize: error creating surface: %s\n", SDL_GetError());

	rfbClientSetClientData(client, SDL_Init, sdl);
	client->width = sdl->pitch / (depth / 8);
	client->frameBuffer=sdl->pixels;

	client->format.bitsPerPixel=depth;
	client->format.redShift=sdl->format->Rshift;
	client->format.greenShift=sdl->format->Gshift;
	client->format.blueShift=sdl->format->Bshift;
	client->format.redMax=sdl->format->Rmask>>client->format.redShift;
	client->format.greenMax=sdl->format->Gmask>>client->format.greenShift;
	client->format.blueMax=sdl->format->Bmask>>client->format.blueShift;
	SetFormatAndEncodings(client);

	/* create or resize the window */
	if(!sdlWindow) {
	    sdlWindow = SDL_CreateWindow(client->desktopName,
					 SDL_WINDOWPOS_UNDEFINED,
					 SDL_WINDOWPOS_UNDEFINED,
					 width,
					 height,
					 sdlFlags);
	    if(!sdlWindow)
		rfbClientErr("resize: error creating window: %s\n", SDL_GetError());
	} else {
	    SDL_SetWindowSize(sdlWindow, width, height);
	}

	/* create the renderer if it does not already exist */
	if(!sdlRenderer) {
	    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
	    if(!sdlRenderer)
		rfbClientErr("resize: error creating renderer: %s\n", SDL_GetError());
	    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");  /* make the scaled rendering look smoother. */
	}
	SDL_RenderSetLogicalSize(sdlRenderer, width, height);  /* this is a departure from the SDL1.2-based version, but more in the sense of a VNC viewer in keeeping aspect ratio */

	/* (re)create the texture that sits in between the surface->pixels and the renderer */
	if(sdlTexture)
	    SDL_DestroyTexture(sdlTexture);
	sdlTexture = SDL_CreateTexture(sdlRenderer,
				       SDL_PIXELFORMAT_ARGB8888,
				       SDL_TEXTUREACCESS_STREAMING,
				       width, height);
	if(!sdlTexture)
	    rfbClientErr("resize: error creating texture: %s\n", SDL_GetError());
	return TRUE;
}

static void update(rfbClient* cl,int x,int y,int w,int h) {
	SDL_Surface *sdl = rfbClientGetClientData(cl, SDL_Init);
	/* update texture from surface->pixels */
	SDL_Rect r = {x,y,w,h};
 	if(SDL_UpdateTexture(sdlTexture, &r, sdl->pixels + y*sdl->pitch + x*4, sdl->pitch) < 0)
	    rfbClientErr("update: failed to update texture: %s\n", SDL_GetError());
	/* copy texture to renderer and show */
	if(SDL_RenderClear(sdlRenderer) < 0)
	    rfbClientErr("update: failed to clear renderer: %s\n", SDL_GetError());
	if(SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL) < 0)
	    rfbClientErr("update: failed to copy texture to renderer: %s\n", SDL_GetError());
	SDL_RenderPresent(sdlRenderer);
}


static void cleanup(rfbClient* cl)
{
  /*
    just in case we're running in listenLoop:
    close viewer window by restarting SDL video subsystem
  */
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  if(cl)
    rfbClientCleanup(cl);
}


static rfbBool handleSDLEvent(rfbClient *cl, SDL_Event *e)
{
	switch(e->type) {
	case SDL_QUIT:
                if(listenLoop)
		  {
		    cleanup(cl);
		    return FALSE;
		  }
		else
		  {
		    rfbClientCleanup(cl);
		    exit(0);
		  }
	default:
		rfbClientLog("ignore SDL event: 0x%x\n", e->type);
	}
	return TRUE;
}
/*
static void got_selection(rfbClient *cl, const char *text, int len)
{
        rfbClientLog("received clipboard text '%s'\n", text);
        if(SDL_SetClipboardText(text) != 0)
	    rfbClientErr("could not set received clipboard text: %s\n", SDL_GetError());
}
*/


static rfbCredential* get_credential(rfbClient* cl, int credentialType){
        rfbCredential *c = malloc(sizeof(rfbCredential));
	c->userCredential.username = malloc(RFB_BUF_SIZE);
	c->userCredential.password = malloc(RFB_BUF_SIZE);

	if(credentialType != rfbCredentialTypeUser) {
	    rfbClientErr("something else than username and password required for authentication\n");
	    return NULL;
	}

	rfbClientLog("username and password required for authentication!\n");
	printf("user: ");
	fgets(c->userCredential.username, RFB_BUF_SIZE, stdin);
	printf("pass: ");
	fgets(c->userCredential.password, RFB_BUF_SIZE, stdin);

	/* remove trailing newlines */
	c->userCredential.username[strcspn(c->userCredential.username, "\n")] = 0;
	c->userCredential.password[strcspn(c->userCredential.password, "\n")] = 0;

	return c;
}


int main(int argc,char** argv) {
	rfbClient* cl;
	int i, j;
	SDL_Event e;

	for (i = 1, j = 1; i < argc; i++)
		if (!strcmp(argv[i], "-viewonly"))
			viewOnly = 1;
		else if (!strcmp(argv[i], "-resizable"))
			enableResizable = 1;
		else if (!strcmp(argv[i], "-no-resizable"))
			enableResizable = 0;
		else if (!strcmp(argv[i], "-listen")) {
		        listenLoop = 1;
			argv[i] = "-listennofork";
                        ++j;
		}
		else {
			if (i != j)
				argv[j] = argv[i];
			j++;
		}
	argc = j;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
	atexit(SDL_Quit);
	signal(SIGINT, exit);

	do {
	  /* 16-bit: cl=rfbGetClient(5,3,2); */
	  cl=rfbGetClient(8,3,4);
	  cl->MallocFrameBuffer=resize;
	  cl->canHandleNewFBSize = TRUE;
	  cl->GotFrameBufferUpdate=update;
	  //cl->HandleKeyboardLedState=kbd_leds;
	  //cl->HandleTextChat=text_chat;
	  //cl->GotXCutText = got_selection;
	  cl->GetCredential = get_credential;
	  cl->listenPort = LISTEN_PORT_OFFSET;
	  cl->listen6Port = LISTEN_PORT_OFFSET;
	  if(!rfbInitClient(cl,&argc,argv))
	    {
	      cl = NULL; /* rfbInitClient has already freed the client struct */
	      cleanup(cl);
	      break;
	    }

	  while(1) {
	    if(SDL_PollEvent(&e)) {
	      /*
		handleSDLEvent() return 0 if user requested window close.
		In this case, handleSDLEvent() will have called cleanup().
	      */
	      if(!handleSDLEvent(cl, &e))
		break;
	    }
	    else {
	      i=WaitForMessage(cl,500);
	      if(i<0)
		{
		  cleanup(cl);
		  break;
		}
	      if(i)
		if(!HandleRFBServerMessage(cl))
		  {
		    cleanup(cl);
		    break;
		  }
	    }
	  }
	}
	while(listenLoop);

	return 0;
}


