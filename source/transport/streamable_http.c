#include "streamable_http.h"

#define MAX_REQUEST_SIZE 2048

// 线程池相关
#define WORKER_COUNT 2
static Thread worker_threads[WORKER_COUNT];
static int worker_client_fd[WORKER_COUNT];
static volatile int worker_busy[WORKER_COUNT];
static int listen_fd = -1;

Result socket_init() {
    struct sockaddr_in addr;
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return -1;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MCP_PORT);
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    
    if (listen(listen_fd, 1) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    
    srand((unsigned int)time(NULL));
    return 0;
}

// 解析 HTTP header，获取指定 key 的值
char *get_header(char *req, char *key) {
    static char val[128];
    val[0] = 0;
    char *p = strstr(req, key);
    if (p) {
        p += strlen(key);
        while (*p == ' ' || *p == ':') ++p;
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < 127) val[i++] = *p++;
        val[i] = 0;
        return val;
    }
    return NULL;
}

void worker_func(void* arg) {
    int idx = (int)(intptr_t)arg;
    while (1) {
        if (!worker_busy[idx]) {
            svcSleepThread(1000000ULL); // 1ms
            continue;
        }
        int client_fd = worker_client_fd[idx];
        char req[MAX_REQUEST_SIZE];
        int n = recv(client_fd, req, sizeof(req)-1, 0);
        if (n > 0) {
            req[n] = '\0';
            log_info("Received request: %s", req);
            if (strncmp(req, "GET /mcp", 8) == 0) {
                add_sse_connection(client_fd, get_header(req, "Mcp-Session-Id"), get_header(req, "Last-Event-ID"));
                // send(client_fd, "HTTP/1.1 405 Method Not Allowed\r\nconnection: close\r\ncontent-length: 0\r\n\r\n", 78, 0);
                // close(client_fd);
            } else {
                handle_http_request(req, n, client_fd);
                close(client_fd);
            }
        }
        log_info("Processed request from client_fd: %d", client_fd);
        worker_busy[idx] = 0; // 标记空闲
    }
}

void run(void* arg) {
    // 初始化 worker 线程
    for (int i = 0; i < WORKER_COUNT; ++i) {
        worker_busy[i] = 0;
        worker_client_fd[i] = -1;
        Result rs = threadCreate(&worker_threads[i], worker_func, (void*)(intptr_t)i, NULL, 0x10000, 49, -2);
        if (R_FAILED(rs)) {
            log_error("Failed to create worker thread %d (%x)", i, rs);
            return;
        }
        rs = threadStart(&worker_threads[i]);
        if (R_FAILED(rs)) {
            log_error("Failed to start worker thread %d (%x)", i, rs);
            return;
        }
    }
    while (1) {
        if (listen_fd < 0 && R_FAILED(socket_init())) {
            svcSleepThread(10000000ULL); // 10ms
            continue;
        }
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            listen_fd = -1; // 重置监听套接字
            log_error("Failed to accept client connection");
            svcSleepThread(10000000ULL); // 10ms
            continue;
        }
        log_info("Accepted client connection");
        // 分配给空闲 worker
        int assigned = 0;
        for (int i = 0; i < WORKER_COUNT; ++i) {
            if (!worker_busy[i]) {
                worker_client_fd[i] = client_fd;
                worker_busy[i] = 1;
                assigned = 1;
                break;
            }
        }
        if (!assigned) {
            log_error("No available worker thread, rejecting client_fd: %d", client_fd);
            close(client_fd);
        }
    }
}

Result streamable_http_init() {
    Thread listen_thread;
    Result rs = threadCreate(&listen_thread, run, NULL, NULL, 0x1000, 49, -2);
    if (R_FAILED(rs)) {
        log_error("Failed to create listen thread for streamable_http (%x)", rs);
        return rs;
    }
    rs = threadStart(&listen_thread);
    if (R_FAILED(rs)) {
        log_error("Failed to start listen thread for streamable_http (%x)", rs);
        return rs;
    }
    Thread notification_thread;
    rs = threadCreate(&notification_thread, sse_heartbeat, NULL, NULL, 0x1000, 49, -2);
    if (R_FAILED(rs)) {
        log_error("Failed to create notification thread for streamable_http (%x)", rs);
        return rs;
    }
    rs = threadStart(&notification_thread);
    if (R_FAILED(rs)) {
        log_error("Failed to start notification thread for streamable_http (%x)", rs);
        return rs;
    }
    return 0;
}