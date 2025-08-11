// 手柄输入录制工具接口
#pragma once
#include <switch/types.h>
#include <switch/services/hiddbg.h>
#include "../third_party/cJSON.h"

// 列出工具（供 tools/list）
int list_controller_recorder(cJSON *tools);
// 调用工具（供 tools/call）
int call_controller_recorder(cJSON *content, const cJSON *arguments);
// 虚拟输入不再录制，此函数保留空实现占位
void recorder_on_update(const HiddbgHdlsState *state, bool long_press);
