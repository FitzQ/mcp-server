#include "streamable_http.h"

static char protocol_version[32] = "2025-06-18";
static char session_id[40] = {0};

// 生成随机 session id
void gen_session_id(char *buf, int len) {
    for (int i = 0; i < len-1; ++i) {
        int r = rand() % 62;
        buf[i] = (r < 10) ? ('0'+r) : (r < 36 ? 'A'+r-10 : 'a'+r-36);
    }
    buf[len-1] = '\0';
}

// 处理 MCP HTTP 请求
void handle_http_request(char *req, int req_len, int client_fd) {
    
    // 支持 OAuth2 endpoints
    if (strstr(req, "GET /.well-known/oauth-authorization-server") == req) {
        const char *resp =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{"
            "\"issuer\":\"http://192.168.1.15:12345\","  
            "\"authorization_endpoint\":\"http://192.168.1.15:12345/oauth/authorize\","  
            "\"token_endpoint\":\"http://192.168.1.15:12345/oauth/token\","  
            "\"response_types_supported\":[\"code\"]"
            "}";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    if (strstr(req, "GET /.well-known/oauth-client-registration") == req) {
        const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{}";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    if (strstr(req, "GET /oauth/authorize") == req || strstr(req, "POST /oauth/authorize") == req) {
        const char *resp = "HTTP/1.1 501 Not Implemented\r\nContent-Type: application/json\r\n\r\n{\"error\":\"OAuth2 authorization not supported\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    if (strstr(req, "GET /oauth/token") == req || strstr(req, "POST /oauth/token") == req) {
        const char *resp = "HTTP/1.1 501 Not Implemented\r\nContent-Type: application/json\r\n\r\n{\"error\":\"OAuth2 token not supported\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    if (strncmp(req, "POST /mcp", 9) != 0) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    // 处理 MCP 请求，忽略 headers
    (void)get_header(req, "MCP-Protocol-Version");
    (void)get_header(req, "Mcp-Session-Id");
    
    // 解析 body
    const char *body = strstr(req, "\r\n\r\n");
    if (!body) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Missing body\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    body += 4;
    
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Invalid JSON\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    // 处理 JSON-RPC
    const cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    const cJSON *method = cJSON_GetObjectItem(root, "method");
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    
    if (!jsonrpc || !cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0 || 
        !method || !cJSON_IsString(method)) {
        cJSON_Delete(root);
        const char *resp = "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    // 处理 initialize
    if (strcmp(method->valuestring, "initialize") == 0) {
        gen_session_id(session_id, sizeof(session_id));
        
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "protocolVersion", protocol_version);
        
        // 能力协商对象
        cJSON *caps = cJSON_CreateObject();
        
        // logging 能力
        cJSON *logging = cJSON_CreateObject();
        cJSON_AddItemToObject(caps, "logging", logging);
        
        // prompts 能力
        // cJSON *prompts = cJSON_CreateObject();
        // cJSON_AddBoolToObject(prompts, "listChanged", 1);
        // cJSON_AddItemToObject(caps, "prompts", prompts);
        
        // resources 能力
        cJSON *resources = cJSON_CreateObject();
        cJSON_AddBoolToObject(resources, "subscribe", 1);
        cJSON_AddBoolToObject(resources, "listChanged", 1);
        cJSON_AddItemToObject(caps, "resources", resources);
        
        // tools 能力
        cJSON *tools = cJSON_CreateObject();
        cJSON_AddBoolToObject(tools, "listChanged", 1);
        cJSON_AddItemToObject(caps, "tools", tools);
        
        // experimental 能力
        // cJSON *experimental = cJSON_CreateObject();
        // cJSON_AddItemToObject(caps, "experimental", experimental);
        
        cJSON_AddItemToObject(result, "capabilities", caps);
        
        // 服务端信息
        cJSON *info = cJSON_CreateObject();
        cJSON_AddStringToObject(info, "name", "SwitchMCPServer");
        cJSON_AddStringToObject(info, "title", "Nintendo Switch MCP Server");
        cJSON_AddStringToObject(info, "version", "1.0.0");
        cJSON_AddItemToObject(result, "serverInfo", info);
        
        cJSON_AddStringToObject(result, "instructions", "Welcome to Switch MCP Server.");
        
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
        cJSON_AddItemToObject(resp, "result", result);
        
        char *resp_str = cJSON_PrintUnformatted(resp);
        char header[256];
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nMcp-Session-Id: %s\r\nMCP-Protocol-Version: %s\r\n\r\n", session_id, protocol_version);
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }
    
    // 处理 resources/list 方法
    if (strcmp(method->valuestring, "resources/list") == 0 && id) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));

        cJSON *result = cJSON_CreateObject();
        cJSON *resources = cJSON_CreateArray();

        // 当前帧资源
        // list_cur_frame(resources);

        cJSON_AddItemToObject(result, "resources", resources);
        // cJSON_AddStringToObject(result, "nextCursor", "");
    
        cJSON_AddItemToObject(resp, "result", result);
        
        char *resp_str = cJSON_PrintUnformatted(resp);
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }

    // 处理 resources/read 方法
    if (strcmp(method->valuestring, "resources/read") == 0 && id) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));

        cJSON *result = cJSON_CreateObject();
        cJSON *resources = cJSON_CreateArray();

        // 当前帧资源
        // read_cur_frame(resources);

        cJSON_AddItemToObject(result, "resources", resources);
        // cJSON_AddStringToObject(result, "nextCursor", "");

        cJSON_AddItemToObject(resp, "result", result);

        char *resp_str = cJSON_PrintUnformatted(resp);
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }

    // 处理 tools/list 方法
    if (strcmp(method->valuestring, "tools/list") == 0 && id) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
        
        cJSON *result = cJSON_CreateObject();
        cJSON *tools = cJSON_CreateArray();
        
        // controller 工具
        list_controller(tools);
        // cur_frame 工具
        list_cur_frame(tools);
        
        cJSON_AddItemToObject(result, "tools", tools);
        // cJSON_AddStringToObject(result, "nextCursor", "");
        cJSON_AddItemToObject(resp, "result", result);
        
        char *resp_str = cJSON_PrintUnformatted(resp);
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }
    
    // 处理 tools/call 方法
    if (strcmp(method->valuestring, "tools/call") == 0 && id) {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        const cJSON *tool_name = params ? cJSON_GetObjectItem(params, "name") : NULL;
        const cJSON *arguments = params ? cJSON_GetObjectItem(params, "arguments") : NULL;
        
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
        
        cJSON *result = cJSON_CreateObject();
        cJSON *content = cJSON_CreateArray();
        int isError = 0;
        
        if (tool_name && cJSON_IsString(tool_name) && strcmp(tool_name->valuestring, "controller") == 0 && arguments) {
            isError = call_controller(content, arguments);
        } else if (tool_name && cJSON_IsString(tool_name) && strcmp(tool_name->valuestring, "cur_frame") == 0) {
            isError = call_cur_frame(content);
        } else {
            isError = 1;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "text");
            cJSON_AddStringToObject(item, "text", "Unknown tool or missing arguments");
            cJSON_AddItemToArray(content, item);
        }
        
        cJSON_AddItemToObject(result, "content", content);
        cJSON_AddBoolToObject(result, "isError", isError);
        cJSON_AddItemToObject(resp, "result", result);
        
        char *resp_str = cJSON_PrintUnformatted(resp);
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }
    
    // 处理其他通知和方法
    if (strcmp(method->valuestring, "notifications/initialized") == 0) {
        cJSON_Delete(root);
        const char *resp = "HTTP/1.1 202 Accepted\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    
    if (strcmp(method->valuestring, "ping") == 0 && id) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(resp, "result", result);
        
        char *resp_str = cJSON_PrintUnformatted(resp);
        const char *header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
        send(client_fd, header, strlen(header), 0);
        send(client_fd, resp_str, strlen(resp_str), 0);
        free(resp_str);
        cJSON_Delete(resp);
        cJSON_Delete(root);
        return;
    }
    
    // 其它方法暂不支持
    cJSON_Delete(root);
    const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Unsupported method\"}\n";
    log_error("Unsupported method: %s", method->valuestring);
    send(client_fd, resp, strlen(resp), 0);
}

