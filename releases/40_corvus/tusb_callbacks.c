/*
 * TinyUSB Device Callbacks
 * Required for proper USB device enumeration
 */

#include "tusb.h"
#include <stdio.h>

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    printf("USB Device mounted\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    printf("USB Device unmounted\n");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    printf("USB Device suspended\n");
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    printf("USB Device resumed\n");
}

//--------------------------------------------------------------------+
// CDC callbacks
//--------------------------------------------------------------------+

// Invoked when CDC line state changed e.g. connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void) itf;
    (void) rts;
    
    if (dtr) {
        // Terminal connected
        printf("CDC DTR asserted - terminal connected\n");
    } else {
        // Terminal disconnected
        printf("CDC DTR deasserted - terminal disconnected\n");
    }
}

// Invoked when CDC line coding is changed via SET_LINE_CODING
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
    (void) itf;
    printf("CDC line coding: %lu baud, %u stop bits, %u parity, %u data bits\n",
           p_line_coding->bit_rate, 
           p_line_coding->stop_bits,
           p_line_coding->parity,
           p_line_coding->data_bits);
}

// Invoked when received new data
void tud_cdc_rx_cb(uint8_t itf)
{
    (void) itf;
    // Data reception is handled in the main crow emulator loop
    // This callback is just for notification
}
