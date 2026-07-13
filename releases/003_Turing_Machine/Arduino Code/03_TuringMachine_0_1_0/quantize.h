
// quantize.h

#ifndef QUANTIZE_H
#define QUANTIZE_H

#include <Arduino.h>

// Define the scale types
enum ScaleType {
  SCALE_CHROMATIC,    // 0
  SCALE_MAJOR,        // 1
  SCALE_MINOR,        // 2
  SCALE_MINOR_PENTATONIC, // 3
  SCALE_DORIAN,       // 4
  SCALE_PELOG,        // 5
  SCALE_WHOLETONE,    // 6
  NUM_SCALES          // Total number of scales
};

// Scale intervals in semitones
const int chromatic_intervals[]     = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const int major_intervals[]         = {0, 2, 4, 5, 7, 9, 11};
const int minor_intervals[]         = {0, 2, 3, 5, 7, 8, 10};
const int minor_pent_intervals[]    = {0, 3, 5, 7, 10};
const int dorian_intervals[]        = {0, 2, 3, 5, 7, 9, 10};
const int pelog_intervals[]         = {0, 1, 3, 7, 10};
const int wholetone_intervals[]     = {0, 2, 4, 6, 8, 10};

// Struct to hold scale information
struct Scale {
  const int* intervals;
  int size;
};

// Array of scales
const Scale scales[NUM_SCALES] = {
  {chromatic_intervals,    sizeof(chromatic_intervals) / sizeof(int)},  // SCALE_CHROMATIC
  {major_intervals,        sizeof(major_intervals) / sizeof(int)},      // SCALE_MAJOR
  {minor_intervals,        sizeof(minor_intervals) / sizeof(int)},      // SCALE_MINOR
  {minor_pent_intervals,   sizeof(minor_pent_intervals) / sizeof(int)}, // SCALE_MINOR_PENTATONIC
  {dorian_intervals,       sizeof(dorian_intervals) / sizeof(int)},     // SCALE_DORIAN
  {pelog_intervals,        sizeof(pelog_intervals) / sizeof(int)},      // SCALE_PELOG
  {wholetone_intervals,    sizeof(wholetone_intervals) / sizeof(int)}   // SCALE_WHOLETONE
};



// Degree patterns (sieves)
const int degrees_scale[]        = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const int degrees_1_3_5[]        = {1, 3, 5};
const int degrees_1_5[]          = {1, 5};
const int degrees_1_3_5_6[]      = {1, 3, 5, 6};
const int degrees_1_3_5_7[]      = {1, 3, 5, 7};

enum SieveType {
  SIEVE_SCALE,
  SIEVE_1_3_5,
  SIEVE_1_5,
  SIEVE_1_3_5_6,
  SIEVE_1_3_5_7,
  NUM_SIEVES  // Total number of sieves
};

struct DegreePattern {
  const int* degrees;
  int size;
};

const DegreePattern sieves[NUM_SIEVES] = {
  {degrees_scale,        sizeof(degrees_scale) / sizeof(int)},        // SIEVE_SCALE
  {degrees_1_3_5,        sizeof(degrees_1_3_5) / sizeof(int)},        // SIEVE_1_3_5
  {degrees_1_5,          sizeof(degrees_1_5) / sizeof(int)},          // SIEVE_1_5
  {degrees_1_3_5_6,      sizeof(degrees_1_3_5_6) / sizeof(int)},      // SIEVE_1_3_5_6
  {degrees_1_3_5_7,      sizeof(degrees_1_3_5_7) / sizeof(int)}       // SIEVE_1_3_5_7
};




#define MAX_NOTES 128  // Maximum possible MIDI notes



inline int getMidiNote(uint8_t rand_num, int low_note, int high_note, ScaleType scale_type, SieveType sieve_type) {
  int possible_notes[MAX_NOTES];
  int num_possible_notes = 0;

  // Validate scale_type
  if (scale_type < 0 || scale_type >= NUM_SCALES) {
    scale_type = SCALE_CHROMATIC; // Default to chromatic
  }

  const Scale* scale = &scales[scale_type];

  // Validate sieve_type
  if (sieve_type < 0 || sieve_type >= NUM_SIEVES) {
    sieve_type = SIEVE_SCALE; // Default to using the full scale
  }

  const DegreePattern* sieve = &sieves[sieve_type];

  // Apply the sieve to the scale to get sieved intervals
  int sieved_intervals[12]; // Maximum of 12 intervals in an octave
  int sieved_size = 0;

  for (int i = 0; i < sieve->size; ++i) {
    int degree = sieve->degrees[i];
    if (degree >= 1 && degree <= scale->size) {
      // Degrees are 1-based, array indices are 0-based
      int interval = scale->intervals[degree - 1];
      sieved_intervals[sieved_size++] = interval;
    }
  }

  // Build the list of possible notes in the sieved scale within the given range
  for (int note = low_note; note <= high_note; ++note) {
    int note_in_octave = note % 12;
    for (int i = 0; i < sieved_size; ++i) {
      if (note_in_octave == sieved_intervals[i]) {
        possible_notes[num_possible_notes++] = note;
        break;  // Found a matching interval; no need to check further
      }
    }
  }

  // Check if any notes are available in the specified range and scale
  if (num_possible_notes == 0) {
    return -1;  // Return -1 to indicate no available notes
  }

  // Map the 8-bit random number to an index in the possible_notes array
  int index = (rand_num * num_possible_notes) / 256;
  if (index >= num_possible_notes) {
    index = num_possible_notes - 1;
  }

  // Return the MIDI note corresponding to the calculated index
  return possible_notes[index];
}









//
//inline int getMidiNote(uint8_t rand_num, int low_note, int high_note, ScaleType scale_type) {
//  int possible_notes[MAX_NOTES];
//  int num_possible_notes = 0;
//
//  // Validate scale_type
//  if (scale_type < 0 || scale_type >= NUM_SCALES) {
//    scale_type = SCALE_CHROMATIC; // Default to chromatic
//  }
//
//  const Scale* scale = &scales[scale_type];
//
//  // Build the list of possible notes in the scale within the given range
//  for (int note = low_note; note <= high_note; ++note) {
//    int note_in_octave = note % 12;
//    for (int i = 0; i < scale->size; ++i) {
//      if (note_in_octave == scale->intervals[i]) {
//        possible_notes[num_possible_notes++] = note;
//        break;  // Found a matching interval; no need to check further
//      }
//    }
//  }
//
//  // Check if any notes are available in the specified range and scale
//  if (num_possible_notes == 0) {
//    return -1;  // Return -1 to indicate no available notes
//  }
//
//  // Map the 8-bit random number to an index in the possible_notes array
//  int index = (rand_num * num_possible_notes) / 256;
//  if (index >= num_possible_notes) {
//    index = num_possible_notes - 1;
//  }
//
//  // Return the MIDI note corresponding to the calculated index
//  return possible_notes[index];
//}
//






#endif  // QUANTIZE_H
