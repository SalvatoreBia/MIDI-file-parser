#include <stdio.h>
#include <stdlib.h>
#include "include/midi_parser.h"
#include "include/json_generator.h"

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("Usage: %s <input_midi_file> <output_json_file>\n", argv[0]);
        exit(1);
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp)
    {
        printf("Error: Could not open MIDI file '%s'\n", argv[1]);
        exit(1);
    }

    int status;
    MIDI_file midi = get_MIDI_file(fp, &status);
    fclose(fp);

    if (status != 0)
    {
        printf("Error: Failed to parse MIDI file\n");
        exit(1);
    }

    printf("Successfully parsed MIDI file:\n");
    printf("  Format: %u\n", midi.mthd.fmt);
    printf("  Tracks: %u\n", midi.mthd.ntracks);
    if (midi.mthd.fmt <= 1)
        printf("  Ticks per beat: %u\n", midi.mthd.timediv.ticks_per_beat);
    else
        printf("  SMPTE: %d, Ticks per frame: %u\n", 
               midi.mthd.timediv.frames_per_sec.smpte,
               midi.mthd.timediv.frames_per_sec.ticks);

    if (!write_MIDI_to_JSON_file(&midi, argv[2]))
    {
        printf("Error: Failed to write JSON file\n");
        free_MIDI_file(&midi);
        exit(1);
    }

    printf("Successfully generated JSON file: %s\n", argv[2]);

    free_MIDI_file(&midi);
    return 0;
}
