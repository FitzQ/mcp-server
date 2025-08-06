// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/log.h"
#include "tools/cur_frame.h"
#include "tools/controller.h"
#include "transport/streamable_http.h"

// Include the main libnx system header, for Switch development
#include <switch.h>

#define R_ASSERT(res_expr)            \
    ({                                \
        const Result rc = (res_expr); \
        if (R_FAILED(rc))             \
        {                             \
            fatalThrow(rc);           \
        }                             \
    })

#include <stdarg.h>

// Size of the inner heap (adjust as necessary).
// #define HEAP_SIZE 0xA7000
#define HEAP_SIZE 0x200000 // 2MB heap size

#ifdef __cplusplus
extern "C" {
#endif

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;

// Sysmodules will normally only want to use one FS session.
u32 __nx_fs_num_sessions = 1;

// setup a fake heap
char fake_heap[HEAP_SIZE];

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void)
{
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = fake_heap;
    fake_heap_end   = fake_heap + HEAP_SIZE;
}

// Service initialization.
void __appInit(void)
{
    R_ASSERT(smInitialize());
    log_info("smInitialize success");
    R_ASSERT(fsInitialize());
    log_info("fsInitialize success");
    R_ASSERT(fsdevMountSdmc());
    log_info("fsdevMountSdmc success");
    R_ASSERT(timeInitialize());
    R_ASSERT(hiddbgInitialize());
    R_ASSERT(hidInitialize());
    log_info("hidInitialize success");
    R_ASSERT(hidsysInitialize());
    R_ASSERT(setsysInitialize());
    log_info("setsysInitialize success");
    R_ASSERT(cur_frameInitialize());
    log_info("cur_frameInitialize success");

    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
        hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    setsysExit();

    static const SocketInitConfig socketInitConfig = {
        .tcp_tx_buf_size = 0x800,
        .tcp_rx_buf_size = 0x800,
        .tcp_tx_buf_max_size = 0x25000,
        .tcp_rx_buf_max_size = 0x25000,

        //We don't use UDP, set all UDP buffers to 0
        .udp_tx_buf_size = 0,
        .udp_rx_buf_size = 0,

        .sb_efficiency = 1,
    };
    R_ASSERT(socketInitialize(&socketInitConfig));
    log_info("socketInitializeDefault success");
    smExit();
    log_info("smExit called");
}

// Service deinitialization.
void __appExit(void)
{
    log_warning("__appExit called1");
    fsdevUnmountAll();
    fsExit();
    hidExit();
    hidsysExit();
    socketExit();
    timeExit();
    hiddbgExit();
    cur_frameFinalize();
    controllerFinalize();
    log_warning("__appExit called2");
}

#ifdef __cplusplus
}
#endif

// Main program entrypoint

// Forward declaration of the thread function
static void loop() {
    while (1) {
        svcSleepThread(10000000ULL);
    }
}

int main(int argc, char* argv[])
{
    R_ASSERT(streamable_http_init());
    log_info("streamable_http_init called");
    loop();
    return 0;
}
