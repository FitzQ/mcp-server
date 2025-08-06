#include "../third_party/cJSON.h"
#include <switch/types.h> // for u8, u32


int list_cur_frame(cJSON *tools);

int call_cur_frame(cJSON *contents);

Result cur_frameInitialize();
void cur_frameFinalize();