/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "spice-client.h"
#define FLEXVDI_PROTO_IMPL
#include "flexdp.h"
#include "flexvdi-port.h"
#include "printclient.h"
#ifdef ENABLE_SERIALREDIR
#include "serialredir.h"
#endif

typedef enum {
    WAIT_NEW_MESSAGE,
    WAIT_DATA,
} WaitState;


typedef struct flexvdi_port {
    SpicePortChannel * channel;
    gboolean connected;
    GCancellable * cancellable;
    WaitState state;
    FlexVDIMessageHeader curHeader;
    uint8_t * buffer, * bufpos, * bufend;
    FlexVDICapabilitiesMsg agentCapabilities;
} flexvdi_port;


static flexvdi_port port;


typedef struct ConnectionHandler {
    flexvdi_agent_connected_cb cb;
    gpointer data;
} ConnectionHandler;


static GSList * connectionHandlers;


static const size_t HEADER_SIZE = sizeof(FlexVDIMessageHeader);

uint8_t * getMsgBuffer(size_t size) {
    uint8_t * buf = (uint8_t *)g_malloc(size + HEADER_SIZE);
    if (buf) {
        ((FlexVDIMessageHeader *)buf)->size = size;
        return buf + HEADER_SIZE;
    } else
        return NULL;
}


void deleteMsgBuffer(uint8_t * buffer) {
    g_free(buffer - HEADER_SIZE);
}


typedef struct AsyncUserData {
    GAsyncReadyCallback callback;
    gpointer user_data;
} AsyncUserData;


static gpointer newAsyncUserData(GAsyncReadyCallback cb, gpointer u) {
    AsyncUserData * aud = g_malloc(sizeof(AsyncUserData));
    if (aud) {
        aud->callback = cb;
        aud->user_data = u;
    }
    return aud;
}


static void sendMessageCb(GObject * source_object, GAsyncResult * res, gpointer user_data) {
    GError * error = NULL;
    sendMessageFinish(source_object, res, &error);
    if (error != NULL)
        g_warning("Error sending message, %s", error->message);
    g_clear_error(&error);
    deleteMsgBuffer(user_data);
}


static void sendMessageAsyncCb(GObject * source_object, GAsyncResult * res,
                               gpointer user_data) {
    AsyncUserData * aud = (AsyncUserData *)user_data;
    if (!g_cancellable_is_cancelled(port.cancellable)) {
        aud->callback(source_object, res, aud->user_data);
    }
    g_free(aud);
}


void sendMessage(uint32_t type, uint8_t * buffer) {
    sendMessageAsync(type, buffer, sendMessageCb, buffer);
}


void sendMessageAsync(uint32_t type, uint8_t * buffer,
                      GAsyncReadyCallback callback, gpointer user_data) {
    FlexVDIMessageHeader * head = (FlexVDIMessageHeader *)(buffer - HEADER_SIZE);
    size_t size = head->size + HEADER_SIZE;
    g_debug("Sending message type %d, size %d", (int)type, (int)size);
    head->type = type;
    marshallMessage(type, buffer, head->size);
    marshallHeader(head);
    spice_port_channel_write_async(port.channel, head, size, port.cancellable, sendMessageAsyncCb,
                                   newAsyncUserData(callback, user_data));
}


void sendMessageFinish(GObject * source_object, GAsyncResult * res, GError ** error) {
    spice_port_channel_write_finish(SPICE_PORT_CHANNEL(source_object), res, error);
}


static void preparePortBuffer(size_t size) {
    g_free(port.buffer);
    port.buffer = port.bufpos = (uint8_t *)g_malloc(size);
    port.bufend = port.bufpos + size;
}


static void port_data(SpicePortChannel * pchannel, gpointer data, int size);

void flexvdi_port_open(SpiceChannel * channel) {
    gboolean opened = FALSE;
    g_object_get(channel, "port-opened", &opened, NULL);
    if (opened) {
        g_info("flexVDI guest agent connected");
        g_signal_connect(channel, "port-data", G_CALLBACK(port_data), NULL);
        port.connected = TRUE;
        port.channel = SPICE_PORT_CHANNEL(channel);
        port.cancellable = g_cancellable_new();
        port.buffer = NULL;
        memset(port.agentCapabilities.caps, 0, sizeof(port.agentCapabilities));
        preparePortBuffer(sizeof(FlexVDIMessageHeader));
        port.state = WAIT_NEW_MESSAGE;
        uint8_t * buf = getMsgBuffer(sizeof(FlexVDIResetMsg));
        if (buf) {
            sendMessage(FLEXVDI_RESET, buf);
        }
        buf = getMsgBuffer(sizeof(FlexVDICapabilitiesMsg));
        if (buf) {
            FlexVDICapabilitiesMsg * capMsg = (FlexVDICapabilitiesMsg *)buf;
            capMsg->caps[0] = capMsg->caps[1] = capMsg->caps[2] = capMsg->caps[3] = 0;
            setCapability(capMsg, FLEXVDI_CAP_PRINTING);
            sendMessage(FLEXVDI_CAPABILITIES, buf);
        }
    } else {
        g_info("flexVDI guest agent disconnected");
        g_signal_handlers_disconnect_by_func(channel, G_CALLBACK(port_data), NULL);
        port.connected = FALSE;
        port.channel = NULL;
        g_cancellable_cancel(port.cancellable);
        g_object_unref(port.cancellable);
        port.cancellable = NULL;
    }
}


