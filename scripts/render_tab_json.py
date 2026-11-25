#!/usr/bin/env python3
"""
render_tab_json.py - Full-Pipeline Guitar Score Validator, Musical Expression Engine,
ASCII Tab Visualizer, Standard MIDI Exporter & Multi-Instrument C++ Physical Modeling Renderer

Usage:
    python3 render_tab_json.py <input_score.json> <output.wav> \
        [--instrument steel_acoustic|nylon_classical|electric_clean|electric_overdrive] \
        [--mic balanced|close|room] [--measures START-END] \
        [--preview-tab] [--midi] [--mp3] [--qa] [--normalize] [--no-strict]
"""

import argparse
import difflib
import json
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# 1. Articulation, Instrument, Mic & Pattern Maps
# ---------------------------------------------------------------------------
ART_MAP = {
    "REST": "ART_REST",
    "PICK": "ART_PICK",
    "PALM_MUTE": "ART_PALM_MUTE",
    "PM": "ART_PALM_MUTE",
    "DEAD_NOTE": "ART_DEAD_NOTE",
    "MUTE": "ART_DEAD_NOTE",
    "CHUCK_MUTE": "ART_CHUCK_MUTE",
    "CHUCK": "ART_CHUCK_MUTE",
    "CHOKE": "ART_CHUCK_MUTE",
    "STACCATO": "ART_STACCATO",
    "HAMMER": "ART_HAMMER",
    "PULL": "ART_PULL",
    "TAP": "ART_TAP",
    "SLIDE": "ART_SLIDE",
    "SHIFT_SLIDE": "ART_SHIFT_SLIDE",
    "SLIDE_IN": "ART_SLIDE_IN",
    "SLIDE_OUT": "ART_SLIDE_OUT",
    "BEND_QUARTER": "ART_BEND_QUARTER",
    "BEND_HALF_UP": "ART_BEND_HALF_UP",
    "BEND_UP": "ART_BEND_UP",
    "BEND_1_5_UP": "ART_BEND_1_5_UP",
    "BEND_UP_REL": "ART_BEND_UP_REL",
    "PREBEND_REL": "ART_PREBEND_REL",
    "BEND_HOLD_UP": "ART_BEND_HOLD_UP",
    "VIBRATO": "ART_VIBRATO",
    "VIB_BEND_TAIL": "ART_VIB_BEND_TAIL",
    "TREMOLO": "ART_TREMOLO",
    "TRILL": "ART_TRILL",
    "CHORD": "ART_CHORD",
    "ARP_ROLL": "ART_ARP_ROLL",
    "ARP_UP": "ART_ARP_ROLL",
    "ARP_DOWN": "ART_ARP_DOWN",
    "RASGUEADO": "ART_RASGUEADO",
    "HARMONIC": "ART_HARMONIC",
    "NH": "ART_HARMONIC",
    "ARTIFICIAL_HARM": "ART_ARTIFICIAL_HARM",
    "AH": "ART_ARTIFICIAL_HARM",
    "TAPPED_HARM": "ART_TAPPED_HARM",
    "TH": "ART_TAPPED_HARM",
    "PERC_KICK": "ART_PERC_KICK",
    "KICK": "ART_PERC_KICK",
    "PERC_SLAP": "ART_PERC_SLAP",
    "SLAP": "ART_PERC_SLAP",
    "PERC_TAP": "ART_PERC_TAP",
    "GOLPE": "ART_PERC_TAP",
    "SLAP_STRUM": "ART_SLAP_STRUM",
    "AM": "ART_SLAP_STRUM",
    "GRACE_NOTE": "ART_GRACE_NOTE",
    "GRACE": "ART_GRACE_NOTE",
    "LET_RING": "ART_LET_RING",
    "TIE": "ART_LET_RING",
    "FRET_NOISE": "ART_FRET_NOISE",
    "SLIDE_NOISE": "ART_FRET_NOISE",
    "PICK_SCRAPE": "ART_PICK_SCRAPE",
    "SCRAPE": "ART_PICK_SCRAPE",
    "SLAP_HARMONIC": "ART_SLAP_HARMONIC",
    "SLAP_HARM": "ART_SLAP_HARMONIC",
    "SH": "ART_SLAP_HARMONIC",
}

INSTRUMENT_MAP = {
    "steel_acoustic":     ("INST_STEEL_ACOUSTIC", 25),     # GM Acoustic Guitar (steel)
    "nylon_classical":    ("INST_NYLON_CLASSICAL", 24),    # GM Acoustic Guitar (nylon)
    "electric_clean":     ("INST_ELECTRIC_CLEAN", 27),     # GM Electric Guitar (clean)
    "electric_overdrive": ("INST_ELECTRIC_OVERDRIVE", 29), # GM Overdriven Guitar
}

MIC_MAP = {
    "balanced": "MIC_BALANCED",
    "close":    "MIC_CLOSE",
    "room":     "MIC_ROOM",
}

VALID_PATTERNS = {
    "fingerpick_ballad",
    "fingerpick_53231323",
    "fingerpick_half",
    "fingerpick_quarter",
    "arpeggio_hold",
    "arpeggio_cinematic",
    "strum_folk",
    "waltz_3_4",
    "ballad_6_8",
}

# ---------------------------------------------------------------------------
# 2. Tuning Presets & Pitch Helpers
# ---------------------------------------------------------------------------
TUNING_PRESETS = {
    "standard":       [64, 59, 55, 50, 45, 40],
    "drop_d":         [64, 59, 55, 50, 45, 38],
    "dadgad":         [62, 57, 55, 50, 45, 38],
    "eb_standard":    [63, 58, 54, 49, 44, 39],
    "half_step_down": [63, 58, 54, 49, 44, 39],
    "open_d":         [62, 57, 54, 50, 45, 38],
    "open_g":         [62, 59, 55, 50, 43, 38],
}

NOTE_TO_PC = {
    "C": 0, "B#": 0,
    "C#": 1, "#C": 1, "DB": 1, "Db": 1,
    "D": 2,
    "D#": 3, "#D": 3, "EB": 3, "Eb": 3,
    "E": 4, "FB": 4, "Fb": 4,
    "F": 5, "E#": 5, "#E": 5,
    "F#": 6, "#F": 6, "GB": 6, "Gb": 6,
    "G": 7,
    "G#": 8, "#G": 8, "AB": 8, "Ab": 8,
    "A": 9,
    "A#": 10, "#A": 10, "BB": 10, "Bb": 10,
    "B": 11, "CB": 11, "Cb": 11,
}

PC_TO_SHARP = ["C", "#C", "D", "#D", "E", "F", "#F", "G", "#G", "A", "Bb", "B"]
MAJOR_SCALE_SEMITONES = {1: 0, 2: 2, 3: 4, 4: 5, 5: 7, 6: 9, 7: 11}
ROMAN_MAP = {"VII": 7, "VI": 6, "IV": 4, "V": 5, "III": 3, "II": 2, "I": 1}


def parse_note_midi(token) -> int:
    if isinstance(token, int):
        return token
    s = str(token).strip()
    m = re.match(r"^([#b]?[A-Ga-g][#b]?)(-?\d+)$", s)
    if not m:
        raise ValueError(f"Invalid MIDI/note specification in tuning: {token!r}")
    name, octv = m.group(1), int(m.group(2))
    name_norm = name[0].upper() + name[1:] if len(name) > 1 and name[0] not in "#b" else (name[0] + name[1].upper() if len(name) == 2 else name.upper())
    if name_norm not in NOTE_TO_PC:
        raise ValueError(f"Unknown pitch name in tuning: {token!r}")
    return (octv + 1) * 12 + NOTE_TO_PC[name_norm]


def resolve_tuning(tuning_spec) -> list:
    if not tuning_spec:
        return list(TUNING_PRESETS["standard"])
    if isinstance(tuning_spec, str):
        key = tuning_spec.strip().lower().replace("-", "_").replace(" ", "_")
        if key not in TUNING_PRESETS:
            sugg = difflib.get_close_matches(key, TUNING_PRESETS.keys(), n=3)
            raise ValueError(f"Unknown tuning preset '{tuning_spec}'. Valid: {sorted(TUNING_PRESETS.keys())}. Suggestions: {sugg}")
        return list(TUNING_PRESETS[key])
    if isinstance(tuning_spec, list) and len(tuning_spec) == 6:
        return [parse_note_midi(x) for x in tuning_spec]
    raise ValueError(f"Invalid tuning specification: {tuning_spec!r}.")


