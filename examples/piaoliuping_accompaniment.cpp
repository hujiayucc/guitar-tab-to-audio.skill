#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

static const int SAMPLE_RATE = 48000;
static const double PI = 3.14159265358979323846;
static const double BPM = 76.0; // Expressive acoustic ballad tempo (1=E, 4/4)
static const double BEAT_SEC = 60.0 / BPM;

// Standard tuning open string MIDI: 1:E4(64), 2:B3(59), 3:G3(55), 4:D3(50), 5:A2(45), 6:E2(40)
static const int OPEN_STRING_MIDI[7] = {0, 64, 59, 55, 50, 45, 40};

static inline double midi_to_freq(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// Chord diagrams from the score: frets for [dummy, str1, str2, str3, str4, str5, str6]
// Note: index 1 = String 1 (high E), index 6 = String 6 (low E)
struct ChordShape {
    const char *name;
    int fret[7]; // fret[1..6]
};

enum ChordId {
    CH_NONE = 0,
    CH_E7_POS,   // E(7):   6:x, 5:7, 4:9, 3:9, 2:9, 1:7
    CH_BM7_POS,  // Bm7(7): 6:7, 5:9, 4:7, 3:7, 2:7, 1:7
    CH_A5_POS,   // A(5):   6:5, 5:7, 4:7, 3:6, 2:5, 1:5
    CH_C8_POS,   // C(8):   6:8, 5:10,4:10,3:9, 2:8, 1:8
    CH_ASUS2,    // Asus2:  6:x, 5:0, 4:2, 3:2, 2:0, 1:0
    CH_GSM7,     // #Gm7:   6:4, 5:6, 4:4, 3:4, 2:4, 1:4
    CH_CSM7,     // #Cm7:   6:x, 5:4, 4:6, 3:4, 2:5, 1:4
    CH_BSUS4,    // Bsus4:  6:x, 5:2, 4:4, 3:4, 2:0, 1:0
    CH_B,        // B:      6:x, 5:2, 4:4, 3:4, 2:4, 1:2
    CH_CS7,      // #C7:    6:x, 5:4, 4:6, 3:4, 2:6, 1:4
    CH_FSM11,    // #Fm11:  6:2, 5:4, 4:4, 3:2, 2:0, 1:0
    CH_E,        // E:      6:0, 5:2, 4:2, 3:1, 2:0, 1:0
    CH_EM        // Em:     6:0, 5:2, 4:2, 3:0, 2:0, 1:0
};

static const ChordShape CHORDS[] = {
    {"NONE",   {0,  0, 0, 0, 0, 0, 0}},
    {"E(7)",   {0,  7, 9, 9, 9, 7,-1}},
    {"Bm7(7)", {0,  7, 7, 7, 7, 9, 7}},
    {"A(5)",   {0,  5, 5, 6, 7, 7, 5}},
    {"C(8)",   {0,  8, 8, 9,10,10, 8}},
    {"Asus2",  {0,  0, 0, 2, 2, 0,-1}},
    {"#Gm7",   {0,  4, 4, 4, 4, 6, 4}},
    {"#Cm7",   {0,  4, 5, 4, 6, 4,-1}},
    {"Bsus4",  {0,  0, 0, 4, 4, 2,-1}},
    {"B",      {0,  2, 4, 4, 4, 2,-1}},
    {"#C7",    {0,  4, 6, 4, 6, 4,-1}},
    {"#Fm11",  {0,  0, 0, 2, 4, 4, 2}},
    {"E",      {0,  0, 0, 1, 2, 2, 0}},
    {"Em",     {0,  0, 0, 0, 2, 2, 0}}
};

enum EventType {
    EV_REST = 0,
    EV_PLUCK,      // Simultaneous pluck of 1..4 strings from current chord (or explicit fret)
    EV_ARP_ROLL,   // Wavy arrow upward arpeggio roll (bass to treble)
    EV_HARMONIC    // Natural harmonic (Harm. 7 or Harm. 12)
};

struct TabEvent {
    ChordId chord;
    EventType type;
    double dur_beats;
    std::vector<int> strings; // Strings to pluck (e.g. {5, 3, 2, 1})
    int explicit_string;      // If > 0, override fret on this string
    int explicit_fret;
};

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

struct GuitarString {
    static const int MAX_PARTIALS = 26;
    int string_idx = 1;
    bool active = false;
    bool is_harmonic = false;
    int current_fret = -1;
    double f0 = 220.0;
    double age_sec = 0.0;
    double delay_start_sec = 0.0; // For arpeggio roll stagger
    double damp_rate = 1.0;
    int num_active_partials = 0;

    double re[MAX_PARTIALS];
    double im[MAX_PARTIALS];
    double cos_dphi[MAX_PARTIALS];
    double sin_dphi[MAX_PARTIALS];
    double decay_step[MAX_PARTIALS];
    double transient_env = 0.0;
    double trans_re = 1.0, trans_im = 0.0, trans_cos = 1.0, trans_sin = 0.0;
    double trans_decay = 0.99;
    uint32_t rng = 123456789;

    void init(int idx) {
        string_idx = idx;
        rng = 24680u * (uint32_t)(idx + 1);
        for (int k = 0; k < MAX_PARTIALS; ++k) {
            re[k] = 0.0;
            im[k] = 0.0;
            cos_dphi[k] = 1.0;
            sin_dphi[k] = 0.0;
            decay_step[k] = 0.999;
        }
    }

    inline double rand_bipolar() {
        rng = rng * 1664525u + 1013904223u;
        return ((int32_t)rng) / 2147483648.0;
    }

    void trigger(int fret, double roll_delay_sec = 0.0, bool harmonic = false) {
        if (fret < 0) return;
        current_fret = fret;
        is_harmonic = harmonic;
        delay_start_sec = roll_delay_sec;
        age_sec = 0.0;
        damp_rate = 1.0;

        double base_midi = OPEN_STRING_MIDI[string_idx] + fret;
        if (harmonic) {
            if (fret == 12) base_midi = OPEN_STRING_MIDI[string_idx] + 12.0;
            else if (fret == 7) base_midi = OPEN_STRING_MIDI[string_idx] + 19.01955;
        }
        f0 = midi_to_freq(base_midi);

        bool is_wound = (string_idx >= 4);
        double beta = is_wound ? 0.165 : 0.145;
        double base_sustain = harmonic ? 3.6 : (is_wound ? 2.7 : 2.2);
        if (!harmonic) {
            base_sustain *= std::pow(0.982, (double)fret);
        }

        double dt = 1.0 / SAMPLE_RATE;
        double B = is_wound ? 0.00007 : 0.00004;
        num_active_partials = 0;

        for (int k = 1; k <= MAX_PARTIALS; ++k) {
            int idx = k - 1;
            double fk = f0 * k * std::sqrt(1.0 + B * k * k);
            if (fk >= SAMPLE_RATE * 0.45) {
                re[idx] = 0.0;
                im[idx] = 0.0;
                break;
            }
            num_active_partials = k;
            double dphi = 2.0 * PI * fk * dt;
            cos_dphi[idx] = std::cos(dphi);
            sin_dphi[idx] = std::sin(dphi);

            double a = 0.0;
            double tau = 1.0;
            if (harmonic) {
                if (k == 1) a = 0.95;
                else if (k == 2) a = 0.25;
                else if (k == 3) a = 0.08;
                else a = 0.0;
                tau = base_sustain / (1.0 + 0.35 * (k - 1));
            } else {
                double pluck_comb = std::fabs(std::sin(k * PI * beta));
                double spectral_slope = 1.0 / std::pow((double)k, is_wound ? 1.32 : 1.22);
                if (k == 2) spectral_slope *= 1.15;
                a = pluck_comb * spectral_slope;
                tau = base_sustain / (1.0 + 0.15 * (k - 1) + 0.04 * (k - 1) * (k - 1));
            }
            re[idx] = a;
            im[idx] = 0.0;
            decay_step[idx] = std::exp(-dt / tau);
        }

        transient_env = harmonic ? 0.25 : 0.85;
        double tf = f0 * 3.2;
        trans_re = 1.0;
        trans_im = 0.0;
        trans_cos = std::cos(2.0 * PI * tf * dt);
        trans_sin = std::sin(2.0 * PI * tf * dt);
        trans_decay = std::exp(-dt / 0.0032);
        active = true;
    }

    inline double render_sample() {
        if (!active) return 0.0;
        double dt = 1.0 / SAMPLE_RATE;
        if (delay_start_sec > 0.0) {
            delay_start_sec -= dt;
            return 0.0;
        }

        double att_env = (age_sec < 0.0022) ? (age_sec / 0.0022) : 1.0;
        double extra_damp = (damp_rate > 1.0) ? 0.99972 : 1.0;

        double sum = 0.0;
        double energy = 0.0;
        for (int idx = 0; idx < num_active_partials; ++idx) {
            double r = re[idx];
            double i = im[idx];
            double nr = (r * cos_dphi[idx] - i * sin_dphi[idx]) * decay_step[idx] * extra_damp;
            double ni = (r * sin_dphi[idx] + i * cos_dphi[idx]) * decay_step[idx] * extra_damp;
            re[idx] = nr;
            im[idx] = ni;
            sum += ni;
            energy += (nr * nr + ni * ni);
        }

        double trans = 0.0;
        if (transient_env > 1e-4) {
            double nr = trans_re * trans_cos - trans_im * trans_sin;
            double ni = trans_re * trans_sin + trans_im * trans_cos;
            trans_re = nr;
            trans_im = ni;
            trans = rand_bipolar() * transient_env * 0.18 * ni;
            transient_env *= trans_decay;
        }

        age_sec += dt;
        if (energy < 1e-10 && age_sec > 0.05) {
            active = false;
        }
        return (sum * att_env + trans) * 0.21;
    }
};

struct StereoReverb {
    std::vector<double> combL[4], combR[4];
    int combIdxL[4]{}, combIdxR[4]{};
    double combLpL[4]{}, combLpR[4]{};
    std::vector<double> allpassL[2], allpassR[2];
    int apIdxL[2]{}, apIdxR[2]{};

    void init() {
        const int cTimes[4] = {1553, 1733, 1949, 2137};
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
        const double fb[4] = {0.75, 0.73, 0.71, 0.69};
        const double damp = 0.36;

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

        outL = inL * 0.83 + wetL * 0.24;
        outR = inR * 0.83 + wetR * 0.24;
    }
};

// Helper builders for each measure pattern on the 2-page tab
static void add_measure_E7_Bm7(std::vector<TabEvent> &s) {
    // E(7) (beats 1-2) & Bm7(7) (beats 3-4)
    s.push_back({CH_E7_POS,  EV_ARP_ROLL, 0.75, {5, 3, 2, 1}, 0, 0});
    s.push_back({CH_E7_POS,  EV_PLUCK,    0.25, {4}, 0, 0});
    s.push_back({CH_E7_POS,  EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_E7_POS,  EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_BM7_POS, EV_ARP_ROLL, 1.00, {6, 3, 2}, 0, 0});
    s.push_back({CH_BM7_POS, EV_REST,     1.00, {}, 0, 0});
}

static void add_measure_A5_C8(std::vector<TabEvent> &s) {
    // A(5) (beats 1-2) & C(8) (beats 3-4)
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 0.75, {6, 3, 2}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {4}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {1}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.50, {6}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {4}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {2}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {2}, 0, 0});
    s.push_back({CH_C8_POS, EV_PLUCK,    0.25, {1}, 0, 0});
}

