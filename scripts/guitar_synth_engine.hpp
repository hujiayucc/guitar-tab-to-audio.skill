#ifndef GUITAR_SYNTH_ENGINE_HPP
#define GUITAR_SYNTH_ENGINE_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

namespace GuitarSynth {

static const int SAMPLE_RATE = 48000;
static const double PI = 3.14159265358979323846;
static const int OPEN_STRING_MIDI[7] = {0, 64, 59, 55, 50, 45, 40}; // Standard: 1:E4 .. 6:E2

enum InstrumentType {
    INST_STEEL_ACOUSTIC = 0,  // 原声钢弦民谣木吉他（默认）
    INST_NYLON_CLASSICAL,     // 古典尼龙弦木吉他（温暖圆润）
    INST_ELECTRIC_CLEAN,      // 电吉他清音（单线圈透亮长延音）
    INST_ELECTRIC_OVERDRIVE   // 电吉他过载/Solo失真（电子管软削波 + 4x12箱体）
};

enum MicPosition {
    MIC_BALANCED = 0,         // 均衡标准（音孔+12品混合）
    MIC_CLOSE,                // 近场清晰（干声比例高、颗粒感强）
    MIC_ROOM                  // 录音棚房间氛围（湿声与早反射更丰富）
};

enum Articulation {
    ART_REST = 0,
    ART_PICK,             // 普通拨弦 / 扫弦单音
    ART_PALM_MUTE,        // P.M. 右手掌根护弦闷音
    ART_DEAD_NOTE,        // × 左手虚按哑音（单弦制音）
    ART_CHUCK_MUTE,       // 切音（扫弦切音 / 拍弦止音，全弦急速消音 + 高频切音刷弦声）
    ART_STACCATO,         // . 断音 / 跳音（发音至42%时值后快速制音）
    ART_HAMMER,           // H 左手击弦
    ART_PULL,             // P 左手勾弦
    ART_TAP,              // T 右手点弦（高能量指尖锤击品丝）
    ART_SLIDE,            // sl. 连音滑音（不重拨）
    ART_SHIFT_SLIDE,      // 换把拨弦滑音（滑到目标品后重拨）
    ART_SLIDE_IN,         // /N 或 \N 无头滑入
    ART_SLIDE_OUT,        // N\ 或 N/ 无尾滑出并衰减
    ART_BEND_QUARTER,     // 1/4 微推弦 (+0.5 半音)
    ART_BEND_HALF_UP,     // 1/2 半音推弦 (+1.0 半音)
    ART_BEND_UP,          // full 全音推弦 (+2.0 半音)
    ART_BEND_1_5_UP,      // 1 1/2 小三度推弦 (+3.0 半音)
    ART_BEND_UP_REL,      // full 推弦并释放回落
    ART_PREBEND_REL,      // 预推后拨弦并释放回落
    ART_BEND_HOLD_UP,     // 延音微揉后尾端全音推弦
    ART_VIBRATO,          // ~~~ 揉弦
    ART_VIB_BEND_TAIL,    // ~~~ 揉弦后尾端接全音推弦
    ART_TREMOLO,          // /// 拨片连续震音 / 古典轮指
    ART_TRILL,            // tr~~~ 双音快速击勾颤音
    ART_CHORD,            // 多弦齐拨（双音/柱式和弦/横按）
    ART_ARP_ROLL,         // ↑ 向上琶音（从低音弦至高音弦依次触发）
    ART_ARP_DOWN,         // ↓ 向下反琶音（从高音弦至低音弦依次触发）
    ART_RASGUEADO,        // 弗拉门戈扇形轮扫
    ART_HARMONIC,         // N.H. 自然泛音 (12/7/5/4/9品)
    ART_ARTIFICIAL_HARM,  // A.H. 人工泛音 (按弦品 + 12品高八度泛音)
    ART_TAPPED_HARM,      // T.H. 点弦泛音 (点击 +12品泛音 + 品丝瞬态)
    ART_PERC_KICK,        // 打板底鼓 (Palm Hit / Kick：手掌根击面板低音共振 ~92Hz)
    ART_PERC_SLAP,        // 拇指拍弦军鼓 (Thumb Slap：拍击低音弦撞击品丝金属声)
    ART_PERC_TAP,         // 指尖面板/侧板鼓点 (Golpe / Finger Tap：高频木板敲击声)
    ART_SLAP_STRUM,       // AM 拍弦扫弦二合一 (拇指Slap拍击低音弦 + 指甲同时弹扫和弦高音弦)
    ART_GRACE_NOTE,       // 前倚音 / 装饰音 (约35ms极短前倚音平滑过渡至主音)
    ART_LET_RING,         // 延音线 / 保留音 (免疫自动换弦与换和弦阻尼，保持自然延音)
    ART_FRET_NOISE,       // 换把擦弦声 (左手手指滑过缠绕弦的高频擦弦瞬态)
    ART_PICK_SCRAPE,      // 拨片刮弦 (拨片沿低音缠绕弦刮擦的颗粒感扫频声)
    ART_SLAP_HARMONIC     // 拍泛音 (右手拍击+12品丝触发金属撞击与清脆八度泛音)
};

static inline double midi_to_freq(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

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

// Multi-tap Early Reflections & Body/Cabinet IR Approximation
struct BodyEarlyReflections {
    static const int BUF_LEN = 2048;
    double bufL[BUF_LEN]{}, bufR[BUF_LEN]{};
    int idx = 0;
    double mix = 0.14;

    void init(MicPosition mic) {
        std::memset(bufL, 0, sizeof(bufL));
        std::memset(bufR, 0, sizeof(bufR));
        idx = 0;
        if (mic == MIC_CLOSE) mix = 0.06;
        else if (mic == MIC_ROOM) mix = 0.24;
        else mix = 0.13;
    }

    inline void process(double inL, double inR, double &outL, double &outR) {
        bufL[idx] = inL;
        bufR[idx] = inR;
        // 6 short body/room early reflection taps (~1.8ms to ~19ms)
        const int tapsL[6] = {89, 173, 311, 487, 677, 919};
        const int tapsR[6] = {103, 199, 337, 521, 709, 967};
        const double gains[6] = {0.34, -0.26, 0.21, -0.16, 0.12, 0.09};
        double erL = 0.0, erR = 0.0;
        for (int i = 0; i < 6; ++i) {
            erL += bufL[(idx - tapsL[i] + BUF_LEN) & (BUF_LEN - 1)] * gains[i];
            erR += bufR[(idx - tapsR[i] + BUF_LEN) & (BUF_LEN - 1)] * gains[i];
        }
        idx = (idx + 1) & (BUF_LEN - 1);
        outL = inL + erL * mix;
        outR = inR + erR * mix;
    }
};

// Sympathetic Open-String Resonance Bank (simulates subtle acoustic coupling between strings)
struct SympatheticResonator {
    double re[6]{}, im[6]{};
    double cos_w[6]{}, sin_w[6]{}, decay[6]{};
    double amount = 0.022;

    void init(const int tuning_midi[7], int capo, InstrumentType inst) {
        double dt = 1.0 / SAMPLE_RATE;
        amount = (inst == INST_STEEL_ACOUSTIC) ? 0.024 :
                 (inst == INST_NYLON_CLASSICAL) ? 0.018 : 0.010;
        for (int s = 1; s <= 6; ++s) {
            double f = midi_to_freq((double)(tuning_midi[s] + capo));
            double w = 2.0 * PI * f * dt;
            cos_w[s - 1] = std::cos(w);
            sin_w[s - 1] = std::sin(w);
            decay[s - 1] = std::exp(-dt / 1.45);
            re[s - 1] = 0.0;
            im[s - 1] = 0.0;
        }
    }

    inline double process(double excite) {
        double sum = 0.0;
        for (int i = 0; i < 6; ++i) {
            double r = re[i] + excite * 0.0025;
            double m = im[i];
            double nr = (r * cos_w[i] - m * sin_w[i]) * decay[i];
            double ni = (r * sin_w[i] + m * cos_w[i]) * decay[i];
            re[i] = nr;
            im[i] = ni;
            sum += ni;
        }
        return sum * amount;
    }
};

// Instrument Body / Amp-Cab Tone Shaper
struct GuitarToneChain {
    InstrumentType inst = INST_STEEL_ACOUSTIC;
    Biquad f1, f2, f3, f4, f5, f6, f7;

    void init(InstrumentType type) {
        inst = type;
        if (type == INST_NYLON_CLASSICAL) {
            f1.setHighPass(68.0, 0.707);
            f2.setPeaking(112.0, 3.6, 5.2);   // Cedar/Nylon Helmholtz air resonance
            f3.setPeaking(225.0, 3.4, 4.8);   // Warm classical top mode
            f4.setPeaking(440.0, 3.0, 2.2);
            f5.setPeaking(950.0, 2.8, -2.4);
            f6.setPeaking(2100.0, 1.8, 1.4);  // Gentle finger warmth
            f7.setLowPass(6800.0, 0.707);     // Nylon silky high rolloff
        } else if (type == INST_ELECTRIC_CLEAN) {
            f1.setHighPass(75.0, 0.707);
            f2.setPeaking(140.0, 1.8, 2.5);   // Combo cab warmth
            f3.setPeaking(480.0, 2.2, -1.8);  // Fender-style mid scoop
            f4.setPeaking(1250.0, 2.0, 1.5);
            f5.setPeaking(2850.0, 2.2, 4.2);  // Single-coil sparkle
            f6.setPeaking(4600.0, 2.0, 1.8);
            f7.setLowPass(7800.0, 0.85);      // Guitar speaker rolloff
        } else if (type == INST_ELECTRIC_OVERDRIVE) {
            f1.setHighPass(85.0, 0.707);
            f2.setPeaking(150.0, 2.0, 3.2);   // 4x12 cab thump
            f3.setPeaking(720.0, 2.2, 3.4);   // Singing lead mids
            f4.setPeaking(1650.0, 2.5, 2.6);
            f5.setPeaking(3200.0, 2.0, 1.8);
            f6.setPeaking(4800.0, 2.5, -3.0); // Tame fizz
            f7.setLowPass(5400.0, 0.90);      // 4x12 speaker cabinet cutoff
        } else {
            // Default: Steel-String Acoustic Guitar (Spruce top + Rosewood body)
            f1.setHighPass(63.0, 0.707);
            f2.setPeaking(101.0, 4.1, 5.1);
            f3.setPeaking(202.0, 3.7, 5.6);
            f4.setPeaking(390.0, 3.5, 3.4);
            f5.setPeaking(810.0, 3.0, -1.9);
            f6.setPeaking(2480.0, 2.2, 3.6);
            f7.setLowPass(10800.0, 0.707);
        }
    }

    inline double process(double x) {
        if (inst == INST_ELECTRIC_OVERDRIVE) {
            // Asymmetric tube overdrive stage before speaker cabinet EQ
            double pre = x * 5.2;
            double driven = (pre >= 0.0) ? std::tanh(pre) : (std::tanh(pre * 0.82) * 1.08);
            x = driven * 0.42;
        }
        return f7.process(f6.process(f5.process(f4.process(f3.process(f2.process(f1.process(x)))))));
    }
};

// Single voice for Acoustic Guitar Body Percussion & Chucking
struct BodyPercVoice {
    bool active = false;
    Articulation type = ART_PERC_KICK;
    double age_sec = 0.0;
    double velocity = 1.0;
    uint32_t rng = 987654321u;

    inline double rand_bipolar() {
        rng = rng * 1664525u + 1013904223u;
        return ((int32_t)rng) / 2147483648.0;
    }

    void trigger(Articulation art, double vel = 1.0, uint32_t seed = 987654321u) {
        type = art;
        age_sec = 0.0;
        velocity = std::max(0.05, std::min(1.5, vel));
        rng = seed;
        active = true;
    }

    inline double render_sample() {
        if (!active) return 0.0;
        double dt = 1.0 / SAMPLE_RATE;
        double t = age_sec;
        age_sec += dt;

        if (t > 0.35) {
            active = false;
            return 0.0;
        }

        double out = 0.0;
        double noise = rand_bipolar();

        if (type == ART_PERC_KICK) {
            double f_inst = 82.0 + 55.0 * std::exp(-t / 0.018);
            double thump = std::sin(2.0 * PI * f_inst * t) * std::exp(-t / 0.065);
            double sub = std::sin(2.0 * PI * 96.0 * t) * std::exp(-t / 0.085) * 0.6;
            double knock = noise * std::exp(-t / 0.008) * 0.25;
            out = (thump * 0.85 + sub + knock) * 0.72;
        } else if (type == ART_PERC_SLAP || type == ART_SLAP_STRUM) {
            double clack = noise * (0.65 * std::exp(-t / 0.014) + 0.35 * std::exp(-t / 0.035));
            double metal_ring = (std::sin(2.0 * PI * 1850.0 * t) + 0.6 * std::sin(2.0 * PI * 3120.0 * t)) * std::exp(-t / 0.016);
            double body_hit = std::sin(2.0 * PI * 165.0 * t) * std::exp(-t / 0.028) * 0.45;
            out = (clack * 0.55 + metal_ring * 0.28 + body_hit) * 0.65;
        } else if (type == ART_PERC_TAP) {
            double wood_mode1 = std::sin(2.0 * PI * 245.0 * t) * std::exp(-t / 0.025);
            double wood_mode2 = std::sin(2.0 * PI * 680.0 * t) * std::exp(-t / 0.014);
            double click = noise * std::exp(-t / 0.007) * 0.75;
            out = (wood_mode1 * 0.45 + wood_mode2 * 0.35 + click) * 0.62;
        } else if (type == ART_CHUCK_MUTE) {
            double env = (t < 0.004) ? (t / 0.004) : std::exp(-(t - 0.004) / 0.015);
            double comb = std::sin(2.0 * PI * 1450.0 * t) * 0.3 + std::sin(2.0 * PI * 2650.0 * t) * 0.25;
            out = (noise * 0.75 + comb * noise) * env * 0.68;
        } else if (type == ART_SLAP_HARMONIC) {
            double click = noise * std::exp(-t / 0.009) * 0.55;
            double fret_ping = std::sin(2.0 * PI * 2400.0 * t) * std::exp(-t / 0.012) * 0.35;
            out = (click + fret_ping) * 0.48;
        }

        return out * velocity;
    }
};

// 4-Voice Polyphonic Acoustic Guitar Body Percussion Pool
struct BodyPercussion {
    static const int MAX_VOICES = 4;
    BodyPercVoice voices[MAX_VOICES];
    int next_voice = 0;
    uint32_t seed_counter = 1;

    void trigger(Articulation art, double velocity = 1.0) {
        int slot = next_voice;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!voices[i].active) {
                slot = i;
                break;
            }
        }
        next_voice = (slot + 1) % MAX_VOICES;
        voices[slot].trigger(art, velocity, 987654321u + 2654435761u * (seed_counter++));
    }

