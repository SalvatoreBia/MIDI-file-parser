#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define MThd_string 0x4D546864
#define MTrk_string 0x4D54726B

// ------------------------------------------------------

typedef struct
{
    uint16_t fmt;
    uint16_t ntracks;
    union
    {
        uint16_t ticks_per_beat;
        struct
        {
            int8_t  smpte;
            uint8_t ticks;
        } frames_per_sec;
    } timediv;
} MThd;

typedef struct
{
    uint8_t  type;
    uint8_t  channel;
    uint8_t  param1;
    uint8_t  param2;
} Channel_event;

typedef struct
{
    uint8_t  type;
    uint32_t len;
    void*    data;
} Meta_event;

typedef struct
{
    uint32_t len;
    void*    data;
} Sysex_event;

typedef enum { CH, META, SYS } Event_kind;
typedef struct
{
    uint32_t delta_time;
    Event_kind kind;
    union
    {
        Channel_event channel_ev;
        Meta_event    meta_ev;
        Sysex_event   sysex_ev;
    } ev;
    
} MTrk_event;

typedef struct
{
    uint32_t    size;
    MTrk_event *events;
    size_t      count;
    size_t      cap;
} MTrk;

typedef struct
{
    MThd  mthd;
    MTrk *mtrk;
} MIDI_file;

// ---------------------------------------------------

int check_for_MThd(MThd *mthd, FILE *fp);
int parse_MTrk_channel_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read);
int parse_MTrk_meta_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read);
int parse_MTrk_sysex_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read);
int parse_MTrk_events(MTrk *mtrk, FILE *fp);
int parse_MTrk(MTrk *mtrk, FILE *fp);

MIDI_file get_MIDI_file(FILE *fp, int *status);

void free_MTrk(MTrk *mtrk);
void free_MIDI_file(MIDI_file *midi);

#endif /* MIDI_PARSER_H */
