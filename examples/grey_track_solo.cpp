#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

static const int SAMPLE_RATE = 48000;
static const double PI = 3.14159265358979323846;
static const double BPM = 72.0;
static const double BEAT_SEC = 60.0 / BPM; // 0.8333333333 sec

enum Articulation {
    ART_REST = 0,
    ART_PICK,
    ART_HAMMER,
    ART_PULL,
    ART_SLIDE,         // Continuation of previous note sliding to this fret
    ART_SLIDE_IN,      // Picked grace slide from (fret - 2) into fret
    ART_BEND_UP,       // Full bend up (+2 semitones)
    ART_BEND_UP_REL,   // Full bend up (+2 semitones) then release back to 0
    ART_PREBEND_REL,   // Start at +2 semitones and release to 0
    ART_BEND_HOLD_UP,  // Measure 14 beat 1: slight pre-bend settle then full bend at end
    ART_VIBRATO,       // Expressive vibrato
    ART_VIB_BEND_TAIL, // Measure 15 beat 3: vibrato then full bend up at the end
    ART_CHORD          // Multi-string simultaneous pluck / quick strum
};

struct NoteSpec {
    int measure;
    int string_no;     // 1..6 (1 = high E4, 6 = low E2), 0 for rest
    int fret;
    double dur_beats;
    Articulation art;
    // Optional extra chord notes (string, fret)
    int string2;
    int fret2;
    int string3;
    int fret3;
    int slide_from;    // Explicit start fret for slide
};

// Standard tuning MIDI numbers for strings 1..6
// String 1: E4 (64), String 2: B3 (59), String 3: G3 (55),
// String 4: D3 (50), String 5: A2 (45), String 6: E2 (40)
static const int OPEN_STRING_MIDI[7] = {0, 64, 59, 55, 50, 45, 40};

