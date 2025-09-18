#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define MThd_string 0x4D546864 // "MThd"
#define MTrk_string 0x4D54726B // "MTrk"

typedef struct
{
    int8_t smpte;
    uint8_t ticks;
} Fps;

typedef struct
{
    int is_valid;
    uint16_t fmt;
    uint16_t ntracks;
    struct
    {
        uint16_t raw;
        int type;
        union
        {
            uint16_t tpb;
            Fps fps;
        };
    } timediv;
} MThd;

typedef enum
{
    NOTE_OFF = 0X8,
    NOTE_ON = 0X9,
    NOTE_AFTERTOUCH = 0XA,
    CONTROLLER = 0XB,
    PROGRAM_CHANGE = 0XC,
    CHANNEL_AFTERTOUCH = 0XD,
    PITCH_BEND = 0XE
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
    uint32_t delta_time;

    union
    {
        struct
        {
            uint8_t status;
            uint16_t params;
            size_t nparams;
        } MIDI_channel_ev;

        struct
        {
            uint8_t meta_type;
            uint32_t len;
            const uint8_t *data;
        } Meta_ev;

        struct
        {
            uint8_t sysex_type;
            uint32_t len;
            const uint8_t *data;
        } Sysex_ev;
    };
} Event;

typedef struct
{
    uint32_t size;
    Event *events;
    size_t nevents;
    size_t capacity;
} MTrk;

typedef struct
{
    MThd mthd;
    MTrk *mtrk;
} MIDI_file;

// ----------------------------------------------------

static void free_midi_file(MIDI_file *midi);

static uint32_t make_u32_from_dword(uint8_t buf[])
{
    uint32_t res = 0;
    for (size_t i = 0; i < 4; ++i)
        res |= ((uint32_t)buf[i] << (24 - (8 * i)));

    return res;
}

static int mtrk_push_event(MTrk *mtrk, const Event *ev)
{
    if (mtrk->nevents == mtrk->capacity)
    {
        size_t newcap = mtrk->capacity ? mtrk->capacity * 2 : 64;
        Event *p = (Event *)realloc(mtrk->events, newcap * sizeof(*p));
        if (!p)
            return 0;
        mtrk->events = p;
        mtrk->capacity = newcap;
    }
    mtrk->events[mtrk->nevents++] = *ev;
    return 1;
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
        mthd->timediv.tpb = raw & 0x7FFF;
    }
    else
    {
        // first bit set -> frame per second
        // the first 7 bits are the smpte
        // the remaining ones are the ticks per frame
        mthd->timediv.type = 1;
        mthd->timediv.fps.smpte = (int8_t)((raw >> 8) & 0xFF);
        mthd->timediv.fps.ticks = (uint8_t)(raw & 0xFF);
    }
}

static int event_status_check(uint8_t status)
{
    int channel = (int)(status & 0x0F);
    int ev = (int)((status & 0xF0) >> 4);

    if (channel < 0 || channel > 15)
        return 0;
    if (ev < NOTE_OFF || ev > PITCH_BEND)
        return 0;

    return 1;
}

static uint32_t parse_VLQ(uint8_t buf[], int *status, int *out_len)
{
    // parsing logic: i have a 32bit number
    // that's represented as a VLQ
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
            if (i == 3)
            {
                *status = 0;
                return 0;
            }
            len++;
        }
        else if (!(buf[i] & 0x80))
        {
            len++;
            break;
        }
    }

    uint32_t vlq = 0;
    for (int i = 0; i < len; ++i)
    {
        uint8_t bit7 = buf[i] & 0x7F;
        int left_shift = 7 * (len - i - 1);
        vlq |= ((uint32_t)bit7 << left_shift);
    }

    *status = 1;
    *out_len = len;
    return vlq;
}

