#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../include/json_generator.h"

static void write_hex_bytes(FILE *fp, const uint8_t *data, size_t len)
{
    fprintf(fp, "\"");
    for (size_t i = 0; i < len; ++i)
    {
        fprintf(fp, "%02X", data[i]);
        if (i < len - 1) fprintf(fp, " ");
    }
    fprintf(fp, "\"");
}

static void write_text_data(FILE *fp, const uint8_t *data, size_t len)
{
    fprintf(fp, "\"");
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t c = data[i];
        if (c == '"') fprintf(fp, "\\\"");
        else if (c == '\\') fprintf(fp, "\\\\");
        else if (c == '\n') fprintf(fp, "\\n");
        else if (c == '\r') fprintf(fp, "\\r");
        else if (c == '\t') fprintf(fp, "\\t");
        else if (c >= 32 && c <= 126) fprintf(fp, "%c", c);
        else fprintf(fp, "\\u%04X", c);
    }
    fprintf(fp, "\"");
}

static const char* get_channel_event_name(uint8_t type)
{
    switch (type)
    {
    case 0x8: return "Note Off";
    case 0x9: return "Note On";
    case 0xA: return "Polyphonic Key Pressure";
    case 0xB: return "Control Change";
    case 0xC: return "Program Change";
    case 0xD: return "Channel Pressure";
    case 0xE: return "Pitch Bend";
    default: return "Unknown Channel Event";
    }
}

static const char* get_meta_event_name(uint8_t type)
{
    switch (type)
    {
    case 0x00: return "Sequence Number";
    case 0x01: return "Text Event";
    case 0x02: return "Copyright Notice";
    case 0x03: return "Track Name";
    case 0x04: return "Instrument Name";
    case 0x05: return "Lyric";
    case 0x06: return "Marker";
    case 0x07: return "Cue Point";
    case 0x09: return "Device Name";
    case 0x20: return "MIDI Channel Prefix";
    case 0x21: return "MIDI port";
    case 0x2F: return "End of Track";
    case 0x51: return "Set Tempo";
    case 0x54: return "SMPTE Offset";
    case 0x58: return "Time Signature";
    case 0x59: return "Key Signature";
    case 0x7F: return "Sequencer Specific";
    default:   return "Unknown Meta Event";
    }
}

static void write_channel_event(FILE *fp, const Channel_event *ch)
{
    fprintf(fp, "{\n");
    fprintf(fp, "        \"type\": \"channel\",\n");
    fprintf(fp, "        \"name\": \"%s\",\n", get_channel_event_name(ch->type));
    fprintf(fp, "        \"channel\": %u,\n", ch->channel);
    fprintf(fp, "        \"message_type\": \"0x%X\",\n", ch->type);
    
    switch (ch->type)
    {
    case 0x8:
    case 0x9:
        fprintf(fp, "        \"note\": %u,\n", ch->param1);
        fprintf(fp, "        \"velocity\": %u\n", ch->param2);
        break;
    case 0xA:
        fprintf(fp, "        \"note\": %u,\n", ch->param1);
        fprintf(fp, "        \"pressure\": %u\n", ch->param2);
        break;
    case 0xB:
        fprintf(fp, "        \"controller\": %u,\n", ch->param1);
        fprintf(fp, "        \"value\": %u\n", ch->param2);
        break;
    case 0xC:
        fprintf(fp, "        \"program\": %u\n", ch->param1);
        break;
    case 0xD:
        fprintf(fp, "        \"pressure\": %u\n", ch->param1);
        break;
    case 0xE:
        fprintf(fp, "        \"lsb\": %u,\n", ch->param1);
        fprintf(fp, "        \"msb\": %u\n", ch->param2);
        break;
    }
    fprintf(fp, "      }");
}

static void write_meta_event(FILE *fp, const Meta_event *meta)
{
    fprintf(fp, "{\n");
    fprintf(fp, "        \"type\": \"meta\",\n");
    fprintf(fp, "        \"name\": \"%s\",\n", get_meta_event_name(meta->type));
    fprintf(fp, "        \"meta_type\": \"0x%02X\",\n", meta->type);
    fprintf(fp, "        \"length\": %u", meta->len);
    
    if (meta->data)
    {
        uint8_t *data = (uint8_t*)meta->data;
        fprintf(fp, ",\n");
        
        switch (meta->type)
        {
        case 0x00:
            fprintf(fp, "        \"sequence_number\": %u\n", (data[0] << 8) | data[1]);
            break;
            
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x09:
            fprintf(fp, "        \"text\": ");
            write_text_data(fp, data, meta->len);
            fprintf(fp, "\n");
            break;
            
        case 0x20:
            fprintf(fp, "        \"channel\": %u\n", data[0]);
            break;

        case 0x21:
            fprintf(fp, "        \"port\": %u\n", data[0]);
            break;
            
        case 0x2F:
            break;
            
        case 0x51:
            {
                uint32_t tempo = (data[0] << 16) | (data[1] << 8) | data[2];
                fprintf(fp, "        \"microseconds_per_quarter_note\": %u,\n", tempo);
                fprintf(fp, "        \"bpm\": %.2f\n", 60000000.0 / tempo);
            }
            break;
            
        case 0x54:
            {
                uint8_t hour = data[0] & 0x1F;
                uint8_t rr = (data[0] >> 5) & 0x03;
                const char* rate_names[] = {"24 fps", "25 fps", "30 fps (drop frame)", "30 fps"};
                fprintf(fp, "        \"hours\": %u,\n", hour);
                fprintf(fp, "        \"minutes\": %u,\n", data[1]);
                fprintf(fp, "        \"seconds\": %u,\n", data[2]);
                fprintf(fp, "        \"frames\": %u,\n", data[3]);
                fprintf(fp, "        \"fractional_frames\": %u,\n", data[4]);
                fprintf(fp, "        \"frame_rate\": \"%s\"\n", rate_names[rr]);
            }
            break;
            
        case 0x58:
            fprintf(fp, "        \"numerator\": %u,\n", data[0]);
            fprintf(fp, "        \"denominator\": %u,\n", 1 << data[1]);
            fprintf(fp, "        \"clocks_per_metronome_click\": %u,\n", data[2]);
            fprintf(fp, "        \"32nd_notes_per_24_clocks\": %u\n", data[3]);
            break;
            
        case 0x59:
            {
                int8_t key = *(int8_t*)data;
                uint8_t scale = data[1];
                fprintf(fp, "        \"key\": %d,\n", key);
                fprintf(fp, "        \"scale\": \"%s\"\n", scale ? "minor" : "major");
            }
            break;
            
        case 0x7F:
        default:
            fprintf(fp, "        \"data\": ");
            write_hex_bytes(fp, data, meta->len);
            fprintf(fp, "\n");
            break;
        }
    }
    else
    {
        fprintf(fp, "\n");
    }
    
    fprintf(fp, "      }");
}

