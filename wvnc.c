#include <stdlib.h>
#include <string.h>
#include <rfb/rfbclient.h>
#include <pthread.h>
#ifdef USE_ZLIB
#include <zlib.h>
#endif
#include "plugin.h"
#define get_user_data(x) ((wvnc_user_data_t *)x)
#define R_SHIFT(x) (0)
#define G_SHIFT(x) ((x >= 24) ? 8 :)
/*
Vnc to web socket using the
libvncserver/libvncclient
*/
typedef struct
{
    uint8_t cmd;
    uint16_t size;
    uint8_t *data;

} wvnc_cmd_t;
typedef struct
{
    void *client;
    int status;
    void *vncl;
} wvnc_user_data_t;

typedef struct
{
    uint8_t r_shift;
    uint8_t g_shift;
    uint8_t b_shift;
    uint8_t r_max;
    uint8_t g_max;
    uint8_t b_max;
} wvnc_pixel_format_t;

void *vnc_fatal(void *client, const char *msg);
void *identify();
void *process(void *cl, int wait);
static rfbBool resize(rfbClient *client);
void open_session(void *client, const char *addr);
void *consume_client(void *cl, wvnc_cmd_t header);
static rfbCredential *get_credential(rfbClient *cl, int credentialType);
static void update(rfbClient *cl, int x, int y, int w, int h);

#ifdef USE_ZLIB
int buff_compress(uint8_t* src, uint8_t* dest, int len)
{
    z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;
    // setup "a" as the input and "b" as the compressed output
    defstream.avail_in = (uInt)len; // size of input
    defstream.next_in = (Bytef *)src; // input char array
    defstream.avail_out = (uInt)len; // size of output
    defstream.next_out = (Bytef *)dest; // output char array
    
    // the actual compression work.
    deflateInit(&defstream, Z_BEST_COMPRESSION);
    deflate(&defstream, Z_FINISH);
    deflateEnd(&defstream);
    return defstream.total_out;
}
#endif
int get_pixel_format(uint8_t deep, wvnc_pixel_format_t *d)
{
    switch (deep)
    {
    case 32:
    case 24:
        d->r_shift = 0;
        d->g_shift = 8;
        d->b_shift = 16;
        d->r_max = d->b_max = d->g_max = 255;
        return 1;
        break;
    case 16:
        // RGB 565 format
        d->r_shift = 0;
        d->g_shift = 5;
        d->b_shift = 11;
        d->r_max = 31;
        d->b_max = 31;
        d->g_max = 63;
        return 1;
    default:
        break;
    }
    return 0;
}

void pexit()
{
}

void *identify()
{
    void *ptr = (void *)pthread_self();
    //printf("stackaddr = %p\n", ptr);
    return ptr;
}

void *process(void *data, int wait)
{
    wvnc_user_data_t *user_data = get_user_data(data);
    uint8_t *buff = NULL;
    ws_msg_header_t *h = NULL;
    while (!(h = ws_read_header(user_data->client)) && user_data->status && wait)
        ;
    if (h)
    {
        if (h->mask == 0)
        {
            LOG("%s\n", "data is not masked");
            ws_close(user_data->client, 1012);
            user_data->status = 0;
            free(h);
            return NULL;
            //break;
        }
        if (h->opcode == WS_CLOSE)
        {
            LOG("%s\n", "Websocket: connection closed");
            ws_close(user_data->client, 1011);
            user_data->status = 0;
            free(h);
            return 0;
            //break;
        }
        else if (h->opcode == WS_BIN)
        {
            int l;
            buff = (uint8_t *)malloc(h->plen + 1);
            if (!buff)
            {
                free(h);
                return vnc_fatal(user_data, "Cannot alloc memory for the command");
            }
            // read the command from the client
            int len = h->plen;
            if ((l = ws_read_data(user_data->client, h, h->plen, buff)) > 0)
            {

                // process client command
                wvnc_cmd_t header;
                header.cmd = buff[0];
                buff[len] = '\0';
                header.data = (buff + 3);
                memcpy(&header.size, buff + 1, 2);
                void *st = consume_client(user_data, header);
                if (buff)
                    free(buff);
                free(h);
                return st;
            }
            else
            {
                vnc_fatal(user_data, "Invalid request");
                if (buff)
                    free(buff);
                free(h);
                return 0;
                //break;
            }
            //buff = NULL;
        }
        else
        {
            vnc_fatal(user_data, "Unknow opcode");
            free(h);
            return 0;
        }
    }
    return 0;
}

