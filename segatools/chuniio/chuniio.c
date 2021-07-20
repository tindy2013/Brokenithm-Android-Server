#include <windows.h>

#include <process.h>
#include <stdbool.h>
#include <stdint.h>

#include "chuniio/chuniio.h"
#include "chuniio/config.h"

static unsigned int __stdcall chuni_io_slider_thread_proc(void *ctx);

static bool chuni_io_coin;
static uint16_t chuni_io_coins;
static uint8_t chuni_io_hand_pos;
static HANDLE chuni_io_slider_thread;
static bool chuni_io_slider_stop_flag;
static struct chuni_io_config chuni_io_cfg;

struct chuni_io_ipc_memory_info
{
    uint8_t airIoStatus[6];
    uint8_t sliderIoStatus[32];
    uint8_t ledRgbData[32 * 3];
    uint8_t testBtn;
    uint8_t serviceBtn;
    uint8_t coinInsertion;
    uint8_t cardRead;
    uint8_t remoteCardRead;
    uint8_t remoteCardType;
    uint8_t remoteCardId[10];
};
typedef struct chuni_io_ipc_memory_info chuni_io_ipc_memory_info;
static HANDLE chuni_io_file_mapping_handle;
chuni_io_ipc_memory_info* chuni_io_file_mapping;

void chuni_io_init_shared_memory()
{
    if (chuni_io_file_mapping)
    {
        return;
    }
    if ((chuni_io_file_mapping_handle = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, sizeof(chuni_io_ipc_memory_info), "Local\\BROKENITHM_SHARED_BUFFER")) == 0)
    {
        return;
    }

    if ((chuni_io_file_mapping = (chuni_io_ipc_memory_info*)MapViewOfFile(chuni_io_file_mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(chuni_io_ipc_memory_info))) == 0)
    {
        return;
    }

    memset(chuni_io_file_mapping, 0, sizeof(chuni_io_ipc_memory_info));
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
}

uint16_t chuni_io_get_api_version(void)
{
    return 0x0101;
}

HRESULT chuni_io_jvs_init(void)
{
    chuni_io_config_load(&chuni_io_cfg, L".\\segatools.ini");

    chuni_io_init_shared_memory();

    return S_OK;
}

void chuni_io_jvs_read_coin_counter(uint16_t *out)
{
    if (out == NULL) {
        return;
    }

    if (chuni_io_file_mapping && chuni_io_file_mapping->coinInsertion) {
        chuni_io_coins++;
        chuni_io_file_mapping->coinInsertion = 0;
    } else {
        if (GetAsyncKeyState(chuni_io_cfg.vk_coin)) {
            if (!chuni_io_coin) {
                chuni_io_coin = true;
                chuni_io_coins++;
            }
        } else {
            chuni_io_coin = false;
        }
    }

    *out = chuni_io_coins;
}

void chuni_io_jvs_poll(uint8_t *opbtn, uint8_t *beams)
{
    size_t i;

    if ((chuni_io_file_mapping && chuni_io_file_mapping->testBtn) || GetAsyncKeyState(chuni_io_cfg.vk_test)) {
        *opbtn |= 0x01; /* Test */
    }

    if ((chuni_io_file_mapping && chuni_io_file_mapping->serviceBtn) || GetAsyncKeyState(chuni_io_cfg.vk_service)) {
        *opbtn |= 0x02; /* Service */
    }

    if (GetAsyncKeyState(chuni_io_cfg.vk_ir)) {
        if (chuni_io_hand_pos < 6) {
            chuni_io_hand_pos++;
        }
    } else {
        if (chuni_io_hand_pos > 0) {
            chuni_io_hand_pos--;
        }
    }

    for (i = 0 ; i < 6 ; i++) {
        if ((chuni_io_file_mapping && chuni_io_file_mapping->airIoStatus[i]) || chuni_io_hand_pos > i) {
            *beams |= (1 << i);
        }
    }
}

HRESULT chuni_io_slider_init(void)
{
    return S_OK;
}

void chuni_io_slider_start(chuni_io_slider_callback_t callback)
{
    if (chuni_io_slider_thread != NULL) {
        return;
    }

    chuni_io_slider_thread = (HANDLE) _beginthreadex(
            NULL,
            0,
            chuni_io_slider_thread_proc,
            callback,
            0,
            NULL);
}

void chuni_io_slider_stop(void)
{
    if (chuni_io_slider_thread == NULL) {
        return;
    }

    chuni_io_slider_stop_flag = true;

    WaitForSingleObject(chuni_io_slider_thread, INFINITE);
    CloseHandle(chuni_io_slider_thread);
    chuni_io_slider_thread = NULL;
    chuni_io_slider_stop_flag = false;
}

void chuni_io_slider_set_leds(const uint8_t *rgb)
{
	if (chuni_io_file_mapping) {
        memcpy(chuni_io_file_mapping->ledRgbData, rgb, 32 * 3);
    }
}

static unsigned int __stdcall chuni_io_slider_thread_proc(void *ctx)
{
    chuni_io_slider_callback_t callback;
    uint8_t pressure[32];

    callback = ctx;

    while (!chuni_io_slider_stop_flag) {
        if (chuni_io_file_mapping) {
            memcpy(pressure, chuni_io_file_mapping->sliderIoStatus, 32);
        }

        callback(pressure);
        Sleep(1);
    }

    return 0;
}