    inline double render_sample() {
        double sum = 0.0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices[i].active) sum += voices[i].render_sample();
        }
        return sum;
    }
};

struct StereoReverb {
    std::vector<double> combL[4], combR[4];
    int combIdxL[4]{}, combIdxR[4]{};
    double combLpL[4]{}, combLpR[4]{};
    std::vector<double> allpassL[2], allpassR[2];
    int apIdxL[2]{}, apIdxR[2]{};
    double dry_gain = 0.84;
    double wet_gain = 0.23;

    void init(MicPosition mic = MIC_BALANCED) {
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
        if (mic == MIC_CLOSE) {
            dry_gain = 0.90; wet_gain = 0.13;
        } else if (mic == MIC_ROOM) {
            dry_gain = 0.74; wet_gain = 0.34;
        } else {
            dry_gain = 0.84; wet_gain = 0.23;
        }
    }

    inline void process(double inL, double inR, double &outL, double &outR) {
        double wetL = 0.0, wetR = 0.0;
        const double fb[4] = {0.74, 0.72, 0.70, 0.68};
        const double damp = 0.37;

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

        outL = inL * dry_gain + wetL * wet_gain;
        outR = inR * dry_gain + wetR * wet_gain;
    }
};

struct GuitarString {
    static const int MAX_PARTIALS = 26;
    int string_idx = 1;
    int open_midi_base = 64;
    int capo_semitones = 0;
    InstrumentType inst_type = INST_STEEL_ACOUSTIC;
    bool active = false;
    bool fast_phasor_mode = true;
    bool let_ring = false;
    int current_fret = -1;
    double current_midi = 60.0;
    double target_midi = 60.0;
    double slide_start_midi = 60.0;
    double slide_end_midi = 60.0;
    double trill_alt_midi = 62.0;
    double age_sec = 0.0;
    double note_dur_sec = 1.0;
    double delay_start_sec = 0.0;
    double damp_rate = 1.0;
    double next_retrigger_sec = 1e9;
    double current_velocity = 1.0;
    Articulation current_art = ART_PICK;

