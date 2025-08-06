
#include <switch/types.h>
#include "controller.h"
#include "../third_party/cJSON.h"
#include <switch/services/hid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch/services/hid.h> // 包含键盘按键定义
#include <switch/services/hiddbg.h>
#include "../util/log.h"

typedef struct {
    HiddbgHdlsState hdlState; // 当前手柄状态
    bool idle; // 是否空输入
    bool long_press; // 是否长按
} InputState;
static InputState *inputStateCurr = {0};
static InputState *inputStateNext = {0};

static int initialized = 0;
static HiddbgHdlsSessionId hdlsSessionId = {0};
static HiddbgHdlsHandle hdlHandle = {0};
static HiddbgHdlsDeviceInfo deviceInfo = {0};
static Thread hdlThread;
static const size_t HDLS_THREAD_STACK_SIZE = 0x1000;
static u8 *workmem = NULL;
static size_t workmem_size = 0x1000;

static Mutex hdlStateMutex;

void update_hdls_state(const HiddbgHdlsState *args, bool is_long_press);

int list_controller(cJSON *tools)
{
    // controller 工具
    const char *controller_tool_json =
        "{\n"
        "    \"name\": \"controller\",\n"
        "    \"annotations\": {\n"
        "        \"title\": \"switch gamepad\",\n"
        "        \"readOnlyHint\": false\n"
        "    },\n"
        "    \"description\": \"controller state: buttons, analog sticks, six axis sensor (acceleration/angle)。\",\n"
        "    \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"properties\": {\n"
        "            \"buttons\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Button bitmask，支持以下名称及对应值：\\nA=0x00000001, B=0x00000002, X=0x00000004, Y=0x00000008,\\nLSTICK=0x00000010, RSTICK=0x00000020,\\nL=0x00000040, R=0x00000080, ZL=0x00000100, ZR=0x00000200,\\nPLUS=0x00000400, MINUS=0x00000800,\\nDPAD_LEFT=0x00001000, DPAD_UP=0x00002000, DPAD_RIGHT=0x00004000, DPAD_DOWN=0x00008000,\\nHOME=0x00010000, CAPTURE=0x00020000\\n可用十六进制或十进制 bitmask 组合多个按钮。\"\n"
        "            },\n"
        "            \"analog_stick_lx\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"左摇杆x轴位置(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"analog_stick_ly\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"左摇杆y轴位置(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"analog_stick_rx\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"右摇杆x轴位置(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"analog_stick_ry\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"右摇杆y轴位置(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_accelerationx\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor acceleration x(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_accelerationy\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor acceleration y(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_accelerationz\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor acceleration z(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_anglex\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor angle x(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_angley\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor angle y(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"six_axis_sensor_anglez\": {\n"
        "                \"type\": \"number\",\n"
        "                \"description\": \"Six axis sensor angle z(-2147483648~2147483647)\"\n"
        "            },\n"
        "            \"long_press\": {\n"
        "                \"type\": \"boolean\",\n"
        "                \"description\": \"Whether the button is long pressed, long press will not auto reset the value\"\n"
        "            }\n"
        "        },\n"
        "        \"required\": []\n"
        "    }\n"
        "}";
    cJSON *tool = cJSON_Parse(controller_tool_json);
    cJSON_AddItemToArray(tools, tool);
    return 1;
}
Result controllerInitialize();
int call_controller(cJSON *content, const cJSON *arguments)
{
    if (!initialized && R_FAILED(controllerInitialize()))
    {
        log_error("initializing controller failed");
        return -1;
    }

    HiddbgHdlsState args = {0};
    int has_any = 0;

    // 按照 inputSchema 直接读取各属性
    const cJSON *buttons = cJSON_GetObjectItem(arguments, "buttons");
    if (buttons && cJSON_IsNumber(buttons)) {
        args.buttons = (u64)buttons->valuedouble;
        has_any = 1;
    }
    const cJSON *lx = cJSON_GetObjectItem(arguments, "analog_stick_lx");
    if (lx && cJSON_IsNumber(lx)) {
        args.analog_stick_l.x = (s32)lx->valuedouble;
        has_any = 1;
    }
    const cJSON *ly = cJSON_GetObjectItem(arguments, "analog_stick_ly");
    if (ly && cJSON_IsNumber(ly)) {
        args.analog_stick_l.y = (s32)ly->valuedouble;
        has_any = 1;
    }
    const cJSON *rx = cJSON_GetObjectItem(arguments, "analog_stick_rx");
    if (rx && cJSON_IsNumber(rx)) {
        args.analog_stick_r.x = (s32)rx->valuedouble;
        has_any = 1;
    }
    const cJSON *ry = cJSON_GetObjectItem(arguments, "analog_stick_ry");
    if (ry && cJSON_IsNumber(ry)) {
        args.analog_stick_r.y = (s32)ry->valuedouble;
        has_any = 1;
    }
    const cJSON *accx = cJSON_GetObjectItem(arguments, "six_axis_sensor_accelerationx");
    if (accx && cJSON_IsNumber(accx)) {
        args.six_axis_sensor_acceleration.x = (float)accx->valuedouble;
        has_any = 1;
    }
    const cJSON *accy = cJSON_GetObjectItem(arguments, "six_axis_sensor_accelerationy");
    if (accy && cJSON_IsNumber(accy)) {
        args.six_axis_sensor_acceleration.y = (float)accy->valuedouble;
        has_any = 1;
    }
    const cJSON *accz = cJSON_GetObjectItem(arguments, "six_axis_sensor_accelerationz");
    if (accz && cJSON_IsNumber(accz)) {
        args.six_axis_sensor_acceleration.z = (float)accz->valuedouble;
        has_any = 1;
    }
    const cJSON *angx = cJSON_GetObjectItem(arguments, "six_axis_sensor_anglex");
    if (angx && cJSON_IsNumber(angx)) {
        args.six_axis_sensor_angle.x = (float)angx->valuedouble;
        has_any = 1;
    }
    const cJSON *angy = cJSON_GetObjectItem(arguments, "six_axis_sensor_angley");
    if (angy && cJSON_IsNumber(angy)) {
        args.six_axis_sensor_angle.y = (float)angy->valuedouble;
        has_any = 1;
    }
    const cJSON *angz = cJSON_GetObjectItem(arguments, "six_axis_sensor_anglez");
    if (angz && cJSON_IsNumber(angz)) {
        args.six_axis_sensor_angle.z = (float)angz->valuedouble;
        has_any = 1;
    }
    bool is_long_press = false;
    const cJSON *long_press = cJSON_GetObjectItem(arguments, "long_press");
    if (long_press && cJSON_IsBool(long_press)) {
        is_long_press = (bool)long_press->valueint;
    }

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    if (has_any)
    {
        update_hdls_state(&args, is_long_press);
        char buf[256];
        snprintf(buf, sizeof(buf), "Simulated HdlsState: buttons=0x%lx, L=(%d,%d), R=(%d,%d), accel=(%.2f,%.2f,%.2f), angle=(%.2f,%.2f,%.2f)",
                 args.buttons,
                 args.analog_stick_l.x, args.analog_stick_l.y,
                 args.analog_stick_r.x, args.analog_stick_r.y,
                 args.six_axis_sensor_acceleration.x, args.six_axis_sensor_acceleration.y, args.six_axis_sensor_acceleration.z,
                 args.six_axis_sensor_angle.x, args.six_axis_sensor_angle.y, args.six_axis_sensor_angle.z);

        cJSON_AddStringToObject(item, "text", buf);
        cJSON_AddItemToArray(content, item);
        return 0;
    }
    else
    {
        cJSON_AddStringToObject(item, "text", "No valid HdlsState input fields");
        cJSON_AddItemToArray(content, item);
        return 1;
    }
}

void hdls_state_thread(void *arg)
{
    bool isAttached = false, idle = true;
    InputState *temp = NULL;
    while (1)
    {
        hiddbgIsHdlsVirtualDeviceAttached(hdlsSessionId, hdlHandle, &isAttached);
        if (!isAttached) {
            log_info("Attempting to attach HDLS virtual device...\n");
            Result res = hiddbgAttachHdlsVirtualDevice(&hdlHandle, &deviceInfo);
            if (R_FAILED(res)) {
                log_error("Failed to attach HDLS virtual device: %d\n", res);
                svcSleepThread(1000 * 1000000ULL); // 1000ms
                continue;
            }
        }
        Result res = hiddbgSetHdlsState(hdlHandle, (HiddbgHdlsState *)&inputStateCurr->hdlState);
        isAttached = R_FAILED(res) ? false : true;
        idle = inputStateCurr->idle;
        mutexLock(&hdlStateMutex);
        if (inputStateCurr->long_press && inputStateNext->idle) {
            // 长按且没有新输入，保持当前状态，不切换
            // 什么都不做
        } else {
            // 非长按 或 有新输入，重置并切换
            inputStateCurr->hdlState = (HiddbgHdlsState){0};
            inputStateCurr->hdlState.battery_level = 4;
            inputStateCurr->idle = true;
            temp = inputStateCurr;
            inputStateCurr = inputStateNext;
            inputStateNext = temp;
        }
        mutexUnlock(&hdlStateMutex);
        svcSleepThread(idle ? 16000000ULL : 50 * 1000000ULL); // 16ms 有效输入时按下50ms
    }
}

Result controllerInitialize()
{
    memset(&hdlHandle, 0, sizeof(hdlHandle));
    memset(&deviceInfo, 0, sizeof(deviceInfo));

    inputStateCurr = (InputState*)malloc(sizeof(InputState));
    inputStateNext = (InputState*)malloc(sizeof(InputState));
    log_info("memory allocated for input states");

    memset(inputStateCurr, 0, sizeof(InputState));
    memset(inputStateNext, 0, sizeof(InputState));

    workmem = aligned_alloc(0x1000, workmem_size);
    if (!workmem)
    {
        log_error("Failed to allocate workmem_size\n");
        return -1;
    }

    Result workRes = hiddbgAttachHdlsWorkBuffer(&hdlsSessionId, workmem, workmem_size);
    if (R_FAILED(workRes))
    {
        log_error("Failed to attach HdlsWorkBuffer: %d\n", workRes);
        free(workmem);
        workmem = NULL;
        return -2;
    }

    // deviceInfo.deviceType = HidDeviceType_FullKey15; // Pro Controller
    deviceInfo.deviceType = HidDeviceType_FullKey3;
    deviceInfo.npadInterfaceType = HidNpadInterfaceType_Bluetooth;
    deviceInfo.singleColorBody = 0xFFFFFFFF;
    deviceInfo.singleColorButtons = 0x0000FF; // #0000FF
    deviceInfo.colorLeftGrip = 0x0038A8; // #0038A8
    deviceInfo.colorRightGrip = 0xE00034; // #E00034

    inputStateCurr->hdlState.battery_level = 4;
    inputStateNext->hdlState.battery_level = 4;
    inputStateCurr->idle = true;
    inputStateNext->idle = true;

    Result res = threadCreate(&hdlThread, hdls_state_thread, NULL, NULL, HDLS_THREAD_STACK_SIZE, 49, -2);
    if (R_FAILED(res))
    {
        log_error("Failed to create HDLS thread: %d\n", res);
        free(workmem);
        workmem = NULL;
        return -3;
    }
    res = threadStart(&hdlThread);
    if (R_FAILED(res))
    {
        log_error("Failed to start HDLS thread: %d\n", res);
        threadClose(&hdlThread);
        return -4;
    }
    initialized = 1;
    log_info("Controller initialized successfully");
    return 0;
}

void controllerFinalize()
{
    if (hdlHandle.handle)
    {
        hiddbgDetachHdlsVirtualDevice(hdlHandle);
        hdlHandle = (HiddbgHdlsHandle){0};
    }
    if (hdlsSessionId.id)
    {
        hiddbgReleaseHdlsWorkBuffer(hdlsSessionId);
        hdlsSessionId = (HiddbgHdlsSessionId){0};
    }
    if (workmem)
    {
        free(workmem);
        workmem = NULL;
    }
    if (hdlThread.handle)
    {
        threadClose(&hdlThread);
    }
    initialized = 0;
}

void update_hdls_state(const HiddbgHdlsState *args, bool is_long_press)
{
    // 设置手柄操作参数
    if (args)
    {
        mutexLock(&hdlStateMutex);
        inputStateNext->hdlState.buttons = args->buttons;
        inputStateNext->hdlState.analog_stick_l = args->analog_stick_l;
        inputStateNext->hdlState.analog_stick_r = args->analog_stick_r;
        inputStateNext->hdlState.six_axis_sensor_acceleration = args->six_axis_sensor_acceleration;
        inputStateNext->hdlState.six_axis_sensor_angle = args->six_axis_sensor_angle;
        inputStateNext->idle = false; // 设置为有效输入状态
        inputStateNext->long_press = is_long_press;
        mutexUnlock(&hdlStateMutex);
    }
}