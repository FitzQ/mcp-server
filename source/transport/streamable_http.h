#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "../third_party/cJSON.h"
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include "../tools/controller.h"
#include "../tools/cur_frame.h"


#define MCP_PORT 12345

typedef struct {
    char *event; // 事件类型
    char *data;  // 事件数据
    char *id;    // 事件 ID
} SSEvent;

char *get_header(char *req, char *key);
Result add_sse_connection(int client_fd, char *Mcp_Session_Id, char *Last_Event_ID);
void handle_http_request(char *req, int req_len, int client_fd);

void sse_heartbeat(void* arg);
Result streamable_http_init();
