// Minimal USB CDC test - temporary debugging version
#include "pico/stdlib.h"
#include <cstdio>
#include "tusb.h"

extern "C" void usb_serial_init(void);

// Simple USB enumeration test
int main()
{
    stdio_init_all();
    
    printf("=== MINIMAL USB CDC TEST ===\n");
    printf("Starting USB enumeration test...\n");
    
    // Initialize USB serial
    usb_serial_init();
    printf("USB serial init complete\n");
    
    // Initialize TinyUSB
    tusb_init();
    printf("TinyUSB init complete\n");
    
    // Service USB for enumeration
    printf("Servicing USB task for enumeration...\n");
    for (int i = 0; i < 2000; i++) {
        tud_task();
        sleep_ms(1);
        
        if (i % 200 == 0) {
            printf("USB task loop %d/2000\n", i);
            
            // Check if device is mounted
            if (tud_mounted()) {
                printf("*** USB DEVICE MOUNTED! ***\n");
            }
            
            // Check if CDC is connected
            if (tud_cdc_connected()) {
                printf("*** CDC CONNECTED! ***\n");
            }
        }
    }
    
    printf("USB enumeration test complete\n");
    printf("Final status - Mounted: %s, CDC Connected: %s\n", 
           tud_mounted() ? "YES" : "NO",
           tud_cdc_connected() ? "YES" : "NO");
    
    // Simple echo loop for testing
    printf("Starting simple echo loop...\n");
    while (true) {
        tud_task();
        
        if (tud_cdc_available()) {
            char buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            if (count > 0) {
                printf("Received %lu bytes via CDC\n", count);
                // Echo back
                tud_cdc_write(buf, count);
                tud_cdc_write_flush();
            }
        }
        
        sleep_ms(1);
    }
    
    return 0;
}

// Required TinyUSB device task hook
void tud_task_hook(void)
{
    // TinyUSB handles this automatically
}