static void add_verse_block_4bars(std::vector<TabEvent> &s, bool with_pickup_B) {
    // Bar 1: Asus2
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK,    1.00, {3, 2}, 0, 0});
    s.push_back({CH_ASUS2, EV_REST,     2.00, {}, 0, 0});
    // Bar 2: #Gm7 & #Cm7
    s.push_back({CH_GSM7,  EV_PLUCK,    0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK,    1.00, {3, 2}, 0, 0});
    s.push_back({CH_CSM7,  EV_ARP_ROLL, 1.00, {5, 4, 3, 2}, 0, 0});
    s.push_back({CH_CSM7,  EV_REST,     1.00, {}, 0, 0});
    // Bar 3: Asus2
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK,    1.00, {3}, 0, 0}); // tied 8th + 8th
    s.push_back({CH_ASUS2, EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_ASUS2, EV_REST,     1.00, {}, 0, 0});
    // Bar 4: Bsus4 (+ optional B arpeggio bar)
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {1}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    1.00, {2}, 0, 0}); // tied 8th + 8th
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {2}, 0, 0});
    if (with_pickup_B) {
        s.push_back({CH_BSUS4, EV_PLUCK,    1.00, {1}, 0, 0});
        // Extra 2-beat cadence bar on B
        s.push_back({CH_B,     EV_ARP_ROLL, 2.00, {5, 4, 3, 2}, 0, 0});
    } else {
        s.push_back({CH_BSUS4, EV_REST,     1.00, {}, 0, 0});
    }
}

