# 433 Sense of Space

A whimsical loop card for the Music Thing Workshop Computer, inspired by John
Cage's **4'33"**: a small fragment of theatre ambience is looped for about 91
seconds and placed in imaginary performance spaces.

It is not silence. It is the room, the audience, the building, and the slightly
ridiculous act of listening very hard.

## Controls

| Control | Function |
|---------|----------|
| Main | Reserved for future mischief |
| X | Performance space, from small dry room to long cathedral |
| Y | Reverb amount, from almost dry to fully washed |
| Switch | No performance obligation |

## Outputs

| Jack | Function |
|------|----------|
| Audio Out 1 | Left stereo output |
| Audio Out 2 | Right stereo output |

## LEDs

The left column shows the selected space size. The right column shows the reverb
amount.

## Builds

Two firmware builds are provided:

| File | Card size | Audio asset |
|------|-----------|-------------|
| `UF2/433_sense_of_space_2mb.uf2` | 2 MB | 91 seconds, 10 kHz stereo signed 8-bit |
| `UF2/433_sense_of_space_16mb.uf2` | 16 MB | 91 seconds, 12 kHz stereo signed 8-bit |

Both versions loop the same section and use the same controls. The 16 MB build keeps
a little more bandwidth in the baked recording.

## Licence And Source Audio

The embedded ambience is derived from BBC Sound Effects Archive item `07003043`
(`bbc_theatres--_07003043`). BBC Sound Effects licensing allows archive sounds to be
used "for non-commercial, personal or research purposes", or licensed separately for
other uses:
https://sound-effects.bbcrewind.co.uk/licensing

Because the UF2 files include that BBC-derived audio, the complete firmware images are
not offered as a general Creative Commons release. Any public, commercial, or otherwise
non-personal use should be checked against the BBC terms or covered by a separate BBC
licence.

## Technical Notes

The source is baked into the firmware as a 91 second stereo signed 8-bit loop.
Playback is linearly interpolated to the Workshop Computer's 48 kHz audio rate, then
sent through the integer Dattorro-style reverb from Reverb+.
