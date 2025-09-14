#include "pico/stdlib.h"
#include <cstdio>
#include "tusb.h"
#include "crow_emulator.h"

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
    
    // Initialize TinyUSB
    tusb_init();
    
    printf("Workshop Computer Crow Emulator\n");
    printf("Initializing...\n");
    
    CrowEmulator crow_emu;
    
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