    double phase[MAX_PARTIALS]{};
    double amp[MAX_PARTIALS]{};
    double decay_tau[MAX_PARTIALS]{};
    double init_amp[MAX_PARTIALS]{};

    int num_active_partials = 0;
    double re[MAX_PARTIALS]{};
    double im[MAX_PARTIALS]{};
    double cos_dphi[MAX_PARTIALS]{};
    double sin_dphi[MAX_PARTIALS]{};
    double decay_step[MAX_PARTIALS]{};

    double transient_env = 0.0;
    double transient_freq = 1200.0;
    uint32_t rng = 123456789;

    void init(int idx, int capo = 0, int custom_open_midi = -1, InstrumentType inst = INST_STEEL_ACOUSTIC) {
        string_idx = idx;
        capo_semitones = capo;
        inst_type = inst;
        open_midi_base = (custom_open_midi > 0) ? custom_open_midi : OPEN_STRING_MIDI[std::max(1, std::min(6, idx))];
        rng = 13579u * (uint32_t)(idx + 1);
    }

    inline bool can_auto_damp() const {
        return !let_ring;
    }

    inline double rand_bipolar() {
        rng = rng * 1664525u + 1013904223u;
        return ((int32_t)rng) / 2147483648.0;
    }

    void trigger(int fret, double dur_sec, Articulation art, int aux_fret = -1,
                 double roll_delay_sec = 0.0, double velocity = 1.0) {
        if (fret < 0 && art != ART_DEAD_NOTE && art != ART_FRET_NOISE && art != ART_PICK_SCRAPE) return;
        current_fret = fret;
        current_art = art;
        note_dur_sec = std::max(0.01, dur_sec);
        delay_start_sec = std::max(0.0, roll_delay_sec);
        age_sec = 0.0;
        damp_rate = 1.0;
        next_retrigger_sec = 1e9;
        current_velocity = std::max(0.05, std::min(1.5, velocity));

        bool harmonic = (art == ART_HARMONIC || art == ART_ARTIFICIAL_HARM ||
                         art == ART_TAPPED_HARM || art == ART_SLAP_HARMONIC);
        let_ring = (art == ART_LET_RING || harmonic);

        if (art == ART_FRET_NOISE || art == ART_PICK_SCRAPE) {
            num_active_partials = 0;
            for (int k = 0; k < MAX_PARTIALS; ++k) {
                amp[k] = 0.0; re[k] = 0.0; im[k] = 0.0;
            }
            fast_phasor_mode = false;
            transient_env = 0.0;
            active = true;
            return;
        }

        double open_midi = (double)(open_midi_base + capo_semitones);
        double base_midi = open_midi + std::max(0, fret);

        if (art == ART_HARMONIC) {
            if (fret == 12) base_midi = open_midi + 12.0;
            else if (fret == 7 || fret == 19) base_midi = open_midi + 19.01955;
            else if (fret == 5) base_midi = open_midi + 24.0;
            else if (fret == 4 || fret == 9) base_midi = open_midi + 27.86314;
        } else if (art == ART_ARTIFICIAL_HARM || art == ART_TAPPED_HARM || art == ART_SLAP_HARMONIC) {
            int node_offset = (aux_fret > fret) ? (aux_fret - fret) : 12;
            if (node_offset == 7 || node_offset == 19) base_midi = open_midi + fret + 19.01955;
            else if (node_offset == 5) base_midi = open_midi + fret + 24.0;
            else base_midi = open_midi + fret + 12.0;
        }

        if (art == ART_SLIDE_IN) {
            int sf = (aux_fret >= 0) ? aux_fret : std::max(0, fret - 2);
            slide_start_midi = open_midi + sf;
            slide_end_midi = base_midi;
            current_midi = slide_start_midi;
        } else if (art == ART_GRACE_NOTE) {
            int gf = (aux_fret >= 0) ? aux_fret : std::max(0, fret - 2);
            slide_start_midi = open_midi + gf;
            slide_end_midi = base_midi;
            current_midi = slide_start_midi;
        } else if (art == ART_SLIDE || art == ART_SHIFT_SLIDE) {
            slide_start_midi = (aux_fret >= 0) ? (open_midi + aux_fret) : current_midi;
            slide_end_midi = base_midi;
            current_midi = slide_start_midi;
        } else if (art == ART_SLIDE_OUT) {
            slide_start_midi = base_midi;
            int end_f = (aux_fret >= 0) ? aux_fret : std::max(0, fret - 4);
            slide_end_midi = open_midi + end_f;
            current_midi = base_midi;
        } else if (art == ART_TRILL) {
            int alt_f = (aux_fret >= 0) ? aux_fret : (fret + 2);
            trill_alt_midi = open_midi + alt_f;
            current_midi = base_midi;
        } else {
            current_midi = base_midi;
            slide_start_midi = base_midi;
            slide_end_midi = base_midi;
        }
        target_midi = base_midi;

        if (art == ART_TREMOLO) next_retrigger_sec = 0.075;

        bool is_pitch_modulated = (art == ART_SLIDE || art == ART_SHIFT_SLIDE ||
                                   art == ART_SLIDE_IN || art == ART_SLIDE_OUT ||
                                   art == ART_GRACE_NOTE ||
                                   art == ART_BEND_QUARTER || art == ART_BEND_HALF_UP ||
                                   art == ART_BEND_UP || art == ART_BEND_1_5_UP ||
                                   art == ART_BEND_UP_REL || art == ART_PREBEND_REL ||
                                   art == ART_BEND_HOLD_UP || art == ART_VIBRATO ||
                                   art == ART_VIB_BEND_TAIL || art == ART_TREMOLO ||
                                   art == ART_TRILL || art == ART_STACCATO);
        fast_phasor_mode = !is_pitch_modulated;

        bool is_wound = (string_idx >= 4);
        double beta = (inst_type == INST_NYLON_CLASSICAL) ? 0.21 : (is_wound ? 0.162 : 0.144);
        double sustain_mult = 1.0;
        if (inst_type == INST_NYLON_CLASSICAL) sustain_mult = 0.85;
        else if (inst_type == INST_ELECTRIC_CLEAN) sustain_mult = 1.45;
        else if (inst_type == INST_ELECTRIC_OVERDRIVE) sustain_mult = 2.10;

        double base_sustain = (harmonic ? 3.6 : (is_wound ? 2.55 : 2.05)) * sustain_mult;
        if (art == ART_LET_RING) base_sustain *= 1.35;
        if (!harmonic) base_sustain *= std::pow(0.98, (double)std::max(0, fret));

        if (art == ART_PALM_MUTE) base_sustain *= 0.26;
        else if (art == ART_DEAD_NOTE) base_sustain = 0.032;

        bool is_legato = (art == ART_HAMMER || art == ART_PULL || art == ART_SLIDE);

        double base_slope = (inst_type == INST_NYLON_CLASSICAL) ? 1.58 :
                            (inst_type == INST_ELECTRIC_CLEAN) ? 1.12 :
                            (inst_type == INST_ELECTRIC_OVERDRIVE) ? 1.05 :
                            (is_wound ? 1.30 : 1.20);
        double bright_exp = base_slope - 0.18 * (current_velocity - 1.0);
        bright_exp = std::max(0.92, std::min(1.85, bright_exp));

        double dt = 1.0 / SAMPLE_RATE;
        double f0 = midi_to_freq(base_midi);
        double B = (inst_type == INST_NYLON_CLASSICAL) ? 0.000018 : (is_wound ? 0.000072 : 0.000042);
        num_active_partials = 0;

        for (int k = 1; k <= MAX_PARTIALS; ++k) {
            int idx = k - 1;
            double a = 0.0;
            double tau = 1.0;
            if (harmonic) {
                if (k == 1) a = 0.95;
                else if (k == 2) a = 0.26;
                else if (k == 3) a = 0.09;
                tau = base_sustain / (1.0 + 0.35 * (k - 1));
            } else if (art == ART_PALM_MUTE) {
                double pluck_comb = std::fabs(std::sin(k * PI * 0.22));
                double pm_lp = std::exp(-0.48 * (k - 1));
                a = pluck_comb * pm_lp * 1.25;
                tau = base_sustain / (1.0 + 0.45 * (k - 1));
            } else if (art == ART_DEAD_NOTE) {
                a = (k <= 8) ? (0.35 / std::pow((double)k, 0.7)) : 0.0;
                tau = base_sustain / (1.0 + 0.25 * (k - 1));
            } else {
                double pluck_comb = std::fabs(std::sin(k * PI * beta));
                double slope = 1.0 / std::pow((double)k, bright_exp);
                if (k == 2) slope *= 1.16;
                if (k == 3) slope *= 1.08;
                double new_amp = pluck_comb * slope;
                if (is_legato) {
                    double lf = std::exp(-0.14 * (k - 1));
                    a = std::max(amp[idx] * 0.65, new_amp * 0.82 * lf);
                } else if (art == ART_TAP) {
                    double tf_curve = std::exp(-0.08 * (k - 1));
                    a = new_amp * 0.95 * tf_curve;
                } else {
                    a = new_amp;
                }
                double hi_damp = (inst_type == INST_NYLON_CLASSICAL) ? 0.085 : 0.042;
                tau = base_sustain / (1.0 + 0.15 * (k - 1) + hi_damp * (k - 1) * (k - 1));
            }
            a *= current_velocity;
            amp[idx] = a;
            init_amp[idx] = a;
            decay_tau[idx] = tau;

            double fk = f0 * k * std::sqrt(1.0 + B * k * k);
            if (fk < SAMPLE_RATE * 0.45) {
                num_active_partials = k;
                double dphi = 2.0 * PI * fk * dt;
                cos_dphi[idx] = std::cos(dphi);
                sin_dphi[idx] = std::sin(dphi);
                re[idx] = a;
                im[idx] = 0.0;
                decay_step[idx] = std::exp(-dt / tau);
            } else {
                re[idx] = 0.0; im[idx] = 0.0;
            }
        }

        double inst_trans_scale = (inst_type == INST_NYLON_CLASSICAL) ? 0.55 : 1.0;
        double vel_trans = std::pow(current_velocity, 1.15) * inst_trans_scale;
        if (art == ART_SLIDE) transient_env = 0.15 * vel_trans;
        else if (is_legato) transient_env = 0.35 * vel_trans;
        else if (art == ART_TAP || art == ART_TAPPED_HARM || art == ART_SLAP_HARMONIC) transient_env = 0.82 * vel_trans;
        else if (art == ART_DEAD_NOTE) transient_env = 1.25 * vel_trans;
        else if (harmonic) transient_env = 0.25 * vel_trans;
        else transient_env = 0.90 * vel_trans;

        transient_freq = (art == ART_TAP || art == ART_TAPPED_HARM || art == ART_SLAP_HARMONIC) ? (f0 * 4.2) : (f0 * 3.3);
        active = true;
    }