# ---------------------------------------------------------------------------
# 3. 12-Key Algorithmic Chord & Functional Degree Engine
# ---------------------------------------------------------------------------
BUILTIN_CHORDS = {
    "C":       {"1": 0, "2": 1, "3": 0, "4": 2, "5": 3, "6": -1, "bass": 5},
    "Cmaj7":   {"1": 0, "2": 0, "3": 0, "4": 2, "5": 3, "6": -1, "bass": 5},
    "Cadd9":   {"1": 3, "2": 3, "3": 0, "4": 2, "5": 3, "6": -1, "bass": 5},
    "C/E":     {"1": 0, "2": 1, "3": 0, "4": 2, "5": 3, "6": 0,  "bass": 6},
    "C/G":     {"1": 0, "2": 1, "3": 0, "4": 2, "5": 3, "6": 3,  "bass": 6},
    "Dm":      {"1": 1, "2": 3, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "Dm7":     {"1": 1, "2": 1, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "Em":      {"1": 0, "2": 0, "3": 0, "4": 2, "5": 2, "6": 0,  "bass": 6},
    "Em7":     {"1": 0, "2": 3, "3": 0, "4": 2, "5": 2, "6": 0,  "bass": 6},
    "Em/D":    {"1": 0, "2": 0, "3": 0, "4": 0, "5": -1,"6": -1, "bass": 4},
    "E":       {"1": 0, "2": 0, "3": 1, "4": 2, "5": 2, "6": 0,  "bass": 6},
    "E7":      {"1": 0, "2": 0, "3": 1, "4": 0, "5": 2, "6": 0,  "bass": 6},
    "E/#G":    {"1": 0, "2": 0, "3": 1, "4": 2, "5": 2, "6": 4,  "bass": 6},
    "F":       {"1": 1, "2": 1, "3": 2, "4": 3, "5": 3, "6": 1,  "bass": 6},
    "Fmaj7":   {"1": 0, "2": 1, "3": 2, "4": 3, "5": 3, "6": 1,  "bass": 6},
    "Fm":      {"1": 1, "2": 1, "3": 1, "4": 3, "5": 3, "6": 1,  "bass": 6},
    "F/G":     {"1": 1, "2": 1, "3": 2, "4": 3, "5": -1,"6": 3,  "bass": 6},
    "G":       {"1": 3, "2": 0, "3": 0, "4": 0, "5": 2, "6": 3,  "bass": 6},
    "G7":      {"1": 1, "2": 0, "3": 0, "4": 0, "5": 2, "6": 3,  "bass": 6},
    "Gsus4":   {"1": 3, "2": 1, "3": 0, "4": 0, "5": 2, "6": 3,  "bass": 6},
    "G/B":     {"1": 3, "2": 3, "3": 0, "4": 0, "5": 2, "6": -1, "bass": 5},
    "Am":      {"1": 0, "2": 1, "3": 2, "4": 2, "5": 0, "6": -1, "bass": 5},
    "Am7":     {"1": 0, "2": 1, "3": 0, "4": 2, "5": 0, "6": -1, "bass": 5},
    "Am/G":    {"1": 0, "2": 1, "3": 2, "4": 2, "5": 0, "6": 3,  "bass": 6},
    "D":       {"1": 2, "2": 3, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "D7":      {"1": 2, "2": 1, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "Dsus2":   {"1": 0, "2": 3, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "Dsus4":   {"1": 3, "2": 3, "3": 2, "4": 0, "5": -1,"6": -1, "bass": 4},
    "D/#F":    {"1": 2, "2": 3, "3": 2, "4": 0, "5": 0, "6": 2,  "bass": 6},
    "A":       {"1": 0, "2": 2, "3": 2, "4": 2, "5": 0, "6": -1, "bass": 5},
    "A7":      {"1": 0, "2": 2, "3": 0, "4": 2, "5": 0, "6": -1, "bass": 5},
    "Asus2":   {"1": 0, "2": 0, "3": 2, "4": 2, "5": 0, "6": -1, "bass": 5},
    "Asus4":   {"1": 0, "2": 3, "3": 2, "4": 2, "5": 0, "6": -1, "bass": 5},
    "A/#C":    {"1": 0, "2": 2, "3": 2, "4": 2, "5": 4, "6": -1, "bass": 5},
    "Bm":      {"1": 2, "2": 3, "3": 4, "4": 4, "5": 2, "6": -1, "bass": 5},
    "Bm7":     {"1": 2, "2": 3, "3": 2, "4": 4, "5": 2, "6": -1, "bass": 5},
    "Bm7b5":   {"1": -1,"2": 3, "3": 2, "4": 3, "5": 2, "6": -1, "bass": 5},
    "Bm/A":    {"1": 2, "2": 3, "3": 4, "4": 4, "5": 0, "6": -1, "bass": 5},
    "B":       {"1": 2, "2": 4, "3": 4, "4": 4, "5": 2, "6": -1, "bass": 5},
    "B7":      {"1": 2, "2": 0, "3": 2, "4": 1, "5": 2, "6": -1, "bass": 5},
    "Bsus4":   {"1": 0, "2": 0, "3": 4, "4": 4, "5": 2, "6": -1, "bass": 5},
    "B/#D":    {"1": 2, "2": 4, "3": 4, "4": 1, "5": -1,"6": -1, "bass": 4},
    "#Fm":     {"1": 2, "2": 2, "3": 2, "4": 4, "5": 4, "6": 2,  "bass": 6},
    "#Fm7":    {"1": 2, "2": 2, "3": 2, "4": 2, "5": 4, "6": 2,  "bass": 6},
    "#Fm7b5":  {"1": 0, "2": 1, "3": 2, "4": 2, "5": -1,"6": 2,  "bass": 6},
    "#Fm11":   {"1": 0, "2": 0, "3": 2, "4": 4, "5": 4, "6": 2,  "bass": 6},
    "#Fm/E":   {"1": 0, "2": 0, "3": 2, "4": 4, "5": 4, "6": 0,  "bass": 6},
    "#F7":     {"1": 2, "2": 2, "3": 3, "4": 2, "5": 4, "6": 2,  "bass": 6},
    "#Gm7":    {"1": 4, "2": 4, "3": 4, "4": 4, "5": 6, "6": 4,  "bass": 6},
    "#G7":     {"1": 4, "2": 4, "3": 5, "4": 4, "5": 6, "6": 4,  "bass": 6},
    "#Cm7":    {"1": 4, "2": 5, "3": 4, "4": 6, "5": 4, "6": -1, "bass": 5},
    "#Cm/B":   {"1": 4, "2": 5, "3": 4, "4": 6, "5": 2, "6": -1, "bass": 5},
    "#C7":     {"1": 4, "2": 6, "3": 4, "4": 6, "5": 4, "6": -1, "bass": 5},
    "Bb":      {"1": 1, "2": 3, "3": 3, "4": 3, "5": 1, "6": -1, "bass": 5},
    "A(5)":    {"1": 5, "2": 5, "3": 6, "4": 7, "5": 7, "6": 5,  "bass": 6},
    "E(7)":    {"1": 7, "2": 9, "3": 9, "4": 9, "5": 7, "6": -1, "bass": 5},
    "Bm7(7)":  {"1": 7, "2": 7, "3": 7, "4": 7, "5": 9, "6": 7,  "bass": 6},
    "C(8)":    {"1": 8, "2": 8, "3": 9, "4": 10,"5": 10,"6": 8,  "bass": 6},
}

QUALITY_INTERVALS = {
    "":       [0, 4, 7],
    "maj":    [0, 4, 7],
    "m":      [0, 3, 7],
    "min":    [0, 3, 7],
    "5":      [0, 7],
    "7":      [0, 4, 7, 10],
    "maj7":   [0, 4, 7, 11],
    "m7":     [0, 3, 7, 10],
    "m7b5":   [0, 3, 6, 10],
    "m7-5":   [0, 3, 6, 10],
    "dim":    [0, 3, 6],
    "dim7":   [0, 3, 6, 9],
    "aug":    [0, 4, 8],
    "sus2":   [0, 2, 7],
    "sus4":   [0, 5, 7],
    "7sus4":  [0, 5, 7, 10],
    "add9":   [0, 2, 4, 7],
    "madd9":  [0, 2, 3, 7],
    "6":      [0, 4, 7, 9],
    "m6":     [0, 3, 7, 9],
    "9":      [0, 2, 4, 7, 10],
    "m9":     [0, 2, 3, 7, 10],
    "m11":    [0, 3, 5, 7, 10],
}


def normalize_chord_name(name: str) -> str:
    s = name.strip().replace("m7-5", "m7b5")
    out = []
    for p in s.split("/"):
        m = re.match(r"^([A-G])#(.*)$", p)
        out.append(f"#{m.group(1)}{m.group(2)}" if m else p)
    return "/".join(out)


def parse_pitch_class(token: str) -> int:
    t = token.strip()
    if t in NOTE_TO_PC:
        return NOTE_TO_PC[t]
    raise ValueError(f"Unknown pitch class: {token!r}")


def synthesize_voicing(chord_name: str, tuning_midi: list) -> dict:
    parts = chord_name.strip().split("/")
    if len(parts) > 2:
        raise ValueError(f"Invalid chord syntax: {chord_name!r}")
    main = parts[0]
    bass_str = parts[1] if len(parts) == 2 else None

    m = re.match(r"^([#b]?[A-G][#b]?)(.*)$", main)
    if not m:
        raise ValueError(f"Cannot parse chord name: {chord_name!r}")
    root_tok, qual = m.group(1), m.group(2).strip()
    qual_norm = qual.replace("m7-5", "m7b5")
    if root_tok not in NOTE_TO_PC:
        raise ValueError(f"Unknown chord root '{root_tok}' in '{chord_name}'")
    if qual_norm not in QUALITY_INTERVALS:
        sugg = difflib.get_close_matches(qual_norm, QUALITY_INTERVALS.keys(), n=3)
        raise ValueError(f"Unsupported chord quality '{qual}' in '{chord_name}'. Suggestions: {sugg}")

    root_pc = NOTE_TO_PC[root_tok]
    bass_pc = parse_pitch_class(bass_str) if bass_str else root_pc
    chord_pcs = {(root_pc + iv) % 12 for iv in QUALITY_INTERVALS[qual_norm]}

    best_bass = None
    for s_no in (6, 5, 4):
        open_pc = tuning_midi[s_no - 1] % 12
        fret = (bass_pc - open_pc) % 12
        if fret <= 7:
            score = fret + (0 if s_no == 6 else (0.5 if s_no == 5 else 1.5))
            if best_bass is None or score < best_bass[0]:
                best_bass = (score, s_no, fret)
    if best_bass is None:
        s_no = 5
        fret = (bass_pc - tuning_midi[s_no - 1]) % 12
        best_bass = (fret, s_no, fret)

    _, bass_string, bass_fret = best_bass
    anchor = max(0, bass_fret - 1) if bass_fret >= 2 else 0

    voicing = {"bass": bass_string}
    for s_no in range(1, 7):
        if s_no > bass_string:
            voicing[str(s_no)] = -1
            continue
        if s_no == bass_string:
            voicing[str(s_no)] = bass_fret
            continue
        open_pc = tuning_midi[s_no - 1] % 12
        candidates = []
        for f in range(0, 13):
            if (open_pc + f) % 12 in chord_pcs:
                if f == 0 and anchor <= 1:
                    candidates.append((0.0, f))
                elif anchor <= f <= anchor + 4:
                    candidates.append((abs(f - bass_fret) + 0.5, f))
        if candidates:
            candidates.sort()
            voicing[str(s_no)] = candidates[0][1]
        else:
            best_f = min(range(0, 12), key=lambda f: (0 if (open_pc + f) % 12 in chord_pcs else 99) + abs(f - bass_fret))
            voicing[str(s_no)] = best_f
    return voicing


def resolve_chord_voicing(chord_name: str, chords_dict: dict, tuning_midi: list) -> dict:
    if chord_name in chords_dict:
        return chords_dict[chord_name]
    norm = normalize_chord_name(chord_name)
    if norm in chords_dict:
        return chords_dict[norm]
    if tuning_midi == TUNING_PRESETS["standard"]:
        if chord_name in BUILTIN_CHORDS:
            return BUILTIN_CHORDS[chord_name]
        if norm in BUILTIN_CHORDS:
            return BUILTIN_CHORDS[norm]
    return synthesize_voicing(chord_name, tuning_midi)


def parse_single_degree(tok: str) -> tuple:
    s = tok.strip()
    m = re.match(r"^([#b]?)([1-7]|VII|VI|IV|V|III|II|I)(.*)$", s)
    if not m:
        raise ValueError(f"Invalid functional degree token: {tok!r}")
    acc, deg_part, qual = m.group(1), m.group(2), m.group(3).strip()
    deg_num = int(deg_part) if deg_part.isdigit() else ROMAN_MAP[deg_part]
    semi = MAJOR_SCALE_SEMITONES[deg_num] + (1 if acc == "#" else (-1 if acc == "b" else 0))
    return semi % 12, qual


def resolve_degree_to_chord(deg_str: str, key_name: str) -> str:
    key_clean = key_name.strip()
    if key_clean not in NOTE_TO_PC:
        raise ValueError(f"Unsupported key '{key_name}'.")
    key_pc = NOTE_TO_PC[key_clean]
    parts = str(deg_str).strip().split("/")
    if len(parts) > 2:
        raise ValueError(f"Invalid slash degree '{deg_str}'")
    semi, qual = parse_single_degree(parts[0])
    root_name = PC_TO_SHARP[(key_pc + semi) % 12]
    chord = f"{root_name}{qual.replace('m7-5', 'm7b5')}"
    if len(parts) == 2:
        b_semi, _ = parse_single_degree(parts[1])
        bass_name = PC_TO_SHARP[(key_pc + b_semi) % 12]
        chord = f"{chord}/{bass_name}"
    return chord


# ---------------------------------------------------------------------------
# 4. Accompaniment Pattern Expander & Voice-Led Jianpu Fingerstyle Expander
# ---------------------------------------------------------------------------
def expand_progression_item(item: dict, key_name: str, default_pattern: str,
                            chords_dict: dict, tuning_midi: list) -> list:
    m = int(item.get("measure", 1))
    beats = float(item.get("beats", 4.0))
    pattern = str(item.get("pattern", default_pattern)).strip()
    if pattern not in VALID_PATTERNS:
        sugg = difflib.get_close_matches(pattern, VALID_PATTERNS, n=3)
        raise ValueError(f"Unknown accompaniment pattern '{pattern}' in measure {m}. Suggestions: {sugg}")

    chord_name = str(item.get("chord", "")).strip()
    if not chord_name:
        if "degree" not in item:
            raise ValueError(f"Progression item in measure {m} must specify 'chord' or 'degree'.")
        chord_name = resolve_degree_to_chord(item["degree"], key_name)

    cinfo = resolve_chord_voicing(chord_name, chords_dict, tuning_midi)
    chords_dict[chord_name] = cinfo

    bass = int(cinfo.get("bass", 5))
    if bass < 4 or int(cinfo.get(str(bass), -1)) < 0:
        for candidate in (6, 5, 4):
            if int(cinfo.get(str(candidate), -1)) >= 0:
                bass = candidate
                break

    vel_base = float(item.get("velocity", 1.0))
    ev_list = []

    def add_ev(dur, art, str_list, vel=1.0):
        ev_list.append({
            "measure": m, "dur_beats": dur, "art": art,
            "chord": chord_name, "strings": str_list,
            "velocity": round(vel * vel_base, 3),
        })

    if pattern == "arpeggio_hold":
        add_ev(beats, "ARP_ROLL", [bass, 3, 2, 1], 0.95)
    elif pattern == "arpeggio_cinematic":
        if beats >= 3.9:
            add_ev(1.5, "ARP_ROLL", [bass, 3, 2, 1], 1.00)
            add_ev(0.5, "PICK", [3], 0.80)
            add_ev(0.5, "PICK", [2], 0.85)
            add_ev(0.5, "PICK", [4], 0.78)
            add_ev(beats - 3.0, "PICK", [3], 0.82)
        else:
            add_ev(beats, "ARP_ROLL", [bass, 3, 2, 1], 0.95)
    elif pattern in ("waltz_3_4", "ballad_6_8") or abs(beats - 3.0) < 0.05:
        steps = [([bass], 1.05), ([3], 0.80), ([2], 0.85), ([1], 0.92), ([2], 0.82), ([3], 0.78)]
        step_dur = beats / 6.0
        for st, v in steps:
            add_ev(step_dur, "PICK", st, v)
    elif pattern == "strum_folk" and beats >= 3.9:
        add_ev(1.0, "ARP_ROLL", [bass, 4, 3], 1.08)
        add_ev(0.5, "ARP_ROLL", [bass, 4, 3, 2, 1], 0.95)
        add_ev(1.0, "ARP_DOWN", [1, 2, 3], 0.85)
        add_ev(0.5, "ARP_DOWN", [1, 2, 3], 0.80)
        add_ev(0.5, "ARP_ROLL", [bass, 4, 3, 2, 1], 1.00)
        add_ev(0.5, "ARP_DOWN", [1, 2, 3], 0.82)
    elif pattern == "fingerpick_53231323" and beats >= 3.9:
        for st, v in (([bass], 1.05), ([3], 0.80), ([2], 0.88), ([3], 0.78),
                      ([1], 0.95), ([3], 0.78), ([2], 0.86), ([3], 0.76)):
            add_ev(0.5, "PICK", st, v)
    elif pattern == "fingerpick_quarter" or abs(beats - 1.0) < 0.05:
        add_ev(0.5, "PICK", [bass], 1.02)
        add_ev(beats - 0.5, "PICK", [3, 2, 1], 0.88)
    elif pattern == "fingerpick_half" or abs(beats - 2.0) < 0.05:
        for st, v in (([bass], 1.05), ([4], 0.80), ([3, 2], 0.90), ([3], 0.78)):
            add_ev(0.5, "PICK", st, v)
    elif beats >= 3.9:
        for st, v in (([bass], 1.05), ([4], 0.80), ([3, 2], 0.92), ([4], 0.78),
                      ([1], 0.96), ([3], 0.78), ([2], 0.88), ([3], 0.76)):
            add_ev(0.5, "PICK", st, v)
    else:
        add_ev(beats, "ARP_ROLL", [bass, 3, 2, 1], 0.95)

    return ev_list


def parse_jianpu_to_midi(token: str, key_name: str, base_octave_midi: int = 60) -> int:
    s = str(token).strip()
    if s in ("0", "-", "REST"):
        return -1
    m = re.match(r"^([#b]?)([1-7])([.,']*)$", s)
    if not m:
        raise ValueError(f"Invalid Jianpu pitch token: {token!r}")
    acc, deg, oct_marks = m.group(1), int(m.group(2)), m.group(3)
    key_pc = NOTE_TO_PC[key_name]
    semi = MAJOR_SCALE_SEMITONES[deg] + (1 if acc == "#" else (-1 if acc == "b" else 0))
    oct_shift = oct_marks.count("'") - (oct_marks.count(".") + oct_marks.count(","))
    root_c4 = base_octave_midi + key_pc
    if root_c4 > 67:
        root_c4 -= 12
    return root_c4 + semi + 12 * oct_shift


def expand_jianpu_section(jianpu_items: list, key_name: str, chords_dict: dict, tuning_midi: list) -> list:
    """Expands E-type Jianpu melody + chord/degree with hand-position continuity & melody-over-accompaniment balance."""
    events = []
    last_hand_pos = 2.0
    for item in jianpu_items:
        m = int(item.get("measure", 1))
        chord_name = str(item.get("chord", "")).strip()
        if not chord_name and "degree" in item:
            chord_name = resolve_degree_to_chord(item["degree"], key_name)
        cinfo = None
        bass_str, bass_fret = 5, 0
        if chord_name:
            cinfo = resolve_chord_voicing(chord_name, chords_dict, tuning_midi)
            chords_dict[chord_name] = cinfo
            bass_str = int(cinfo.get("bass", 5))
            bass_fret = int(cinfo.get(str(bass_str), 0))
            if bass_fret < 0:
                for cand in (6, 5, 4):
                    if int(cinfo.get(str(cand), -1)) >= 0:
                        bass_str, bass_fret = cand, int(cinfo[str(cand)])
                        break
            if bass_fret > 0:
                last_hand_pos = float(bass_fret)

        beat_cursor = 0.0
        for idx, n in enumerate(item.get("notes", [])):
            dur = float(n["beats"])
            pitch_tok = str(n.get("pitch", "0")).strip()
            midi = parse_jianpu_to_midi(pitch_tok, key_name)
            art = str(n.get("art", "PICK")).upper()
            vel = float(n.get("velocity", 1.0))

            ev_notes = []
            add_bass = (cinfo is not None) and (idx == 0 or (abs(beat_cursor - 2.0) < 0.05 and n.get("bass", True)))
            if add_bass:
                ev_notes.append({"string": bass_str, "fret": bass_fret, "art": "PICK", "velocity": round(0.92 * vel, 3)})

            if midi >= 0:
                # Choose (string, fret) minimizing hand jump while preferring open strings & treble strings 1-3
                candidates = []
                for s_no in (1, 2, 3, 4, 5):
                    fret = midi - tuning_midi[s_no - 1]
                    if 0 <= fret <= 15:
                        jump_cost = 0.0 if fret == 0 else abs(fret - last_hand_pos)
                        str_penalty = (s_no - 1) * 0.65
                        high_fret_penalty = max(0, fret - 9) * 1.2
                        candidates.append((jump_cost + str_penalty + high_fret_penalty, s_no, fret))
                if not candidates:
                    raise ValueError(f"Jianpu note '{pitch_tok}' (MIDI {midi}) out of guitar range in measure {m}")
                candidates.sort()
                _, chosen_s, chosen_f = candidates[0]
                if chosen_f > 0:
                    last_hand_pos = 0.6 * last_hand_pos + 0.4 * chosen_f

                if ev_notes and ev_notes[0]["string"] == chosen_s:
                    ev_notes.clear()
                ev_notes.append({"string": chosen_s, "fret": chosen_f, "art": art, "velocity": round(1.12 * vel, 3)})

                if idx == 0 and cinfo is not None:
                    used = {x["string"] for x in ev_notes}
                    for inner_s in (3, 4, 2):
                        if inner_s not in used and int(cinfo.get(str(inner_s), -1)) >= 0:
                            ev_notes.append({
                                "string": inner_s, "fret": int(cinfo[str(inner_s)]),
                                "art": "PICK", "velocity": round(0.72 * vel, 3),
                            })
                            break

            if not ev_notes:
                events.append({"measure": m, "dur_beats": dur, "art": "REST", "chord": chord_name})
            else:
                events.append({
                    "measure": m, "dur_beats": dur,
                    "art": "CHORD" if len(ev_notes) > 1 and art == "PICK" else art,
                    "chord": chord_name, "notes": ev_notes, "velocity": vel,
                })
            beat_cursor += dur
    return events


# ---------------------------------------------------------------------------
# 5. Macro Expression (Dynamics, Groove, Rubato), Sanity Linter & ASCII Tab Preview
# ---------------------------------------------------------------------------
def resolve_dynamics_for_measure(measure: int, dynamics_map: list) -> float:
    if not dynamics_map:
        return 1.0
    level = 1.0
    for entry in dynamics_map:
        m_start = int(entry.get("measure", 1))
        m_end = int(entry.get("end_measure", m_start))
        lvl = float(entry.get("level", 1.0))
        if "crescendo_to" in entry and m_start <= measure <= m_end and m_end > m_start:
            t = (measure - m_start) / float(m_end - m_start)
            return lvl + t * (float(entry["crescendo_to"]) - lvl)
        if measure >= m_start:
            level = float(entry.get("crescendo_to", lvl)) if measure > m_end else lvl
    return max(0.3, min(1.4, level))


def compute_metric_accent(beat_pos: float, beats_per_bar: float) -> float:
    """Subtle natural metric groove weight (downbeat slightly accented, off-beats softer)."""
    if abs(beat_pos) < 0.05:
        return 1.06
    if abs(beats_per_bar - 4.0) < 0.1 and abs(beat_pos - 2.0) < 0.05:
        return 1.01
    if abs(beat_pos - round(beat_pos)) < 0.05:
        return 0.97
    return 0.93


def print_ascii_tab_preview(resolved_events: list) -> None:
    """Prints a readable 6-line ASCII tab with measure headers and mm:ss.s timeline."""
    print("\n=== ASCII Tab & Timeline Preview ===")
    by_measure = {}
    for ev in resolved_events:
        by_measure.setdefault(ev["measure"], []).append(ev)

    art_tag = {
        "ART_HAMMER": "h", "ART_PULL": "p", "ART_TAP": "t",
        "ART_SLIDE": "/", "ART_SHIFT_SLIDE": "/", "ART_SLIDE_IN": "/", "ART_SLIDE_OUT": "\\",
        "ART_BEND_UP": "b", "ART_BEND_HALF_UP": "b", "ART_BEND_QUARTER": "b", "ART_BEND_1_5_UP": "b",
        "ART_BEND_UP_REL": "br", "ART_PREBEND_REL": "pb", "ART_VIBRATO": "~",
        "ART_HARMONIC": "*", "ART_ARTIFICIAL_HARM": "AH", "ART_TAPPED_HARM": "TH",
        "ART_SLAP_HARMONIC": "SH", "ART_PALM_MUTE": "pm", "ART_DEAD_NOTE": "x",
        "ART_STACCATO": ".", "ART_GRACE_NOTE": "g", "ART_LET_RING": "L",
    }

    labels = ["e|", "B|", "G|", "D|", "A|", "E|"]
    for m in sorted(by_measure.keys()):
        evs = by_measure[m]
        t0 = evs[0]["start_sec"]
        mm, ss = int(t0 // 60), t0 % 60.0
        chords_seen = []
        for e in evs:
            if e["chord"] and (not chords_seen or chords_seen[-1] != e["chord"]):
                chords_seen.append(e["chord"])
        chord_hdr = " -> ".join(chords_seen) if chords_seen else "-"
        print(f"Measure {m:3d}  [{mm:02d}:{ss:04.1f}]  Chords: {chord_hdr}")
        rows = [lbl for lbl in labels]
        for e in evs:
            col = {}
            for s_no, f_no, n_art, _, _ in e["notes"]:
                if n_art == "ART_DEAD_NOTE":
                    tok = "x"
                elif n_art == "ART_FRET_NOISE":
                    tok = "~n"
                elif n_art == "ART_PICK_SCRAPE":
                    tok = "sc"
                else:
                    tok = f"{f_no}{art_tag.get(n_art, '')}"
                col[s_no] = tok
            if not col:
                if e["art"] == "ART_PERC_KICK":
                    col[6] = "K"
                elif e["art"] == "ART_PERC_SLAP":
                    col[5] = "S"
                elif e["art"] == "ART_PERC_TAP":
                    col[1] = "T"
                elif e["art"] == "ART_CHUCK_MUTE":
                    for s_no in range(1, 7):
                        col[s_no] = "X"
            width = max([2] + [len(v) + 1 for v in col.values()])
            for s_no in range(1, 7):
                tok = col.get(s_no, "-")
                rows[s_no - 1] += tok.ljust(width, "-")
        for r in rows:
            print("  " + r + "|")


def run_music_sanity_linter(resolved_events: list) -> None:
    """Checks for extreme sudden left-hand fret jumps (>8 frets in <0.5 beat) that often indicate OCR typos."""
    prev_fret, prev_m = None, None
    warnings = []
    for ev in resolved_events:
        fretted = [f for _, f, art, _, _ in ev["notes"] if f > 0 and art not in ("ART_HARMONIC", "ART_TAPPED_HARM", "ART_TAP")]
        if not fretted:
            if any(f == 0 for _, f, _, _, _ in ev["notes"]):
                prev_fret = None  # Open string allows comfortable hand shift
            continue
        cur_f = sum(fretted) / len(fretted)
        if prev_fret is not None and ev["dur_beats"] <= 0.5 and abs(cur_f - prev_fret) > 8.0:
            warnings.append(f"Measure {ev['measure']} (from M{prev_m}): rapid fret jump {prev_fret:.0f} -> {cur_f:.0f} (check OCR accuracy)")
        prev_fret, prev_m = cur_f, ev["measure"]
    for w in warnings[:6]:
        print(f"[Linter INFO] {w}")


# ---------------------------------------------------------------------------
# 6. Zero-Dependency Standard MIDI File (SMF Type 1) Exporter
# ---------------------------------------------------------------------------
def _vlq(val: int) -> bytes:
    val = max(0, int(val))
    out = [val & 0x7F]
    val >>= 7
    while val:
        out.append((val & 0x7F) | 0x80)
        val >>= 7
    return bytes(reversed(out))


def _midi_track(events: list) -> bytes:
    events_sorted = sorted(events, key=lambda x: (x[0], x[1]))
    data = bytearray()
    last_tick = 0
    for tick, _, raw in events_sorted:
        delta = max(0, tick - last_tick)
        data.extend(_vlq(delta))
        data.extend(raw)
        last_tick = tick
    data.extend(_vlq(0) + b"\xFF\x2F\x00")
    return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)


def write_standard_midi(midi_path: str, resolved_events: list, tuning_midi: list,
                        capo: int, time_sig: str, gm_program: int, title: str) -> None:
    tpq = 480
    ts_m = re.match(r"^(\d+)\s*/\s*(\d+)$", time_sig)
    ts_num, ts_den = (int(ts_m.group(1)), int(ts_m.group(2))) if ts_m else (4, 4)
    ts_den_pow = int(round(math.log2(ts_den)))

    # Track 0: Tempo & Meta
    meta = [
        (0, 0, b"\xFF\x03" + _vlq(len(title.encode("utf-8"))) + title.encode("utf-8")),
        (0, 1, b"\xFF\x58\x04" + bytes([ts_num, ts_den_pow, 24, 8])),
    ]
    last_bpm = None
    tick_cursor = 0
    per_string_tracks = {s: [(0, 0, bytes([0xC0 | (s - 1), gm_program]))] for s in range(1, 7)}

    for ev in resolved_events:
        if ev["bpm"] != last_bpm:
            us_per_quarter = int(round(60_000_000.0 / ev["bpm"]))
            meta.append((tick_cursor, 2, b"\xFF\x51\x03" + us_per_quarter.to_bytes(3, "big")))
            last_bpm = ev["bpm"]

        dur_ticks = max(1, int(round(ev["dur_beats"] * tpq)))
        for k, (s_no, f_no, n_art, aux_f, vel) in enumerate(ev["notes"]):
            if f_no < 0 or n_art in ("ART_FRET_NOISE", "ART_PICK_SCRAPE"):
                continue
            ch = s_no - 1
            midi_note = tuning_midi[s_no - 1] + capo + f_no
            if n_art == "ART_HARMONIC":
                midi_note = tuning_midi[s_no - 1] + capo + (12 if f_no == 12 else (19 if f_no in (7, 19) else 24))
            elif n_art in ("ART_ARTIFICIAL_HARM", "ART_TAPPED_HARM", "ART_SLAP_HARMONIC"):
                midi_note += 12
            midi_note = max(0, min(127, midi_note))
            m_vel = max(1, min(127, int(round(vel * 92))))

            roll_ticks = 0
            if ev["art"] in ("ART_ARP_ROLL", "ART_ARP_DOWN"):
                roll_ticks = k * 14
            elif ev["art"] in ("ART_RASGUEADO", "ART_SLAP_STRUM"):
                roll_ticks = k * 8

            t_on = tick_cursor + roll_ticks
            note_len = max(12, int(dur_ticks * (0.42 if n_art == "ART_STACCATO" else 0.95)))
            t_off = t_on + note_len

            trk = per_string_tracks[s_no]
            # Reset pitch bend to center (8192) on note onset
            trk.append((t_on, 1, bytes([0xE0 | ch, 0x00, 0x40])))
            trk.append((t_on, 2, bytes([0x90 | ch, midi_note, m_vel])))

            # Pitch-bend expression curves (standard +/-2 semitone range: 4096 per semitone)
            if n_art in ("ART_BEND_QUARTER", "ART_BEND_HALF_UP", "ART_BEND_UP", "ART_BEND_UP_REL"):
                semis = 0.5 if n_art == "ART_BEND_QUARTER" else (1.0 if n_art == "ART_BEND_HALF_UP" else 2.0)
                steps = 8
                for st in range(1, steps + 1):
                    p = st / float(steps)
                    if n_art == "ART_BEND_UP_REL":
                        amt = semis * (p / 0.5) if p <= 0.5 else semis * (1.0 - (p - 0.5) / 0.5)
                    else:
                        amt = semis * min(1.0, p / 0.6)
                    pb = max(0, min(16383, int(round(8192 + amt * 4096))))
                    trk.append((t_on + int(note_len * p), 3, bytes([0xE0 | ch, pb & 0x7F, (pb >> 7) & 0x7F])))
            elif n_art == "ART_VIBRATO":
                for st in range(1, 7):
                    p = st / 6.0
                    pb = max(0, min(16383, int(round(8192 + 950 * math.sin(2.0 * math.pi * st * 0.5)))))
                    trk.append((t_on + int(note_len * p), 3, bytes([0xE0 | ch, pb & 0x7F, (pb >> 7) & 0x7F])))

            trk.append((t_off, 4, bytes([0x80 | ch, midi_note, 0])))
            trk.append((t_off, 5, bytes([0xE0 | ch, 0x00, 0x40])))

        tick_cursor += dur_ticks

    tracks = [_midi_track(meta)] + [_midi_track(per_string_tracks[s]) for s in range(1, 7)]
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), tpq)
    with open(midi_path, "wb") as f:
        f.write(header)
        for tr in tracks:
            f.write(tr)
    print(f"Rendered MIDI: {midi_path} (SMF Type 1, 6-string per-channel tracks)")


# ---------------------------------------------------------------------------
# 7. Validation, Resolution & C++ Code Generation
# ---------------------------------------------------------------------------
def parse_time_signature(ts_str: str) -> float:
    m = re.match(r"^(\d+)\s*/\s*(\d+)$", str(ts_str).strip())
    if not m:
        raise ValueError(f"Invalid time_signature '{ts_str}'.")
    num, den = int(m.group(1)), int(m.group(2))
    if num <= 0 or den not in (2, 4, 8, 16):
        raise ValueError(f"Unsupported time_signature '{ts_str}'.")
    return num * (4.0 / den)


def validate_art(art_str: str, context: str) -> str:
    key = str(art_str).strip().upper()
    if key not in ART_MAP:
        sugg = difflib.get_close_matches(key, ART_MAP.keys(), n=3)
        raise ValueError(f"Unknown articulation '{art_str}' at {context}. Suggestions: {sugg}")
    return ART_MAP[key]


def resolve_bpm_for_measure(measure: int, default_bpm: float, tempo_map: list) -> float:
    bpm = default_bpm
    for entry in tempo_map:
        if measure >= int(entry.get("measure", 1)):
            bpm = float(entry["bpm"])
    return bpm


def parse_measure_range(range_str: str) -> tuple:
    if not range_str:
        return None
    m = re.match(r"^(\d+)(?:\s*-\s*(\d+))?$", range_str.strip())
    if not m:
        raise ValueError(f"Invalid --measures format '{range_str}'. Expected 'M' or 'START-END' (e.g., '5-8').")
    s = int(m.group(1))
    e = int(m.group(2)) if m.group(2) else s
    if e < s:
        raise ValueError(f"Invalid --measures range: {s}-{e}")
    return (s, e)


def validate_and_resolve_score(score_data: dict, strict: bool = True,
                               inst_override: str = None, mic_override: str = None,
                               measure_range: tuple = None) -> dict:
    bpm = float(score_data.get("bpm", 72.0))
    if bpm <= 10.0 or bpm > 360.0:
        raise ValueError(f"Invalid BPM: {bpm}")
    capo = int(score_data.get("capo", 0))
    key_name = str(score_data.get("key", "C")).strip()
    if key_name not in NOTE_TO_PC:
        raise ValueError(f"Unknown key '{key_name}'.")
    time_sig = str(score_data.get("time_signature", "4/4"))
    expected_beats = parse_time_signature(time_sig)
    tuning_midi = resolve_tuning(score_data.get("tuning", "standard"))
    default_pattern = str(score_data.get("default_pattern", "fingerpick_ballad")).strip()
    tempo_map = sorted(score_data.get("tempo_map", []), key=lambda x: int(x.get("measure", 1)))
    dynamics_map = sorted(score_data.get("dynamics_map", []), key=lambda x: int(x.get("measure", 1)))
    rubato = bool(score_data.get("rubato", False))

    inst_key = (inst_override or str(score_data.get("instrument", "steel_acoustic"))).strip().lower()
    if inst_key not in INSTRUMENT_MAP:
        raise ValueError(f"Unknown instrument '{inst_key}'. Valid: {sorted(INSTRUMENT_MAP.keys())}")
    inst_enum, gm_program = INSTRUMENT_MAP[inst_key]

    mic_key = (mic_override or str(score_data.get("mic_position", "balanced"))).strip().lower()
    if mic_key not in MIC_MAP:
        raise ValueError(f"Unknown mic_position '{mic_key}'. Valid: {sorted(MIC_MAP.keys())}")
    mic_enum = MIC_MAP[mic_key]

    humanize_raw = score_data.get("humanize", False)
    humanize_amt = 1.0 if humanize_raw is True else (0.0 if humanize_raw is False else float(humanize_raw))
    rng = random.Random(int(score_data.get("humanize_seed", 42)))

    chords = {}
    if tuning_midi == TUNING_PRESETS["standard"]:
        chords.update(BUILTIN_CHORDS)
    for k, v in score_data.get("chords", {}).items():
        chords[k] = v

    events = list(score_data.get("events", []))
    if not events and "progression" in score_data:
        score_data["mode"] = score_data.get("mode", "accompaniment")
        for item in score_data["progression"]:
            events.extend(expand_progression_item(item, key_name, default_pattern, chords, tuning_midi))
    elif not events and "jianpu" in score_data:
        score_data["mode"] = score_data.get("mode", "accompaniment")
        events.extend(expand_jianpu_section(score_data["jianpu"], key_name, chords, tuning_midi))

    if not events:
        raise ValueError("Score contains no events (provide 'events', 'progression', or 'jianpu').")

    mode = str(score_data.get("mode", "solo")).strip().lower()
    if mode not in ("solo", "accompaniment"):
        raise ValueError(f"Invalid mode '{mode}'.")

    flexible_measures = set(int(x) for x in score_data.get("flexible_measures", []))
    if score_data.get("allow_pickup", False):
        flexible_measures.update((0, 1))

    measure_beats = {}
    total_beats = 0.0
    for idx, ev in enumerate(events):
        m = int(ev.get("measure", 1))
        b = float(ev["dur_beats"])
        if b <= 0:
            raise ValueError(f"Non-positive dur_beats ({b}) at event #{idx} (measure {m})")
        if ev.get("fermata", False) or ev.get("pickup", False):
            flexible_measures.add(m)
        measure_beats[m] = measure_beats.get(m, 0.0) + b
        total_beats += b

    max_measure = max(measure_beats.keys())
    print(f"[Validator] Title: {score_data.get('title', 'Untitled')}")
    print(f"[Validator] Instrument: {inst_key} | Mic: {mic_key} | Key: 1={key_name} | TimeSig: {time_sig} | BPM: {bpm} | Capo: {capo} | Mode: {mode}")

    beat_errors = []
    for m in sorted(measure_beats.keys()):
        mb = measure_beats[m]
        status = "OK"
        if m in flexible_measures:
            status = "FLEXIBLE"
        elif abs(mb - expected_beats) > 0.025:
            status = f"MISMATCH (expected {expected_beats:.2f})"
            beat_errors.append(f"Measure {m}: {mb:.4f} beats (expected {expected_beats:.2f})")
        if measure_range is None or (measure_range[0] <= m <= measure_range[1]):
            print(f"  Measure {m:3d}: {mb:.4f} beats [{status}]")

    if beat_errors:
        msg = "Measure beat-count mismatch detected:\n  - " + "\n  - ".join(beat_errors)
        if strict:
            raise ValueError(msg + "\n(Fix beat counts, mark pickup/fermata via 'flexible_measures', or pass --no-strict.)")
        print(f"[Validator WARN] {msg}", file=sys.stderr)

    # Resolve all events with dynamics, groove, rubato, per-note art, and timeline
    chord_id_map = {"": 0}
    next_cid = 1
    for cname in chords.keys():
        chord_id_map[cname] = next_cid
        next_cid += 1

    resolved = []
    cur_sec = 0.0
    beat_in_bar = {}

    for idx, ev in enumerate(events):
        m = int(ev.get("measure", 1))
        if measure_range and not (measure_range[0] <= m <= measure_range[1]):
            continue

        ev_bpm = float(ev.get("bpm", resolve_bpm_for_measure(m, bpm, tempo_map)))
        dur_beats = float(ev["dur_beats"])
        rubato_mult = 1.055 if (rubato and m == max_measure) else 1.0
        dur_sec = dur_beats * (60.0 / ev_bpm) * rubato_mult

        bpos = beat_in_bar.get(m, 0.0)
        beat_in_bar[m] = bpos + dur_beats

        ev_art = validate_art(ev.get("art", "PICK"), f"event #{idx} (measure {m})")
        dyn_mult = resolve_dynamics_for_measure(m, dynamics_map)
        groove_mult = compute_metric_accent(bpos, expected_beats) if humanize_amt > 0.0 else 1.0
        ev_vel = float(ev.get("velocity", 1.0)) * dyn_mult * groove_mult
        ev_aux = int(ev.get("slide_from", ev.get("trill_to", ev.get("harm_node", ev.get("grace_from", -1)))))

        cname = str(ev.get("chord", "")).strip()
        if cname and cname not in chords:
            chords[cname] = resolve_chord_voicing(cname, chords, tuning_midi)
            chord_id_map[cname] = next_cid
            next_cid += 1
        cid = chord_id_map.get(cname, 0)

        cf = [-1] * 7
        if cname and cname in chords:
            for s_idx in range(1, 7):
                cf[s_idx] = int(chords[cname].get(str(s_idx), -1))

        parsed_notes = []
        raw_notes = ev.get("notes", [])
        default_note_art = "ART_PICK" if ev_art in ("ART_PERC_KICK", "ART_PERC_TAP", "ART_PERC_SLAP", "ART_SLAP_STRUM") else ev_art

        if not raw_notes and "string" in ev:
            s_no = int(ev["string"])
            if not (1 <= s_no <= 6):
                raise ValueError(f"String out of range (1..6): {s_no} at event #{idx}")
            f_no = int(ev.get("fret", cf[s_no]))
            if f_no < -1 or f_no > 24:
                raise ValueError(f"Fret out of range (-1..24): {f_no} at event #{idx}")
            parsed_notes.append((s_no, f_no, default_note_art, ev_aux, ev_vel))
        elif not raw_notes and "strings" in ev:
            for s_raw in ev["strings"]:
                s_no = int(s_raw)
                if not (1 <= s_no <= 6):
                    raise ValueError(f"String out of range (1..6): {s_no} at event #{idx}")
                f_no = int(ev.get("fret", cf[s_no]))
                if f_no < -1 or f_no > 24:
                    raise ValueError(f"Fret out of range (-1..24): {f_no} at event #{idx}")
                parsed_notes.append((s_no, f_no, default_note_art, ev_aux, ev_vel))
        else:
            for n_idx, item in enumerate(raw_notes):
                s_no = int(item["string"])
                if not (1 <= s_no <= 6):
                    raise ValueError(f"String out of range (1..6): {s_no} at event #{idx} note #{n_idx}")
                f_no = int(item.get("fret", cf[s_no]))
                if f_no < -1 or f_no > 24:
                    raise ValueError(f"Fret out of range (-1..24): {f_no} at event #{idx} note #{n_idx}")
                n_art = validate_art(item["art"], f"event #{idx} note #{n_idx}") if "art" in item else default_note_art
                n_aux = int(item.get("slide_from", item.get("trill_to", item.get("harm_node", item.get("grace_from", ev_aux)))))
                n_vel = float(item.get("velocity", 1.0)) * dyn_mult * groove_mult if "velocity" in item else ev_vel
                parsed_notes.append((s_no, f_no, n_art, n_aux, n_vel))

        if ev_art == "ART_ARP_ROLL" and len(parsed_notes) > 1:
            parsed_notes.sort(key=lambda x: x[0], reverse=True)
        elif ev_art == "ART_ARP_DOWN" and len(parsed_notes) > 1:
            parsed_notes.sort(key=lambda x: x[0])

        jitters = []
        final_notes = []
        for s_no, f_no, n_art, n_aux, v_val in parsed_notes[:6]:
            jit = 0.0
            if humanize_amt > 0.0:
                v_val = max(0.05, min(1.5, v_val * (1.0 + rng.uniform(-0.04, 0.04) * humanize_amt)))
                jit = max(0.0, rng.uniform(0.0, 0.0035) * humanize_amt)
            else:
                v_val = max(0.05, min(1.5, v_val))
            final_notes.append((s_no, f_no, n_art, n_aux, round(v_val, 4)))
            jitters.append(round(jit, 6))

        resolved.append({
            "measure": m, "start_sec": cur_sec, "dur_beats": dur_beats, "dur_sec": dur_sec,
            "bpm": ev_bpm, "art": ev_art, "velocity": round(max(0.05, min(1.5, ev_vel)), 4),
            "chord": cname, "cid": cid, "cf": cf, "notes": final_notes, "jitters": jitters,
        })
        cur_sec += dur_sec

    if not resolved:
        raise ValueError(f"No events matched measure range {measure_range}.")

    return {
        "title": score_data.get("title", "Untitled"),
        "capo": capo, "tuning_midi": tuning_midi, "time_sig": time_sig, "mode": mode,
        "inst_enum": inst_enum, "gm_program": gm_program, "mic_enum": mic_enum,
        "resolved": resolved,
    }


def generate_cpp_from_resolved(ctx: dict, header_path: str) -> str:
    resolved = ctx["resolved"]
    cpp_lines = [
        f'#include "{header_path}"',
        "#include <string>",
        "using namespace GuitarSynth;",
        "",
        "struct GenEvent {",
        "    double dur_sec;",
        "    Articulation art;",
        "    double velocity;",
        "    int notes_count;",
        "    int strings[6];",
        "    int frets[6];",
        "    Articulation note_arts[6];",
        "    int note_aux[6];",
        "    double note_vels[6];",
        "    double note_jitters[6];",
        "    int chord_id;",
        "    int chord_frets[7];",
        "};",
        "",
        "int main(int argc, char **argv) {",
        '    const char *out_wav = (argc > 1) ? argv[1] : "out_guitar.wav";',
        f"    const int CAPO = {ctx['capo']};",
        f"    const int TUNING_MIDI[7] = {{0, {', '.join(str(x) for x in ctx['tuning_midi'])}}};",
        f"    const InstrumentType INST_TYPE = {ctx['inst_enum']};",
        f"    const MicPosition MIC_POS = {ctx['mic_enum']};",
        f"    const bool IS_SOLO = {'true' if ctx['mode'] == 'solo' else 'false'};",
        "    std::vector<GenEvent> events;",
        f"    events.reserve({len(resolved)});",
    ]

    for ev in resolved:
        nc = len(ev["notes"])
        s_arr, f_arr, a_arr, aux_arr, v_arr, j_arr = [0]*6, [0]*6, ["ART_PICK"]*6, [-1]*6, [1.0]*6, [0.0]*6
        for i in range(nc):
            s_arr[i], f_arr[i], a_arr[i], aux_arr[i], v_arr[i] = ev["notes"][i]
            j_arr[i] = ev["jitters"][i]
        cpp_lines.append(
            f"    events.push_back({{{ev['dur_sec']:.6f}, {ev['art']}, {ev['velocity']:.4f}, {nc}, "
            f"{{{', '.join(str(x) for x in s_arr)}}}, {{{', '.join(str(x) for x in f_arr)}}}, "
            f"{{{', '.join(a_arr)}}}, {{{', '.join(str(x) for x in aux_arr)}}}, "
            f"{{{', '.join(f'{x:.4f}' for x in v_arr)}}}, {{{', '.join(f'{x:.6f}' for x in j_arr)}}}, "
            f"{ev['cid']}, {{{', '.join(str(x) for x in ev['cf'])}}}}});"
        )

    cpp_lines += [
        "    GuitarString strings[7];",
        "    for (int i = 1; i <= 6; ++i) strings[i].init(i, CAPO, TUNING_MIDI[i], INST_TYPE);",
        "    BodyPercussion perc;",
        "    SympatheticResonator symp; symp.init(TUNING_MIDI, CAPO, INST_TYPE);",
        "    GuitarToneChain tone; tone.init(INST_TYPE);",
        "    BodyEarlyReflections er; er.init(MIC_POS);",
        "    StereoReverb reverb; reverb.init(MIC_POS);",
        "    std::vector<int16_t> pcm;",
        "    int prev_cid = 0;",
        "    for (const auto &ev : events) {",
        "        double dur_sec = ev.dur_sec;",
        "        int n_samples = (int)std::round(dur_sec * SAMPLE_RATE);",
        "        if (ev.art == ART_CHUCK_MUTE || ev.art == ART_PERC_SLAP) {",
        "            for (int s = 1; s <= 6; ++s) strings[s].damp_rate = 42.0;",
        "        } else if (IS_SOLO) {",
        "            if (ev.art != ART_REST && ev.notes_count > 0) {",
        "                for (int s = 1; s <= 6; ++s) {",
        "                    bool used = false;",
        "                    for (int k = 0; k < ev.notes_count; ++k) if (ev.strings[k] == s) used = true;",
        "                    if (!used && strings[s].can_auto_damp()) strings[s].damp_rate = 5.5;",
        "                }",
        "            }",
        "        } else {",
        "            if (ev.chord_id != 0 && ev.chord_id != prev_cid) {",
        "                for (int s = 1; s <= 6; ++s) {",
        "                    if (strings[s].current_fret != ev.chord_frets[s] && strings[s].can_auto_damp())",
        "                        strings[s].damp_rate = 6.5;",
        "                }",
        "                prev_cid = ev.chord_id;",
        "            }",
        "        }",
        "        if (ev.art == ART_PERC_KICK || ev.art == ART_PERC_SLAP || ev.art == ART_PERC_TAP ||",
        "            ev.art == ART_SLAP_STRUM || ev.art == ART_CHUCK_MUTE || ev.art == ART_SLAP_HARMONIC) {",
        "            perc.trigger(ev.art, ev.velocity);",
        "        }",
        "        if (ev.art != ART_REST && ev.art != ART_CHUCK_MUTE) {",
        "            for (int k = 0; k < ev.notes_count; ++k) {",
        "                int s = ev.strings[k]; int f = ev.frets[k];",
        "                Articulation trig_art = ev.note_arts[k];",
        "                if (s >= 1 && s <= 6 && (f >= 0 || trig_art == ART_DEAD_NOTE || trig_art == ART_FRET_NOISE || trig_art == ART_PICK_SCRAPE)) {",
        "                    double rdelay = ev.note_jitters[k];",
        "                    if (ev.art == ART_ARP_ROLL || ev.art == ART_ARP_DOWN) rdelay += k * 0.022;",
        "                    else if (ev.art == ART_RASGUEADO || ev.art == ART_SLAP_STRUM) rdelay += k * 0.012;",
        "                    strings[s].trigger(f, dur_sec, trig_art, ev.note_aux[k], rdelay, ev.note_vels[k]);",
        "                }",
        "            }",
        "        }",
        "        for (int n = 0; n < n_samples; ++n) {",
        "            double rawL = 0.0, rawR = 0.0;",
        "            for (int s = 1; s <= 6; ++s) {",
        "                double sig = strings[s].render_sample();",
        "                double pan = (3.5 - s) * 0.072;",
        "                rawL += sig * (1.0 - pan); rawR += sig * (1.0 + pan);",
        "            }",
        "            double p_sig = perc.render_sample();",
        "            rawL += p_sig; rawR += p_sig;",
        "            double mono = 0.5 * (rawL + rawR);",
        "            double symp_sig = symp.process(mono);",
        "            double body = tone.process(mono + symp_sig);",
        "            double inL = body + (rawL - mono) * 0.62;",
        "            double inR = body + (rawR - mono) * 0.62;",
        "            double erL = 0.0, erR = 0.0, outL = 0.0, outR = 0.0;",
        "            er.process(inL, inR, erL, erR);",
        "            reverb.process(erL, erR, outL, outR);",
        "            outL = std::tanh(outL * 1.42) * 0.88;",
        "            outR = std::tanh(outR * 1.42) * 0.88;",
        "            pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outL * 32767.0)));",
        "            pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outR * 32767.0)));",
        "        }",
        "    }",
        "    int tail = SAMPLE_RATE;",
        "    for (int n = 0; n < tail; ++n) {",
        "        double fade = 1.0 - (double)n / tail;",
        "        double raw = 0.0;",
        "        for (int s = 1; s <= 6; ++s) raw += strings[s].render_sample();",
        "        raw += perc.render_sample();",
        "        double body = tone.process(raw + symp.process(raw));",
        "        double erL = 0.0, erR = 0.0, outL = 0.0, outR = 0.0;",
        "        er.process(body, body, erL, erR);",
        "        reverb.process(erL, erR, outL, outR);",
        "        outL = std::tanh(outL * 1.42) * 0.88 * fade;",
        "        outR = std::tanh(outR * 1.42) * 0.88 * fade;",
        "        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outL * 32767.0)));",
        "        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outR * 32767.0)));",
        "    }",
        "    write_wav(out_wav, pcm);",
        '    std::printf("Rendered WAV: %s (%zu frames)\\n", out_wav, pcm.size() / 2);',
        "    return 0;",
        "}",
    ]
    return "\n".join(cpp_lines)


# ---------------------------------------------------------------------------
# 8. Cross-Platform Compile/Run (noexec-safe) + FFmpeg MP3 & Loudness QA
# ---------------------------------------------------------------------------
def compile_and_run_cpp(cpp_code: str, out_wav: str) -> None:
    out_dir = os.path.dirname(os.path.abspath(out_wav))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    last_err = None
    for parent in (None, out_dir, os.getcwd()):
        tmpdir = tempfile.mkdtemp(prefix=".guitar_build_", dir=parent)
        try:
            cpp_path = os.path.join(tmpdir, "gen_score.cpp")
            bin_path = os.path.join(tmpdir, "gen_score_bin")
            with open(cpp_path, "w", encoding="utf-8") as f:
                f.write(cpp_code)
            subprocess.run(["g++", "-O3", cpp_path, "-o", bin_path, "-lm"], check=True)
            os.chmod(bin_path, 0o755)
            try:
                subprocess.run([bin_path, out_wav], check=True)
                return
            except PermissionError as exc:
                last_err = exc
                continue
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)
    raise RuntimeError(f"Failed to execute compiled binary: {last_err}")


def run_volumedetect(audio_path: str) -> tuple:
    res = subprocess.run(
        ["ffmpeg", "-i", audio_path, "-af", "volumedetect", "-f", "null", "-"],
        capture_output=True, text=True, check=True,
    )
    mean_v, max_v = None, None
    for line in res.stderr.splitlines():
        m1 = re.search(r"mean_volume:\s*(-?[\d.]+)\s*dB", line)
        m2 = re.search(r"max_volume:\s*(-?[\d.]+)\s*dB", line)
        if m1: mean_v = float(m1.group(1))
        if m2: max_v = float(m2.group(1))
    return mean_v, max_v


def normalize_wav_if_needed(wav_path: str, mean_v: float, max_v: float) -> tuple:
    if max_v is None or mean_v is None:
        return mean_v, max_v
    if -6.0 <= max_v <= -1.5 and -20.0 <= mean_v <= -12.0:
        return mean_v, max_v
    gain_db = -2.5 - max_v
    if mean_v + gain_db > -12.2:
        gain_db = min(gain_db, -12.5 - mean_v)
    if abs(gain_db) < 0.3:
        return mean_v, max_v
    tmp_wav = wav_path + ".norm.wav"
    subprocess.run(
        ["ffmpeg", "-y", "-i", wav_path, "-af", f"volume={gain_db:.2f}dB", tmp_wav],
        capture_output=True, text=True, check=True,
    )
    os.replace(tmp_wav, wav_path)
    return run_volumedetect(wav_path)


def main():
    parser = argparse.ArgumentParser(description="Guitar Score Validator, ASCII Tab Previewer, MIDI Exporter & Multi-Instrument Audio Renderer")
    parser.add_argument("input_json", help="Path to input score JSON file")
    parser.add_argument("output_wav", help="Path to output WAV file")
    parser.add_argument("--instrument", choices=sorted(INSTRUMENT_MAP.keys()), help="Override guitar instrument preset")
    parser.add_argument("--mic", choices=sorted(MIC_MAP.keys()), help="Override microphone/room position")
    parser.add_argument("--measures", help="Render only a measure range, e.g. '5-8' or '12'")
    parser.add_argument("--preview-tab", action="store_true", help="Print 6-line ASCII tab & mm:ss.s timeline for visual auditing")
    parser.add_argument("--midi", action="store_true", help="Export Standard MIDI File (.mid) with per-string pitch-bend curves")
    parser.add_argument("--mp3", action="store_true", help="Also transcode to 320kbps stereo MP3")
    parser.add_argument("--qa", action="store_true", help="Run FFmpeg volumedetect loudness QA check")
    parser.add_argument("--normalize", action="store_true", help="Auto-adjust gain if outside QA loudness range")
    parser.add_argument("--no-strict", action="store_true", help="Downgrade measure beat-count mismatches to warnings")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(script_dir, "guitar_synth_engine.hpp")

    with open(args.input_json, "r", encoding="utf-8") as f:
        score_data = json.load(f)

    m_range = parse_measure_range(args.measures) if args.measures else None
    ctx = validate_and_resolve_score(
        score_data, strict=not args.no_strict,
        inst_override=args.instrument, mic_override=args.mic, measure_range=m_range,
    )
    run_music_sanity_linter(ctx["resolved"])
    if args.preview_tab:
        print_ascii_tab_preview(ctx["resolved"])

    cpp_code = generate_cpp_from_resolved(ctx, header_path)
    compile_and_run_cpp(cpp_code, args.output_wav)

    if args.midi:
        midi_path = os.path.splitext(args.output_wav)[0] + ".mid"
        write_standard_midi(midi_path, ctx["resolved"], ctx["tuning_midi"], ctx["capo"],
                            ctx["time_sig"], ctx["gm_program"], ctx["title"])

    if args.qa or args.normalize:
        mean_v, max_v = run_volumedetect(args.output_wav)
        if args.normalize:
            mean_v, max_v = normalize_wav_if_needed(args.output_wav, mean_v, max_v)
        ok_max = (max_v is not None) and (-6.0 <= max_v <= -1.5)
        ok_mean = (mean_v is not None) and (-20.0 <= mean_v <= -12.0)
        status = "PASS" if (ok_max and ok_mean) else "WARN"
        print(f"[QA {status}] max_volume={max_v:.1f} dB (target -6.0..-1.5), mean_volume={mean_v:.1f} dB (target -20.0..-12.0)")

    if args.mp3:
        mp3_path = os.path.splitext(args.output_wav)[0] + ".mp3"
        subprocess.run(
            ["ffmpeg", "-y", "-i", args.output_wav, "-codec:a", "libmp3lame", "-b:a", "320k", mp3_path],
            capture_output=True, text=True, check=True,
        )
        print(f"Rendered MP3: {mp3_path}")


if __name__ == "__main__":
    main()