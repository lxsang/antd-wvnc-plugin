#define PLUGIN_IMPLEMENT 1
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rfb/rfbclient.h>
#include <pthread.h>
#include <jpeglib.h>
#include <antd/plugin.h>
#include <antd/scheduler.h>
#include <antd/ws.h>
#include <sys/time.h>

#define get_user_data(x) ((wvnc_user_data_t *)x)
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

typedef enum
{
    DISCONNECTED,
    READY,
    CONNECTED
} wvnc_connect_t;

typedef struct
{
    antd_request_t *wscl;
    wvnc_connect_t status;
    rfbClient *vncl;
    uint8_t quality;
    long long last_update;
    uint8_t bbp;
    uint16_t ux;
    uint16_t uy;
    uint16_t uw;
    uint16_t uh;
    //int rate;
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
void *process(void *cl, int wait);
static rfbBool resize(rfbClient *client);
void open_session(void *client, const char *addr);
void *consume_client(void *cl, wvnc_cmd_t header);
static rfbCredential *get_credential(rfbClient *cl, int credentialType);
static void update(rfbClient *cl, int x, int y, int w, int h);
static void finish_update(rfbClient *cl);

static long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; 
    return milliseconds;
}

int jpeg_compress(uint8_t *buff, int w, int h, int bytes, int quality)
{
    uint8_t *tmp = buff;
    uint8_t *tmp_row = NULL;
    struct jpeg_compress_struct cinfo = {0};
    struct jpeg_error_mgr jerror = {0};
    jerror.trace_level = 10;
    cinfo.err = jpeg_std_error(&jerror);
    jerror.trace_level = 10;
    cinfo.err->trace_level = 10;
    jpeg_create_compress(&cinfo);
    uint8_t *out = NULL;
    unsigned long outbuffer_size = 0;
    jpeg_mem_dest(&cinfo, &out, &outbuffer_size);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = bytes == 4 ? 4 : 3;
    switch (bytes)
    {
    case 2:
        cinfo.in_color_space = JCS_EXT_RGB;
        break;
    case 4:
        cinfo.in_color_space = JCS_EXT_RGBA;
        break;
    default:
        return 0;
    }
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, true);
    jpeg_start_compress(&cinfo, true);
    //unsigned counter = 0;
    JSAMPROW row_pointer[1];
    row_pointer[0] = NULL;
    uint8_t *offset;
    uint16_t value;
    while (cinfo.next_scanline < cinfo.image_height)
    {
        if (bytes == 2)
        {
            tmp_row = (uint8_t *)malloc(w * cinfo.input_components);

            for (size_t i = 0; i < (size_t)w; i++)
            {
                offset = tmp + cinfo.next_scanline * w * bytes + i * bytes;
                value = offset[0] | (offset[1] << 8);
                tmp_row[i * cinfo.input_components] = (value & 0x1F) * (255 / 31);
                tmp_row[i * cinfo.input_components + 1] = ((value >> 5) & 0x3F) * (255 / 63);
                tmp_row[i * cinfo.input_components + 2] = ((value >> 11) & 0x1F) * (255 / 31);
            }
            row_pointer[0] = (JSAMPROW)tmp_row;
        }
        else
        {
            row_pointer[0] = (JSAMPROW)(&tmp[cinfo.next_scanline * w * bytes]);
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
        if (tmp_row)
        {
            free(tmp_row);
            tmp_row = NULL;
        }
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    //LOG("before %d after %d\n",  w*h*bbp, );
    if (outbuffer_size < (unsigned long)(w * h * bytes))
    {
        memcpy(buff, out, outbuffer_size);
    }
    else
    {
        outbuffer_size = 0;
    }
    //if(bbp == 4) free(tmp);
    free(out);
    return outbuffer_size;
}

int get_pixel_format(uint8_t deep, wvnc_pixel_format_t *d)
{
    switch (deep)
    {
    case 32:
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

void *process(void *data, int wait)
{
    wvnc_user_data_t *user_data = get_user_data(data);
    uint8_t *buff = NULL;
    ws_msg_header_t *h = NULL;
    while (!(h = ws_read_header(user_data->wscl->client)) && user_data->status != DISCONNECTED && wait)
        ;
    if (h)
    {
        if (h->mask == 0)
        {
            ERROR("Data is not masked opcode 0x%04x data len %d", h->opcode,h->plen);
            ws_close(user_data->wscl->client, 1012);
            user_data->status = DISCONNECTED;
            free(h);
            return NULL;
            //break;
        }
        if (h->opcode == WS_CLOSE)
        {
            LOG("Websocket: connection closed");
            ws_close(user_data->wscl->client, 1011);
            user_data->status = DISCONNECTED;
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
            if ((l = ws_read_data(user_data->wscl->client, h, h->plen, buff)) > 0)
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
    //int depth = client->format.bitsPerPixel;
    client->updateRect.x = client->updateRect.y = 0;
    client->updateRect.w = width;
    client->updateRect.h = height;
    void *data = rfbClientGetClientData(client, client);
    wvnc_user_data_t *user_data = get_user_data(data);
    //client->width = sdl->pitch / (depth / 8);
    if (client->frameBuffer)
    {
        free(client->frameBuffer);
        client->frameBuffer = NULL;
    }
    client->frameBuffer = (uint8_t *)malloc(width * height * user_data->bbp / 8);
    wvnc_pixel_format_t pxf;
    if (!get_pixel_format(user_data->bbp, &pxf))
    {
        vnc_fatal(user_data, "Cannot get pixel format");
        return FALSE;
    }
    client->format.bitsPerPixel = user_data->bbp;
    client->format.redShift = pxf.r_shift;
    client->format.greenShift = pxf.g_shift;
    client->format.blueShift = pxf.b_shift;
    client->format.redMax = pxf.r_max;
    client->format.greenMax = pxf.g_max;
    client->format.blueMax = pxf.b_max;
    if (SetFormatAndEncodings(client))
    {
        LOG("width %d, height %d, depth %d\n", width, height, client->format.bitsPerPixel);
    }
    else
    {
        ERROR("Unable to set VNC format and Encoding");
    }
    /* create or resize the window */
    // send data to client
    uint8_t cmd[5];
    cmd[0] = 0x83; // resize command
    cmd[1] = (uint8_t)(width & 0xFF);
    cmd[2] = (uint8_t)(width >> 8);
    cmd[3] = (uint8_t)(height & 0xFF);
    cmd[4] = (uint8_t)(height >> 8);
    ws_b(user_data->wscl->client, cmd, 5);
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

static void update(rfbClient *cl, int x, int y, int w, int h)
{
    wvnc_user_data_t *user_data = get_user_data(rfbClientGetClientData(cl, cl));
    user_data->ux = x < user_data->ux?x: user_data->ux;
    user_data->uy = y < user_data->uy?y: user_data->uy;
    int b1 = x + w;   
    int b2 = user_data->ux + user_data->uw; 
    user_data->uw = b1 > b2 ? b1 - user_data->ux : b2 - user_data-> ux;
    b1 = y + h;   
    b2 = user_data->uy + user_data->uh; 
    user_data->uh = b1 > b2 ? b1 - user_data->uy : b2 - user_data->uy;
}

static void finish_update(rfbClient *client)
{
    wvnc_user_data_t *user_data = get_user_data(rfbClientGetClientData(client, client));
    long long current_time = current_timestamp();
    if(current_time - user_data->last_update < 40)
    {
        return;
    }
    uint8_t bytes = (uint8_t)client->format.bitsPerPixel / 8;
    user_data->last_update = current_time;
    int cw = user_data->uw;
    int ch = user_data->uh;
    int x = user_data->ux;
    int y = user_data->uy;
    int size = cw * ch * bytes;
    uint8_t flag = 0;
    uint8_t *cmd = (uint8_t *)malloc(size + 10); // + 9
    uint8_t *tmp = cmd + 10;
    //LOG("w %d h %d x %d y %d", cw, ch, x, y);
    
    if (!cmd || !tmp)
    {
        vnc_fatal(user_data, "Cannot allocate data for update");
        return;
    }
    if (!client->frameBuffer)
    {
        LOG("Client frame buffer data not found");
        return;
    }
    if(size == 0)
    {
        return;
    }
    uint8_t *dest_ptr = tmp;
    uint8_t *src_ptr;

    //cpy line by line
    
    for (int j = y; j < ch + y; j++)
    {
        src_ptr = client->frameBuffer + (j * client->width * bytes + x*bytes );
        memcpy(dest_ptr, src_ptr, cw * bytes);
        if(bytes == 4)
            for (int i = 3; i < cw * bytes; i += bytes)
                dest_ptr[i] = 255;
        dest_ptr += cw * bytes;
    }
    cmd[0] = 0x84; //update command
    cmd[1] = (uint8_t)(x & 0xFF);
    cmd[2] = (uint8_t)(x >> 8);
    cmd[3] = (uint8_t)(y & 0xFF);
    cmd[4] = (uint8_t)(y >> 8);
    cmd[5] = (uint8_t)(cw & 0xFF);
    cmd[6] = (uint8_t)(cw >> 8);
    cmd[7] = (uint8_t)(ch & 0xFF);
    cmd[8] = (uint8_t)(ch >> 8);
    int ret = jpeg_compress(tmp, cw, ch, bytes, user_data->quality);
    if (ret > 0)
    {
        flag |= 0x01;
        size = ret;
    }
    cmd[9] = flag | user_data->bbp;
    ws_b(user_data->wscl->client, cmd, size + 10);
    user_data->ux = 0xFFFF;
    user_data->uy = 0xFFFF;
    user_data->uw = 0;
    user_data->uh = 0;
    free(cmd);
}

static rfbCredential *get_credential(rfbClient *cl, int credentialType)
{
    wvnc_user_data_t *user_data = get_user_data(rfbClientGetClientData(cl, cl));
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
    ws_b(user_data->wscl->client, cmd, 1);
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
    void *data = rfbClientGetClientData(client, client);
    wvnc_user_data_t *user_data = get_user_data(data);
    cmd[0] = 0x81; // resize command
    ws_b(user_data->wscl->client, cmd, 1);

    // call process to get the password
    char *pwd = (char *)process(user_data, 1);
    //not free
    if (!pwd)
    {
        vnc_fatal(user_data, "Cannot read user password");
        return NULL;
    }
    //LOG("Password is '%s'\n", pwd);
    return pwd;
}

void open_session(void *data, const char *addr)
{
    int argc = 2;
    char *argv[2];
    argv[0] = "-listennofork";
    int len = 0;
    FILE *fp = NULL;
    char *buffer = NULL;
    char c;
    int st;
    wvnc_user_data_t *user_data = get_user_data(data);
    if (access(addr, F_OK) != -1)
    {
        //open the file
        fp = fopen(addr, "r");
        if (fp == NULL)
        {
            vnc_fatal(data, "Unable to read server file");
            return;
        }

        // find length of first line
        // lines end in "\n", but some malformed text files may not have this char at all
        // and whole file contents will be considered as the first line
        while ((c = fgetc(fp)) != EOF)
        {
            if (c == '\n')
            {
                break;
            }
            len++;
        }

        // allocate memory for size of first line (len)
        buffer = (char *)malloc(sizeof(char) * (len + 1));

        // seek to beginning of file
        fseek(fp, 0, SEEK_SET);
        buffer[len] = '\0';
        st = fread(buffer, sizeof(char), len, fp);
        UNUSED(st);
        fclose(fp);
        argv[1] = buffer;
    }
    else
    {
        argv[1] = (char *)addr;
    }
    LOG("client.BBP: %d\n", user_data->bbp);
    LOG("client.JPEG.quality: %d\n", user_data->quality);
    LOG("Server: %s\n", argv[1]);
    //LOG("Rate is %d\n", user_data->rate);
    if (!rfbInitClient(user_data->vncl, &argc, argv))
    {
        user_data->vncl = NULL; /* rfbInitClient has already freed the client struct */
        //cleanup(vncl);
        vnc_fatal(user_data, "Cannot connect to the server");
        if (buffer)
            free(buffer);
        return;
    }
    if (buffer)
        free(buffer);
    user_data->status = CONNECTED;
}

void waitfor(void *data)
{
    fd_set fd_in;
    wvnc_user_data_t *user_data = get_user_data(data);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500;
    while (user_data->status != DISCONNECTED)
    {
        FD_ZERO(&fd_in);
        FD_SET(user_data->wscl->client->sock, &fd_in);
        int rc = select(user_data->wscl->client->sock + 1, &fd_in, NULL, NULL, &timeout);
        if (rc == -1)
        {
            LOG("Client may disconnected");
            return;
        }
        if (rc > 0 && FD_ISSET(user_data->wscl->client->sock, &fd_in))
        {
            process(user_data, 0);
        }
        if (user_data->status == CONNECTED)
        {
            // process other message
            int status = WaitForMessage(user_data->vncl, 200); //500
            if (status < 0)
            {
                ERROR("VNC WaitForMessage return %d", status);
                break;
            }
            if (status)
            {
                status = HandleRFBServerMessage(user_data->vncl);
                if (!status)
                {
                    ERROR("VNC HandleRFBServerMessage fail");
                    break;
                }
            }
        }
    }
}

void *vnc_fatal(void *data, const char *msg)
{
    // print the message then close the
    // connection
    wvnc_user_data_t *user_data = get_user_data(data);

    int len, size;
    len = strlen(msg);
    size = len + 1;
    ERROR("VNC FATAL: %s", msg);
    uint8_t *cmd = (uint8_t *)malloc(size);
    cmd[0] = 0xFE; // error opcode
    if (cmd)
    {
        memcpy(cmd + 1, (uint8_t *)msg, len);
        ws_b(user_data->wscl->client, cmd, size);
        free(cmd);
    }
    // quit the socket
    ws_close(user_data->wscl->client, 1011);
    user_data->status = DISCONNECTED;
    return 0;
}

void *consume_client(void *ptr, wvnc_cmd_t header)
{
    uint8_t cmd = header.cmd;
    uint16_t size = header.size;
    wvnc_user_data_t *user_data = get_user_data(ptr);
    uint8_t *data;
    // in case of string
    switch (cmd)
    {
    case 0x01: /*client open a connection*/
        user_data->bbp = header.data[0];
        user_data->quality = header.data[1];
        open_session(user_data, (char *)(header.data + 2));
        break;
    case 0x02: //client enter a vnc password
        if (!header.data)
            return NULL;
        return strdup((char *)header.data);
    case 0x03: // client enter a credential
        data = (uint8_t *)malloc(size);
        memcpy(data, header.data, size);
        return data;
        break;
    case 0x04: // ack from client
        data = (uint8_t *)malloc(1);
        *data = (uint8_t)header.data[0];
        return data;
        break;
    case 0x05: //mouse event
        //LOG("MOuse event %d\n", header.data[4]);
        SendPointerEvent(user_data->vncl,
            (header.data[0] | (header.data[1] << 8)) & 0xFFFF,
            (header.data[2] | (header.data[3] << 8)) & 0xFFFF, header.data[4]);
        break;
    case 0x06: // key board event
        // LOG("Key is %c %d", (header.data[0] | (header.data[1] << 8)) & 0xFFFF, header.data[2]);
        SendKeyEvent(user_data->vncl, (header.data[0] | (header.data[1] << 8)) & 0xFFFF, header.data[2] ? TRUE : FALSE);
        break;
    case 0x07:
        SendClientCutText(user_data->vncl, (char *)header.data, strlen((char *)header.data));
        break;
    case 0x08:
        LOG("Receive ping message from client: %s", (char*)header.data);
        break;
    default:
        return vnc_fatal(user_data, "Unknown client command");
    }
    return NULL;
}

static void got_clipboard(rfbClient *cl, const char *text, int len)
{
    // LOG("received clipboard text '%s'", text);
    void *data = rfbClientGetClientData(cl, cl);
    wvnc_user_data_t *user_data = get_user_data(data);
    uint8_t *cmd = (uint8_t *)malloc(len + 1);
    cmd[0] = 0x85;
    memcpy(cmd + 1, text, len);
    ws_b(user_data->wscl->client, cmd, len + 1);
    free(cmd);
    uint8_t *ack = (uint8_t *)process(user_data, 1);
    if (!ack || !(*ack))
    {
        LOG("Fail to set client clipboard");
        if (ack)
            free(ack);
        return;
    }
    free(ack);
}

void event_loop(void *data)
{
    wvnc_user_data_t *user_data = get_user_data(data);
    rfbClient *vncl = NULL;
    vncl = rfbGetClient(8, 3, 4);
    vncl->MallocFrameBuffer = resize;
    vncl->canHandleNewFBSize = TRUE;
    vncl->FinishedFrameBufferUpdate = finish_update;
    vncl->GotFrameBufferUpdate = update;
    vncl->GetPassword = get_password;
    //cl->HandleKeyboardLedState=kbd_leds;
    //cl->HandleTextChat=text_chat;
    vncl->GotXCutText = got_clipboard;
    vncl->GetCredential = get_credential;
    vncl->listenPort = LISTEN_PORT_OFFSET;
    vncl->listen6Port = LISTEN_PORT_OFFSET;
    user_data->status = READY; // 1 for ready for connect
    user_data->vncl = vncl;
    rfbClientSetClientData(vncl, vncl, user_data);
    waitfor((void *)user_data);
    // child
    if (user_data->vncl && user_data->vncl->frameBuffer)
    {
        free(user_data->vncl->frameBuffer);
        user_data->vncl->frameBuffer = NULL;
    }
    if (user_data->vncl)
        rfbClientCleanup(user_data->vncl);
    // close the connection before free data
    destroy_request(user_data->wscl);
    free(user_data);
}
void *handle(void *data)
{
    antd_request_t *rq = (antd_request_t *)data;
    pthread_t th;
    antd_task_t *task = NULL;
    void *cl = (void *)rq->client;
    if (ws_enable(rq->request))
    {
        wvnc_user_data_t *user_data = (wvnc_user_data_t *)malloc(sizeof(wvnc_user_data_t));
        user_data->wscl = rq;
        user_data->last_update = current_timestamp();
        user_data->ux = 0xFFFF;
        user_data->uy = 0xFFFF;
        user_data->uw = 0;
        user_data->uh = 0;
        if (pthread_create(&th, NULL, (void *(*)(void *))event_loop, (void *)user_data) != 0)
        {
            free(user_data);
            perror("pthread_create: cannot create thread for wvnc\n");
        }
        else
        {
            pthread_detach(th);
            task = antd_create_task(NULL, NULL, NULL, time(NULL));
            return task;
        }
    }
    else
    {
        antd_error(cl, 400, "Please use a websocket connection");
    }
    task = antd_create_task(NULL, (void *)rq, NULL, rq->client->last_io);
    return task;
    //LOG("%s\n", "EXIT Streaming..");
}
void init()
{
}
void destroy()
{
}