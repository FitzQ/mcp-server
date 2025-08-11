#include "controller_recorder.h"
#include <switch.h>
#include <switch/services/hid.h>
#include <switch/runtime/devices/console.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for snprintf
#include "../util/log.h"

typedef struct {
    u64 tick;                 // 系统 tick 时间戳
    HiddbgHdlsState state;    // 真实手柄状态（复用 HdlsState 结构存）
    bool long_press;          // 恒为 false（兼容旧字段）
} RecordedEvent;

static RecordedEvent *g_events = NULL;
static size_t g_capacity = 0;
static size_t g_count = 0;
static bool g_recording = false;     // 正在录制（仅真实手柄）
static Mutex g_recorderMutex;        // 互斥
static Thread g_real_thread;         // 真实手柄采集线程
static HiddbgHdlsState g_last_real_state = {0}; // 上次已记录的真实输入状态

static void ensure_capacity(size_t want) {
    if (want <= g_capacity) return;
    size_t new_cap = g_capacity ? g_capacity : 256;
    while (new_cap < want) new_cap *= 2;
    RecordedEvent *n = (RecordedEvent*)realloc(g_events, new_cap * sizeof(RecordedEvent));
    if (!n) {
        log_error("[recorder] realloc failed, dropping events");
        return; // 不扩容，后续事件将被忽略
    }
    g_events = n;
    g_capacity = new_cap;
    log_info("[recorder] capacity -> %zu", g_capacity);
}

int list_controller_recorder(cJSON *tools) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", "controller_recorder");
    cJSON_AddStringToObject(tool, "title", "controller_recorder");
    cJSON_AddStringToObject(tool, "description", "record REAL controller inputs only (start/stop/dump/clear)");

    cJSON *inputSchema = cJSON_CreateObject();
    cJSON_AddStringToObject(inputSchema, "type", "object");
    cJSON *properties = cJSON_CreateObject();

    cJSON *action = cJSON_CreateObject();
    cJSON_AddStringToObject(action, "type", "string");
    cJSON *enumArr = cJSON_CreateArray();
    cJSON_AddItemToArray(enumArr, cJSON_CreateString("start"));
    cJSON_AddItemToArray(enumArr, cJSON_CreateString("stop"));
    cJSON_AddItemToArray(enumArr, cJSON_CreateString("dump"));
    cJSON_AddItemToArray(enumArr, cJSON_CreateString("clear"));
    cJSON_AddItemToArray(enumArr, cJSON_CreateString("save"));
    cJSON_AddItemToObject(action, "enum", enumArr);
    cJSON_AddStringToObject(action, "description", "recorder action");
    cJSON_AddItemToObject(properties, "action", action);

    cJSON *maxEvents = cJSON_CreateObject();
    cJSON_AddStringToObject(maxEvents, "type", "number");
    cJSON_AddStringToObject(maxEvents, "description", "(optional) preallocate events capacity when start");
    cJSON_AddItemToObject(properties, "max_events", maxEvents);

    // 不再支持虚拟录制，固定真实手柄

    cJSON_AddItemToObject(inputSchema, "properties", properties);
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("action"));
    cJSON_AddItemToObject(inputSchema, "required", required);
    cJSON_AddItemToObject(tool, "inputSchema", inputSchema);
    cJSON_AddItemToArray(tools, tool);
    return 0;
}

void recorder_on_update(const HiddbgHdlsState *state, bool long_press) { (void)state; (void)long_press; }

static bool real_state_changed(const HiddbgHdlsState *a, const HiddbgHdlsState *b) {
    if (a->buttons != b->buttons) return true;
    // 模拟摇杆微抖动过滤阈值
    const int TH = 200; // 约 0.6% full range (32767)
    if (abs(a->analog_stick_l.x - b->analog_stick_l.x) > TH) return true;
    if (abs(a->analog_stick_l.y - b->analog_stick_l.y) > TH) return true;
    if (abs(a->analog_stick_r.x - b->analog_stick_r.x) > TH) return true;
    if (abs(a->analog_stick_r.y - b->analog_stick_r.y) > TH) return true;
    return false;
}

