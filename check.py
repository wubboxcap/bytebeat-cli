import sys
import mido
from mido import MidiFile

def get_midi_bpm(mid):
    """Extracts the initial BPM from the MIDI file's set_tempo messages."""
    for track in mid.tracks:
        for msg in track:
            if msg.type == 'set_tempo':
                # mido.tempo2bpm converts microseconds per beat to BPM
                return round(mido.tempo2bpm(msg.tempo), 2)
    return 120.0  # Standard MIDI default if no tempo track exists


def midi_to_dot_t(midi_path, root_note=60, time_scale=0.5):
    """
    Converts a MIDI file into the custom .t format.
    
    - duration = (delta_ticks / ticks_per_beat) * time_scale
    - root_note = MIDI offset pitch (60 = C3)
    - pitches space-separated (e.g. "0.5 0 4 7")
    """
    mid = MidiFile(midi_path)
    ticks_per_beat = mid.ticks_per_beat
    bpm = get_midi_bpm(mid)
    
    print(f"Loaded '{midi_path}' | Native BPM: {bpm} | Ticks Per Beat: {ticks_per_beat}")

    # Collect and flatten all note events across tracks
    events = []
    for track in mid.tracks:
        abs_ticks = 0
        for msg in track:
            abs_ticks += msg.time
            if msg.type in ('note_on', 'note_off'):
                is_on = (msg.type == 'note_on' and msg.velocity > 0)
                events.append((abs_ticks, is_on, msg.note))

    if not events:
        return '.t musescore ""'

    # Sort by tick time; evaluate note_off (False) before note_on (True) at exact same tick
    events.sort(key=lambda x: (x[0], x[1]))

    tokens = []
    active_notes = set()
    last_tick = events[0][0]

    i = 0
    while i < len(events):
        current_tick = events[i][0]
        delta_ticks = current_tick - last_tick

        if delta_ticks > 0:
            # Calculate musical beats adjusted by time_scale
            duration_beats = (delta_ticks / ticks_per_beat) * time_scale
            duration_beats = round(duration_beats, 4)
            
            dur_str = f"{int(duration_beats)}" if duration_beats.is_integer() else f"{duration_beats}"

            if active_notes:
                offsets = sorted([n - root_note for n in active_notes])
                pitch_str = " ".join(str(o) for o in offsets)
                tokens.append(f'"{dur_str} {pitch_str}"')
            else:
                tokens.append(f'"-{dur_str} 0"')

            last_tick = current_tick

        # Update active note state at current tick
        while i < len(events) and events[i][0] == current_tick:
            _, is_on, note = events[i]
            if is_on:
                active_notes.add(note)
            else:
                active_notes.discard(note)
            i += 1

    return f'.t musescore {" ".join(tokens)}'


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python midi_to_dot_t.py <path_to_midi> [output_file] [time_scale]")
        print("Example: python midi_to_dot_t.py song.mid out.txt 0.5")
        sys.exit(1)

    input_midi = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) >= 3 else None
    
    # Optional time scale multiplier (default 0.5 cuts durations in half)
    scale = float(sys.argv[3]) if len(sys.argv) >= 4 else 0.5

    result = midi_to_dot_t(input_midi, root_note=65, time_scale=scale)

    if output_path:
        with open(output_path, "w") as f:
            f.write(result + "\n")
        print(f"Successfully converted '{input_midi}' -> '{output_path}'")
    else:
        print(result)