static int parse_mtrk(MIDI_file *midi, MThd *mthd, uint16_t at_idx, FILE *f)
{
    // MTrk chunk id check
    uint8_t buf[4];
    if (fread(buf, sizeof(buf), 1, f) != 1)
        return 0;
    uint32_t id = make_u32_from_dword(buf);
    if (id != MTrk_string)
        return 0;

    // get the chunk size
    if (fread(buf, sizeof(buf), 1, f) != 1)
        return 0;
    uint32_t chunk_size = make_u32_from_dword(buf);

    MTrk mtrk;
    memset(&mtrk, 0, sizeof(MTrk));

    mtrk.size = chunk_size;
    uint32_t bytes_read = 0;
    uint8_t running_status = 0;

    while (bytes_read < chunk_size)
    {
        // read the delta time of the event
        // keep in mind that fread moves the cursor
        // four bytes, but since the delta time is a
        // VLQ, reposition it correctly before reading
        // other data
        long start = ftell(f);
        if (fread(buf, sizeof(buf), 1, f) != 1)
            return 0;
        int status, nbytes_delta;
        uint32_t delta_time = parse_VLQ(buf, &status, &nbytes_delta);
        if (!status)
            return 0;

        Event ev;
        memset(&ev, 0, sizeof(ev));

        ev.delta_time = delta_time;
        bytes_read += nbytes_delta;
        fseek(f, start + nbytes_delta, SEEK_SET);

        if (fread(buf, sizeof(buf) - 3, 1, f) != 1)
            return 0;
        // if greater than 0x80, the running status
        // will be overwritten by this new one
        if (buf[0] >= 0x80)
        {
            // meta event
            if (buf[0] == 0xFF)
            {
                ev.kind = EV_META;
                bytes_read++;

                uint8_t type;
                fread(&type, 1, 1, f);
                ev.Meta_ev.meta_type = type;
                bytes_read += 1;

                long meta_start = ftell(f);
                uint8_t len_buf[4];
                int status, nbytes_len;
                fread(&len_buf, sizeof(len_buf), 1, f);
                uint32_t len = parse_VLQ(len_buf, &status, &nbytes_len);
                if (!status)
                    return 0;

                ev.Meta_ev.len = len;
                bytes_read += nbytes_len;
                fseek(f, meta_start + nbytes_len, SEEK_SET);
                // if the meta event is End Of Track, but we still need to read bytes,
                // there's something wrong -> stop parsing
                if (type == 0x2F && len == 0 && (bytes_read != chunk_size))
                    return 0;

                // test for other meta types
                int err = 0;
                switch (type)
                {
                case 0x00:
                    if (len != 2)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t *sn_data = (uint8_t *)malloc(2);
                    if (!sn_data)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t sn[2];
                    fread(sn, sizeof(sn), 1, f);
                    memcpy(sn_data, sn, sizeof(sn));
                    ev.Meta_ev.data = (const uint8_t *)sn_data;
                    bytes_read += 2;
                    break;

                case 0x01:
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                    char *str = (char *)malloc((size_t)len + 1);
                    if (!str)
                    {
                        err = 1;
                        break;
                    }
                    fread(str, (size_t)len, 1, f);
                    str[len] = '\0';
                    ev.Meta_ev.data = (const uint8_t *)str;
                    bytes_read += len;
                    break;

                case 0x20:
                    if (len != 1)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t *channel_data = (uint8_t *)malloc(1);
                    if (!channel_data)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t channel;
                    fread(&channel, 1, 1, f);
                    if (channel > 15)
                    {
                        free(channel_data);
                        err = 1;
                        break;
                    }
                    *channel_data = channel;
                    ev.Meta_ev.data = (const uint8_t *)channel_data;
                    bytes_read++;
                    break;

                case 0x21:
                    if (len != 1)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t *port_data = (uint8_t *)malloc(1);
                    if (!port_data)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t port;
                    fread(&port, 1, 1, f);
                    *port_data = port;
                    ev.Meta_ev.data = (const uint8_t *)port_data;
                    bytes_read++;
                    break;

                case 0x51:
                    uint8_t *ms_per_qn = (uint8_t *)malloc(3);
                    if (!ms_per_qn)
                    {
                        err = 1;
                        break;
                    }

                    fread(ms_per_qn, 3, 1, f);
                    uint32_t check = (uint32_t)ms_per_qn[0] << 16 |
                                     (uint32_t)ms_per_qn[1] << 8 |
                                     (uint32_t)ms_per_qn[2];

                    if (check > 8355711)
                    {
                        err = 1;
                        break;
                    }

                    ev.Meta_ev.data = (const uint8_t *)ms_per_qn;
                    bytes_read += 3;
                    break;

                case 0x54:
                    uint8_t *smpte_offset = (uint8_t *)malloc(5);
                    if (!smpte_offset)
                    {
                        err = 1;
                        break;
                    }

                    fread(smpte_offset, 5, 1, f);
                    ev.Meta_ev.data = (const uint8_t *)smpte_offset;
                    bytes_read += 5;
                    break;

                case 0x58:
                    uint8_t *timesig_data = (uint8_t *)malloc(4);
                    if (!timesig_data)
                    {
                        err = 1;
                        break;
                    }
                    uint8_t timesig[4];
                    fread(timesig, 4, 1, f);
                    memcpy(timesig_data, timesig, 4);
                    ev.Meta_ev.data = (const uint8_t *)timesig_data;
                    bytes_read += 4;
                    break;

                case 0x59:
                    if (len != 2)
                    {
                        err = 1;
                        break;
                    }

                    uint8_t *key = (uint8_t *)malloc(2);
                    if (!key)
                    {
                        err = 1;
                        break;
                    }

                    size_t n = fread(key, 1, 2, f);
                    if (n != 2)
                    {
                        free(key);
                        err = 1;
                        break;
                    }

                    int8_t sf = (int8_t)key[0];
                    uint8_t mi = key[1];

                    if (sf < -7 || sf > 7 || mi > 1)
                    {
                        free(key);
                        err = 1;
                        break;
                    }

                    ev.Meta_ev.data = (const uint8_t *)key;
                    bytes_read += 2;
                    break;

                case 0x7F:
                    if (len < 1)
                    {
                        err = 1;
                        break;
                    }

                    uint8_t *blob = (uint8_t *)malloc((size_t)len);
                    if (!blob)
                    {
                        err = 1;
                        break;
                    }

                    size_t n_blob = fread(blob, 1, (size_t)len, f);
                    if (n_blob != (size_t)len)
                    {
                        free(blob);
                        err = 1;
                        break;
                    }

                    ev.Meta_ev.data = (const uint8_t *)blob;
                    bytes_read += len;
                    break;

                case 0x2F:
                    if (len != 0)
                    {
                        err = 1;
                        break;
                    }
                    ev.Meta_ev.data = NULL;
                    break;

                default:
                    if (len > 0)
                    {
                        uint8_t *unknown_data = (uint8_t *)malloc((size_t)len);
                        if (!unknown_data)
                        {
                            err = 1;
                            break;
                        }
                        size_t n_read = fread(unknown_data, 1, (size_t)len, f);
                        if (n_read != (size_t)len)
                        {
                            free(unknown_data);
                            err = 1;
                            break;
                        }
                        ev.Meta_ev.data = (const uint8_t *)unknown_data;
                        bytes_read += len;
                    }
                    else
                    {
                        ev.Meta_ev.data = NULL;
                    }
                    break;
                }
                if (err == 1)
                    return 0;

                if (!mtrk_push_event(&mtrk, &ev))
                    return 0;
                running_status = 0;
            }
            // sysex event
            else if (buf[0] == 0xF0 || buf[0] == 0xF7)
            {
                ev.kind = EV_SYSEX;
                bytes_read += 1;

                ev.Sysex_ev.sysex_type = buf[0];

                long len_pos = ftell(f);
                uint8_t vbuf[4];
                size_t remaining = chunk_size - bytes_read;
                size_t to_read = remaining < 4 ? remaining : 4;

                if (fread(vbuf, 1, to_read, f) != to_read)
                    return 0;

                int ok = 0, nvlq = 0;
                uint32_t len = parse_VLQ(vbuf, &ok, &nvlq);
                if (!ok || nvlq > (int)to_read)
                    return 0;

                ev.Sysex_ev.len = len;
                bytes_read += (uint32_t)nvlq;
                fseek(f, len_pos + nvlq, SEEK_SET);

                if (len > (chunk_size - bytes_read))
                    return 0;

                uint8_t *payload = NULL;
                if (len > 0)
                {
                    payload = (uint8_t *)malloc((size_t)len);
                    if (!payload)
                        return 0;
                    if (fread(payload, 1, (size_t)len, f) != (size_t)len)
                    {
                        free(payload);
                        return 0;
                    }
                }

                ev.Sysex_ev.data = (const uint8_t *)payload;
                bytes_read += len;

                if (!mtrk_push_event(&mtrk, &ev))
                    return 0;
                running_status = 0;
            }
            // midi channel event
            else
            {
                ev.kind = EV_CH;
                if (!event_status_check(buf[0]))
                    return 0;

                ev.MIDI_channel_ev.status = buf[0];
                running_status = buf[0];
                bytes_read++;
                int type = (int)((buf[0] & 0xF0) >> 4);
                uint8_t params[2];
                if (type == PROGRAM_CHANGE || type == CHANNEL_AFTERTOUCH)
                {
                    // here we need to parse just one param
                    fread(params, sizeof(params) - 1, 1, f);
                    ev.MIDI_channel_ev.params = (uint16_t)params[0];
                    ev.MIDI_channel_ev.nparams = 1;
                    bytes_read += 1;
                }
                else
                {
                    fread(params, sizeof(params), 1, f);
                    ev.MIDI_channel_ev.params = ((uint16_t)params[1] << 8) | params[0];
                    ev.MIDI_channel_ev.nparams = 2;
                    bytes_read += 2;
                }

                if (!mtrk_push_event(&mtrk, &ev))
                    return 0;
            }
        }
        else
        {
            ev.kind = EV_CH;

            if (running_status == 0 || !event_status_check(running_status))
                return 0;

            ev.MIDI_channel_ev.status = running_status;

            int type = (int)((running_status & 0xF0) >> 4);

            uint8_t d1 = buf[0];
            if (d1 >= 0x80)
                return 0;

            bytes_read += 1;

            if (type == PROGRAM_CHANGE || type == CHANNEL_AFTERTOUCH)
            {
                ev.MIDI_channel_ev.params = (uint16_t)d1;
                ev.MIDI_channel_ev.nparams = 1;
            }
            else
            {
                if ((chunk_size - bytes_read) < 1)
                    return 0;

                uint8_t d2;
                if (fread(&d2, 1, 1, f) != 1)
                    return 0;
                if (d2 >= 0x80)
                    return 0;

                ev.MIDI_channel_ev.params = ((uint16_t)d2 << 8) | d1;
                ev.MIDI_channel_ev.nparams = 2;
                bytes_read += 1;
            }

            if (!mtrk_push_event(&mtrk, &ev))
                return 0;
        }
    }

    midi->mtrk[at_idx] = mtrk;

    return 1;
}

