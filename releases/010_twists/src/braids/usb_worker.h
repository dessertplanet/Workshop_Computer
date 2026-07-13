//

#ifndef BRAIDS_USB_WORKER_H_
#define BRAIDS_USB_WORKER_H_

#include "braids/settings.h"
#include "braids/midi_message.h"
#include "hardware/flash.h"

#define OPT_MAGIC 0x60
#define OPT_SHAPE_1   1
#define OPT_SHAPE_2   2
#define OPT_SHAPE_3   3
#define OPT_SHAPE_4   4
#define OPT_SHAPE_5   5
#define OPT_SHAPE_6   6
#define OPT_ADVCA     7
#define OPT_ADATTACK  8
#define OPT_ADDECAY   9
#define OPT_MIDICHANENGINE  10
#define OPT_MIDICHANOUT1    11
#define OPT_MIDICHANOUT2    12

#define CONFIG_LENGTH 13
#define SYSEX_INDEX_MANUFACTURER 1
#define SYSEX_INDEX_COMMAND 2
#define SYSEX_INDEX_LENGTH 3
#define SYSEX_MANUFACTURER_DEV 125
#define SYSEX_COMMAND_PREVIEW 1
#define SYSEX_COMMAND_WRITE_FLASH 2
#define SYSEX_COMMAND_READ 3

namespace braids {

typedef void (*midi_in_callback_t)(MIDIMessage);

class UsbWorker {
 public:

  UsbWorker() { }
  ~UsbWorker() { }

  void Init(midi_in_callback_t midiInCallback);
  void Run();

 private:
  void MidiTask();
  void ProcessSysExCommand(uint8_t *packet);
  void SetConfigFromFlash();
  int SetConfigFromSysEx(uint8_t *packet);
  void PostConfigProcessing();

  uint8_t config_[CONFIG_LENGTH];
  uint8_t packet_[32];
  uint32_t configFlashAddr_ = (PICO_FLASH_SIZE_BYTES - 4096) - (PICO_FLASH_SIZE_BYTES - 4096)%4096;
  
  midi_in_callback_t midi_in_callback;

  DISALLOW_COPY_AND_ASSIGN(UsbWorker);
};

}  // namespace braids

#endif // BRAIDS_USB_WORKER_H_
