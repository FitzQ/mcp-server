#include "streamable_http.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define SSE_BUFFER_SIZE 512
#define MAX_SSE_CONNECTIONS 2
static int sse_connection = 0;

typedef struct STORED_CONNECTION {
    int client_fd;
    int event_id; // 用于跟踪事件 ID
    char *Mcp_Session_Id;
    Thread thread;
} StoredConnection;

static StoredConnection sse_connections[MAX_SSE_CONNECTIONS] = {0};

static void free_connection(int idx) {
    if (idx < 0 || idx >= sse_connection) return;
    if (sse_connections[idx].client_fd >= 0) {
        close(sse_connections[idx].client_fd);
    }
    if (sse_connections[idx].Mcp_Session_Id) {
        free(sse_connections[idx].Mcp_Session_Id);
        sse_connections[idx].Mcp_Session_Id = NULL;
    }
    sse_connections[idx] = (StoredConnection){0};
}

static void compact_connections(int start_idx) {
    if (start_idx < 0 || start_idx >= sse_connection) return;
    for (int i = start_idx; i < sse_connection - 1; ++i) {
        sse_connections[i] = sse_connections[i+1];
    }
    sse_connections[sse_connection-1] = (StoredConnection){0};
    --sse_connection;
}

// 发送SSE头部
void sse_send_header(int sock) {
    const char *header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    ssize_t sent = send(sock, header, strlen(header), 0);
    if (sent <= 0) {
        log_error("Failed to send SSE header fd=%d errno=%d", sock, errno);
        return;
    }
    // 立即发送一个正式心跳事件，符合客户端固定格式
    const char *hb_evt = "event: message\n"
                         "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n\n";
    send(sock, hb_evt, strlen(hb_evt), 0);
    log_info("SSE header sent successfully to client_fd: %d", sock);
}

// 发送一条SSE事件
void notificate_all(SSEvent *ssevent) {
    if (!ssevent || sse_connection <= 0) return;

    char buf[SSE_BUFFER_SIZE];
    int len = 0;
    if (ssevent->event) {
        int written = snprintf(buf + len, SSE_BUFFER_SIZE - len, "event: %s\n", ssevent->event);
        if (written < 0 || written >= SSE_BUFFER_SIZE - len) return; // 防止溢出
        len += written;
    }
    const char *p = ssevent->data ? ssevent->data : "";
    while (*p && len < SSE_BUFFER_SIZE - 16) { // 预留结尾空间
        int chunk = strcspn(p, "\n");
        int written = snprintf(buf + len, SSE_BUFFER_SIZE - len, "data: %.*s\n", chunk, p);
        if (written < 0 || written >= SSE_BUFFER_SIZE - len) break;
        len += written;
        p += chunk;
        if (*p == '\n') p++;
    }
    if (len < SSE_BUFFER_SIZE - 2) {
        buf[len++] = '\n';
        buf[len] = '\0';
    }

    for (int i = 0; i < sse_connection; ) {
        ssize_t sent = send(sse_connections[i].client_fd, buf, len, 0);
        if (sent <= 0) {
            log_error("SSE send failed fd=%d errno=%d, removing", sse_connections[i].client_fd, errno);
            free_connection(i);
            compact_connections(i); // i 位置已填充下一个连接，再继续同 i
            continue;
        }
        ++i;
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
        svcSleepThread(4000000000ULL); // 4s heartbeat
    }
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
        // 复用该槽：关闭旧 fd，保留/替换 session id (相同值)
        if (sse_connections[indx].client_fd >= 0) close(sse_connections[indx].client_fd);
        sse_connections[indx].client_fd = client_fd;
        sse_connections[indx].event_id = Last_Event_ID ? atoi(Last_Event_ID) + 1 : 1;
    } else {
        if (sse_connection >= MAX_SSE_CONNECTIONS) {
            log_error("SSE connection race overflow");
            close(client_fd);
            return -4;
        }
        sse_connections[sse_connection].client_fd = client_fd;
        // 复制 session id，避免使用静态缓冲被覆盖
        size_t sid_len = strlen(Mcp_Session_Id) + 1;
        char *sid_copy = (char*)malloc(sid_len);
        if (!sid_copy) {
            log_error("Failed to alloc for session id");
            close(client_fd);
            return -5;
        }
        memcpy(sid_copy, Mcp_Session_Id, sid_len);
        sse_connections[sse_connection].Mcp_Session_Id = sid_copy;
        sse_connections[sse_connection].event_id = Last_Event_ID ? atoi(Last_Event_ID) + 1 : 1;
        ++sse_connection;
    }
    log_info("New SSE connection added, total: %d", sse_connection);
    sse_send_header(client_fd);
    return 0;
}