static void add_prechorus_3bars(std::vector<TabEvent> &s) {
    // Bar 15: Asus2 & B
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 16: #Gm7 & #C7
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 17: #Fm11
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {1}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {3}, 0, 0});
}

static void add_chorus_4bars_Bsus4_E_B_Cm7(std::vector<TabEvent> &s) {
    // Bar 18 / 26: Bsus4 (with 1弦2品 fill on beat 4)
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_BSUS4, EV_ARP_ROLL, 1.00, {5, 4, 3, 2}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {1}, 1, 2}); // 1弦2品
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {2}, 0, 0});
    // Bar 19 / 27: E
    s.push_back({CH_E,     EV_PLUCK,    0.50, {6}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {3, 2}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {1}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    // Bar 20 / 28: B
    s.push_back({CH_B,     EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {1}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK,    0.50, {3}, 0, 0});
    // Bar 21 / 29: #Cm7
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {5}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {1}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK,    0.50, {3}, 0, 0});
}

static void add_chorus_main_section(std::vector<TabEvent> &s) {
    // Part 1: Bars 18-21
    add_chorus_4bars_Bsus4_E_B_Cm7(s);

    // Part 2: Bars 22-25 (#Gm7 | Asus2 - B | #Gm7 - #Cm7 | #Fm11)
    // Bar 22: #Gm7
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {1}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3}, 0, 0});
    // Bar 23: Asus2 & B
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 24: #Gm7 & #Cm7
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_CSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 25: #Fm11
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {1}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {3}, 0, 0});

    // Part 3: Bars 26-29
    add_chorus_4bars_Bsus4_E_B_Cm7(s);

    // Part 4: Bars 30-34 (#Gm7 | Asus2 - B | #Gm7 - #C7 | #Fm11 - B | E)
    // Bar 30: #Gm7
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {1}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3}, 0, 0});
    // Bar 31: Asus2 & B
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_ASUS2, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 32: #Gm7 & #C7
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_GSM7,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_CS7,   EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 33: #Fm11 & B
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_FSM11, EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {5}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {3, 2}, 0, 0});
    s.push_back({CH_B,     EV_PLUCK, 0.50, {4}, 0, 0});
    // Bar 34: E
    s.push_back({CH_E,     EV_PLUCK,    0.50, {6}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_E,     EV_ARP_ROLL, 1.00, {6, 4, 3, 2, 1}, 0, 0});
    s.push_back({CH_E,     EV_REST,     1.00, {}, 0, 0});
}

