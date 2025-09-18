

import json
import sys
import time
import threading
from pathlib import Path
import subprocess
import tempfile
import os

try:
    import mido
    MIDO_AVAILABLE = True
except ImportError:
    MIDO_AVAILABLE = False
    print("Warning: mido not available. Install with: pip3 install --user mido")

try:
    import pygame
    PYGAME_AVAILABLE = True
except ImportError:
    PYGAME_AVAILABLE = False
    print("Warning: pygame not available. Install with: pip3 install --user pygame")

class MIDIPlayer:
    def __init__(self):
        self.soundfont_paths = [
            "/usr/share/soundfonts/FluidR3_GM.sf2",
            "/usr/share/soundfonts/default.sf2",
            "/usr/share/sounds/sf2/default.sf2"
        ]
        self.soundfont = None
        self.fluidsynth_process = None
        self.is_playing = False
        
        for sf_path in self.soundfont_paths:
            if os.path.exists(sf_path):
                self.soundfont = sf_path
                break
        
        if not self.soundfont:
            print("Warning: No soundfont found. MIDI playback may not work.")
        else:
            print(f"Using soundfont: {self.soundfont}")
    
    def create_midi_from_data(self, midi_data, output_file):
        if not MIDO_AVAILABLE:
            print("Error: mido library required for MIDI creation")
            return False
        
        try:
            mid = mido.MidiFile(type=midi_data.header.get('format', 1))
            
            time_div = midi_data.header.get('time_division', {})
            if time_div.get('type') == 'ticks_per_beat':
                mid.ticks_per_beat = time_div.get('ticks_per_beat', 480)
            
            for track_data in midi_data.tracks:
                track = mido.MidiTrack()
                
                for event_data in track_data.get('events', []):
                    delta_time = event_data.get('delta_time', 0)
                    
                    if event_data.get('type') == 'channel':
                        event_type = event_data.get('event_type')
                        channel = event_data.get('channel', 1) - 1  
                        params = event_data.get('params', 0)
                        
                        if event_type == 9:  
                            note = params & 0xFF
                            velocity = (params >> 8) & 0xFF
                            msg = mido.Message('note_on', channel=channel, note=note, 
                                             velocity=velocity, time=delta_time)
                        elif event_type == 8:  
                            note = params & 0xFF
                            velocity = (params >> 8) & 0xFF
                            msg = mido.Message('note_off', channel=channel, note=note, 
                                             velocity=velocity, time=delta_time)
                        elif event_type == 11:  
                            control = params & 0xFF
                            value = (params >> 8) & 0xFF
                            msg = mido.Message('control_change', channel=channel, 
                                             control=control, value=value, time=delta_time)
                        elif event_type == 12:  
                            program = params & 0xFF
                            msg = mido.Message('program_change', channel=channel, 
                                             program=program, time=delta_time)
                        else:
                            continue
                            
                        track.append(msg)
                        
                    elif event_data.get('type') == 'meta':
                        meta_type = event_data.get('meta_type')
                        
                        if meta_type == 0x51:  
                            microsec = event_data.get('microseconds_per_quarter_note', 500000)
                            msg = mido.MetaMessage('set_tempo', tempo=microsec, time=delta_time)
                        elif meta_type == 0x2F:  
                            msg = mido.MetaMessage('end_of_track', time=delta_time)
                        elif meta_type in [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07]:  
                            text = event_data.get('text', '')
                            if meta_type == 0x01:
                                msg = mido.MetaMessage('text', text=text, time=delta_time)
                            elif meta_type == 0x02:
                                msg = mido.MetaMessage('copyright', text=text, time=delta_time)
                            elif meta_type == 0x03:
                                msg = mido.MetaMessage('track_name', text=text, time=delta_time)
                            elif meta_type == 0x04:
                                msg = mido.MetaMessage('instrument_name', text=text, time=delta_time)
                            elif meta_type == 0x05:
                                msg = mido.MetaMessage('lyric', text=text, time=delta_time)
                            elif meta_type == 0x06:
                                msg = mido.MetaMessage('marker', text=text, time=delta_time)
                            elif meta_type == 0x07:
                                msg = mido.MetaMessage('cue_marker', text=text, time=delta_time)
                        else:
                            continue
                            
                        track.append(msg)
                
                if not track or track[-1].type != 'end_of_track':
                    track.append(mido.MetaMessage('end_of_track'))
                
                mid.tracks.append(track)
            
            mid.save(output_file)
            return True
            
        except Exception as e:
            print(f"Error creating MIDI file: {e}")
            return False
    
    def play_with_fluidsynth(self, midi_file):
        """Play MIDI file using FluidSynth with advanced clipping prevention."""
        if not self.soundfont:
            print("Error: No soundfont available for playback")
            return False
        
        try:
            cmd = ['fluidsynth', 
                   '-a', 'alsa',           
                   '-g', '0.4',     ## gain
                   '-R', '0',              
                   '-C', '0',              
                   '-z', '64',             
                   '-r', '44100',          
                   '-c', '2',              
                   '--audio-bufcount=8',   
                   '--audio-bufsize=512',  
                   self.soundfont, 
                   midi_file]
            
            print(f"Starting FluidSynth with anti-clipping settings...")
            print("If volume is too low, use your system volume control to adjust")
            print("Playing... Press Ctrl+C to stop")
            
            self.fluidsynth_process = subprocess.Popen(
                cmd, 
                stdout=subprocess.DEVNULL, 
                stderr=subprocess.DEVNULL
            )
            self.is_playing = True
            
            try:
                self.fluidsynth_process.wait()
            except KeyboardInterrupt:
                print("\nPlayback stopped by user")
                self.fluidsynth_process.terminate()
                self.fluidsynth_process.wait()
            
            self.is_playing = False
            return True
            
        except Exception as e:
            print(f"Error playing with FluidSynth: {e}")
            return False
    
    def play_with_pygame(self, midi_file):
        """Play MIDI file using pygame (alternative method)."""
        if not PYGAME_AVAILABLE:
            print("Error: pygame not available for playback")
            return False
        
        try:
            pygame.mixer.init(frequency=44100, size=-16, channels=2, buffer=1024)
            pygame.mixer.music.load(midi_file)
            pygame.mixer.music.play()
            
            print("Playing with pygame... Press Ctrl+C to stop")
            while pygame.mixer.music.get_busy():
                time.sleep(0.1)
            
            return True
            
        except Exception as e:
            print(f"Error playing with pygame: {e}")
            return False
    
    def stop(self):
        """Stop current playback."""
        if self.fluidsynth_process and self.fluidsynth_process.poll() is None:
            self.fluidsynth_process.terminate()
            self.fluidsynth_process.wait()
        
        if PYGAME_AVAILABLE:
            try:
                pygame.mixer.music.stop()
            except:
                pass
        
        self.is_playing = False