int flexvdi_agent_supports_capability(int cap) {
    return supportsCapability(&port.agentCapabilities, cap);
}


static void handleCapabilitiesMsg(FlexVDICapabilitiesMsg * msg) {
    memcpy(&port.agentCapabilities, msg, sizeof(FlexVDICapabilitiesMsg));
    g_debug("flexVDI guest agent capabilities: %08x %08x %08x %08x",
            port.agentCapabilities.caps[3], port.agentCapabilities.caps[2],
            port.agentCapabilities.caps[1], port.agentCapabilities.caps[0]);
    GSList * it;
    for (it = connectionHandlers; it != NULL; it = g_slist_next(it)) {
        ConnectionHandler * handler = (ConnectionHandler *)it->data;
        handler->cb(handler->data);
    }
}


static void handleMessage(uint32_t type, uint8_t * msg) {
    switch(type) {
    case FLEXVDI_CAPABILITIES:
        handleCapabilitiesMsg((FlexVDICapabilitiesMsg *)msg);
        break;
    case FLEXVDI_PRINTJOB:
        handlePrintJob((FlexVDIPrintJobMsg *)msg);
        break;
    case FLEXVDI_PRINTJOBDATA:
        handlePrintJobData((FlexVDIPrintJobDataMsg *)msg);
        break;
    }
}


static void prepareOneByte() {
    size_t i;
    uint8_t * p = port.buffer;
    for (i = 0; i < sizeof(FlexVDIMessageHeader) - 1; ++i)
        p[i] = p[i + 1];
    port.bufpos = &p[i];
}


static void port_data(SpicePortChannel * pchannel, gpointer data, int size) {
    void * end = data + size;
    while (data < end || port.buffer == port.bufend) { // Special case: read 0 bytes
        // Fill the buffer
        size = end - data;
        if (port.bufend - port.bufpos < size)
            size = port.bufend - port.bufpos;
        memcpy(port.bufpos, data, size);
        port.bufpos += size;
        data += size;
        if (port.bufpos < port.bufend) return; // Data consumed, buffer not filled
        switch (port.state) {
        case WAIT_NEW_MESSAGE:
            port.curHeader = *((FlexVDIMessageHeader *)port.buffer);
            unmarshallHeader(&port.curHeader);
            if (port.curHeader.size > FLEXVDI_MAX_MESSAGE_LENGTH) {
                g_warning("Oversized message (%u > %u)",
                           port.curHeader.size, FLEXVDI_MAX_MESSAGE_LENGTH);
                prepareOneByte();
            } else if (port.curHeader.type >= FLEXVDI_MAX_MESSAGE_TYPE) {
                g_warning("Unknown message type %d", port.curHeader.type);
                prepareOneByte();
            } else {
                preparePortBuffer(port.curHeader.size);
                port.state = WAIT_DATA;
            }
            break;
        case WAIT_DATA:
            g_debug("Received message type %u, size %u",
                       port.curHeader.type, port.curHeader.size);
            if (!unmarshallMessage(port.curHeader.type, port.buffer, port.curHeader.size)) {
                g_warning("Wrong message size on reception (%u)",
                           port.curHeader.size);
            } else {
                handleMessage(port.curHeader.type, port.buffer);
            }
            preparePortBuffer(sizeof(FlexVDIMessageHeader));
            port.state = WAIT_NEW_MESSAGE;
            break;
        }
    }
}


int flexvdi_is_agent_connected(void) {
    return port.connected;
}


void flexvdi_on_agent_connected(flexvdi_agent_connected_cb cb, gpointer data) {
    ConnectionHandler * h = g_malloc(sizeof(ConnectionHandler));
    h->cb = cb;
    h->data = data;
    connectionHandlers = g_slist_prepend(connectionHandlers, h);
}


int flexvdi_get_printer_list(GSList ** printer_list) {
    return flexvdiSpiceGetPrinterList(printer_list);
}


int flexvdi_share_printer(const char * printer) {
    if (port.connected) {
        return flexvdiSpiceSharePrinter(printer);
    } else {
        g_warning("The flexVDI guest agent is not connected");
        return FALSE;
    }
}


int flexvdi_unshare_printer(const char * printer) {
    if (port.connected) {
        return flexvdiSpiceUnsharePrinter(printer);
    } else {
        g_warning("The flexVDI guest agent is not connected");
        return FALSE;
    }
}
