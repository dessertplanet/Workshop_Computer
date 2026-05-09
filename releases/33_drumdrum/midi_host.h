#pragma once

// Minimal class-compliant USB MIDI host for Music Thing 8mu support.
//
// Pico SDK 2.2.0 ships TinyUSB 0.18, which does not include a MIDI host
// class driver — only a descriptor-parser hint enabled via CFG_TUH_MIDI.
// We register our own driver via TinyUSB's usbh_app_driver_get_cb()
// extension point and read 32-bit USB-MIDI Event Packets directly off
// the bulk-IN endpoint. CC messages are dispatched into gState.
//
// CC mapping (channel-agnostic, edge-detected on buttons):
//   34..41  faders         step pitches (or velocities when edit mode = 1)
//   50..57  faders         step velocities (always)
//   28      fader           edit cursor (0..7)
//   22      button (press)  toggle pitch ↔ velocity edit mode
//   23      button (press)  toggle play/pause
//   24      button (press)  reset to step 1
//   all others              ignored

#ifdef __cplusplus
extern "C" {
#endif

void midi_host_init(void);

#ifdef __cplusplus
}
#endif