    inline double get_instant_midi() const {
        double p = age_sec / std::max(0.01, note_dur_sec);
        if (p > 1.0) p = 1.0;

        if (current_art == ART_GRACE_NOTE) {
            double gd = std::min(0.036, note_dur_sec * 0.25);
            if (age_sec < gd) return slide_start_midi;
            double trans = std::min(1.0, (age_sec - gd) / 0.008);
            return slide_start_midi + (slide_end_midi - slide_start_midi) * trans;
        }
        if (current_art == ART_SLIDE_IN) {
            double sd = std::min(0.055, note_dur_sec * 0.32);
            double sp = (age_sec < sd) ? (age_sec / sd) : 1.0;
            return slide_start_midi + (slide_end_midi - slide_start_midi) * (sp * sp * (3.0 - 2.0 * sp));
        }
        if (current_art == ART_SLIDE || current_art == ART_SHIFT_SLIDE) {
            double sd = std::min(0.080, note_dur_sec * 0.42);
            double sp = (age_sec < sd) ? (age_sec / sd) : 1.0;
            return slide_start_midi + (slide_end_midi - slide_start_midi) * (sp * sp * (3.0 - 2.0 * sp));
        }
        if (current_art == ART_SLIDE_OUT) {
            if (p < 0.35) return slide_start_midi;
            double sp = (p - 0.35) / 0.65;
            return slide_start_midi + (slide_end_midi - slide_start_midi) * (sp * sp);
        }
        if (current_art == ART_BEND_QUARTER || current_art == ART_BEND_HALF_UP ||
            current_art == ART_BEND_UP || current_art == ART_BEND_1_5_UP) {
            double semis = 2.0;
            if (current_art == ART_BEND_QUARTER) semis = 0.5;
            else if (current_art == ART_BEND_HALF_UP) semis = 1.0;
            else if (current_art == ART_BEND_1_5_UP) semis = 3.0;
            double bp = std::min(1.0, p / 0.58);
            double s = bp * bp * (3.0 - 2.0 * bp);
            double vib = (p > 0.6) ? 0.08 * std::sin(2.0 * PI * 5.5 * (age_sec - 0.5 * note_dur_sec)) : 0.0;
            return target_midi + semis * s + vib;
        }
        if (current_art == ART_BEND_UP_REL) {
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
            double bend_semis = 2.0;
            if (p > 0.18) {
                double rp = std::min(1.0, (p - 0.18) / 0.68);
                bend_semis = 2.0 * (1.0 - rp * rp * (3.0 - 2.0 * rp));
            }
            return target_midi + bend_semis;
        }
        if (current_art == ART_BEND_HOLD_UP) {
            if (p < 0.12) return target_midi + 0.8 * (1.0 - p / 0.12);
            if (p < 0.78) return target_midi + 0.14 * std::sin(2.0 * PI * 5.4 * (age_sec - 0.12 * note_dur_sec));
            double bp = (p - 0.78) / 0.22;
            return target_midi + 2.0 * (bp * bp * (3.0 - 2.0 * bp));
        }
        if (current_art == ART_VIB_BEND_TAIL) {
            if (p < 0.76) {
                double env = std::min(1.0, age_sec / 0.15);
                return target_midi + env * 0.22 * std::sin(2.0 * PI * 5.6 * age_sec);
            }
            double bp = (p - 0.76) / 0.24;
            return target_midi + 2.0 * (bp * bp * (3.0 - 2.0 * bp));
        }
        if (current_art == ART_VIBRATO) {
            double onset = std::min(1.0, std::max(0.0, (age_sec - 0.08) / 0.22));
            return target_midi + onset * 0.21 * std::sin(2.0 * PI * 5.5 * age_sec);
        }
        if (current_art == ART_TRILL) {
            int cycle = (int)(age_sec / 0.052);
            return (cycle % 2 == 0) ? target_midi : trill_alt_midi;
        }
        return target_midi;
    }