static rfbBool resize(rfbClient *client)
{
    int width = client->width;
    int height = client->height;
    int depth = client->format.bitsPerPixel;
    LOG("width %d, height %d, depth %d\n", width, height, depth);
    client->updateRect.x = client->updateRect.y = 0;
    client->updateRect.w = width;
    client->updateRect.h = height;
    //depth = 32;
    void *data = rfbClientGetClientData(client, identify());
    wvnc_user_data_t *user_data = get_user_data(data);
    //client->width = sdl->pitch / (depth / 8);
    if (client->frameBuffer)
        free(client->frameBuffer);
    client->frameBuffer = (uint8_t *)malloc(width * height * depth / 8);
    wvnc_pixel_format_t pxf;
    if (!get_pixel_format(depth, &pxf))
    {
        vnc_fatal(user_data, "Cannot get pixel format");
        return FALSE;
    }
    client->format.bitsPerPixel = depth;
    client->format.redShift = pxf.r_shift;
    client->format.greenShift = pxf.g_shift;
    client->format.blueShift = pxf.b_shift;
    client->format.redMax = pxf.r_max;
    client->format.greenMax = pxf.g_max;
    client->format.blueMax = pxf.b_max;
    SetFormatAndEncodings(client);

    /* create or resize the window */
    // send data to client
    uint8_t cmd[6];
    cmd[0] = 0x83; // resize command
    cmd[1] = (uint8_t)(width & 0xFF);
    cmd[2] = (uint8_t)(width >> 8);
    cmd[3] = (uint8_t)(height & 0xFF);
    cmd[4] = (uint8_t)(height >> 8);
    cmd[5] = (uint8_t)depth;
    ws_b(user_data->client, cmd, 6);
    uint8_t *ack = (uint8_t *)process(user_data, 1);
    if (!ack || !(*ack))
    {
        LOG("Client fail to resize\n");
        if (ack)
            free(ack);
        return FALSE;
    }
    free(ack);
    return TRUE;
}

static void update(rfbClient *client, int x, int y, int w, int h)
{
    wvnc_user_data_t *user_data = get_user_data(rfbClientGetClientData(client, identify()));
    uint8_t bbp = (uint8_t)client->format.bitsPerPixel / 8;
    int size = w * h * bbp;
    uint8_t *cmd = (uint8_t *)malloc(size + 10); // + 9
#ifdef USE_ZLIB
    uint8_t* buff = (uint8_t *)malloc(size);
    if (!buff)
    {
        vnc_fatal(user_data, "Cannot allocate data for update");
        return;
    }
#endif
    if (!cmd)
    {
        vnc_fatal(user_data, "Cannot allocate data for update");
        return;
    }
    if (!client->frameBuffer)
    {
        LOG("Client frame buffe data not found\n");
        return;
    }
#ifdef USE_ZLIB
    uint8_t *dest_ptr = buff; 
#else
    uint8_t *dest_ptr = cmd + 10; 
#endif
    uint8_t *src_ptr;

    //cpy line by line
    int cw = client->width;
    for(int j = y; j < y+ h; j++)
    {
        src_ptr = client->frameBuffer + ( j*cw*bbp + x*bbp);
        memcpy(dest_ptr, src_ptr, w*bbp);
        if(bbp == 4)
            for(int i= bbp -1; i < w*bbp; i+=bbp)
                dest_ptr[i] = 255;
        dest_ptr += w*bbp;
    }
    cmd[0] = 0x84; //update command
    cmd[1] = (uint8_t)(x & 0xFF);
    cmd[2] = (uint8_t)(x >> 8);
    cmd[3] = (uint8_t)(y & 0xFF);
    cmd[4] = (uint8_t)(y >> 8);
    cmd[5] = (uint8_t)(w & 0xFF); 
    cmd[6] = (uint8_t)(w >> 8);
    cmd[7] = (uint8_t)(h & 0xFF);
    cmd[8] = (uint8_t)(h >> 8);
    cmd[9] = 0x0;
#ifdef USE_ZLIB
    cmd[9] = 0x1;
    size = buff_compress(buff, cmd+10, size);
#endif
    ws_b(user_data->client, cmd, size + 10);
    free(cmd);
#ifdef USE_ZLIB
    free(buff);
#endif
}

static rfbCredential *get_credential(rfbClient *cl, int credentialType)
{
    wvnc_user_data_t *user_data = get_user_data(rfbClientGetClientData(cl, identify()));
    rfbCredential *c = malloc(sizeof(rfbCredential));
    c->userCredential.username = malloc(RFB_BUF_SIZE);
    c->userCredential.password = malloc(RFB_BUF_SIZE);

    if (credentialType != rfbCredentialTypeUser)
    {
        vnc_fatal(user_data, "something else than username and password required for authentication\n");
        return NULL;
    }
    uint8_t cmd[1];
    cmd[0] = 0x82;
    ws_b(user_data->client, cmd, 1);
    char *up = (char *)process(user_data, 1);
    if (!up)
    {
        if (c)
        {
            free(c->userCredential.username);
            free(c->userCredential.password);
            free(c);
            return vnc_fatal(user_data, "Cannot get user credential");
        }
    }
    char *pass = up;
    while (*pass != '\0')
        pass++;
    pass++;
    LOG("User name %s, pass: %s\n", up, pass);
    memcpy(c->userCredential.username, up, strlen(up) + 1);
    memcpy(c->userCredential.password, pass, strlen(pass) + 1);
    free(up);
    // remove trailing newlines
    //c->userCredential.username[strcspn(c->userCredential.username, "\n")] = 0;
    //c->userCredential.password[strcspn(c->userCredential.password, "\n")] = 0;

    return c;
}