static u64 buttons_from_real(u64 keys) {
    u64 btn = 0;
    if (keys & HidNpadButton_A) btn |= HidNpadButton_A;
    if (keys & HidNpadButton_B) btn |= HidNpadButton_B;
    if (keys & HidNpadButton_X) btn |= HidNpadButton_X;
    if (keys & HidNpadButton_Y) btn |= HidNpadButton_Y;
    if (keys & HidNpadButton_StickL) btn |= HidNpadButton_StickL;
    if (keys & HidNpadButton_StickR) btn |= HidNpadButton_StickR;
    if (keys & HidNpadButton_L) btn |= HidNpadButton_L;
    if (keys & HidNpadButton_R) btn |= HidNpadButton_R;
    if (keys & HidNpadButton_ZL) btn |= HidNpadButton_ZL;
    if (keys & HidNpadButton_ZR) btn |= HidNpadButton_ZR;
    if (keys & HidNpadButton_Plus) btn |= HidNpadButton_Plus;
    if (keys & HidNpadButton_Minus) btn |= HidNpadButton_Minus;
    if (keys & HidNpadButton_Left) btn |= HidNpadButton_Left;
    if (keys & HidNpadButton_Right) btn |= HidNpadButton_Right;
    if (keys & HidNpadButton_Up) btn |= HidNpadButton_Up;
    if (keys & HidNpadButton_Down) btn |= HidNpadButton_Down;
    if (keys & HiddbgNpadButton_Home) btn |= HiddbgNpadButton_Home; // 若真实读取不到 Home/Capture 可忽略
    if (keys & HiddbgNpadButton_Capture) btn |= HiddbgNpadButton_Capture;
    return btn;
}

// 使用 libnx pad 封装读取（兼容当前头文件无 hidScanInput/hidKeysHeld 情况）
static bool read_current_real(HiddbgHdlsState *out) {
    static bool s_init = false;
    static PadState s_pad; // 仅使用 No1 + Handheld (padInitializeDefault)
    if (!s_init) {
        padConfigureInput(1, HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight);
        padInitializeDefault(&s_pad);
        s_init = true;
    }
    padUpdate(&s_pad);
    if (!padIsConnected(&s_pad)) return false;
    u64 btns = padGetButtons(&s_pad);
    HidAnalogStickState ls = padGetStickPos(&s_pad, 0);
    HidAnalogStickState rs = padGetStickPos(&s_pad, 1);
    memset(out, 0, sizeof(*out));
    out->buttons = buttons_from_real(btns);
    out->analog_stick_l.x = ls.x; out->analog_stick_l.y = ls.y;
    out->analog_stick_r.x = rs.x; out->analog_stick_r.y = rs.y;
    out->battery_level = 4;
    return true;
}

static void real_poll_thread(void *arg) {
    (void)arg;
    memset(&g_last_real_state, 0, sizeof(g_last_real_state));
    while (g_recording) {
        HiddbgHdlsState cur = {0};
        bool ok = read_current_real(&cur);
        if (ok && real_state_changed(&cur, &g_last_real_state)) {
            mutexLock(&g_recorderMutex);
            ensure_capacity(g_count + 1);
            if (g_count < g_capacity) {
                RecordedEvent *e = &g_events[g_count++];
                e->tick = svcGetSystemTick();
                e->state = cur;
                e->long_press = false;
            }
            g_last_real_state = cur;
            mutexUnlock(&g_recorderMutex);
        }
        svcSleepThread(16 * 1000000ULL); // ~60Hz 采样
    }
    threadExit();
}