    inline double render_sample() {
        if (!active) return 0.0;
        double dt = 1.0 / SAMPLE_RATE;
        if (delay_start_sec > 0.0) {
            delay_start_sec -= dt;
            return 0.0;
        }

        if (current_art == ART_FRET_NOISE) {
            double dur = std::min(0.11, note_dur_sec);
            if (age_sec >= dur) { active = false; return 0.0; }
            double p = age_sec / dur;
            double env = std::sin(PI * p);
            double f_sweep = 2200.0 + 1800.0 * p;
            double grit = (std::sin(2.0 * PI * f_sweep * age_sec) + 0.5 * std::sin(2.0 * PI * (f_sweep * 1.43) * age_sec)) * rand_bipolar();
            age_sec += dt;
            return grit * env * current_velocity * 0.14;
        }
        if (current_art == ART_PICK_SCRAPE) {
            if (age_sec >= note_dur_sec) { active = false; return 0.0; }
            double p = age_sec / note_dur_sec;
            double env = (p < 0.08) ? (p / 0.08) : ((p > 0.82) ? ((1.0 - p) / 0.18) : 1.0);
            double f_sweep = 650.0 + 1850.0 * p;
            double winding = std::sin(2.0 * PI * (48.0 + 35.0 * p) * age_sec);
            double scrape = (0.55 + 0.45 * winding) * std::sin(2.0 * PI * f_sweep * age_sec) * (0.4 + 0.6 * rand_bipolar());
            age_sec += dt;
            return scrape * env * current_velocity * 0.24;
        }

        if (current_art == ART_STACCATO && age_sec >= note_dur_sec * 0.42) damp_rate = std::max(damp_rate, 32.0);
        if (current_art == ART_SLIDE_OUT && age_sec >= note_dur_sec * 0.45) damp_rate = std::max(damp_rate, 14.0);
        if (current_art == ART_TREMOLO && age_sec >= next_retrigger_sec && age_sec < note_dur_sec * 0.92) {
            next_retrigger_sec += 0.075;
            for (int k = 0; k < MAX_PARTIALS; ++k) amp[k] = std::max(amp[k] * 0.75, init_amp[k] * 0.88);
            transient_env = 0.65 * current_velocity;
        }

        double att_env = (age_sec < 0.0020) ? (age_sec / 0.0020) : 1.0;
        double sum = 0.0;

        if (fast_phasor_mode) {
            double extra_damp = (damp_rate > 1.0) ? std::exp(-dt * damp_rate) : 1.0;
            double energy = 0.0;
            for (int idx = 0; idx < num_active_partials; ++idx) {
                double r = re[idx];
                double i = im[idx];
                double nr = (r * cos_dphi[idx] - i * sin_dphi[idx]) * decay_step[idx] * extra_damp;
                double ni = (r * sin_dphi[idx] + i * cos_dphi[idx]) * decay_step[idx] * extra_damp;
                re[idx] = nr; im[idx] = ni;
                sum += ni;
                energy += (nr * nr + ni * ni);
            }
            if (energy < 1e-10 && age_sec > 0.05) active = false;
        } else {
            double inst_midi = get_instant_midi();
            current_midi = inst_midi;
            double f0 = midi_to_freq(inst_midi);
            double B = (inst_type == INST_NYLON_CLASSICAL) ? 0.000018 : ((string_idx >= 4) ? 0.000072 : 0.000042);
            double max_a = 0.0;
            for (int k = 1; k <= MAX_PARTIALS; ++k) {
                int idx = k - 1;
                if (amp[idx] < 1e-6) continue;
                double fk = f0 * k * std::sqrt(1.0 + B * k * k);
                if (fk >= SAMPLE_RATE * 0.45) { amp[idx] = 0.0; continue; }
                phase[idx] += 2.0 * PI * fk * dt;
                if (phase[idx] > 2.0 * PI) phase[idx] -= 2.0 * PI;
                sum += amp[idx] * std::sin(phase[idx]);
                amp[idx] *= std::exp(-dt * damp_rate / decay_tau[idx]);
                if (amp[idx] > max_a) max_a = amp[idx];
            }
            if (max_a < 1e-5 && age_sec > 0.05) active = false;
        }

        double trans = 0.0;
        if (transient_env > 1e-4) {
            trans = rand_bipolar() * transient_env * 0.21 * std::sin(2.0 * PI * transient_freq * age_sec);
            transient_env *= std::exp(-dt / 0.0033);
        }

        age_sec += dt;
        return (sum * att_env + trans) * 0.22;
    }
};

static inline void write_wav(const char *filename, const std::vector<int16_t> &interleaved_stereo) {
    FILE *f = std::fopen(filename, "wb");
    if (!f) { std::perror("fopen"); std::exit(1); }
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

} // namespace GuitarSynth

#endif // GUITAR_SYNTH_ENGINE_HPP