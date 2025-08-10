#ifndef UI_H
#define UI_H

#include <stdio.h>
#include "computer.h"

#define VOLT_SEMITONE 0.0833333333333333 // 1 / 12
#define MUX_BY_VOLT 0.0002442002442      // 1 / 4095

// Arpeggiator direction modes (simple: UP or DOWN)
enum ArpMode
{
    ARP_UP = 0,
    ARP_DOWN = 1
};

class UI
{
public:
    void init(Computer *computer);
    int getArpLength();
    float getRootVolts();
    uint getNoteLengthMS();
    uint getChord();
    void spinRandomOuts();

    void update();
    void checkSwitch();
    uint32_t last_switch_change = 0;
    static constexpr uint32_t LED_SUPPRESS_MS = 2000; // LED suppression duration

    ArpMode getArpMode() const { return arp_mode; }
    bool consumeModeChanged()
    {
        bool v = mode_changed;
        mode_changed = false;
        return v;
    }

private:
    float root_volts = 0.0f;
    uint32_t coin_weight[2];

    uint8_t fix_length = 6;
    bool fix_length_on = false;
    ComputerSwitchState prev_switch_state;

    Computer *_computer;

    // Long-press handling for DOWN position
    ArpMode arp_mode = ARP_UP;                    // default direction
    uint32_t down_press_start = 0;                // ms when we entered DOWN
    bool down_long_consumed = false;              // true if long press already handled
    bool down_pending_short = false;              // true until we decide it's a short press
    static constexpr uint32_t LONGPRESS_MS = 800; // long-press threshold
    bool mode_changed = false;                    // set true when arp_mode toggles (long press)
};

#endif