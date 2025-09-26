#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include "midi_parser.h"


void free_MTrk(MTrk *mtrk)
{
    if (mtrk)
    {
        for (size_t i = 0; i < mtrk->count; ++i)
        {
            if (mtrk->events[i].kind == META)
            {
                void *data = mtrk->events[i].ev.meta_ev.data;
                if (data) free(data);
            }
            else if (mtrk->events[i].kind == SYS)
            {
                void *data = mtrk->events[i].ev.sysex_ev.data;
                if (data) free(data);
            }
        }
        if (mtrk->events) free(mtrk->events);
    }
}

void free_MIDI_file(MIDI_file *midi)
{
    if (midi && midi->mtrk)
    {
        for (uint16_t i = 0; i < midi->mthd.ntracks; ++i)
        {
            free_MTrk(&midi->mtrk[i]);
        }
        free(midi->mtrk);
        midi->mtrk = NULL;
    }
}

int check_for_MThd(MThd *mthd, FILE *fp)
{
    if (!mthd || !fp) return 0;

    // check for MThd string
    uint8_t buf[6];
    if (fread(buf, 1, 4, fp) != 4) return 0;

    uint32_t id = (uint32_t)buf[0] << 24 |
                  (uint32_t)buf[1] << 16 |
                  (uint32_t)buf[2] << 8  |
                  (uint32_t)buf[3];
    if (id != MThd_string) return 0;

    // get chunk size (for MThd must be 6)
    if (fread(buf, 1, 4, fp) != 4) return 0;
    uint32_t size = (uint32_t)buf[0] << 24 |
                    (uint32_t)buf[1] << 16 |
                    (uint32_t)buf[2] << 8  |
                    (uint32_t)buf[3];
    if (size != 0x00000006) return 0;

    // read actual content
    if (fread(buf, 1, 6, fp) != 6) return 0;
    mthd->fmt     = (uint16_t)buf[0] << 8 | (uint16_t)buf[1];
    mthd->ntracks = (uint16_t)buf[2] << 8 | (uint16_t)buf[3];

    if (mthd->fmt > 2 || mthd->ntracks == 0)  return 0;
    if (mthd->fmt == 0 && mthd->ntracks != 1) return 0;

    uint16_t td   = (uint16_t)buf[4] << 8 | (uint16_t)buf[5];
    if (td & 0x8000)
    {
        mthd->timediv.frames_per_sec.smpte = (int8_t)buf[4];
        mthd->timediv.frames_per_sec.ticks = buf[5];
    }
    else
    {
        mthd->timediv.ticks_per_beat = td & 0x7FFF;
    }

    return 1;
}

static uint32_t get_VLQ(FILE *fp, int *status, uint32_t *bytes_read)
{
    uint32_t vlq = 0, n = 0;
    for (; n < 4; ++n)
    {
        int c = fgetc(fp);
        if (c == EOF) { *status = -1; return vlq; }
        vlq = (vlq << 7) | (uint32_t)(c & 0x7F);
        if ((c & 0x80) == 0)
        {
            *bytes_read = n + 1;
            *status = 0;
            return vlq;
        }
    }
    *status = -1;
    return vlq;
}

int parse_MTrk_channel_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read)
{
    size_t idx = mtrk->count;
    mtrk->events[idx].kind = CH;

    uint8_t type    = mtrk->events[idx].ev.channel_ev.type;
    // uint8_t channel = mtrk->events[idx].ev.channel_ev.channel;

    switch (type)
    {
    case 0x0C:
    case 0x0D:
        uint8_t param;
        if (fread(&param, 1, 1, fp) != 1) return 0;
        if (param > 127) return 0;
        mtrk->events[idx].ev.channel_ev.param1 = param;
        *bytes_read = 1;
        break;
        
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xE:
        uint8_t param1, param2;
        if (fread(&param1, 1, 1, fp) != 1) return 0;
        if (fread(&param2, 1, 1, fp) != 1) return 0;
        if (param1 > 127 || param2 > 127)  return 0;
        mtrk->events[idx].ev.channel_ev.param1 = param1;
        mtrk->events[idx].ev.channel_ev.param2 = param2;
        *bytes_read = 2;
        break;
        
    default:
        return 0;
    }
    
    return 1;
}