static inline double midi_to_freq(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// Biquad filter for acoustic guitar body resonance & tone shaping
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setPeaking(double freq, double Q, double gainDB) {
        double A = std::pow(10.0, gainDB / 40.0);
        double w0 = 2.0 * PI * freq / SAMPLE_RATE;
        double alpha = std::sin(w0) / (2.0 * Q);
        double cosw = std::cos(w0);
        double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cosw) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cosw) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    void setLowPass(double freq, double Q) {
        double w0 = 2.0 * PI * freq / SAMPLE_RATE;
        double alpha = std::sin(w0) / (2.0 * Q);
        double cosw = std::cos(w0);
        double a0 = 1.0 + alpha;
        b0 = ((1.0 - cosw) * 0.5) / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = ((1.0 - cosw) * 0.5) / a0;
        a1 = (-2.0 * cosw) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void setHighPass(double freq, double Q) {
        double w0 = 2.0 * PI * freq / SAMPLE_RATE;
        double alpha = std::sin(w0) / (2.0 * Q);
        double cosw = std::cos(w0);
        double a0 = 1.0 + alpha;
        b0 = ((1.0 + cosw) * 0.5) / a0;
        b1 = -(1.0 + cosw) / a0;
        b2 = ((1.0 + cosw) * 0.5) / a0;
        a1 = (-2.0 * cosw) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    inline double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Physical Modal Acoustic Guitar String
struct GuitarString {
    static const int MAX_PARTIALS = 28;
    int string_idx = 1;
    bool active = false;
    double current_midi = 60.0;
    double target_midi = 60.0;
    double age_sec = 0.0;
    double note_dur_sec = 1.0;
    double damp_rate = 1.0; // 1.0 normal, higher when muted by other string
    Articulation current_art = ART_PICK;
    double slide_start_midi = 60.0;
    double slide_end_midi = 60.0;

    double phase[MAX_PARTIALS];
    double amp[MAX_PARTIALS];
    double decay_tau[MAX_PARTIALS];

    // Pick transient noise state
    double transient_env = 0.0;
    double transient_freq = 1200.0;
    uint32_t rng = 123456789;

    void init(int idx) {
        string_idx = idx;
        rng = 13579u * (uint32_t)(idx + 1);
        for (int k = 0; k < MAX_PARTIALS; ++k) {
            phase[k] = 0.0;
            amp[k] = 0.0;
            decay_tau[k] = 1.0;
        }
    }

    inline double rand_bipolar() {
        rng = rng * 1664525u + 1013904223u;
        return ((int32_t)rng) / 2147483648.0;
    }

    void trigger(int fret, double dur_sec, Articulation art, int slide_from_fret = -1) {
        double base_midi = OPEN_STRING_MIDI[string_idx] + fret;
        current_art = art;
        note_dur_sec = dur_sec;
        age_sec = 0.0;
        damp_rate = 1.0;

        if (art == ART_SLIDE_IN) {
            int start_f = (slide_from_fret >= 0) ? slide_from_fret : std::max(0, fret - 2);
            slide_start_midi = OPEN_STRING_MIDI[string_idx] + start_f;
            slide_end_midi = base_midi;
            current_midi = slide_start_midi;
        } else if (art == ART_SLIDE) {
            if (slide_from_fret >= 0) {
                slide_start_midi = OPEN_STRING_MIDI[string_idx] + slide_from_fret;
            } else {
                slide_start_midi = current_midi;
            }
            slide_end_midi = base_midi;
            current_midi = slide_start_midi;
        } else {
            current_midi = base_midi;
            slide_start_midi = base_midi;
            slide_end_midi = base_midi;
        }
        target_midi = base_midi;

        // Pluck position beta (0.145 near soundhole/bridge gives rich acoustic guitar timbre)
        // Wound bass strings (4, 5, 6) vs plain steel treble strings (1, 2, 3)
        bool is_wound = (string_idx >= 4);
        double beta = is_wound ? 0.16 : 0.142;
        double base_sustain = is_wound ? 2.4 : 1.95;
        // Higher frets have slightly shorter sustain due to shorter vibrating length
        base_sustain *= std::pow(0.978, (double)fret);

        bool is_legato = (art == ART_HAMMER || art == ART_PULL || art == ART_SLIDE);

        for (int k = 1; k <= MAX_PARTIALS; ++k) {
            int idx = k - 1;
            double pluck_comb = std::fabs(std::sin(k * PI * beta));
            // Acoustic guitar partial rolloff: warm body fundamental + crisp bronze/steel upper partials
            double spectral_slope = 1.0 / std::pow((double)k, is_wound ? 1.28 : 1.18);
            // Boost 2nd and 3rd harmonics slightly for rich acoustic woodiness
            if (k == 2) spectral_slope *= 1.18;
            if (k == 3) spectral_slope *= 1.10;

            double new_amp = pluck_comb * spectral_slope;

            if (is_legato) {
                // Hammer-on / Pull-off / Slide keeps existing string vibration and adds finger impulse
                // Softer high-frequency partials compared to pick attack
                double legato_filter = std::exp(-0.14 * (k - 1));
                if (art == ART_SLIDE) {
                    amp[idx] = std::max(amp[idx] * 0.88, new_amp * 0.65 * legato_filter);
                } else {
                    amp[idx] = std::max(amp[idx] * 0.55, new_amp * 0.82 * legato_filter);
                }
            } else {
                amp[idx] = new_amp;
            }

            // Frequency-dependent decay time constant
            decay_tau[idx] = base_sustain / (1.0 + 0.16 * (k - 1) + 0.045 * (k - 1) * (k - 1));
        }

        // Excitation transient (pick click / flesh-nail / fret hammer thump)
        if (art == ART_SLIDE) {
            transient_env = 0.15;
        } else if (art == ART_HAMMER || art == ART_PULL) {
            transient_env = 0.35;
        } else {
            transient_env = 1.0;
        }
        transient_freq = midi_to_freq(base_midi) * 3.5;
        active = true;
    }

    // Compute instantaneous pitch offset in semitones for bends, slides, and vibrato
    inline double get_instant_midi() {
        double p = age_sec / std::max(0.01, note_dur_sec);
        if (p > 1.0) p = 1.0;

        if (current_art == ART_SLIDE_IN) {
            // Fast grace slide during first 28% of note duration (max 55 ms)
            double slide_dur = std::min(0.055, note_dur_sec * 0.32);
            double sp = (age_sec < slide_dur) ? (age_sec / slide_dur) : 1.0;
            // Smooth S-curve
            double s = sp * sp * (3.0 - 2.0 * sp);
            return slide_start_midi + (slide_end_midi - slide_start_midi) * s;
        }

        if (current_art == ART_SLIDE) {
            // Expressive slide across first 38% of note duration (max 80 ms)
            double slide_dur = std::min(0.080, note_dur_sec * 0.40);
            double sp = (age_sec < slide_dur) ? (age_sec / slide_dur) : 1.0;
            double s = sp * sp * (3.0 - 2.0 * sp);
            return slide_start_midi + (slide_end_midi - slide_start_midi) * s;
        }

        if (current_art == ART_BEND_UP) {
            // Full bend (+2.0 semitones): rises smoothly over first 55% of note and holds at +2.0
            double bp = std::min(1.0, p / 0.58);
            double s = bp * bp * (3.0 - 2.0 * bp);
            double vib = (p > 0.6) ? 0.08 * std::sin(2.0 * PI * 5.6 * (age_sec - 0.5 * note_dur_sec)) : 0.0;
            return target_midi + 2.0 * s + vib;
        }

        if (current_art == ART_BEND_UP_REL) {
            // Full bend up (+2.0 semitones) in first 45%, hold briefly, release back to 0 by 92%
            double bend_semis = 0.0;
            if (p < 0.42) {
                double bp = p / 0.42;
                bend_semis = 2.0 * (bp * bp * (3.0 - 2.0 * bp));
            } else if (p < 0.54) {
                bend_semis = 2.0;
            } else {
                double rp = std::min(1.0, (p - 0.54) / 0.40);
                bend_semis = 2.0 * (1.0 - rp * rp * (3.0 - 2.0 * rp));
            }
            return target_midi + bend_semis;
        }

        if (current_art == ART_PREBEND_REL) {
            // Pre-bend at +2.0 semitones, hold slightly then release smoothly to 0
            double bend_semis = 2.0;
            if (p > 0.18) {
                double rp = std::min(1.0, (p - 0.18) / 0.68);
                bend_semis = 2.0 * (1.0 - rp * rp * (3.0 - 2.0 * rp));
            }
            return target_midi + bend_semis;
        }

        if (current_art == ART_BEND_HOLD_UP) {
            // Measure 14 beat 1: starts with gentle release/inflection into fret 14, vibrato, then full bend at tail
            double bend_semis = 0.0;
            if (p < 0.12) {
                double rp = p / 0.12;
                bend_semis = 0.8 * (1.0 - rp);
            } else if (p < 0.78) {
                bend_semis = 0.14 * std::sin(2.0 * PI * 5.4 * (age_sec - 0.12 * note_dur_sec));
            } else {
                double bp = (p - 0.78) / 0.22;
                bend_semis = 2.0 * (bp * bp * (3.0 - 2.0 * bp));
            }
            return target_midi + bend_semis;
        }

        if (current_art == ART_VIB_BEND_TAIL) {
            // Measure 15 beat 3: expressive wide vibrato for 75% of note, then full bend (+2.0) at end
            if (p < 0.76) {
                double env = std::min(1.0, age_sec / 0.15);
                return target_midi + env * 0.22 * std::sin(2.0 * PI * 5.6 * age_sec);
            } else {
                double bp = (p - 0.76) / 0.24;
                return target_midi + 2.0 * (bp * bp * (3.0 - 2.0 * bp));
            }
        }

        if (current_art == ART_VIBRATO) {
            // Classic expressive guitar vibrato (~5.5 Hz, +/- 0.20 semitones, delayed onset)
            double onset = std::min(1.0, std::max(0.0, (age_sec - 0.08) / 0.22));
            return target_midi + onset * 0.21 * std::sin(2.0 * PI * 5.5 * age_sec);
        }

        // For regular notes longer than 0.45 sec, add subtle natural finger vibrato
        if (note_dur_sec >= 0.38 && age_sec > 0.14) {
            double onset = std::min(1.0, (age_sec - 0.14) / 0.20);
            return target_midi + onset * 0.11 * std::sin(2.0 * PI * 5.3 * age_sec);
        }

        return target_midi;
    }

    inline double render_sample() {
        if (!active) return 0.0;

        double dt = 1.0 / SAMPLE_RATE;
        double inst_midi = get_instant_midi();
        current_midi = inst_midi;
        double f0 = midi_to_freq(inst_midi);

        // String inharmonicity coefficient B
        double B = (string_idx >= 4) ? 0.000075 : 0.000045;

        // Soft attack envelope (1.8 ms for pick, 3.2 ms for legato) to prevent digital click
        double att_time = (current_art == ART_HAMMER || current_art == ART_PULL || current_art == ART_SLIDE) ? 0.0032 : 0.0018;
        double att_env = (age_sec < att_time) ? (age_sec / att_time) : 1.0;

        double sum = 0.0;
        double max_a = 0.0;
        for (int k = 1; k <= MAX_PARTIALS; ++k) {
            int idx = k - 1;
            if (amp[idx] < 1e-6) continue;

            double fk = f0 * k * std::sqrt(1.0 + B * k * k);
            if (fk >= SAMPLE_RATE * 0.45) {
                amp[idx] = 0.0;
                continue;
            }

            phase[idx] += 2.0 * PI * fk * dt;
            if (phase[idx] > 2.0 * PI) phase[idx] -= 2.0 * PI;

            sum += amp[idx] * std::sin(phase[idx]);
            amp[idx] *= std::exp(-dt * damp_rate / decay_tau[idx]);
            if (amp[idx] > max_a) max_a = amp[idx];
        }

        // Physical pick-string scrape & bridge impulse during initial 12 ms
        double trans = 0.0;
        if (transient_env > 1e-4) {
            double noise = rand_bipolar();
            trans = noise * transient_env * 0.22 * std::sin(2.0 * PI * transient_freq * age_sec);
            transient_env *= std::exp(-dt / 0.0035);
        }

        age_sec += dt;
        if (max_a < 1e-5 && age_sec > 0.05) {
            active = false;
        }

        return (sum * att_env + trans) * 0.24;
    }
};

// Simple Schroeder / Studio Room Reverb for Acoustic Guitar Ambience
struct StereoReverb {
    std::vector<double> combL[4], combR[4];
    int combIdxL[4]{}, combIdxR[4]{};
    double combLpL[4]{}, combLpR[4]{};
    std::vector<double> allpassL[2], allpassR[2];
    int apIdxL[2]{}, apIdxR[2]{};

    void init() {
        const int cTimes[4] = {1553, 1733, 1949, 2137}; // around 32..45 ms at 48kHz
        for (int i = 0; i < 4; ++i) {
            combL[i].assign(cTimes[i], 0.0);
            combR[i].assign(cTimes[i] + 29, 0.0);
        }
        const int aTimes[2] = {241, 557};
        for (int i = 0; i < 2; ++i) {
            allpassL[i].assign(aTimes[i], 0.0);
            allpassR[i].assign(aTimes[i] + 17, 0.0);
        }
    }

    inline void process(double inL, double inR, double &outL, double &outR) {
        double wetL = 0.0, wetR = 0.0;
        const double fb[4] = {0.74, 0.72, 0.70, 0.68};
        const double damp = 0.38;

        for (int i = 0; i < 4; ++i) {
            double dL = combL[i][combIdxL[i]];
            combLpL[i] = dL * (1.0 - damp) + combLpL[i] * damp;
            combL[i][combIdxL[i]] = inL + combLpL[i] * fb[i];
            if (++combIdxL[i] >= (int)combL[i].size()) combIdxL[i] = 0;
            wetL += dL;

            double dR = combR[i][combIdxR[i]];
            combLpR[i] = dR * (1.0 - damp) + combLpR[i] * damp;
            combR[i][combIdxR[i]] = inR + combLpR[i] * fb[i];
            if (++combIdxR[i] >= (int)combR[i].size()) combIdxR[i] = 0;
            wetR += dR;
        }
        wetL *= 0.25;
        wetR *= 0.25;

        for (int i = 0; i < 2; ++i) {
            double bufL = allpassL[i][apIdxL[i]];
            double vL = wetL + bufL * 0.5;
            allpassL[i][apIdxL[i]] = vL;
            wetL = bufL - vL * 0.5;
            if (++apIdxL[i] >= (int)allpassL[i].size()) apIdxL[i] = 0;

            double bufR = allpassR[i][apIdxR[i]];
            double vR = wetR + bufR * 0.5;
            allpassR[i][apIdxR[i]] = vR;
            wetR = bufR - vR * 0.5;
            if (++apIdxR[i] >= (int)allpassR[i].size()) apIdxR[i] = 0;
        }

        outL = inL * 0.84 + wetL * 0.22;
        outR = inR * 0.84 + wetR * 0.22;
    }
};

static std::vector<NoteSpec> build_score() {
    std::vector<NoteSpec> s;
    auto R = [&](int m, double b) {
        s.push_back({m, 0, 0, b, ART_REST, 0, 0, 0, 0, -1});
    };
    auto N = [&](int m, int str, int fret, double b, Articulation art = ART_PICK, int slide_from = -1) {
        s.push_back({m, str, fret, b, art, 0, 0, 0, 0, slide_from});
    };
    auto C2 = [&](int m, int s1, int f1, int s2, int f2, double b) {
        s.push_back({m, s1, f1, b, ART_CHORD, s2, f2, 0, 0, -1});
    };
    auto C3 = [&](int m, int s1, int f1, int s2, int f2, int s3, int f3, double b) {
        s.push_back({m, s1, f1, b, ART_CHORD, s2, f2, s3, f3, -1});
    };

    // ==================== Measure 1 ====================
    // Beat 1: 16th rest, 5弦2 P 0 (32nds), 6弦2 (16th), 5弦0 (16th, slides to 4)
    R(1, 0.25);
    N(1, 5, 2, 0.125, ART_PICK);
    N(1, 5, 0, 0.125, ART_PULL);
    N(1, 6, 2, 0.25,  ART_PICK);
    N(1, 5, 0, 0.25,  ART_PICK);
    // Beat 2: 5弦4 (sl.), 4弦2, 4弦4, 3弦2
    N(1, 5, 4, 0.25,  ART_SLIDE, 0);
    N(1, 4, 2, 0.25,  ART_PICK);
    N(1, 4, 4, 0.25,  ART_PICK);
    N(1, 3, 2, 0.25,  ART_PICK);
    // Beat 3: 3弦4 P 2 (32nds), 4弦4 (16th), 3弦2 (16th), 3弦4 (16th, sl. to 6)
    N(1, 3, 4, 0.125, ART_PICK);
    N(1, 3, 2, 0.125, ART_PULL);
    N(1, 4, 4, 0.25,  ART_PICK);
    N(1, 3, 2, 0.25,  ART_PICK);
    N(1, 3, 4, 0.25,  ART_PICK);
    // Beat 4: 3弦6 (quarter note, slid from 4)
    N(1, 3, 6, 1.00,  ART_SLIDE, 4);

    // ==================== Measure 2 ====================
    // Beat 1: 16th rest, 3弦/11, 2弦10, 2弦12
    R(2, 0.25);
    N(2, 3, 11, 0.25, ART_SLIDE_IN, 9);
    N(2, 2, 10, 0.25, ART_PICK);
    N(2, 2, 12, 0.25, ART_PICK);
    // Beat 2: 2弦12(full), 1弦12(8th), 2弦12 P 10 (32nds)
    N(2, 2, 12, 0.25, ART_BEND_UP);
    N(2, 1, 12, 0.50, ART_PICK);
    N(2, 2, 12, 0.125, ART_PICK);
    N(2, 2, 10, 0.125, ART_PULL);
    // Beat 3: 3弦11(16th), 2弦10(8th), 3弦11(16th)
    N(2, 3, 11, 0.25, ART_PICK);
    N(2, 2, 10, 0.50, ART_PICK);
    N(2, 3, 11, 0.25, ART_PICK);
    // Beat 4: 3弦9 H 11 (16ths), 3弦11 (8th)
    N(2, 3, 9,  0.25, ART_PICK);
    N(2, 3, 11, 0.25, ART_HAMMER);
    N(2, 3, 11, 0.50, ART_PICK);

    // ==================== Measure 3 ====================
    // Beat 1: 16th rest, 4弦/9, 4弦7, 4弦9 (sl. to 11)
    R(3, 0.25);
    N(3, 4, 9,  0.25, ART_SLIDE_IN, 7);
    N(3, 4, 7,  0.25, ART_PICK);
    N(3, 4, 9,  0.25, ART_PICK);
    // Beat 2: 4弦11 (sl.), 3弦9, double-stop (3弦11 + 2弦10, 8th)
    N(3, 4, 11, 0.25, ART_SLIDE, 9);
    N(3, 3, 9,  0.25, ART_PICK);
    C2(3, 3, 11, 2, 10, 0.50);
    // Beats 3 & 4: Triad (4弦9 + 3弦9 + 2弦9, half note)
    C3(3, 4, 9, 3, 9, 2, 9, 2.00);

    // ==================== Measure 4 ====================
    // Beat 1: 16th rest, 1弦12 P 9 (32nds), 2弦10 (16th), 1弦9 (16th)
    R(4, 0.25);
    N(4, 1, 12, 0.125, ART_PICK);
    N(4, 1, 9,  0.125, ART_PULL);
    N(4, 2, 10, 0.25,  ART_PICK);
    N(4, 1, 9,  0.25,  ART_PICK);
    // Beat 2: 2弦10 (16th), 2弦7 (16th), 2弦10 P 7 (32nds), 3弦9 (16th)
    N(4, 2, 10, 0.25,  ART_PICK);
    N(4, 2, 7,  0.25,  ART_PICK);
    N(4, 2, 10, 0.125, ART_PICK);
    N(4, 2, 7,  0.125, ART_PULL);
    N(4, 3, 9,  0.25,  ART_PICK);
    // Beat 3: 2弦7 (16th), 3弦9 sl. 11 (32nds), 2弦9 (16th), 2弦10 (16th)
    N(4, 2, 7,  0.25,  ART_PICK);
    N(4, 3, 9,  0.125, ART_PICK);
    N(4, 3, 11, 0.125, ART_SLIDE, 9);
    N(4, 2, 9,  0.25,  ART_PICK);
    N(4, 2, 10, 0.25,  ART_PICK);
    // Beat 4: 1弦9 (quarter note)
    N(4, 1, 9,  1.00,  ART_PICK);

    // ==================== Measure 5 ====================
    // Beat 1: 16th rest, 5弦9, 5弦11, 5弦12
    R(5, 0.25);
    N(5, 5, 9,  0.25, ART_PICK);
    N(5, 5, 11, 0.25, ART_PICK);
    N(5, 5, 12, 0.25, ART_PICK);
    // Beat 2: 4弦9, 4弦11, 4弦12, 3弦9
    N(5, 4, 9,  0.25, ART_PICK);
    N(5, 4, 11, 0.25, ART_PICK);
    N(5, 4, 12, 0.25, ART_PICK);
    N(5, 3, 9,  0.25, ART_PICK);
    // Beat 3: 3弦11, 2弦10, 1弦9, 1弦12
    N(5, 3, 11, 0.25, ART_PICK);
    N(5, 2, 10, 0.25, ART_PICK);
    N(5, 1, 9,  0.25, ART_PICK);
    N(5, 1, 12, 0.25, ART_PICK);
    // Beat 4: 1弦9, 2弦10, 3弦9, 2弦10
    N(5, 1, 9,  0.25, ART_PICK);
    N(5, 2, 10, 0.25, ART_PICK);
    N(5, 3, 9,  0.25, ART_PICK);
    N(5, 2, 10, 0.25, ART_PICK);

    // ==================== Measure 6 ====================
    // Beat 1: 1弦12, 3弦9, 2弦10, 1弦12
    N(6, 1, 12, 0.25, ART_PICK);
    N(6, 3, 9,  0.25, ART_PICK);
    N(6, 2, 10, 0.25, ART_PICK);
    N(6, 1, 12, 0.25, ART_PICK);
    // Beat 2: 2弦9, 2弦10, 3弦9, 3弦7
    N(6, 2, 9,  0.25, ART_PICK);
    N(6, 2, 10, 0.25, ART_PICK);
    N(6, 3, 9,  0.25, ART_PICK);
    N(6, 3, 7,  0.25, ART_PICK);
    // Beats 3 & 4: 3弦6 (dotted quarter = 1.50), 4弦9 (0.25), 4弦7 (0.25)
    N(6, 3, 6,  1.50, ART_PICK);
    N(6, 4, 9,  0.25, ART_PICK);
    N(6, 4, 7,  0.25, ART_PICK);

    // ==================== Measure 7 ====================
    // Beats 1 & 2: 4弦7 H 9 (0.125 + 1.375 = 1.50), 4弦7 (0.25), 4弦6 (0.25)
    N(7, 4, 7,  0.125, ART_PICK);
    N(7, 4, 9,  1.375, ART_HAMMER);
    N(7, 4, 7,  0.25,  ART_PICK);
    N(7, 4, 6,  0.25,  ART_PICK);
    // Beats 3 & 4: 3弦9 (0.25), 3弦7 (1.25), 3弦6 H 7 (0.25 + 0.25)
    N(7, 3, 9,  0.25,  ART_PICK);
    N(7, 3, 7,  1.25,  ART_PICK);
    N(7, 3, 6,  0.25,  ART_PICK);
    N(7, 3, 7,  0.25,  ART_HAMMER);

    // ==================== Measure 8 ====================
    // Whole note 3弦6 with continuous vibrato
    N(8, 3, 6,  4.00,  ART_VIBRATO);

    // ==================== Measure 9 ====================
    // Beat 1: 16th rest, 4弦9 P 7 (32nds), 5弦9 (16th), 4弦7 (16th)
    R(9, 0.25);
    N(9, 4, 9,  0.125, ART_PICK);
    N(9, 4, 7,  0.125, ART_PULL);
    N(9, 5, 9,  0.25,  ART_PICK);
    N(9, 4, 7,  0.25,  ART_PICK);
    // Beat 2: 4弦9/11 (16th), 3弦9 (16th), 3弦11 P 9 (32nds), 4弦11 (16th)
    N(9, 4, 11, 0.25,  ART_SLIDE_IN, 9);
    N(9, 3, 9,  0.25,  ART_PICK);
    N(9, 3, 11, 0.125, ART_PICK);
    N(9, 3, 9,  0.125, ART_PULL);
    N(9, 4, 11, 0.25,  ART_PICK);
    // Beat 3: 3弦9, 3弦11, 2弦9, 2弦10
    N(9, 3, 9,  0.25,  ART_PICK);
    N(9, 3, 11, 0.25,  ART_PICK);
    N(9, 2, 9,  0.25,  ART_PICK);
    N(9, 2, 10, 0.25,  ART_PICK);
    // Beat 4: 2弦12 (quarter note)
    N(9, 2, 12, 1.00,  ART_PICK);

    // ==================== Measure 10 ====================
    // Beat 1: 16th rest, 1弦12 P 9 (32nds), 2弦10 (16th), 3弦9 (16th)
    R(10, 0.25);
    N(10, 1, 12, 0.125, ART_PICK);
    N(10, 1, 9,  0.125, ART_PULL);
    N(10, 2, 10, 0.25,  ART_PICK);
    N(10, 3, 9,  0.25,  ART_PICK);
    // Beat 2: 4弦11 (16th), 3弦9 (16th), 3弦9 H 11 (16ths)
    N(10, 4, 11, 0.25,  ART_PICK);
    N(10, 3, 9,  0.25,  ART_PICK);
    N(10, 3, 9,  0.25,  ART_PICK);
    N(10, 3, 11, 0.25,  ART_HAMMER);
    // Beats 3 & 4: Circled 3弦11 (half note)
    N(10, 3, 11, 2.00,  ART_VIBRATO);

    // ==================== Measure 11 ====================
    // Beat 1: 16th rest, 1弦14-14 (32nds), 1弦14 (16th), 1弦12 (16th)
    R(11, 0.25);
    N(11, 1, 14, 0.125, ART_PICK);
    N(11, 1, 14, 0.125, ART_PICK);
    N(11, 1, 14, 0.25,  ART_PICK);
    N(11, 1, 12, 0.25,  ART_PICK);
    // Beat 2: Triplet (1弦10, 1弦9, 2弦12 = 0.50 beat), then 2弦12 sl. 14 (0.50 beat)
    N(11, 1, 10, 1.0 / 6.0, ART_PICK);
    N(11, 1, 9,  1.0 / 6.0, ART_PICK);
    N(11, 2, 12, 1.0 / 6.0, ART_PICK);
    N(11, 2, 12, 0.25,      ART_PICK);
    N(11, 2, 14, 0.25,      ART_SLIDE, 12);
    // Beat 3: 2弦12 (16th), 2弦12 (16th), 2弦12 (8th)
    N(11, 2, 12, 0.25,  ART_PICK);
    N(11, 2, 12, 0.25,  ART_PICK);
    N(11, 2, 12, 0.50,  ART_PICK);
    // Beat 4: 2弦10 (8th), 2弦12 (8th)
    N(11, 2, 10, 0.50,  ART_PICK);
    N(11, 2, 12, 0.50,  ART_PICK);

    // ==================== Measure 12 ====================
    // Beat 1: 2弦12 (8th), 2弦12(full, 16th), 1弦12 (16th)
    N(12, 2, 12, 0.50,  ART_PICK);
    N(12, 2, 12, 0.25,  ART_BEND_UP);
    N(12, 1, 12, 0.25,  ART_PICK);
    // Beat 2: 2弦12(full bend & release, 8th), 2弦10 (16th), 3弦11 (16th)
    N(12, 2, 12, 0.50,  ART_BEND_UP_REL);
    N(12, 2, 10, 0.25,  ART_PICK);
    N(12, 3, 11, 0.25,  ART_PICK);
    // Beat 3: 3弦9 (16th), 3弦9 H 11 (16ths), 3弦11 (16th)
    N(12, 3, 9,  0.25,  ART_PICK);
    N(12, 3, 9,  0.25,  ART_PICK);
    N(12, 3, 11, 0.25,  ART_HAMMER);
    N(12, 3, 11, 0.25,  ART_PICK);
    // Beat 4: 3弦11 (16th), 3弦11 (dotted 8th = 0.75)
    N(12, 3, 11, 0.25,  ART_PICK);
    N(12, 3, 11, 0.75,  ART_PICK);

    // ==================== Measure 13 ====================
    // Beat 1: 4弦9, 4弦7, 3弦9, 3弦7
    N(13, 4, 9,  0.25, ART_PICK);
    N(13, 4, 7,  0.25, ART_PICK);
    N(13, 3, 9,  0.25, ART_PICK);
    N(13, 3, 7,  0.25, ART_PICK);
    // Beat 2: 4弦11, 3弦11, 3弦11, 3弦9
    N(13, 4, 11, 0.25, ART_PICK);
    N(13, 3, 11, 0.25, ART_PICK);
    N(13, 3, 11, 0.25, ART_PICK);
    N(13, 3, 9,  0.25, ART_PICK);
    // Beat 3: 2弦12, 2弦10, 1弦12, 1弦9
    N(13, 2, 12, 0.25, ART_PICK);
    N(13, 2, 10, 0.25, ART_PICK);
    N(13, 1, 12, 0.25, ART_PICK);
    N(13, 1, 9,  0.25, ART_PICK);
    // Beat 4: 2弦14(full, 8th), 2弦14(full up & release, 8th)
    N(13, 2, 14, 0.50, ART_BEND_UP);
    N(13, 2, 14, 0.50, ART_BEND_UP_REL);

    // ==================== Measure 14 ====================
    // Beats 1 & 2: 2弦14 (dotted quarter = 1.50, with bend inflection), 2弦12 (0.25), 2弦14 (0.25)
    N(14, 2, 14, 1.50, ART_BEND_HOLD_UP);
    N(14, 2, 12, 0.25, ART_PICK);
    N(14, 2, 14, 0.25, ART_PICK);
    // Beats 3 & 4: 2弦10 (dotted quarter = 1.50), 2弦12 (0.25), 2弦10 (0.25)
    N(14, 2, 10, 1.50, ART_VIBRATO);
    N(14, 2, 12, 0.25, ART_PICK);
    N(14, 2, 10, 0.25, ART_PICK);

    // ==================== Measure 15 ====================
    // Beats 1 & 2: 2弦12(full, 32nd), 1弦12(32nd), 2弦12(full up/rel, 1.25), 2弦12(16th), 2弦10(16th)
    N(15, 2, 12, 0.125, ART_BEND_UP);
    N(15, 1, 12, 0.125, ART_PICK);
    N(15, 2, 12, 1.25,  ART_BEND_UP_REL);
    N(15, 2, 12, 0.25,  ART_PICK);
    N(15, 2, 10, 0.25,  ART_PICK);
    // Beats 3 & 4: 2弦12 (dotted quarter = 1.50, vibrato then full bend), 2弦10 sl. 9 (16ths)
    N(15, 2, 12, 1.50,  ART_VIB_BEND_TAIL);
    N(15, 2, 10, 0.25,  ART_PICK);
    N(15, 2, 9,  0.25,  ART_SLIDE, 10);

    // ==================== Measure 16 ====================
    // 2弦9 H (10) whole note
    N(16, 2, 9,  0.125, ART_PICK);
    N(16, 2, 10, 3.875, ART_HAMMER);

    // ==================== Measure 17 ====================
    // Beats 1 & 2: 3弦11 (1.50), 3弦9 H 11 (32nds), 2弦9 H 10 (32nds)
    N(17, 3, 11, 1.50,  ART_PICK);
    N(17, 3, 9,  0.125, ART_PICK);
    N(17, 3, 11, 0.125, ART_HAMMER);
    N(17, 2, 9,  0.125, ART_PICK);
    N(17, 2, 10, 0.125, ART_HAMMER);
    // Beats 3 & 4: 2弦12 (1.50), 2弦10 H 12 (16ths)
    N(17, 2, 12, 1.50,  ART_VIBRATO);
    N(17, 2, 10, 0.25,  ART_PICK);
    N(17, 2, 12, 0.25,  ART_HAMMER);

    // ==================== Measure 18 ====================
    // Beat 1: 2弦14(full, 8th), 2弦14(full, 8th)
    N(18, 2, 14, 0.50,  ART_BEND_UP);
    N(18, 2, 14, 0.50,  ART_BEND_UP);
    // Beat 2: 2弦14(prebend/release, 8th), 2弦12 H 14 (16ths)
    N(18, 2, 14, 0.50,  ART_PREBEND_REL);
    N(18, 2, 12, 0.25,  ART_PICK);
    N(18, 2, 14, 0.25,  ART_HAMMER);
    // Beats 3 & 4: Circled 2弦10 (half note)
    N(18, 2, 10, 2.00,  ART_VIBRATO);

    // ==================== Measure 19 ====================
    // Beats 1 & 2: 2弦5 H (7) (half note)
    N(19, 2, 5,  0.125, ART_PICK);
    N(19, 2, 7,  1.875, ART_HAMMER);
    // Beat 3: Triplet 3弦7(full) x 3 (each 1/3 beat)
    N(19, 3, 7,  1.0 / 3.0, ART_BEND_UP);
    N(19, 3, 7,  1.0 / 3.0, ART_BEND_UP);
    N(19, 3, 7,  1.0 / 3.0, ART_BEND_UP);
    // Beat 4: 3弦7(full bend & release, 8th), 3弦7 (16th), 3弦5 (16th)
    N(19, 3, 7,  0.50,  ART_BEND_UP_REL);
    N(19, 3, 7,  0.25,  ART_PICK);
    N(19, 3, 5,  0.25,  ART_PICK);

    // ==================== Measure 20 ====================
    // 2弦5 H (7) whole note + 2.0 beats natural fadeout
    N(20, 2, 5,  0.125, ART_PICK);
    N(20, 2, 7,  5.875, ART_HAMMER);

    return s;
}

static void write_wav(const char *filename, const std::vector<int16_t> &interleaved_stereo) {
    FILE *f = std::fopen(filename, "wb");
    if (!f) {
        std::perror("fopen");
        std::exit(1);
    }
    uint32_t data_bytes = (uint32_t)(interleaved_stereo.size() * sizeof(int16_t));
    uint32_t riff_size = 36 + data_bytes;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 2;
    uint32_t sample_rate = SAMPLE_RATE;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = num_channels * (bits_per_sample / 8);

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff_size, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    std::fwrite(&fmt_size, 4, 1, f);
    std::fwrite(&audio_format, 2, 1, f);
    std::fwrite(&num_channels, 2, 1, f);
    std::fwrite(&sample_rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);
    std::fwrite(&bits_per_sample, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);
    std::fwrite(interleaved_stereo.data(), sizeof(int16_t), interleaved_stereo.size(), f);
    std::fclose(f);
}

int main(int argc, char **argv) {
    const char *out_wav = (argc > 1) ? argv[1] : "/tmp/grey_track_solo.wav";
    std::vector<NoteSpec> score = build_score();

    // Verify beat sum per measure (Measures 1..19 should be 4.00 beats, Measure 20 is 6.00 with ring-out)
    double measure_beats[21] = {0.0};
    double total_beats = 0.0;
    for (const auto &n : score) {
        measure_beats[n.measure] += n.dur_beats;
        total_beats += n.dur_beats;
    }
    for (int m = 1; m <= 20; ++m) {
        std::printf("Measure %2d: %.4f beats\n", m, measure_beats[m]);
    }
    std::printf("Total duration: %.2f beats (%.2f seconds at 72 BPM)\n",
                total_beats, total_beats * BEAT_SEC);

    GuitarString strings[7];
    for (int i = 1; i <= 6; ++i) strings[i].init(i);

    // Acoustic guitar body resonance EQ filters (Dreadnought / Grand Auditorium Spruce-Rosewood body)
    Biquad hp_sub, body_helmholtz, body_top, body_wood1, body_wood2, body_pres, lp_air;
    hp_sub.setHighPass(65.0, 0.707);
    body_helmholtz.setPeaking(102.0, 4.2, 5.2); // Air cavity Helmholtz resonance
    body_top.setPeaking(204.0, 3.8, 5.8);       // Main spruce top plate mode
    body_wood1.setPeaking(395.0, 3.5, 3.5);     // Lower bout dipole mode
    body_wood2.setPeaking(820.0, 3.0, -2.0);    // Anti-boxiness scoop
    body_pres.setPeaking(2450.0, 2.2, 3.8);     // Bronze string finger/pick definition
    lp_air.setLowPass(10500.0, 0.707);

    StereoReverb reverb;
    reverb.init();

    int total_samples = (int)((total_beats * BEAT_SEC + 1.0) * SAMPLE_RATE);
    std::vector<int16_t> pcm;
    pcm.reserve(total_samples * 2);

    for (size_t idx = 0; idx < score.size(); ++idx) {
        const NoteSpec &ev = score[idx];
        double dur_sec = ev.dur_beats * BEAT_SEC;
        int n_samples = (int)std::round(dur_sec * SAMPLE_RATE);

        if (ev.art != ART_REST && ev.string_no >= 1 && ev.string_no <= 6) {
            // Softly damp other ringing strings for clean solo phrasing, except during chords
            for (int s = 1; s <= 6; ++s) {
                if (s != ev.string_no && s != ev.string2 && s != ev.string3) {
                    strings[s].damp_rate = 5.5; // Smooth ~70ms finger muting
                }
            }

            strings[ev.string_no].trigger(ev.fret, dur_sec, ev.art, ev.slide_from);
            if (ev.art == ART_CHORD) {
                if (ev.string2 >= 1 && ev.string2 <= 6) {
                    strings[ev.string2].trigger(ev.fret2, dur_sec, ART_PICK);
                }
                if (ev.string3 >= 1 && ev.string3 <= 6) {
                    strings[ev.string3].trigger(ev.fret3, dur_sec, ART_PICK);
                }
            }
        }

        for (int n = 0; n < n_samples; ++n) {
            double rawL = 0.0, rawR = 0.0;
            for (int s = 1; s <= 6; ++s) {
                double sig = strings[s].render_sample();
                // Subtle stereo spread across strings (low strings slightly left, high strings slightly right)
                double pan = (3.5 - s) * 0.07; // -0.175 .. +0.175
                rawL += sig * (1.0 - pan);
                rawR += sig * (1.0 + pan);
            }

            double mono = 0.5 * (rawL + rawR);
            double body = hp_sub.process(mono);
            body = body_helmholtz.process(body);
            body = body_top.process(body);
            body = body_wood1.process(body);
            body = body_wood2.process(body);
            body = body_pres.process(body);
            body = lp_air.process(body);

            double inL = body + (rawL - mono) * 0.6;
            double inR = body + (rawR - mono) * 0.6;

            double outL = 0.0, outR = 0.0;
            reverb.process(inL, inR, outL, outR);

            // Soft analog tape saturation / limiting
            outL = std::tanh(outL * 1.45) * 0.88;
            outR = std::tanh(outR * 1.45) * 0.88;

            int32_t sL = (int32_t)(outL * 32767.0);
            int32_t sR = (int32_t)(outR * 32767.0);
            sL = std::max(-32768, std::min(32767, sL));
            sR = std::max(-32768, std::min(32767, sR));

            pcm.push_back((int16_t)sL);
            pcm.push_back((int16_t)sR);
        }
    }

    // Final 1.0 sec fade-out tail
    int tail_samples = SAMPLE_RATE;
    for (int n = 0; n < tail_samples; ++n) {
        double fade = 1.0 - (double)n / tail_samples;
        double rawL = 0.0, rawR = 0.0;
        for (int s = 1; s <= 6; ++s) {
            double sig = strings[s].render_sample();
            rawL += sig;
            rawR += sig;
        }
        double mono = 0.5 * (rawL + rawR);
        double body = lp_air.process(body_pres.process(body_wood2.process(
            body_wood1.process(body_top.process(body_helmholtz.process(hp_sub.process(mono)))))));
        double outL = 0.0, outR = 0.0;
        reverb.process(body, body, outL, outR);
        outL = std::tanh(outL * 1.45) * 0.88 * fade;
        outR = std::tanh(outR * 1.45) * 0.88 * fade;
        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outL * 32767.0)));
        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outR * 32767.0)));
    }

    write_wav(out_wav, pcm);
    std::printf("Successfully wrote WAV: %s (%zu stereo frames)\n", out_wav, pcm.size() / 2);
    return 0;
}
