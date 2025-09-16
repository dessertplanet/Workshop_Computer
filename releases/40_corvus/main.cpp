#include "pico/stdlib.h"
#include <cstdio>
#include "tusb.h"
#include "crow_emulator.h"

extern "C" void usb_serial_init(void);

/*
Crow Emulator for Workshop Computer

This program emulates the monome crow module using the ComputerCard framework.
It provides drop-in compatibility with existing crow scripts and norns integration.

Hardware mapping:
- Workshop Audio In 1/2 → Crow Input 1/2
- Workshop Audio Out 1/2 → Crow Output 1/2  
- Workshop CV Out 1/2 → Crow Output 3/4

USB Communication:
- Appears as USB CDC serial device (like real crow)
- Supports crow command protocol (^^v, ^^i, etc.)
- Multicore processing: Core 0 = audio, Core 1 = USB/serial
*/

int main()
{
    stdio_init_all();

    printf("Workshop Computer Crow Emulator\n");
    printf("Initializing...\n");
    
    // Initialize the emulator first (this launches Core 1)
    CrowEmulator crow_emu;
    
    // Allow Core 1 to start up before initializing USB
    sleep_ms(10);
    
    // Initialize USB after Core 1 is ready
    usb_serial_init(); // populate dynamic serial before TinyUSB enumeration
    tusb_init();
    
    // Give USB time to enumerate before starting audio processing
    printf("Waiting for USB enumeration...\n");
    
    // Service USB task during enumeration period to ensure device is detected
    for (int i = 0; i < 1000; i++) {
        tud_task();
        sleep_ms(1);
        if (i % 100 == 0) {
            printf("USB enumeration... %d%%\n", i / 10);
        }
    }
    
    printf("Starting crow emulation...\n");
    crow_emu.RunCrowEmulator(); // This calls ProcessSample at 48kHz and never returns
}

// TinyUSB device task - required for USB processing
// This will be called by the TinyUSB device stack periodically
void tud_task_hook(void)
{
    // TinyUSB device task is handled automatically by the stack
    // No additional processing needed here
}
