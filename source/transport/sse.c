#include "streamable_http.h"

#define SSE_BUFFER_SIZE 4096
#define MAX_SSE_CONNECTIONS 4
static int sse_connection = 0;

typedef struct STORED_CONNECTION {
    int client_fd;
    int event_id; // 用于跟踪事件 ID
    char *Mcp_Session_Id;
    Thread thread;
} StoredConnection;

static StoredConnection sse_connections[MAX_SSE_CONNECTIONS] = {0};

// 发送SSE头部
void sse_send_header(int sock) {
    const char *header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    Result res = send(sock, header, strlen(header), 0);
    if (res <= 0) {
        log_error("Failed to send SSE header (%d)", res);

    } else {
        log_info("SSE header sent successfully to client_fd: %d", sock);
    }
}

// 发送一条SSE事件
void notificate_all(SSEvent *ssevent) {
    if (!ssevent || sse_connection <= 0) {
        return;
    }
    log_info("debug point 1");
    char buf[500];
    int len = 0;
    log_info("debug point 2");
    // if (ssevent->id) len += snprintf(buf+len, SSE_BUFFER_SIZE-len, "id: %s\n", ssevent->id);
    if (ssevent->event) len += snprintf(buf+len, SSE_BUFFER_SIZE-len, "event: %s\n", ssevent->event);
    log_info("debug point 3");
    const char *p = ssevent->data;
    while (*p) {
        log_info("debug point 4");
        int chunk = strcspn(p, "\n");
        len += snprintf(buf+len, SSE_BUFFER_SIZE-len, "data: %.*s\n", chunk, p);
        p += chunk;
        if (*p == '\n') p++;
    }
    log_info("debug point 5");
    len += snprintf(buf+len, SSE_BUFFER_SIZE-len, "\n");

    log_info("debug point 6");
    for (int i = 0; i < sse_connection; ) {
        if (send(sse_connections[i].client_fd, buf, len, 0) <= 0) {
            log_error("Failed to send SSE event, cleaning up client_fd: %d", sse_connections[i].client_fd);
            close(sse_connections[i].client_fd);
            memmove(&sse_connections[i], &sse_connections[i+1], (sse_connection-i-1)*sizeof(StoredConnection));
            sse_connections[--sse_connection] = (StoredConnection){0};
        } else {
            ++i;
        }
        log_info("debug point 7");
    }
}

// 连接主循环（伪代码）
void sse_heartbeat(void* arg) {
    SSEvent ssevent = {
        .event = "message",
        .data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"
    };

    while (1) {
        notificate_all(&ssevent);
        svcSleepThread(4000000000ULL); // 每4秒推送一次
    }
    // 线程退出时释放资源
    Result rs = threadClose(threadGetSelf());
    log_info("SSE thread closed with result: %d", rs);
}

int connected(const char *Mcp_Session_Id) {
    for (int i = 0; i < sse_connection; i++) {
        if (sse_connections[i].Mcp_Session_Id && strcmp(sse_connections[i].Mcp_Session_Id, Mcp_Session_Id) == 0) {
            return i;
        }
    }
    return -1;
}

Result add_sse_connection(int client_fd, char *Mcp_Session_Id, char *Last_Event_ID) {
    if (sse_connection >= MAX_SSE_CONNECTIONS) {
        log_error("Max SSE connections reached");
        // todo 不要直接关闭，而是返回错误码
        close(client_fd);
        return -1;
    }
    if (client_fd < 0) {
        log_error("Invalid client_fd: %d", client_fd);
        return -2;
    }
    if (!Mcp_Session_Id) {
        log_error("Invalid Mcp_Session_Id");
        close(client_fd);
        return -3;
    }
    int indx = connected(Mcp_Session_Id);
    if (indx >= 0) {
        log_warning("Mcp_Session_Id already connected: %s, will abort old connection", Mcp_Session_Id);
        close(sse_connections[indx].client_fd);
        sse_connections[indx].client_fd = client_fd;
        sse_connections[indx].event_id = Last_Event_ID ? atoi(Last_Event_ID) + 1 : 1;
    } else {
        sse_connections[sse_connection].client_fd = client_fd;
        sse_connections[sse_connection].Mcp_Session_Id = Mcp_Session_Id;
        sse_connections[sse_connection].event_id = Last_Event_ID ? atoi(Last_Event_ID) + 1 : 1;
        sse_connection++;
    }
    log_info("New SSE connection added, total: %d", sse_connection);
    sse_send_header(client_fd);
    return 0;
}
