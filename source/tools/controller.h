#include "../third_party/cJSON.h"
#include <switch/services/hid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <switch/services/hid.h> // 包含键盘按键定义
#include <switch/services/hiddbg.h>
#include "../util/log.h"
#include <switch/types.h>
void controllerFinalize();
int list_controller(cJSON *tools);
int call_controller(cJSON *content, const cJSON *arguments);