static std::vector<TabEvent> build_full_score() {
    std::vector<TabEvent> s;

    // ==================== 1. INTRO (Page 1 System 1: Bars 1-5) ====================
    add_measure_E7_Bm7(s); // Bar 1
    add_measure_A5_C8(s);  // Bar 2
    add_measure_E7_Bm7(s); // Bar 3
    // Bar 4: A(5)
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 0.75, {6, 3, 2}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {4}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {2}, 0, 0}); // tied 16th+16th
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 2.00, {6, 3, 2, 1}, 0, 0}); // Bar 4 beat 4 tied to Bar 5 beat 1
    // Bar 5: A(5) beat 2 arpeggio + 2 beats vocal pickup ("你会在哪")
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 1.00, {6, 3, 2, 1}, 0, 0});
    s.push_back({CH_A5_POS, EV_REST,     2.00, {}, 0, 0});

    // ==================== 2. VERSE 1 PART A (Page 1 System 2: Bars 6-9) ====================
    add_verse_block_4bars(s, false);

    // ==================== 3. FIRST PASS ||: ... :|| (Bars 10-35) ====================
    add_verse_block_4bars(s, true);  // Bars 10-14 (Verse 1 Part B)
    add_prechorus_3bars(s);          // Bars 15-17 (Pre-Chorus 1)
    add_chorus_main_section(s);      // Bars 18-34 (Chorus 1)
    // Bar 35: Em - E turnaround before :||
    s.push_back({CH_EM, EV_PLUCK, 0.50, {6}, 0, 0});
    s.push_back({CH_EM, EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_EM, EV_PLUCK, 0.50, {2}, 0, 0});
    s.push_back({CH_EM, EV_PLUCK, 0.50, {1}, 0, 0});
    s.push_back({CH_E,  EV_PLUCK, 0.50, {4}, 0, 0});
    s.push_back({CH_E,  EV_PLUCK, 0.50, {3}, 0, 0});
    s.push_back({CH_E,  EV_PLUCK, 1.00, {3, 2}, 0, 0});

    // ==================== 4. SECOND PASS ||: ... D.S. (Bars 10-34) ====================
    add_verse_block_4bars(s, true);  // Bars 10-14 (Verse 2)
    add_prechorus_3bars(s);          // Bars 15-17 (Pre-Chorus 2)
    add_chorus_main_section(s);      // Bars 18-34 (Chorus 2)

    // ==================== 5. CODA & OUTRO (Page 2 Systems 4-5: Bars 36-43) ====================
    // Bar 36: E ("如果爱有错")
    s.push_back({CH_E,     EV_PLUCK,    0.50, {6}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    0.50, {4}, 0, 0});
    s.push_back({CH_E,     EV_PLUCK,    1.00, {2, 1}, 0, 0});
    s.push_back({CH_E,     EV_ARP_ROLL, 1.00, {6, 3, 2, 1}, 0, 0});
    s.push_back({CH_E,     EV_REST,     1.00, {}, 0, 0});
    // Bar 37: Asus2 & Bsus4 ("也真的 / 也等得起")
    s.push_back({CH_ASUS2, EV_ARP_ROLL, 1.00, {5, 3, 2, 1}, 0, 0});
    s.push_back({CH_ASUS2, EV_REST,     1.00, {}, 0, 0});
    s.push_back({CH_BSUS4, EV_ARP_ROLL, 0.50, {5, 4, 3, 2}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {3}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_BSUS4, EV_PLUCK,    0.50, {3}, 0, 0});
    // Bar 38: Bsus4 (2-beat measure | 0 0 |)
    s.push_back({CH_BSUS4, EV_PLUCK,    1.00, {4}, 0, 0});
    s.push_back({CH_BSUS4, EV_REST,     1.00, {}, 0, 0});

    // Bar 39: E(7) & Bm7(7)
    add_measure_E7_Bm7(s);
    // Bar 40: A(5) & C(8)
    add_measure_A5_C8(s);
    // Bar 41: E(7) & Bm7(7)
    add_measure_E7_Bm7(s);
    // Bar 42: A(5)
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 0.75, {6, 3, 2}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {4}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.50, {2}, 0, 0});
    s.push_back({CH_A5_POS, EV_PLUCK,    0.25, {3}, 0, 0});
    s.push_back({CH_A5_POS, EV_ARP_ROLL, 1.00, {6, 3, 2, 1}, 0, 0});
    s.push_back({CH_A5_POS, EV_REST,     1.00, {}, 0, 0});

    // Bar 43: Harm------------ (4弦7品 Harm. -> 1弦12品 Harm. + natural ring-out)
    s.push_back({CH_NONE,   EV_HARMONIC, 2.00, {4}, 4, 7});
    s.push_back({CH_NONE,   EV_HARMONIC, 4.00, {1}, 1, 12});

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
    uint16_t audio_format = 1;
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
    const char *out_wav = (argc > 1) ? argv[1] : "/tmp/piaoliuping_guitar.wav";
    std::vector<TabEvent> score = build_full_score();

    double total_beats = 0.0;
    for (const auto &ev : score) total_beats += ev.dur_beats;
    std::printf("Total events: %zu, Total beats: %.2f (%.2f seconds at %.1f BPM)\n",
                score.size(), total_beats, total_beats * BEAT_SEC, BPM);

    GuitarString strings[7];
    for (int i = 1; i <= 6; ++i) strings[i].init(i);

    Biquad hp_sub, body_helmholtz, body_top, body_wood1, body_wood2, body_pres, lp_air;
    hp_sub.setHighPass(62.0, 0.707);
    body_helmholtz.setPeaking(100.0, 4.0, 5.0);
    body_top.setPeaking(200.0, 3.6, 5.5);
    body_wood1.setPeaking(385.0, 3.4, 3.2);
    body_wood2.setPeaking(800.0, 3.0, -1.8);
    body_pres.setPeaking(2500.0, 2.2, 3.5);
    lp_air.setLowPass(11000.0, 0.707);

    StereoReverb reverb;
    reverb.init();

    int total_samples = (int)((total_beats * BEAT_SEC + 1.5) * SAMPLE_RATE);
    std::vector<int16_t> pcm;
    pcm.reserve(total_samples * 2);

    ChordId prev_chord = CH_NONE;

    for (size_t idx = 0; idx < score.size(); ++idx) {
        const TabEvent &ev = score[idx];
        double dur_sec = ev.dur_beats * BEAT_SEC;
        int n_samples = (int)std::round(dur_sec * SAMPLE_RATE);

        // When the left-hand chord shape changes, softly damp strings whose fret changes
        if (ev.chord != CH_NONE && ev.chord != prev_chord) {
            for (int s = 1; s <= 6; ++s) {
                int new_f = CHORDS[ev.chord].fret[s];
                if (strings[s].current_fret != new_f && !strings[s].is_harmonic) {
                    strings[s].damp_rate = 6.5; // Natural left-hand chord change damping
                }
            }
            prev_chord = ev.chord;
        }

        if (ev.type == EV_PLUCK || ev.type == EV_ARP_ROLL) {
            for (size_t k = 0; k < ev.strings.size(); ++k) {
                int str_no = ev.strings[k];
                if (str_no < 1 || str_no > 6) continue;
                int fret = CHORDS[ev.chord].fret[str_no];
                if (ev.explicit_string == str_no) {
                    fret = ev.explicit_fret;
                }
                double roll_delay = (ev.type == EV_ARP_ROLL) ? (k * 0.022) : 0.0;
                strings[str_no].trigger(fret, roll_delay, false);
            }
        } else if (ev.type == EV_HARMONIC) {
            strings[ev.explicit_string].trigger(ev.explicit_fret, 0.0, true);
        }

        for (int n = 0; n < n_samples; ++n) {
            double rawL = 0.0, rawR = 0.0;
            for (int s = 1; s <= 6; ++s) {
                double sig = strings[s].render_sample();
                double pan = (3.5 - s) * 0.075;
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

            double inL = body + (rawL - mono) * 0.65;
            double inR = body + (rawR - mono) * 0.65;

            double outL = 0.0, outR = 0.0;
            reverb.process(inL, inR, outL, outR);

            outL = std::tanh(outL * 1.40) * 0.88;
            outR = std::tanh(outR * 1.40) * 0.88;

            int32_t sL = (int32_t)(outL * 32767.0);
            int32_t sR = (int32_t)(outR * 32767.0);
            sL = std::max(-32768, std::min(32767, sL));
            sR = std::max(-32768, std::min(32767, sR));

            pcm.push_back((int16_t)sL);
            pcm.push_back((int16_t)sR);
        }
    }

    // 1.5 sec natural fadeout tail
    int tail_samples = (int)(1.5 * SAMPLE_RATE);
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
        outL = std::tanh(outL * 1.40) * 0.88 * fade;
        outR = std::tanh(outR * 1.40) * 0.88 * fade;
        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outL * 32767.0)));
        pcm.push_back((int16_t)std::max(-32768.0, std::min(32767.0, outR * 32767.0)));
    }

    write_wav(out_wav, pcm);
    std::printf("Successfully wrote WAV: %s (%zu stereo frames)\n", out_wav, pcm.size() / 2);
    return 0;
}