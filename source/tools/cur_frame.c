

#include "../third_party/cJSON.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <switch/types.h> // for u8, u32
#include "../third_party/stb_base64.h"  // 需项目有 base64.h/base64.c
#include "../util/log.h"

#define CUR_FRAME_WIDTH 1280
#define CUR_FRAME_HEIGHT 720
#define JPEG_BUF_SIZE 0x80000 // 官方推荐大小
static Service capssc;

int capture_jpeg_screenshot(char** out_b64);

int list_cur_frame(cJSON *tools) {
    // cur_frame 工具
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", "cur_frame");
    cJSON_AddStringToObject(tool, "title", "cur_frame");
    cJSON_AddStringToObject(tool, "description", "get current graphic frame");

    cJSON *inputSchema = cJSON_CreateObject();
    cJSON_AddStringToObject(inputSchema, "type", "object");
    cJSON *properties = cJSON_CreateObject();

    cJSON_AddItemToObject(inputSchema, "properties", properties);
    cJSON *required = cJSON_CreateArray();
    // 不强制 required 字段，允许部分参数
    cJSON_AddItemToObject(inputSchema, "required", required);
    cJSON_AddItemToObject(tool, "inputSchema", inputSchema);
    cJSON_AddItemToArray(tools, tool);
    return 0;
}

int call_cur_frame(cJSON *contents) {
    char *b64 = NULL;
    int rc = capture_jpeg_screenshot(&b64);
    log_info("[cur_frame] capture_jpeg_screenshot %s.", rc == 0 ? "succeeded" : "failed");

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "image");
    cJSON_AddStringToObject(item, "mimeType", "image/png");
    cJSON_AddStringToObject(item, "data", b64);
    if (b64) free(b64);
    cJSON_AddItemToArray(contents, item);
    log_info("[cur_frame] Success, image added to contents.");
    return 0;
}

Result cur_frameInitialize() {
    Result rc = smGetService(&capssc, "caps:sc");
    if (R_FAILED(rc)) {
        log_error("[cur_frame] Failed to get caps:sc service! Result: %x", rc);
        return rc;
    }
    log_info("[cur_frame] caps:sc service initialized successfully.");
    return 0;
}

void cur_frameFinalize() {
    if (serviceIsActive(&capssc)) {
        serviceClose(&capssc);
        log_info("[cur_frame] caps:sc service closed.");
    }
}

int capture_jpeg_screenshot(char **out_b64) {
    Result rc;
    u64 jpeg_size = 0;
    size_t heap_free_before_jpeg = 0, heap_free_after_jpeg = 0;
    svcGetInfo(&heap_free_before_jpeg, 0, 0, 6); // InfoType_HeapUsage = 6
    log_info("[mem] heap free before jpeg malloc: %zu", heap_free_before_jpeg);
    void* jpeg_buf = malloc(JPEG_BUF_SIZE);
    svcGetInfo(&heap_free_after_jpeg, 0, 0, 6);
    log_info("[mem] heap free after jpeg malloc: %zu", heap_free_after_jpeg);
    if (!jpeg_buf) {
        log_error("malloc failed\n");
        return -1;
    }

    ViLayerStack layer_stack = 0; // 通常用0，或用viGetDefaultLayerStack()
    s64 timeout = 100000000; // 100ms

    const struct {
        s32 layer_stack;
        u32 pad;
        s64 timeout;
    } in = { layer_stack, 0, timeout };

    rc = serviceDispatchInOut(&capssc, 1204, in, jpeg_size,
        .buffer_attrs = { SfBufferAttr_HipcMapTransferAllowsNonSecure | SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { jpeg_buf, JPEG_BUF_SIZE } },
    );

    if (R_FAILED(rc)) {
        log_error("capsscCaptureJpegScreenShot failed: %x\n", rc);
        free(jpeg_buf);
        *out_b64 = NULL;
        return -3;
    } else {
        log_info("Screenshot captured, jpeg size: %llu\n", jpeg_size);
        *out_b64 = malloc(jpeg_size * 4 / 3 + 16 + 1); // 多分配1字节用于 null 结尾
        if (!*out_b64) {
            log_error("malloc for base64 buffer failed\n");
            free(jpeg_buf);
            return -4;
        }
        int b64_len = stb_base64_encode(jpeg_buf, jpeg_size, *out_b64);
        (*out_b64)[b64_len] = '\0'; // 保证 null 结尾
        free(jpeg_buf);
        return 0;
    }
}