static char *get_password(rfbClient *client)
{
    uint8_t cmd[1];
    void *data = rfbClientGetClientData(client, identify());
    wvnc_user_data_t *user_data = get_user_data(data);
    cmd[0] = 0x81; // resize command
    ws_b(user_data->client, cmd, 1);

    // call process to get the password
    char *pwd = (char *)process(user_data, 1);
    //not free
    if (!pwd)
    {
        vnc_fatal(user_data, "Cannot read user password");
        return NULL;
    }
    LOG("Password is '%s'\n", pwd);
    return pwd;
}

void open_session(void *data, const char *addr)
{
    // main loop
    int argc = 2;
    char *argv[2];
    argv[0] = "-listennofork";
    argv[1] = (char *)addr;
    wvnc_user_data_t *user_data = get_user_data(data);
    if (!rfbInitClient(user_data->vncl, &argc, argv))
    {
        user_data->vncl = NULL; /* rfbInitClient has already freed the client struct */
        //cleanup(vncl);
        vnc_fatal(user_data, "Cannot connect to the server");
        return;
    }
    while (1)
    {
        if (!user_data->status)
        {
            if (user_data->vncl)
                rfbClientCleanup(user_data->vncl);
            break;
        }
        process(user_data, 0);
        //LOG("ENd process \n");
        int status = WaitForMessage(user_data->vncl, 500);//500
        if (status < 0)
        {
            if (user_data->vncl)
                rfbClientCleanup(user_data->vncl);
            break;
        }
        if (status)
        {
            if (!HandleRFBServerMessage(user_data->vncl))
            {
                if (user_data->vncl)
                    rfbClientCleanup(user_data->vncl);
                break;
            }
        }
    }
    return;
}

void *vnc_fatal(void *data, const char *msg)
{
    // print the message then close the
    // connection
    wvnc_user_data_t *user_data = get_user_data(data);

    int len, size;
    len = strlen(msg);
    size = len + 1;
    LOG("%s\n", msg);
    uint8_t *cmd = (uint8_t *)malloc(size);
    cmd[0] = 0xFE; // error opcode
    user_data->status = 0;
    if (cmd)
    {
        memcpy(cmd + 1, (uint8_t *)msg, len);
        ws_b(user_data->client, cmd, size);
        free(cmd);
    }
    // quit the socket
    ws_close(user_data->client, 1011);
    return 0;
}

void *consume_client(void *ptr, wvnc_cmd_t header)
{
    uint8_t cmd = header.cmd;
    uint16_t size = header.size;
    wvnc_user_data_t *user_data = get_user_data(ptr);
    //LOG("Data size is %d\n", size);
    uint8_t *data;
    // in case of string
    switch (cmd)
    {
    case 0x01: /*client open a connection*/
        open_session(user_data, (char *)header.data);
        break;
    case 0x02: //client enter a vnc password
        if (!header.data)
            return NULL;
        return strdup((char *)header.data);
    case 0x03: // client enter a credential
        data = (uint8_t*)malloc(size);
        memcpy(data, header.data, size);
        return data;
        break;
    case 0x04: // ack from client
        data = (uint8_t *)malloc(1);
        *data = (uint8_t)header.data[0];
        return data;
        break;
    case 0x05: //mouse event
        SendPointerEvent(user_data->vncl, header.data[0]|(header.data[1]<<8) , header.data[2]|(header.data[3]<<8), header.data[4]);
        break;
    default:
        return vnc_fatal(user_data, "Unknown client command");
    }
    return NULL;
}

void handle(void *cl, const char *m, const char *rqp, dictionary rq)
{
    if (ws_enable(rq))
    {
#ifdef USE_ZLIB
            LOG("Zlib is enabled\n");
#endif
        //set time out for the tcp socket
        // set timeout to socket
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500;

        if (setsockopt(((antd_client_t *)cl)->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
            perror("setsockopt failed\n");

        //if (setsockopt (((antd_client_t*)cl)->sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(timeout)) < 0)
        //	perror("setsockopt failed\n");

        //rfbClient* vncl;
        rfbClient *vncl = NULL;
        vncl = rfbGetClient(8, 3, 4);
        vncl->MallocFrameBuffer = resize;
        vncl->canHandleNewFBSize = TRUE;
        vncl->GotFrameBufferUpdate = update;
        vncl->GetPassword = get_password;
        //cl->HandleKeyboardLedState=kbd_leds;
        //cl->HandleTextChat=text_chat;
        //cl->GotXCutText = got_selection;
        vncl->GetCredential = get_credential;
        vncl->listenPort = LISTEN_PORT_OFFSET;
        vncl->listen6Port = LISTEN_PORT_OFFSET;
        wvnc_user_data_t *user_data = (wvnc_user_data_t *)malloc(sizeof(wvnc_user_data_t));
        user_data->client = cl;
        user_data->status = 1;
        user_data->vncl = vncl;
        rfbClientSetClientData(vncl, identify(), user_data);
        //while(1)
        //{
        process(user_data, 1);
        //}
    }
    LOG("%s\n", "EXIT Streaming..");
}