static void start_recording(size_t prealloc) {
    mutexLock(&g_recorderMutex);
    if (g_recording) { // 若已在录制
        mutexUnlock(&g_recorderMutex);
        log_warning("[recorder] already recording; stop first");
        return;
    }
    if (prealloc && prealloc > g_capacity) {
        RecordedEvent *n = (RecordedEvent*)realloc(g_events, prealloc * sizeof(RecordedEvent));
        if (n) { g_events = n; g_capacity = prealloc; }
    }
    g_count = 0;
    g_recording = true;
    memset(&g_last_real_state, 0, sizeof(g_last_real_state));
    mutexUnlock(&g_recorderMutex);

    Result r = threadCreate(&g_real_thread, real_poll_thread, NULL, NULL, 0x4000, 49, -2);
    if (R_FAILED(r)) {
        log_error("[recorder] real poll thread create failed %x", r);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    if (R_FAILED(threadStart(&g_real_thread))) {
        log_error("[recorder] real poll thread start failed");
        threadClose(&g_real_thread);
        mutexLock(&g_recorderMutex); g_recording = false; mutexUnlock(&g_recorderMutex);
        return;
    }
    log_info("[recorder] start (capacity=%zu, source=real)", g_capacity);
}

static void stop_recording() {
    mutexLock(&g_recorderMutex);
    g_recording = false;
    mutexUnlock(&g_recorderMutex);
    if (g_real_thread.handle) {
        threadWaitForExit(&g_real_thread);
        threadClose(&g_real_thread);
        g_real_thread.handle = 0;
    }
    log_info("[recorder] stop (events=%zu)", g_count);
}

static void clear_events() {
    mutexLock(&g_recorderMutex);
    g_count = 0;
    mutexUnlock(&g_recorderMutex);
    log_info("[recorder] cleared");
}

static cJSON *events_to_json() {
    cJSON *arr = cJSON_CreateArray();
    mutexLock(&g_recorderMutex);
    for (size_t i = 0; i < g_count; ++i) {
        RecordedEvent *e = &g_events[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "tick", (double)e->tick);
    cJSON_AddNumberToObject(obj, "buttons", (double)e->state.buttons);
        cJSON_AddNumberToObject(obj, "lx", e->state.analog_stick_l.x);
        cJSON_AddNumberToObject(obj, "ly", e->state.analog_stick_l.y);
        cJSON_AddNumberToObject(obj, "rx", e->state.analog_stick_r.x);
        cJSON_AddNumberToObject(obj, "ry", e->state.analog_stick_r.y);
    // from_real 字段已移除（固定真实输入）
        cJSON_AddNumberToObject(obj, "accel_x", e->state.six_axis_sensor_acceleration.x);
        cJSON_AddNumberToObject(obj, "accel_y", e->state.six_axis_sensor_acceleration.y);
        cJSON_AddNumberToObject(obj, "accel_z", e->state.six_axis_sensor_acceleration.z);
        cJSON_AddNumberToObject(obj, "angle_x", e->state.six_axis_sensor_angle.x);
        cJSON_AddNumberToObject(obj, "angle_y", e->state.six_axis_sensor_angle.y);
        cJSON_AddNumberToObject(obj, "angle_z", e->state.six_axis_sensor_angle.z);
        cJSON_AddBoolToObject(obj, "long_press", e->long_press);
        cJSON_AddItemToArray(arr, obj);
    }
    mutexUnlock(&g_recorderMutex);
    return arr;
}

static bool save_events_to_file(char *out_path_buf, size_t out_size) {
    if (g_count == 0) return false;
    // 取第一条或最后一条 tick 作为文件名基准，这里用第一条
    u64 base_tick = g_events[0].tick;
    char path[256];
    // Switch 上 / 实际映射 sd 根，确保目录存在（简单方式：mkdir 但这里假设 /switch/mcp-server 已被创建，若没创建尝试创建）
    // 使用 standard C I/O
    snprintf(path, sizeof(path), "/switch/mcp-server/input_%llu.json", (unsigned long long)base_tick);
    cJSON *arr = events_to_json();
    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json_str) return false;
    FILE *f = fopen(path, "wb");
    if (!f) { free(json_str); return false; }
    size_t len = strlen(json_str);
    bool ok = fwrite(json_str, 1, len, f) == len;
    fclose(f);
    free(json_str);
    if (ok && out_path_buf && out_size) {
        strncpy(out_path_buf, path, out_size - 1);
        out_path_buf[out_size - 1] = '\0';
    }
    return ok;
}

int call_controller_recorder(cJSON *content, const cJSON *arguments) {
    const cJSON *action = cJSON_GetObjectItem(arguments, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "text");
        cJSON_AddStringToObject(item, "text", "missing action");
        cJSON_AddItemToArray(content, item);
        return 1;
    }
    const char *act = action->valuestring;
    int isError = 0;
    if (strcmp(act, "start") == 0) {
        size_t prealloc = 0;
        const cJSON *me = cJSON_GetObjectItem(arguments, "max_events");
        if (me && cJSON_IsNumber(me)) prealloc = (size_t)me->valuedouble;
        start_recording(prealloc);
    } else if (strcmp(act, "stop") == 0) {
        stop_recording();
    } else if (strcmp(act, "clear") == 0) {
        clear_events();
    } else if (strcmp(act, "dump") == 0) {
        // produce json item
        cJSON *jsonItem = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonItem, "type", "text");
        // 将事件数组序列化成字符串
        cJSON *arr = events_to_json();
        char *arr_str = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);
        if (arr_str) {
            cJSON_AddStringToObject(jsonItem, "text", arr_str);
            free(arr_str);
        } else {
            cJSON_AddStringToObject(jsonItem, "text", "[]");
        }
        cJSON_AddItemToArray(content, jsonItem);
        return 0;
    } else if (strcmp(act, "save") == 0) {
        char saved_path[256] = {0};
        bool ok = false;
        mutexLock(&g_recorderMutex); // 保护 g_events 在序列化期间不变化
        ok = save_events_to_file(saved_path, sizeof(saved_path));
        mutexUnlock(&g_recorderMutex);
        cJSON *item2 = cJSON_CreateObject();
        cJSON_AddStringToObject(item2, "type", "text");
        if (ok) {
            cJSON_AddStringToObject(item2, "text", saved_path);
        } else {
            cJSON_AddStringToObject(item2, "text", "save_failed_or_no_events");
        }
        cJSON_AddItemToArray(content, item2);
        return ok ? 0 : 1;
    } else {
        isError = 1;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    if (!isError) {
        char buf[128];
        mutexLock(&g_recorderMutex);
    int rec = g_recording ? 1 : 0; size_t cnt = g_count; size_t cap = g_capacity;
        mutexUnlock(&g_recorderMutex);
    snprintf(buf, sizeof(buf), "action=%s ok recording=%d source=real count=%zu capacity=%zu", act, rec, cnt, cap);
        cJSON_AddStringToObject(item, "text", buf);
    } else {
        cJSON_AddStringToObject(item, "text", "unknown action");
    }
    cJSON_AddItemToArray(content, item);
    return isError;
}
