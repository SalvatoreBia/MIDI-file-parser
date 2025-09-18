# MIDI File Parser

A MIDI file parser written in C that exports parsed data to JSON format. Includes a Python interface for analysis and playback using FluidSynth.

## Usage

## Quick Setup

Run the setup script to compile and install everything:
```bash
./setup.sh
```

Or use the Makefile (run `make help` to check for the options).

### Manual setup

First, compile the C parser:
```bash
gcc -o main main.c -lm
```

Then parse a MIDI file:
```bash
./main "your-file.mid"
```

This creates a JSON file with the same name containing all the parsed MIDI data.

### Analysis and playback

Load the parsed data with the Python script:
```bash
python3 midi_importer.py "your-file.json"
```

You can also load a MIDI file directly (it will look for the corresponding JSON file):
```bash
python3 midi_importer.py "your-file.mid"
```

### Available commands

Once the Python interface is running, you can use these commands:

- `play` - Play the MIDI file using FluidSynth
- `play-pygame` - Alternative playback using pygame
- `notes` - Show all note events
- `notes [track_number]` - Show notes for a specific track
- `tempo` - Display tempo changes
- `text` - Show text events and metadata
- `track <number>` - Display detailed track information
- `create <filename>` - Export the data back to MIDI format
- `quit` - Exit the program

## Requirements

### System packages
```bash
# Fedora/RHEL
sudo dnf install gcc fluidsynth soundfont-fluid python3-pip

# Ubuntu/Debian
sudo apt install gcc fluidsynth fluid-soundfont-gm python3-pip
```

### Python packages
```bash
pip3 install mido pygame
```

## What the parser handles

- MIDI Format 0 (single track) and Format 1 (multiple tracks)
- Note events (note on/off with velocity)
- Program changes (instrument selection)
- Control changes (volume, pan, etc.)
- Pitch bend events
- Meta events (tempo, text, track names, end of track)
- Various time divisions (ticks per quarter note)

## Audio playback

In order to avoid clipping:
- Low gain to avoid distortion
- Disabled reverb and chorus effects
- Reduced polyphony for stability

You can always change these values manually inside the `midi_importer.py` file.

### Credits
The `.mid` you find inside the project were downloaded from [here](https://homestuck.net/music/midis/nothomestuck/Undertale/piano/)