MIDI_file *get_MIDI_file(MThd *mthd, FILE *f)
{
    MIDI_file *midi = (MIDI_file *)malloc(sizeof(MIDI_file));
    if (!midi)
        return NULL;

    midi->mthd = *mthd;
    midi->mtrk = (MTrk *)malloc(mthd->ntracks * sizeof(MTrk));
    if (!midi->mtrk)
    {
        free(midi);
        return NULL;
    }

    memset(midi->mtrk, 0, mthd->ntracks * sizeof(MTrk));

    for (uint16_t i = 0; i < mthd->ntracks; ++i)
    {
        if (!parse_mtrk(midi, mthd, i, f))
        {
            free_midi_file(midi);
            return NULL;
        }
    }

    return midi;
}

MThd check_for_MThd(FILE *f)
{
    MThd mthd;
    memset(&mthd, 0, sizeof(MThd));

    uint8_t buf[6];

    fread(buf, sizeof(buf) - 2, 1, f);
    uint32_t id = make_u32_from_dword(buf);
    fread(buf, sizeof(buf) - 2, 1, f);
    uint32_t chunk_size = make_u32_from_dword(buf);

    if (id != MThd_string)
        return mthd;
    if (chunk_size != 0x00000006)
        return mthd;

    fread(buf, sizeof(buf), 1, f);

    mthd.fmt = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    mthd.ntracks = ((uint16_t)buf[2] << 8) | (uint16_t)buf[3];
    mthd.timediv.raw = ((uint16_t)buf[4] << 8) | (uint16_t)buf[5];
    mthd.is_valid = 1;

    set_timediv(&mthd);
    return mthd;
}