int parse_MTrk_meta_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read)
{
    uint8_t type;
    if (fread(&type, 1, 1, fp) != 1) return 0;

    size_t idx = mtrk->count;
    mtrk->events[idx].kind = META;
    mtrk->events[idx].ev.meta_ev.type = type;
    (*bytes_read)++;

    int code; uint32_t len_bytes;
    uint32_t len = get_VLQ(fp, &code, &len_bytes);
    if (code < 0) return 0;
    mtrk->events[idx].ev.meta_ev.len = len;
    (*bytes_read) += len_bytes;

    uint8_t buf[4];
    printf("type -> %d\n", type);
    switch (type)
    {
    case 0x00:
    {
        if (len != 2) return 0;
        void *val = malloc(len);
        if (!val) return 0;

        if (fread(buf, 1, 2, fp) != 2) { free(val); return 0; }
        *(uint8_t*)val       = buf[0];
        *((uint8_t*)val + 1) = buf[1];
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += 2;
        break;
    }

    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x09:
    {
        void *val = malloc(len);
        if (!val) return 0;

        if (fread(val, 1, len, fp) != len) { free(val); return 0; }
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += (uint32_t)len;
        break;
    }

    case 0x20:
    {
        if (len != 1) return 0;
        void *val = malloc(1);
        if (!val) return 0;

        if (fread(val, 1, 1, fp) != 1) { free(val); return 0; }
        if (*(uint8_t*)val > 15) { free(val); return 0; }
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read)++;
        break;
    }

    case 0x21:
    {
        if (len != 1) return 0;
        void *val = malloc(1);
        if (!val) return 0;

        if (fread(val, 1, 1, fp) != 1) { free(val); return 0; }
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read)++;
        break;
    }

    case 0x2F:
    {
        if (len != 0) return 0;
        mtrk->events[idx].ev.meta_ev.data = NULL;
        return 2;
    }

    case 0x51:
    {
        if (len != 3) return 0;
        void *val = malloc(3);
        if (!val) return 0;

        if (fread(val, 1, 3, fp) != 3)  { free(val); return 0; }

        uint8_t *p = val;
        uint32_t us_per_qn = (p[0] << 16) | (p[1] << 8) | p[2];
        if (us_per_qn > 8355711u) { free(val); return 0; }
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += 3;
        break;
    }

    case 0x54:
    {
        if (len != 5) return 0;
        void *val = malloc(5);
        if (!val) return 0;

        if (fread(val, 1, 5, fp) != 5) { free(val); return 0; }
        
        // check the hour byte for correctness
        // the bit layout is 0rrhhhhh
        uint8_t hour_byte = *(uint8_t*)val;
        uint8_t rr        = (hour_byte >> 5) & 0x03;
        if ((hour_byte) & 0x80 || (hour_byte & 0x1F) > 23) { free(val); return 0; }
        
        // check for fr byte correctness, based on rr
        uint8_t fr = *((uint8_t*)val + 3);
        if ((rr == 0 && fr > 23) ||
            (rr == 1 && fr > 24) ||
            (rr == 2 && fr > 29) ||
            (rr == 3 && fr > 29)) { free(val); return 0; }
        
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += 5;
        break;
    }

    case 0x58:
    {
        if (len != 4) return 0;
        void *val = malloc(4);
        if (!val) return 0;

        if (fread(val, 1, 4, fp) != 4) { free(val); return 0; }
        if (*((uint8_t*)val+3) == 0)   { free(val); return 0; }
        
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += 4;
        break;
    }

    case 0x59:
    {
        if (len != 2) return 0;
        void *val = malloc(2);
        if (!val) return 0;

        if (fread(val, 1, 2, fp) != 2) { free(val); return 0; }
        
        int8_t key = *(int8_t*)val;
        uint8_t scale = *((uint8_t*)val+1);
        if (key < -7 || key > 7 || scale > 1) { free(val); return 0; }
        
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += 2;
        break;
    }

    case 0x7F:
    {
        void *val = malloc(len);
        if (!val) return 0;

        if (fread(val, 1, len, fp) != len) { free(val); return 0; }
        mtrk->events[idx].ev.meta_ev.data = val;
        (*bytes_read) += len;
        break;
    }

    default:
        return 0;
    }

    return 1;
}

int parse_MTrk_sysex_event(MTrk *mtrk, FILE *fp, uint32_t *bytes_read)
{
    size_t idx = mtrk->count;
    mtrk->events[idx].kind = SYS;

    int code; uint32_t len_bytes;
    uint32_t len = get_VLQ(fp, &code, &len_bytes);
    if (code < 0) return 0;

    void *val = malloc(len);
    if (!val) return 0;

    if (fread(val, 1, len, fp) != len) { free(val); return 0; }
    mtrk->events[idx].ev.sysex_ev.len  = len;
    mtrk->events[idx].ev.sysex_ev.data = val;
    (*bytes_read) += len;
    
    return 1;
}

static int MTrk_grow(MTrk *mtrk, size_t min_needed)
{
    size_t cap = mtrk->cap ? mtrk->cap : 64;
    while (cap < min_needed)
    {
        if (cap > SIZE_MAX / 2) return 0;
        cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof(MTrk_event)) return 0;

    MTrk_event *ev = (MTrk_event*) realloc(mtrk->events, cap * sizeof *ev);
    if (!ev) return 0;

    mtrk->events = ev;
    mtrk->cap = cap;
    return 1;
}