class MIDIData:
    def __init__(self, json_file=None):
        """Initialize MIDI data from JSON file or empty."""
        self.header = {}
        self.tracks = []
        
        if json_file:
            self.load_from_json(json_file)
    
    def load_from_json(self, json_file):
        """Load MIDI data from JSON file exported by C parser."""
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
                
            self.header = data.get('header', {})
            self.tracks = data.get('tracks', [])
            
            print(f"Loaded MIDI data from {json_file}")
            print(f"Format: {self.header.get('format', 'Unknown')}")
            print(f"Tracks: {self.header.get('tracks', 'Unknown')}")
            
            time_div = self.header.get('time_division', {})
            if time_div.get('type') == 'ticks_per_beat':
                print(f"Time Division: {time_div.get('ticks_per_beat')} ticks per beat")
            elif time_div.get('type') == 'smpte':
                print(f"Time Division: SMPTE {time_div.get('smpte_format')} format, {time_div.get('ticks_per_frame')} ticks/frame")
                
        except FileNotFoundError:
            print(f"Error: JSON file '{json_file}' not found.")
        except json.JSONDecodeError as e:
            print(f"Error parsing JSON: {e}")
        except Exception as e:
            print(f"Error loading MIDI data: {e}")
    
    def get_track_count(self):
        """Get the number of tracks."""
        return len(self.tracks)
    
    def get_track(self, track_number):
        """Get a specific track by number."""
        if 0 <= track_number < len(self.tracks):
            return self.tracks[track_number]
        return None
    
    def get_channel_events(self, track_number=None):
        """Get all channel events, optionally filtered by track."""
        events = []
        tracks_to_check = [self.tracks[track_number]] if track_number is not None else self.tracks
        
        for track in tracks_to_check:
            for event in track.get('events', []):
                if event.get('type') == 'channel':
                    events.append(event)
        
        return events
    
    def get_note_events(self, track_number=None):
        """Get all note on/off events."""
        note_events = []
        channel_events = self.get_channel_events(track_number)
        
        for event in channel_events:
            event_type = event.get('event_type')
            if event_type in [8, 9]:  
                
                params = event.get('params', 0)
                note = params & 0xFF
                velocity = (params >> 8) & 0xFF
                
                note_events.append({
                    'delta_time': event.get('delta_time'),
                    'type': 'note_off' if event_type == 8 else 'note_on',
                    'channel': event.get('channel'),
                    'note': note,
                    'velocity': velocity
                })
        
        return note_events
    
    def get_meta_events(self, track_number=None, meta_type=None):
        """Get meta events, optionally filtered by track and/or meta type."""
        events = []
        tracks_to_check = [self.tracks[track_number]] if track_number is not None else self.tracks
        
        for track in tracks_to_check:
            for event in track.get('events', []):
                if event.get('type') == 'meta':
                    if meta_type is None or event.get('meta_type') == meta_type:
                        events.append(event)
        
        return events
    
    def get_tempo_changes(self):
        """Get all tempo change events (meta type 0x51)."""
        tempo_events = self.get_meta_events(meta_type=0x51)
        
        tempos = []
        for event in tempo_events:
            microsec_per_qn = event.get('microseconds_per_quarter_note')
            if microsec_per_qn:
                bpm = 60000000 / microsec_per_qn
                tempos.append({
                    'delta_time': event.get('delta_time'),
                    'microseconds_per_quarter_note': microsec_per_qn,
                    'bpm': round(bpm, 2)
                })
        
        return tempos
    
    def get_text_events(self):
        """Get all text-based meta events."""
        text_events = []
        
        for meta_type in [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07]:
            events = self.get_meta_events(meta_type=meta_type)
            for event in events:
                text_events.append({
                    'delta_time': event.get('delta_time'),
                    'type': meta_type,
                    'text': event.get('text', '')
                })
        
        return text_events
    
    def print_summary(self):
        """Print a summary of the MIDI data."""
        print("\n" + "="*50)
        print("MIDI DATA SUMMARY")
        print("="*50)
        
        print(f"Format: {self.header.get('format')}")
        print(f"Number of tracks: {self.get_track_count()}")
        
        time_div = self.header.get('time_division', {})
        if time_div.get('type') == 'ticks_per_beat':
            print(f"Time division: {time_div.get('ticks_per_beat')} ticks per beat")
        
        total_events = sum(len(track.get('events', [])) for track in self.tracks)
        print(f"Total events: {total_events}")
        
        
        channel_events = len(self.get_channel_events())
        meta_events = len(self.get_meta_events())
        note_events = len(self.get_note_events())
        
        print(f"Channel events: {channel_events}")
        print(f"Meta events: {meta_events}")
        print(f"Note events: {note_events}")
        
        
        tempos = self.get_tempo_changes()
        if tempos:
            print(f"\nTempo changes: {len(tempos)}")
            for tempo in tempos:
                print(f"  Delta {tempo['delta_time']}: {tempo['bpm']} BPM")
        
        
        text_events = self.get_text_events()
        if text_events:
            print(f"\nText events: {len(text_events)}")
            for text_event in text_events[:5]:  
                print(f"  Delta {text_event['delta_time']}: \"{text_event['text']}\"")
            if len(text_events) > 5:
                print(f"  ... and {len(text_events) - 5} more")

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 midi_importer.py <json-file>")
        print("   or: python3 midi_importer.py <midi-file>  (will look for .json version)")
        sys.exit(1)
    
    input_file = sys.argv[1]
    
    
    if input_file.endswith('.mid'):
        json_file = input_file.replace('.mid', '.json')
        if not Path(json_file).exists():
            print(f"JSON file '{json_file}' not found.")
            print("Run the C parser first to generate the JSON file.")
            sys.exit(1)
    else:
        json_file = input_file
    
    
    midi = MIDIData(json_file)
    midi.print_summary()
    
    
    print("\n" + "="*50)
    print("Available commands:")
    print("  notes [track_num] - Show note events")
    print("  tempo - Show tempo changes")
    print("  text - Show text events")
    print("  track <num> - Show track info")
    print("  play - Play MIDI using FluidSynth")
    print("  play-pygame - Play MIDI using pygame")
    print("  create <filename> - Create MIDI file from parsed data")
    print("  quit - Exit")
    print("="*50)
    
    player = MIDIPlayer()
    
    while True:
        try:
            command = input("\n> ").strip().split()
            if not command:
                continue
                
            cmd = command[0].lower()
            
            if cmd == 'quit' or cmd == 'q':
                break
            elif cmd == 'notes':
                track_num = int(command[1]) if len(command) > 1 else None
                notes = midi.get_note_events(track_num)
                print(f"\nFound {len(notes)} note events:")
                for i, note in enumerate(notes[:10]):  
                    print(f"  {note['type']} - Note {note['note']}, Vel {note['velocity']}, Ch {note['channel']}, Delta {note['delta_time']}")
                if len(notes) > 10:
                    print(f"  ... and {len(notes) - 10} more")
            elif cmd == 'tempo':
                tempos = midi.get_tempo_changes()
                print(f"\nFound {len(tempos)} tempo changes:")
                for tempo in tempos:
                    print(f"  Delta {tempo['delta_time']}: {tempo['bpm']} BPM ({tempo['microseconds_per_quarter_note']} μs/qn)")
            elif cmd == 'text':
                texts = midi.get_text_events()
                print(f"\nFound {len(texts)} text events:")
                for text in texts:
                    print(f"  Delta {text['delta_time']}: \"{text['text']}\"")
            elif cmd == 'track':
                if len(command) > 1:
                    track_num = int(command[1])
                    track = midi.get_track(track_num)
                    if track:
                        print(f"\nTrack {track_num}:")
                        print(f"  Size: {track['size']} bytes")
                        print(f"  Events: {len(track['events'])}")
                    else:
                        print(f"Track {track_num} not found")
                else:
                    print("Usage: track <number>")
            elif cmd == 'play':
                
                original_midi = None
                if json_file.endswith('.json'):
                    potential_midi = json_file.replace('.json', '.mid')
                    if os.path.exists(potential_midi):
                        original_midi = potential_midi
                
                if original_midi:
                    print(f"Playing original MIDI file: {original_midi}")
                    player.play_with_fluidsynth(original_midi)
                    print("Playback finished.")
                else:
                    print("Original MIDI file not found. Try 'create <filename>' first, then play that file.")
                    
            elif cmd == 'play-pygame':
                
                original_midi = None
                if json_file.endswith('.json'):
                    potential_midi = json_file.replace('.json', '.mid')
                    if os.path.exists(potential_midi):
                        original_midi = potential_midi
                
                if original_midi:
                    print(f"Playing original MIDI file with pygame: {original_midi}")
                    player.play_with_pygame(original_midi)
                    print("Playback finished.")
                else:
                    print("Original MIDI file not found.")
                    
            elif cmd == 'create':
                if len(command) > 1:
                    output_file = command[1]
                    if not output_file.endswith('.mid'):
                        output_file += '.mid'
                    
                    if player.create_midi_from_data(midi, output_file):
                        print(f"MIDI file created: {output_file}")
                    else:
                        print("Failed to create MIDI file")
                else:
                    print("Usage: create <filename>")
            else:
                print(f"Unknown command: {cmd}")
                
        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except (ValueError, IndexError):
            print("Invalid command or arguments")
        except Exception as e:
            print(f"Error: {e}")

if __name__ == "__main__":
    main()
