#!/bin/bash

set -e  

echo "MIDI File Parser - Setup Script"
echo "================================"


command_exists() {
    command -v "$1" >/dev/null 2>&1
}


if [ ! -f "main.c" ]; then
    echo "Error: main.c not found. Run this script from the project directory."
    exit 1
fi


if ! command_exists gcc; then
    echo "Error: gcc not found. Please install gcc:"
    echo "  Fedora/RHEL: sudo dnf install gcc"
    echo "  Ubuntu/Debian: sudo apt install gcc"
    echo "  macOS: xcode-select --install"
    exit 1
fi


echo "Compiling C parser..."
if gcc -o main main.c -lm; then
    echo "C parser compiled successfully"
else
    echo "Failed to compile C parser"
    echo "Check that you have development tools installed."
    exit 1
fi


if ! command_exists python3; then
    echo "Python 3 is required but not installed"
    echo "Please install Python 3 for your system:"
    echo "  Fedora/RHEL: sudo dnf install python3 python3-pip"
    echo "  Ubuntu/Debian: sudo apt install python3 python3-pip"
    echo "  macOS: brew install python3"
    exit 1
fi


if ! command_exists pip3 && ! python3 -m pip --version >/dev/null 2>&1; then
    echo "pip not found. Please install pip for Python 3"
    exit 1
fi


echo "Installing Python dependencies..."
if pip3 install --user -r requirements.txt 2>/dev/null || python3 -m pip install --user -r requirements.txt; then
    echo "Python dependencies installed successfully"
else
    echo "Failed to install Python dependencies"
    echo "Try running manually: pip3 install --user mido pygame"
    echo "Or: python3 -m pip install --user mido pygame"
    exit 1
fi

echo "Checking optional requirements..."
if command_exists fluidsynth; then
    echo "FluidSynth found"
    
    SOUNDFONT_FOUND=false
    SOUNDFONT_PATHS=(
        "/usr/share/soundfonts/FluidR3_GM.sf2"
        "/usr/share/soundfonts/default.sf2" 
        "/usr/share/sounds/sf2/default.sf2"
        "/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls"  
    )
    
    for sf in "${SOUNDFONT_PATHS[@]}"; do
        if [ -f "$sf" ]; then
            echo "Soundfont found: $sf"
            SOUNDFONT_FOUND=true
            break
        fi
    done
    
    if [ "$SOUNDFONT_FOUND" = false ]; then
        echo "No soundfont found - audio playback may not work optimally"
        echo "Consider installing a soundfont package:"
        echo "  Fedora/RHEL: sudo dnf install soundfont-fluid"
        echo "  Ubuntu/Debian: sudo apt install fluid-soundfont-gm"
    fi
else
    echo "FluidSynth not found - high-quality audio playback will not be available"
    echo "The parser will still work, but for audio playback install FluidSynth:"
    echo "  Fedora/RHEL: sudo dnf install fluidsynth soundfont-fluid"
    echo "  Ubuntu/Debian: sudo apt install fluidsynth fluid-soundfont-gm"
    echo "  macOS: brew install fluidsynth"
fi

echo ""
echo "Setup complete!"
echo ""
echo "Usage:"
echo "  ./main my_midi_file.mid"
echo "  python3 midi_importer.py my_midi_file.json"
echo ""