static void write_sysex_event(FILE *fp, const Sysex_event *sysex)
{
    fprintf(fp, "{\n");
    fprintf(fp, "        \"type\": \"sysex\",\n");
    fprintf(fp, "        \"length\": %u,\n", sysex->len);
    fprintf(fp, "        \"data\": ");
    if (sysex->data && sysex->len > 0)
    {
        write_hex_bytes(fp, (uint8_t*)sysex->data, sysex->len);
    }
    else
    {
        fprintf(fp, "\"\"");
    }
    fprintf(fp, "\n      }");
}

static void write_mthd(FILE *fp, const MThd *mthd)
{
    fprintf(fp, "  \"header\": {\n");
    fprintf(fp, "    \"format\": %u,\n", mthd->fmt);
    fprintf(fp, "    \"tracks\": %u,\n", mthd->ntracks);
    
    if (mthd->fmt == 0 || mthd->fmt == 1)
    {
        fprintf(fp, "    \"time_division\": {\n");
        fprintf(fp, "      \"type\": \"ticks_per_beat\",\n");
        fprintf(fp, "      \"ticks_per_beat\": %u\n", mthd->timediv.ticks_per_beat);
        fprintf(fp, "    }\n");
    }
    else
    {
        fprintf(fp, "    \"time_division\": {\n");
        fprintf(fp, "      \"type\": \"frames_per_second\",\n");
        fprintf(fp, "      \"smpte_format\": %d,\n", mthd->timediv.frames_per_sec.smpte);
        fprintf(fp, "      \"ticks_per_frame\": %u\n", mthd->timediv.frames_per_sec.ticks);
        fprintf(fp, "    }\n");
    }
    
    fprintf(fp, "  }");
}

static void write_mtrk(FILE *fp, const MTrk *mtrk, uint16_t track_num)
{
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"track_number\": %u,\n", track_num);
    fprintf(fp, "      \"size\": %u,\n", mtrk->size);
    fprintf(fp, "      \"event_count\": %zu,\n", mtrk->count);
    fprintf(fp, "      \"events\": [\n");
    
    for (size_t i = 0; i < mtrk->count; ++i)
    {
        const MTrk_event *event = &mtrk->events[i];
        
        fprintf(fp, "        {\n");
        fprintf(fp, "          \"delta_time\": %u,\n", event->delta_time);
        fprintf(fp, "          \"event\": ");
        
        switch (event->kind)
        {
        case CH:
            write_channel_event(fp, &event->ev.channel_ev);
            break;
        case META:
            write_meta_event(fp, &event->ev.meta_ev);
            break;
        case SYS:
            write_sysex_event(fp, &event->ev.sysex_ev);
            break;
        }
        
        fprintf(fp, "\n        }");
        if (i < mtrk->count - 1) fprintf(fp, ",");
        fprintf(fp, "\n");
    }
    
    fprintf(fp, "      ]\n");
    fprintf(fp, "    }");
}

int write_MIDI_to_JSON(const MIDI_file *midi, FILE *fp)
{
    if (!midi || !fp) return 0;
    
    fprintf(fp, "{\n");
    
    write_mthd(fp, &midi->mthd);
    
    fprintf(fp, ",\n  \"tracks\": [\n");
    
    for (uint16_t i = 0; i < midi->mthd.ntracks; ++i)
    {
        write_mtrk(fp, &midi->mtrk[i], i);
        if (i < midi->mthd.ntracks - 1) fprintf(fp, ",");
        fprintf(fp, "\n");
    }
    
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    
    return 1;
}

int write_MIDI_to_JSON_file(const MIDI_file *midi, const char *filename)
{
    if (!midi || !filename) return 0;
    
    FILE *fp = fopen(filename, "w");
    if (!fp) return 0;
    
    int result = write_MIDI_to_JSON(midi, fp);
    fclose(fp);
    
    return result;
}
