#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))

// The role is chosen at boot (main.cpp): device, to load a payload from a
// computer, or host, to flash a target in BOOTSEL. Both stacks are built.

//--------------------------------------------------------------------
// Device: one mass storage drive
#define CFG_TUD_ENABLED             1
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_MSC                 1
#define CFG_TUD_CDC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0
// One 512-byte sector per write callback, which is one UF2 block
#define CFG_TUD_MSC_EP_BUFSIZE      512

//--------------------------------------------------------------------
// Host: PICOBOOT through the endpoint API, no class drivers
#define CFG_TUH_ENABLED             1
#define BOARD_TUH_RHPORT            0
#define BOARD_TUD_RHPORT            0
#define CFG_TUH_MAX_SPEED           OPT_MODE_DEFAULT_SPEED
#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN           __attribute__ ((aligned(4)))
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_ENDPOINT_MAX        4
#define CFG_TUH_API_EDPT_XFER       1

#ifdef __cplusplus
}
#endif

#endif
