#ifndef JSON_GENERATOR_H
#define JSON_GENERATOR_H

#include "midi_parser.h"
#include <stdio.h>

// ---------------------------------------------------

int write_MIDI_to_JSON(const MIDI_file *midi, FILE *fp);
int write_MIDI_to_JSON_file(const MIDI_file *midi, const char *filename);

#endif /* JSON_GENERATOR_H */
