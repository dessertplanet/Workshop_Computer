/*
    Chord Blimey!
    A simple arpeggiator for Music Thing Workshop System Computer
    Tom Waters / Random Works Modular 2024

    Send a trigger into Pulse In 1 and get an arpeggio from CV Out & Pulse Out 1

    Pulse Out 2 fires when the last note has finished
    It also fires at startup so you can patch it to Pulse In 1 for looping arpeggios

    CV Out 2 outputs the root note
    The big Knob controls the speed
    The X Knob controls the root note
    The Y Knob controls the chord

    CV 1 In controls the root note (added to X Knob)
    CV 2 In controls the chord 0v - 1v (added to Y Knob)

    Audio Out 1 & 2 output a random voltage 0v - 1v for patching to CV Ins
    At the end of a chord a coin is tossed to decide if the output should change
    If starts off unlikely to change and gets more likely with each toss

    LEDs show the current note in the chord
*/

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "computer.h"
#include "ui.h"

// --- Arp helpers ---
static inline int chord_size(const int *row)
{
    int n = 0;
    while (row[n] != -1)
        n++;
    return n;
}

#define TRIGGER_LENGTH 10

Computer computer;
UI ui;

int chords[12][7] = {
    {0, 4, 7, -1},             // M
    {0, 4, 7, 11, -1},         // M7
    {0, 4, 7, 11, 14, -1},     // M9
    {0, 4, 7, 11, 14, 17, -1}, // M11
    {0, 5, 7, -1},             // SUS4
    {0, 4, 8, -1},             // AUG
    {0, 3, 6, -1},             // DIM
    {0, 4, 7, 10, -1},         // DOM7
    {0, 3, 7, 10, 14, 17, -1}, // m11
    {0, 3, 7, 10, 14, -1},     // m9
    {0, 3, 7, 10, -1},         // m7
    {0, 3, 7, -1}              // m
};

bool pulse_in_got[2] = {false, false};
void pulsein_callback(uint gpio, uint32_t events)
{
    pulse_in_got[gpio - PIN_PULSE1_IN] = true;
}

// current play state
bool chord_play = false;
uint chord = 0;
uint chord_note = 0;
uint32_t last_note_time = 0;

int arp_count = 0;
int arp_length = -1;

int main()
{
    stdio_init_all();
    computer.init();
    computer.calibrateIfSwitchDown();

    computer.setPulseCallback(1, pulsein_callback);
    ui.init(&computer);

    // output trigger on pulse 2 to start looping if patched
    computer.setTimedPulse(2, TRIGGER_LENGTH);

    while (true)
    {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        computer.poll();
        ui.checkSwitch();
        arp_length = ui.getArpLength();

        // If mode changed, restart the cycle immediately in the new direction
        if (ui.consumeModeChanged())
        {
            if (chord_play)
            {
                arp_count = 0;      // restart from beginning of pattern
                last_note_time = 0; // play immediately on next tick
            }
        }

        // if we get a pulse start a new arp
        if (pulse_in_got[0])
        {
            pulse_in_got[0] = false;
            chord_note = 0;
            arp_count = 0;

            ui.spinRandomOuts();

            chord = ui.getChord();
            chord_play = true;
            last_note_time = 0; // reset so we play immediately
        }

        // if time for a new note
        uint note_length = ui.getNoteLengthMS();
        if (chord_play && (now - last_note_time >= note_length || last_note_time == 0))
        {
            // Determine chord size and the total length of this arpeggio cycle
            int csize = chord_size(chords[chord]);                   // notes in this chord
            int base_steps = (arp_length >= 0) ? arp_length : csize; // base notes to emit this cycle

            // For UPUP and DOWNDOWN modes, double the total steps (each note played twice)
            ArpMode mode = ui.getArpMode();
            int total_steps;
            if (mode == ARP_UPUP || mode == ARP_DOWNDOWN)
            {
                total_steps = base_steps * 2;
            }
            else if (mode == ARP_UPDOWN_INC)
            {
                total_steps = base_steps * 2;
            }
            else if (mode == ARP_UPDOWN_EXC)
            {
                total_steps = (base_steps <= 1) ? 1 : (base_steps * 2 - 2);
            }
            else
            {
                total_steps = base_steps; // UP or DOWN
            }

            // End-of-arp condition
            if (csize <= 0 || arp_count >= total_steps)
            {
                computer.setTimedPulse(2, TRIGGER_LENGTH);
                chord_play = false;
                continue;
            }

            // set next note time and update UI
            last_note_time = now;
            ui.update();

            // step number within the full cycle according to current mode
            int s;
            int t = arp_count; // 0..total_steps-1 within a single cycle

            switch (mode)
            {
            case ARP_UP:
                s = t;
                break;

            case ARP_DOWN:
                s = total_steps - 1 - t;
                break;

            case ARP_UPUP:
                // 0,0,1,1,2,2 (ascending)
                s = t / 2;
                break;

            case ARP_DOWNDOWN:
                // ...3,3,2,2,1,1,0,0 (descending)
                s = (base_steps - 1) - (t / 2);
                break;

            case ARP_UPDOWN_INC:
                // Up then down, inclusive (peak is played twice)
                // Sequence length = 2 * base_steps
                // t: 0..base_steps-1 (up), base_steps..2*base_steps-1 (down incl endpoints)
                if (t < base_steps)
                {
                    s = t;
                }
                else
                {
                    s = (2 * base_steps - 1) - t;
                }
                break;

            case ARP_UPDOWN_EXC:
                // Up then down, exclusive (no double-count of endpoints)
                // Sequence length = 2 * base_steps - 2  (unless base_steps <= 1 -> 1)
                if (total_steps <= 1)
                {
                    s = 0;
                }
                else if (t < base_steps)
                {
                    s = t; // up 0..base_steps-1
                }
                else
                {
                    s = total_steps - t; // down base_steps-2 .. 1
                }
                break;

            default:
                s = t; // safe fallback
                break;
            }

            // derive pitch from step number: base degree and octave shift
            int base_idx = (csize > 0) ? (s % csize) : 0;
            int octave_shift = (csize > 0) ? (s / csize) : 0; // 0 for first pass, 1 for second, etc.
            chord_note = base_idx;                            // same degree mapping for all modes; direction is encoded in s

            float chord_root_volts = ui.getRootVolts();
            if (octave_shift > 0)
            {
                chord_root_volts += (float)octave_shift; // +1V per full pass
            }

            float chord_note_volts = chord_root_volts + ((float)chords[chord][chord_note] * VOLT_SEMITONE);
            computer.setCVOutVolts(1, chord_note_volts);
            computer.setCVOutVolts(2, chord_root_volts);

            computer.setTimedPulse(1, TRIGGER_LENGTH);

            // show step number (wrap across 6 LEDs) only if suppression time passed
            if ((now - ui.last_switch_change) >= UI::LED_SUPPRESS_MS)
            {
                int led_index = (total_steps > 0) ? (s % 6) : 0;
                computer.setLEDs(1 << led_index);
            }

            // advance to next step in this cycle
            arp_count++;
        }
    }

    return 0;
}