// --------- CLEANUP FUNCTIONS ------------------

static void free_event(Event *ev)
{
    if (ev->kind == EV_META && ev->Meta_ev.data)
    {
        free((void *)ev->Meta_ev.data);
        ev->Meta_ev.data = NULL;
    }
    else if (ev->kind == EV_SYSEX && ev->Sysex_ev.data)
    {
        free((void *)ev->Sysex_ev.data);
        ev->Sysex_ev.data = NULL;
    }
}

static void free_mtrk(MTrk *mtrk)
{
    if (!mtrk)
        return;

    for (size_t i = 0; i < mtrk->nevents; i++)
    {
        free_event(&mtrk->events[i]);
    }

    if (mtrk->events)
    {
        free(mtrk->events);
        mtrk->events = NULL;
    }

    mtrk->nevents = 0;
    mtrk->capacity = 0;
}

static void free_midi_file(MIDI_file *midi)
{
    if (!midi)
        return;

    if (midi->mtrk)
    {
        for (uint16_t i = 0; i < midi->mthd.ntracks; i++)
        {
            free_mtrk(&midi->mtrk[i]);
        }
        free(midi->mtrk);
        midi->mtrk = NULL;
    }

    free(midi);
}

// --------- EXPORT FUNCTIONS ------------------

static void export_midi_to_json(const MIDI_file *midi, const char *input_filename)
{
    if (!midi || !input_filename)
        return;

    char output_filename[256];
    const char *dot = strrchr(input_filename, '.');
    if (dot && dot != input_filename)
    {
        size_t base_len = dot - input_filename;
        if (base_len >= sizeof(output_filename) - 5)
            base_len = sizeof(output_filename) - 6;
        strncpy(output_filename, input_filename, base_len);
        strcpy(output_filename + base_len, ".json");
    }
    else
    {
        snprintf(output_filename, sizeof(output_filename), "%s.json", input_filename);
    }

    FILE *out = fopen(output_filename, "w");
    if (!out)
    {
        printf("Error: Could not create output file '%s'\n", output_filename);
        return;
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"header\": {\n");
    fprintf(out, "    \"format\": %u,\n", midi->mthd.fmt);
    fprintf(out, "    \"tracks\": %u,\n", midi->mthd.ntracks);

    if (midi->mthd.timediv.type == 0)
    {
        fprintf(out, "    \"time_division\": {\n");
        fprintf(out, "      \"type\": \"ticks_per_beat\",\n");
        fprintf(out, "      \"ticks_per_beat\": %u\n", midi->mthd.timediv.tpb);
        fprintf(out, "    }\n");
    }
    else
    {
        fprintf(out, "    \"time_division\": {\n");
        fprintf(out, "      \"type\": \"smpte\",\n");
        fprintf(out, "      \"smpte_format\": %d,\n", midi->mthd.timediv.fps.smpte);
        fprintf(out, "      \"ticks_per_frame\": %u\n", midi->mthd.timediv.fps.ticks);
        fprintf(out, "    }\n");
    }

    fprintf(out, "  },\n");
    fprintf(out, "  \"tracks\": [\n");

    for (uint16_t i = 0; i < midi->mthd.ntracks; i++)
    {
        const MTrk *track = &midi->mtrk[i];
        fprintf(out, "    {\n");
        fprintf(out, "      \"track_number\": %u,\n", i);
        fprintf(out, "      \"size\": %u,\n", track->size);
        fprintf(out, "      \"events\": [\n");

        for (size_t j = 0; j < track->nevents; j++)
        {
            const Event *ev = &track->events[j];
            fprintf(out, "        {\n");
            fprintf(out, "          \"delta_time\": %u,\n", ev->delta_time);

            switch (ev->kind)
            {
            case EV_CH:
                fprintf(out, "          \"type\": \"channel\",\n");
                fprintf(out, "          \"status\": %u,\n", ev->MIDI_channel_ev.status);
                fprintf(out, "          \"channel\": %u,\n", (ev->MIDI_channel_ev.status & 0x0F) + 1);
                fprintf(out, "          \"event_type\": %u,\n", (ev->MIDI_channel_ev.status & 0xF0) >> 4);
                fprintf(out, "          \"params\": %u,\n", ev->MIDI_channel_ev.params);
                fprintf(out, "          \"param_count\": %zu\n", ev->MIDI_channel_ev.nparams);
                break;

            case EV_META:
                fprintf(out, "          \"type\": \"meta\",\n");
                fprintf(out, "          \"meta_type\": %u,\n", ev->Meta_ev.meta_type);
                fprintf(out, "          \"length\": %u,\n", ev->Meta_ev.len);

                if (ev->Meta_ev.data && ev->Meta_ev.len > 0)
                {
                    switch (ev->Meta_ev.meta_type)
                    {
                    case 0x01:
                    case 0x02:
                    case 0x03:
                    case 0x04:
                    case 0x05:
                    case 0x06:
                    case 0x07:
                        fprintf(out, "          \"text\": \"");
                        for (uint32_t k = 0; k < ev->Meta_ev.len; k++)
                        {
                            char c = (char)ev->Meta_ev.data[k];
                            if (c == '"' || c == '\\')
                                fprintf(out, "\\%c", c);
                            else if (c >= 32 && c <= 126)
                                fprintf(out, "%c", c);
                            else
                                fprintf(out, "\\u%04x", (unsigned char)c);
                        }
                        fprintf(out, "\"\n");
                        break;

                    case 0x51:
                        if (ev->Meta_ev.len == 3)
                        {
                            uint32_t microsec_per_qn = ((uint32_t)ev->Meta_ev.data[0] << 16) |
                                                       ((uint32_t)ev->Meta_ev.data[1] << 8) |
                                                       ((uint32_t)ev->Meta_ev.data[2]);
                            fprintf(out, "          \"microseconds_per_quarter_note\": %u\n", microsec_per_qn);
                        }
                        else
                        {
                            fprintf(out, "          \"data\": [");
                            for (uint32_t k = 0; k < ev->Meta_ev.len; k++)
                            {
                                fprintf(out, "%u", ev->Meta_ev.data[k]);
                                if (k < ev->Meta_ev.len - 1)
                                    fprintf(out, ",");
                            }
                            fprintf(out, "]\n");
                        }
                        break;

                    default:
                        fprintf(out, "          \"data\": [");
                        for (uint32_t k = 0; k < ev->Meta_ev.len; k++)
                        {
                            fprintf(out, "%u", ev->Meta_ev.data[k]);
                            if (k < ev->Meta_ev.len - 1)
                                fprintf(out, ",");
                        }
                        fprintf(out, "]\n");
                        break;
                    }
                }
                else
                {
                    fprintf(out, "          \"data\": null\n");
                }
                break;

            case EV_SYSEX:
                fprintf(out, "          \"type\": \"sysex\",\n");
                fprintf(out, "          \"sysex_type\": %u,\n", ev->Sysex_ev.sysex_type);
                fprintf(out, "          \"length\": %u,\n", ev->Sysex_ev.len);

                if (ev->Sysex_ev.data && ev->Sysex_ev.len > 0)
                {
                    fprintf(out, "          \"data\": [");
                    for (uint32_t k = 0; k < ev->Sysex_ev.len; k++)
                    {
                        fprintf(out, "%u", ev->Sysex_ev.data[k]);
                        if (k < ev->Sysex_ev.len - 1)
                            fprintf(out, ",");
                    }
                    fprintf(out, "]\n");
                }
                else
                {
                    fprintf(out, "          \"data\": null\n");
                }
                break;
            }

            fprintf(out, "        }");
            if (j < track->nevents - 1)
                fprintf(out, ",");
            fprintf(out, "\n");
        }

        fprintf(out, "      ]\n");
        fprintf(out, "    }");
        if (i < midi->mthd.ntracks - 1)
            fprintf(out, ",");
        fprintf(out, "\n");
    }

    fprintf(out, "  ]\n");
    fprintf(out, "}\n");

    fclose(out);
    printf("MIDI data exported to '%s'\n", output_filename);
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
        fclose(file);
        return 1;
    }

    printf("Parsing track chunks...\n");
    MIDI_file *midi = get_MIDI_file(&mthd, file);
    if (!midi)
    {
        printf("[[One or more MTrk are invalid. Exiting...]]\n");
        fclose(file);
        return 1;
    }

    fclose(file);

    printf("Parsing successfully completed.\n");
    export_midi_to_json(midi, filename);

    free_midi_file(midi);
    return 0;
}