static inline int mtrk_ensure_one(MTrk *mtrk)
{
    if (mtrk->count < mtrk->cap) return 1;
    return MTrk_grow(mtrk, mtrk->count + 1);
}

int parse_MTrk_events(MTrk *mtrk, FILE *fp)
{
    uint32_t remaining_bytes = mtrk->size;
    uint8_t running_status = 0;
    while (remaining_bytes > 0)
    {
        // read the event delta time as a VLQ
        int code; uint32_t delta_bytes;
        uint32_t delta = get_VLQ(fp, &code, &delta_bytes);
        if (code < 0) return 0;

        if (!mtrk_ensure_one(mtrk)) return 0;
        size_t idx = mtrk->count;

        mtrk->events[idx].delta_time = delta;
        remaining_bytes -= delta_bytes;

        if (remaining_bytes == 0) return 0;

        // read the event type and dispatch
        uint8_t evtype;
        if (fread(&evtype, 1, 1, fp) != 1) return 0;

        uint32_t bytes_read = 0;
        if (evtype >= 0x80)
        {
            mtrk->events[idx].ev.channel_ev.type    = evtype >> 4;
            mtrk->events[idx].ev.channel_ev.channel = evtype & 0x0F;
            remaining_bytes--;

            if (evtype == 0xFF)
            {
                int fine = parse_MTrk_meta_event(mtrk, fp, &bytes_read) > 0;
                if (!fine) return 0;
                
                if (bytes_read > remaining_bytes) return 0;
                remaining_bytes -= bytes_read;

                // if fine == 2, End Of Track occurred
                if (fine == 2)
                {
                    if (remaining_bytes > 0) fseek(fp, (long)remaining_bytes, SEEK_CUR);
                    mtrk->count++;
                    return 1;
                }
            }
            else if (evtype == 0xF0 || evtype == 0xF7)
            {
                if (!parse_MTrk_sysex_event(mtrk, fp, &bytes_read)) return 0;
                if (bytes_read > remaining_bytes) return 0;
                remaining_bytes -= bytes_read;
            }
            else
            {
                running_status = evtype;
                if (!parse_MTrk_channel_event(mtrk, fp, &bytes_read)) return 0;
                if (bytes_read > remaining_bytes) return 0;
                remaining_bytes -= bytes_read;
            }
        }
        else
        {
            mtrk->events[idx].ev.channel_ev.type    = running_status >> 4;
            mtrk->events[idx].ev.channel_ev.channel = running_status & 0x0F;
            fseek(fp, -1L, SEEK_CUR);
            if (!parse_MTrk_channel_event(mtrk, fp, &bytes_read)) return 0;
            if (bytes_read > remaining_bytes) return 0;
            remaining_bytes -= bytes_read;
        }
        mtrk->count++;
    }

    return 1;
}

int parse_MTrk(MTrk *mtrk, FILE *fp)
{
    if (!mtrk || !fp) return 0;

    // check for MTrk id
    uint8_t buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    uint32_t id = (uint32_t)buf[0] << 24 |
                  (uint32_t)buf[1] << 16 |
                  (uint32_t)buf[2] << 8  |
                  (uint32_t)buf[3];
    
    if (id != MTrk_string) return 0;

    if (fread(buf, 1, 4, fp) != 4) return 0;
    uint32_t size = (uint32_t)buf[0] << 24 |
                    (uint32_t)buf[1] << 16 |
                    (uint32_t)buf[2] << 8  |
                    (uint32_t)buf[3];

    mtrk->size  = size;
    mtrk->count = 0;
    return parse_MTrk_events(mtrk, fp);
}

MIDI_file get_MIDI_file(FILE *fp, int *status)
{
    MIDI_file midi;
    memset(&midi, 0, sizeof(MIDI_file));

    if (!fp) goto fail;
    if (!check_for_MThd(&midi.mthd, fp)) goto fail;

    midi.mtrk = (MTrk*) malloc(sizeof(MTrk) * midi.mthd.ntracks);
    if (!midi.mtrk) goto fail;

    for (uint16_t i = 0; i < midi.mthd.ntracks; ++i)
    {
        memset(&midi.mtrk[i], 0, sizeof(MTrk));
        if (!parse_MTrk(&midi.mtrk[i], fp))
        {
            free_MTrk(&midi.mtrk[i]);
            for (uint16_t j = 0; j < i; ++j)
                free_MTrk(&midi.mtrk[j]);
            free(midi.mtrk);
            goto fail;
        }
    }

    *status = 0;
    return midi;

fail:
    *status = -1; 
    return midi;
}