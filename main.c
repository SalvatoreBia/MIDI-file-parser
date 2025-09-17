#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define MThd_string 0x4D546864   // "MThd"
#define MTrk_string 0x4D54726B   // "MTrk"


typedef struct
{
    int8_t  smpte;
    uint8_t ticks;
} Fps;

typedef struct
{
    int       is_valid;
    uint16_t  fmt;
    uint16_t  ntracks;
    struct
    {
        uint16_t raw;
        int      type;
        union
        {
            uint16_t tpb;
            Fps      fps;
        };
    } timediv;
} MThd;

typedef enum
{
    NOTE_OFF           = 0X8,
    NOTE_ON            = 0X9,
    NOTE_AFTERTOUCH    = 0XA,
    CONTROLLER         = 0XB,
    PROGRAM_CHANGE     = 0XC,
    CHANNEL_AFTERTOUCH = 0XD,
    PITCH_BEND         = 0XE
} Event_status;

typedef enum
{
    EV_CH,
    EV_META,
    EV_SYSEX
} Event_kind;

typedef struct
{
    Event_kind kind;
    uint32_t   delta_time;

    union
    {
        struct
        {
            uint8_t  type_and_channel;
            uint16_t params;
            size_t   nparams;
        } MIDI_channel_ev;

        struct
        {
            uint8_t  meta_type;
            uint32_t len;
            const uint8_t *data;
        } Meta_event;
        
        struct
        {
            uint8_t  sysex_type;
            uint32_t len;
            const uint8_t *data;
        } Sysex_event;
    };
} Event;

typedef struct 
{
    uint32_t size;
    Event *events;
} MTrk;

typedef struct
{
    MThd  mthd;
    MTrk *mtrk;
} MIDI_file;

// ----------------------------------------------------

static uint32_t make_u32_from_dword(uint8_t buf[])
{
    uint32_t res = 0;
    for (size_t i = 0; i < 4; ++i)
        res |= ((uint32_t)buf[i] << 24 - (8*i));
    
    return res;
}

static void set_timediv(MThd *mthd)
{
    // raw data contains 15 bits of data
    // plus the top bit for the type of time division
    uint16_t raw = mthd->timediv.raw;
    if ((raw & 0x8000) == 0)
    {
        // first bit not set -> ticks per beat
        // get the other 15 bits and use them
        mthd->timediv.type = 0;
        mthd->timediv.tpb  = raw & 0x7FFF;
    }
    else
    {
        // first bit set -> frame per second
        // the first 7 bits are the smpte
        // the remaining ones are the ticks per frame
        mthd->timediv.type      = 1;
        mthd->timediv.fps.smpte = (int8_t)((raw >> 8) & 0xFF);
        mthd->timediv.fps.ticks = (uint8_t)(raw & 0xFF);
    }
}

static uint32_t parse_delta_time(uint8_t buf[], int *status, int *out_len)
{
    // parsing logic: i have a 32bit number
    // that represents the delta_time as a VLQ
    // as a first thing we read the first bit to the left
    // if it is a one we increment the len counter and go to
    // the next byte. if we encounter zero we are done and we
    // can start the actual parsing. if we encounter one on 
    // the fourth byte, we reject the delta, since it can be
    // maximum of four bytes.
    int len = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (buf[i] & 0x80)
        {
            if (i == 3) { *status = 0; return 0; }
            len++;
        }
        else if (!(buf[i] & 0x80))
        {
            len++;
            break;
        }
    }

    uint32_t delta_time = 0;
    for (int i = 0; i < len; ++i)
    {
        uint8_t bit7 = buf[i] & 0x7F;
        int left_shift = 7 * (len - i - 1);
        delta_time  |= ((uint32_t)bit7 << left_shift);
    }

    *status = 1;
    *out_len = len;
    return delta_time;
}

static int parse_mtrk(MIDI_file *midi, MThd *mthd, FILE *f)
{
    // MTrk chunk id check
    uint8_t buf[4];
    fread(buf, sizeof(buf), 1, f);
    uint32_t id = make_u32_from_dword(buf);
    if (id != MTrk_string) return 0;

    // get the chunk size
    fread(buf, sizeof(buf), 1, f);
    uint32_t chunk_size = make_u32_from_dword(buf);

    MTrk *mtrk = (MTrk*) malloc(sizeof(MTrk));
    if (!mtrk) return 0;

    mtrk->size = chunk_size;
    uint32_t bytes_read = 0;
    while (bytes_read < chunk_size)
    {
        // read the delta time of the event
        // keep in mind that fread moves the cursor
        // four bytes, but since the delta time is a
        // VLQ, reposition it correctly before reading
        // other data
        int start = ftell(f);
        fread(buf, sizeof(buf), 1, f);
        int status, nbytes_delta;
        uint32_t delta_time = parse_delta_time(buf, &status, &nbytes_delta);
        if (!status) return 0;
        
        Event *ev = (Event*) malloc(sizeof(Event));
        if (!ev) return 0;

        ev->delta_time = delta_time;
        bytes_read += nbytes_delta;
        fseek(f, start + nbytes_delta, SEEK_SET);
        // TODO: start reading the event lol
    }
}

MIDI_file* get_MIDI_file(MThd *mthd, FILE *f)
{
    size_t midi_size = mthd->ntracks * sizeof(MTrk);
    MIDI_file *midi = (MIDI_file*) malloc(midi_size);
    if (!midi) return NULL;

    // TODO: when entering the if, all the previously
    // allocated MTrk should be freed
    for (uint16_t i = 0; i < mthd->ntracks; ++i)
        if (!parse_mtrk(midi, mthd, f)) return NULL;

    return midi;
}

MThd check_for_MThd(FILE *f)
{
    MThd mthd;
    memset(&mthd, 0, sizeof(MThd));

    uint8_t buf[6];

    fread(buf, sizeof(buf)-2, 1, f);
    uint32_t id = make_u32_from_dword(buf);
    fread(buf, sizeof(buf)-2, 1, f);
    uint32_t chunk_size = make_u32_from_dword(buf);

    if (id != MThd_string) return mthd;
    if (chunk_size != 0x00000006) return mthd;

    fread(buf, sizeof(buf), 1, f);

    mthd.fmt          = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    mthd.ntracks      = ((uint16_t)buf[2] << 8) | (uint16_t)buf[3];
    mthd.timediv.raw  = ((uint16_t)buf[4] << 8) | (uint16_t)buf[5];
    mthd.is_valid     = 1;

    set_timediv(&mthd);
    return mthd;
}

// --------- MAIN ----------------------------------

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Command usage: %s <midi-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *filename = argv[1];
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        printf("[[Error opening midi file. Exiting...]]\n");
        return 1;
    }
    printf("Checking for MThd...\n");
    MThd mthd = check_for_MThd(file);
    if (!mthd.is_valid)
    {
        printf("[[MThd not present. Exiting...]]\n");
        goto free_resources;
    }

    printf("Parsing track chunks...\n");
    MIDI_file *midi = get_MIDI_file(&mthd, file);
    if (!midi)
    {
        printf("[[One or more MTrk are invalid. Exiting...]]\n");
        goto free_resources;
    }

    printf("Parsing successfully completed.\n");
    return 0;

free_resources:
    fclose(file);
    return 1;
}