// Minimal MIDI message reading struct
// from https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard/examples/midi_device

#ifndef BRAIDS_MIDI_MESSAGE_H_
#define BRAIDS_MIDI_MESSAGE_H_

struct MIDIMessage
{
	// Parse a MIDI message from the raw packet data
	MIDIMessage(uint8_t* packet)
	{
		channel = (packet[0]&0x0F) + 1;
		command = static_cast<Command>(packet[0] & 0xF0);
		switch (packet[0]&0xF0)
		{
		case Command::NoteOn: // two separate data bytes
		case Command::NoteOff:
		case Command::Aftertouch:
		case Command::CC:
			note = packet[1];
			velocity = packet[2];
			break;
			
		case Command::PatchChange: // single data bytes
			instrument = packet[1];
			break;
			
		case Command::PitchBend: // pitch bend combines msb/lsb
			pitchbend = packet[1] + (packet[2]<<7) - 8192;
			break;
			
		default:
			command = Command::Unknown;
			break;
		}
	}
	
	enum Command {Unknown = 0x00, NoteOn = 0x90, NoteOff = 0x80,
		PitchBend = 0xE0, Aftertouch = 0xA0, CC = 0xB0,	PatchChange = 0xC0};
	Command command;
	uint8_t channel;
	union // Store MIDI data bytes in two-byte union
	{
		struct
		{
			union // data byte 1
			{
				uint8_t note; // note on/off/aftertouch
				uint8_t cc; // CC
				uint8_t instrument; // patch change
			};
			union // data byte 2
			{
				uint8_t velocity; // note on/off
				uint8_t touch; // aftertouch
				uint8_t value; // CC 
			};
		};
		int16_t pitchbend;
	};
};

#endif