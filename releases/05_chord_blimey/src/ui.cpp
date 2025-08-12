#include "ui.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include <math.h>

void UI::init(Computer *computer)
{
    _computer = computer;

    coin_weight[0] = UINT32_MAX;
    coin_weight[1] = UINT32_MAX;

    // Initialize prev state to current to avoid spurious edge handling on boot
    prev_switch_state = _computer->getSwitchState();
}

int UI::getArpLength()
{
    return fix_length_on ? fix_length : -1;
}

float UI::getRootVolts()
{
    return root_volts;
}

uint UI::getNoteLengthMS()
{
    // invert knob value and curve it so we have more control over the faster end
    return pow(2, (4095 - _computer->getPotZValue()) / 300) + 20;
}

uint UI::getChord()
{
    float knoby_volts = _computer->getPotYValue() * MUX_BY_VOLT;
    float cv2_volts = _computer->getCVInVolts(2);

    float total_volts = cv2_volts + knoby_volts;

    if (total_volts > 1.0f)
    {
        return 11;
    }
    else if (total_volts < 0.0f)
    {
        return 0;
    }
    else
    {
        return floor(total_volts / VOLT_SEMITONE);
    }
}

void UI::update()
{
    float knobx_volts = _computer->getPotXValue() * MUX_BY_VOLT;
    root_volts = _computer->getCVInVolts(1) + knobx_volts;
}

void UI::checkSwitch()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Clear LED hold when the time has elapsed
    if (led_hold_active && (int32_t)(now - led_hold_until) >= 0)
    {
        led_hold_active = false;
    }

    // switch
    // up  = play full length of chord
    // mid = limit number of notes
    // down short press = toggle number of notes (existing)
    // down long  press = toggle arp direction mode (new)
    ComputerSwitchState switch_state = _computer->getSwitchState();

    if (switch_state != prev_switch_state)
    {
        // Leaving DOWN: resolve short vs long
        if (prev_switch_state == ComputerSwitchState::DOWN)
        {
            if (down_pending_short && !down_long_consumed)
            {
                // SHORT PRESS (existing behavior)
                fix_length++;
                if (fix_length > 6)
                {
                    fix_length = 1;
                }
                if (!led_hold_active)
                {
                    last_switch_change = now;
                    _computer->setLEDs(0x3f >> (6 - fix_length));
                }
            }
            // reset DOWN press state
            down_pending_short = false;
            down_long_consumed = false;
            down_press_start = 0;
        }

        // Entering new state
        if (switch_state == ComputerSwitchState::DOWN)
        {
            down_press_start = now;
            down_pending_short = true;
            down_long_consumed = false;
            // do not change LEDs yet; we decide after timing
        }
        else if (switch_state == ComputerSwitchState::MID)
        {
            // existing behavior: MID means fixed-length ON
            fix_length_on = true;
            if (!led_hold_active)
            {
                last_switch_change = now;
                _computer->setLEDs(0x3f >> (6 - fix_length));
            }
        }
        else
        { // UP
            // existing behavior: full-length mode (fixed-length OFF)
            fix_length_on = false;
            if (!led_hold_active)
            {
                last_switch_change = 0;
            }
        }

        prev_switch_state = switch_state;
    }

    // While holding in DOWN, detect long press
    if (switch_state == ComputerSwitchState::DOWN &&
        down_press_start != 0 && !down_long_consumed)
    {
        if ((now - down_press_start) >= LONGPRESS_MS)
        {
            // LONG PRESS: cycle through arp direction modes (6 modes)
            arp_mode = (ArpMode)((arp_mode + 1) % 6);
            mode_changed = true; // notify main to adopt new mode
            down_long_consumed = true;
            down_pending_short = false; // suppress short press action

            // Brief LED hint so user sees the change (no pulse here to avoid TRIGGER_LENGTH dep)
            // Different LED patterns for each mode
            uint8_t led_pattern;
            switch (arp_mode)
            {
            case ARP_UP:
                led_pattern = 0b000001;
                break; // rightmost
            case ARP_DOWN:
                led_pattern = 0b100000;
                break; // leftmost
            case ARP_UPUP:
                led_pattern = 0b000011;
                break; // two rightmost
            case ARP_DOWNDOWN:
                led_pattern = 0b110000;
                break; // two leftmost
            case ARP_UPDOWN_INC:
                led_pattern = 0b010010;
                break; // symmetric middle hint
            case ARP_UPDOWN_EXC:
                led_pattern = 0b001100;
                break; // adjacent middle hint
            default:
                led_pattern = 0b000001;
                break;
            }
            _computer->setLEDs(led_pattern);
            // Start LED hold so this hint remains visible; also mark last_switch_change
            led_hold_active = true;
            led_hold_until = now + LED_SUPPRESS_MS;
            last_switch_change = now; // ensures main.cpp suppresses step LEDs during hold
        }
    }
}

void UI::spinRandomOuts()
{
    bool any_changed = false;
    for (int i = 0; i < 2; i++)
    {
        uint32_t change_value = get_rand_32();
        if (coin_weight[i] < change_value)
        {
            coin_weight[i] = UINT32_MAX;

            // output a random value from 0v to 1v
            uint r = get_rand_32() * 0.000000002561137081;
            uint output_val = 1024 - (r * 14.22222222) + 0.5;

            _computer->setAudioOut(i + 1, output_val);
            any_changed = true;
        }
        else
        {
            // increase probability of changing next time
            coin_weight[i] -= 0xfffffff;
        }
    }

    // add delay so outputs settle
    if (any_changed)
    {
        sleep_us(500);
    }
}
