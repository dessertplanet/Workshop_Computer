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

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN           __attribute__ ((aligned(4)))

// Host only. No class drivers: the PICOBOOT interface is driven directly
// through the endpoint API, as in TinyUSB's bare_api example.
#define CFG_TUH_ENABLED             1
#define BOARD_TUH_RHPORT            0
#define CFG_TUH_MAX_SPEED           OPT_MODE_DEFAULT_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_ENDPOINT_MAX        4
#define CFG_TUH_API_EDPT_XFER       1

#ifdef __cplusplus
}
#endif

#endif
