#include "kit.h"

#include <esp_heap_caps.h>
#include <math.h>

#include "config.h"
#include "slots.h"

namespace Kit {

static uint32_t g_sr = 44100;
static uint32_t g_bytes = 0;
static uint32_t g_buildMs = 0;

// Every kit, once built. Buffers are never freed: a voice still ringing from the
// previous kit keeps reading its sound after a switch.
struct KitStore {
    bool built;
    const int16_t *data[kSlotCount];
    uint32_t frames[kSlotCount];
    const char *names[kSlotCount];
};
static KitStore g_kits[kCount];
static uint8_t g_building = 0;
static uint8_t g_current = 0;
static bool g_lofi = false;

// Longest sound in the kit is the crash at 900 ms. Scratch is float so the
// filters and the normalisation pass do not accumulate quantisation error
// before the single conversion to int16 at the end.
static const float kScratchSeconds = 1.0f;
static float *g_scratch = nullptr;
static uint32_t g_scratchCap = 0;
static uint32_t g_len = 0;

// ------------------------------------------------------------------ helpers

// Deterministic noise. A fixed seed per sound means the kit is bit-identical on
// every boot, which matters more than it sounds: a hat that is subtly different
// each power cycle is impossible to A/B against a commissioned replacement.
static uint32_t g_rng = 0x13579BDF;

static inline void seedNoise(uint32_t seed) { g_rng = seed; }

static inline float noise() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (float)(int32_t)g_rng * (1.0f / 2147483648.0f);
}

// exp(x) for x <= 0, good to about 0.01%: plenty for an envelope, and several
// times quicker than expf(), which used to be nearly all of a kit's build time.
static inline float fexp(float x) {
    if (x < -80.0f) return 0.0f;
    float y = x * 1.44269504f;               // to a power of two
    float fi = floorf(y);
    float f = y - fi;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union { float f; int32_t i; } u;
    u.f = p;
    u.i += (int32_t)fi << 23;                // times 2^fi
    return u.f;
}

static inline float expEnv(float t, float tau) { return fexp(-t / tau); }

// One-pole coefficient for a given corner frequency.
static inline float onePole(float hz) {
    float x = expf(-2.0f * (float)M_PI * hz / (float)g_sr);
    return 1.0f - x;
}

static void beginSound(float seconds, uint32_t seed) {
    g_len = min((uint32_t)(seconds * g_sr), g_scratchCap);
    memset(g_scratch, 0, g_len * sizeof(float));
    seedNoise(seed);
}

// Peak-normalises and hands the sound to Slots.
//
// The target is a common peak for every sound in every kit, so that switching
// kits never means re-tuning the mixer. 0.95 rather than 1.0 leaves
// a little room for the resampling and filtering downstream without giving away
// most of a bit — headroom for summing is the limiter's job, not this stage's.
static const float kNormalisePeak = 0.95f;

static void lofi();

static bool emit(uint8_t slot, const char *name) {
    if (g_lofi) lofi();
    float peak = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        peak = max(peak, fabsf(g_scratch[i]));
    }
    if (peak < 1e-6f) return false;

    // Trim the tail once it is inaudible rather than storing the whole decay:
    // an exponential never actually reaches zero and the last 40 dB of a crash
    // is a lot of PSRAM holding silence.
    float floorLevel = peak * 0.0015f;  // ~ -56 dB relative to the peak
    uint32_t len = g_len;
    while (len > 32 && fabsf(g_scratch[len - 1]) < floorLevel) {
        len--;
    }

    int16_t *dest = (int16_t *)heap_caps_malloc(len * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
    if (!dest) {
        Serial.printf("  kit: PSRAM allocation for %s FAILED\n", name);
        return false;
    }

    const float scale = kNormalisePeak * 32767.0f / peak;
    // A short fade at the very end so a truncated tail cannot click.
    const uint32_t fade = min<uint32_t>(len / 8, 256);
    for (uint32_t i = 0; i < len; i++) {
        float v = g_scratch[i] * scale;
        if (i >= len - fade) {
            v *= (float)(len - i) / (float)fade;
        }
        dest[i] = (int16_t)constrain(v, -32767.0f, 32767.0f);
    }

    g_kits[g_building].data[slot] = dest;
    g_kits[g_building].frames[slot] = len;
    g_kits[g_building].names[slot] = name;
    g_bytes += len * sizeof(int16_t);
    return true;
}

// -------------------------------------------------------------- the sounds

// A kick is a sine whose pitch falls fast. The pitch envelope is the whole
// sound: hold the frequency constant and you get a hum, not a kick.
static void makeKick(float startHz, float endHz, float pitchTau, float ampTau,
                     float clickAmount) {
    float phase = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float hz = endHz + (startHz - endHz) * expEnv(t, pitchTau);
        phase += 2.0f * (float)M_PI * hz / (float)g_sr;
        float body = sinf(phase) * expEnv(t, ampTau);
        // A few milliseconds of transient so it cuts through on a small
        // speaker, where the fundamental barely reproduces at all.
        float click = noise() * expEnv(t, 0.0015f) * clickAmount;
        g_scratch[i] = body + click;
    }
}

static void makeSnare(float toneHz, float toneTau, float noiseTau,
                      float noiseMix) {
    float p1 = 0.0f, p2 = 0.0f;
    float lp = 0.0f;
    const float a = onePole(3200.0f);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        p1 += 2.0f * (float)M_PI * toneHz / (float)g_sr;
        p2 += 2.0f * (float)M_PI * (toneHz * 1.48f) / (float)g_sr;
        float tone = (sinf(p1) + 0.7f * sinf(p2)) * expEnv(t, toneTau);

        float n = noise();
        lp += a * (n - lp);
        float body = (n - lp * 0.6f) * expEnv(t, noiseTau);

        g_scratch[i] = tone * (1.0f - noiseMix) + body * noiseMix;
    }
}

// Hats and cymbals are high-passed noise. What separates a closed hat from an
// open one is only the decay, which is why they share this.
static void makeMetal(float cornerHz, float tau, float attackTau) {
    float lp = 0.0f;
    const float a = onePole(cornerHz);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float n = noise();
        lp += a * (n - lp);
        float hp = n - lp;
        // A short attack ramp stops the click that an instantaneous noise
        // onset produces, which reads as a fault rather than a cymbal.
        float attack = attackTau > 0.0f ? (1.0f - expEnv(t, attackTau)) : 1.0f;
        g_scratch[i] = hp * expEnv(t, tau) * attack;
    }
}

static void makeClap(uint8_t bursts, float spacingMs, float tailTau) {
    float lpLo = 0.0f, lpHi = 0.0f;
    const float aLo = onePole(2400.0f);
    const float aHi = onePole(700.0f);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;

        // Three or four short bursts in quick succession, then one longer
        // tail. That stagger is what makes it read as several hands.
        float env = 0.0f;
        for (uint8_t b = 0; b < bursts; b++) {
            float bt = t - (float)b * spacingMs * 0.001f;
            if (bt >= 0.0f) {
                env = max(env, expEnv(bt, 0.008f));
            }
        }
        float tailStart = t - (float)(bursts - 1) * spacingMs * 0.001f;
        if (tailStart >= 0.0f) {
            env = max(env, 0.6f * expEnv(tailStart, tailTau));
        }

        float n = noise();
        lpLo += aLo * (n - lpLo);
        lpHi += aHi * (lpLo - lpHi);
        g_scratch[i] = (lpLo - lpHi) * env;  // band-passed around 0.7-2.4 kHz
    }
}

static void makeTom(float hz, float tau) {
    float phase = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float f = hz * (1.0f + 0.25f * expEnv(t, 0.03f));
        phase += 2.0f * (float)M_PI * f / (float)g_sr;
        float e = expEnv(t, tau);
        g_scratch[i] = (sinf(phase) + 0.12f * noise() * expEnv(t, 0.004f)) * e;
    }
}

// Two detuned square waves through a band-pass: the classic 808 cowbell.
static void makeCowbell(float hzA, float hzB, float tau) {
    float pa = 0.0f, pb = 0.0f;
    float lp = 0.0f, hp = 0.0f;
    const float aLp = onePole(3000.0f);
    const float aHp = onePole(500.0f);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        pa += 2.0f * (float)M_PI * hzA / (float)g_sr;
        pb += 2.0f * (float)M_PI * hzB / (float)g_sr;
        float sq = (sinf(pa) > 0.0f ? 1.0f : -1.0f) * 0.5f +
                   (sinf(pb) > 0.0f ? 1.0f : -1.0f) * 0.5f;
        lp += aLp * (sq - lp);
        hp += aHp * (lp - hp);
        g_scratch[i] = (lp - hp) * expEnv(t, tau);
    }
}

static void makeClave(float hz, float tau) {
    float phase = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        phase += 2.0f * (float)M_PI * hz / (float)g_sr;
        g_scratch[i] =
            (sinf(phase) * 0.9f + noise() * 0.2f * expEnv(t, 0.0008f)) *
            expEnv(t, tau);
    }
}

// A sub-bass tone, so a groove can have a bass line without the microphone.
// Fixed pitch: this is a drum machine, not a synthesiser, and 12 slots are
// better spent on 12 distinct sounds than on a chromatic scale.
static void makeBass(float hz, float tau) {
    float p1 = 0.0f, p2 = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        p1 += 2.0f * (float)M_PI * hz / (float)g_sr;
        p2 += 2.0f * (float)M_PI * hz * 2.0f / (float)g_sr;
        g_scratch[i] =
            (sinf(p1) + 0.22f * sinf(p2) * expEnv(t, 0.05f)) * expEnv(t, tau);
    }
}

// A metronome click: a short high sine with a noise tick on the front. The
// accent is a fifth higher and slightly longer, which reads as "bar starts here"
// without needing to be louder — and not being louder matters, because the
// click has to sit under the music rather than fight it.
static void makeClick(float hz, float tau) {
    float phase = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        phase += 2.0f * (float)M_PI * hz / (float)g_sr;
        g_scratch[i] = (sinf(phase) + 0.35f * noise() * expEnv(t, 0.0006f)) *
                       expEnv(t, tau);
    }
}

static int16_t *g_click[2] = {nullptr, nullptr};
static uint32_t g_clickFrames[2] = {0, 0};

// Same normalise-and-store path as emit(), but into standalone blocks rather
// than a slot.
static bool emitClick(uint8_t which) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        peak = max(peak, fabsf(g_scratch[i]));
    }
    if (peak < 1e-6f) return false;

    uint32_t len = g_len;
    int16_t *dest =
        (int16_t *)heap_caps_malloc(len * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!dest) return false;

    const float scale = kNormalisePeak * 32767.0f / peak;
    const uint32_t fade = min<uint32_t>(len / 8, 128);
    for (uint32_t i = 0; i < len; i++) {
        float v = g_scratch[i] * scale;
        if (i >= len - fade) v *= (float)(len - i) / (float)fade;
        dest[i] = (int16_t)constrain(v, -32767.0f, 32767.0f);
    }

    g_click[which] = dest;
    g_clickFrames[which] = len;
    g_bytes += len * sizeof(int16_t);
    return true;
}

const int16_t *clickData(bool accent) { return g_click[accent ? 1 : 0]; }
uint32_t clickFrames(bool accent) { return g_clickFrames[accent ? 1 : 0]; }


// ------------------------------------------------------- more instruments

// A ride: long, washy metal plus two bell partials that ring out over it.
static void makeRide(float cornerHz, float tau) {
    float lp = 0.0f, p1 = 0.0f, p2 = 0.0f;
    const float a = onePole(cornerHz);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float n = noise();
        lp += a * (n - lp);
        p1 += 2.0f * (float)M_PI * 3100.0f / (float)g_sr;
        p2 += 2.0f * (float)M_PI * 4650.0f / (float)g_sr;
        float bell = (sinf(p1) + 0.6f * sinf(p2)) * 0.22f * expEnv(t, tau * 0.7f);
        g_scratch[i] = ((n - lp) * 0.8f + bell) * expEnv(t, tau) * (1.0f - expEnv(t, 0.001f));
    }
}

// A rimshot: the stick hitting the rim — a short knock and a hard click.
static void makeRim(float hz, float tau) {
    float phase = 0.0f, lp = 0.0f;
    const float a = onePole(5000.0f);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        phase += 2.0f * (float)M_PI * hz / (float)g_sr;
        float n = noise();
        lp += a * (n - lp);
        g_scratch[i] = sinf(phase) * expEnv(t, tau) + (n - lp) * 1.4f * expEnv(t, 0.004f);
    }
}

// A shaker: band-passed noise that swells in and dies away, like a shake.
static void makeShaker(float attack, float tau) {
    float lo = 0.0f, hi = 0.0f;
    const float aLo = onePole(7500.0f), aHi = onePole(3000.0f);
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float n = noise();
        lo += aLo * (n - lo);
        hi += aHi * (lo - hi);
        float env = (1.0f - expEnv(t, attack)) * expEnv(t, tau);
        g_scratch[i] = (lo - hi) * env;
    }
}

// A band-pass resonator, for the vocal formants.
struct Res {
    float b0, b2, a1, a2, x1, x2, y1, y2;
};

static void resInit(Res &r, float hz, float bandwidth) {
    float w = 2.0f * (float)M_PI * hz / (float)g_sr;
    float alpha = sinf(w) * bandwidth / (2.0f * hz);
    float a0 = 1.0f + alpha;
    r.b0 = alpha / a0;
    r.b2 = -alpha / a0;
    r.a1 = -2.0f * cosf(w) / a0;
    r.a2 = (1.0f - alpha) / a0;
    r.x1 = r.x2 = r.y1 = r.y2 = 0.0f;
}

static inline float resRun(Res &r, float x) {
    float y = r.b0 * x + r.b2 * r.x2 - r.a1 * r.y1 - r.a2 * r.y2;
    r.x2 = r.x1; r.x1 = x;
    r.y2 = r.y1; r.y1 = y;
    return y;
}

// A sung vowel: a buzzing source (the vocal cords) shaped by three resonances
// (the mouth). The three formant frequencies are what make "ah" sound like "ah"
// and not "ee". A little vibrato and breath stop it sounding like a machine.
// hSeconds of breath at the start turn "eh" into "hey".
static void makeVowel(float f0, float f0End, float F1, float F2, float F3,
                      float sustain, float tau, float hSeconds) {
    Res r1, r2, r3;
    resInit(r1, F1, 80.0f);
    resInit(r2, F2, 100.0f);
    resInit(r3, F3, 140.0f);
    float phase = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        float t = (float)i / (float)g_sr;
        float glide = (float)i / (float)g_len;
        float f = f0 + (f0End - f0) * glide;
        f *= 1.0f + 0.012f * sinf(2.0f * (float)M_PI * 5.5f * t) * min(t / 0.15f, 1.0f);
        phase += f / (float)g_sr;
        phase -= floorf(phase);
        float src = t < hSeconds ? noise() * 0.6f : (2.0f * phase - 1.0f) + 0.04f * noise();
        float env = (1.0f - expEnv(t, 0.012f)) * (t < sustain ? 1.0f : expEnv(t - sustain, tau));
        float y = resRun(r1, src) + 0.6f * resRun(r2, src) + 0.3f * resRun(r3, src);
        g_scratch[i] = y * env;
    }
}

// A simple low-pass over the whole sound, for the softer, mouth-made drums.
static void lowpass(float hz) {
    float a = onePole(hz), y = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        y += a * (g_scratch[i] - y);
        g_scratch[i] = y;
    }
}

// The lo-fi treatment: dull it, drop it to about 11 kHz, crush it to 6 bits,
// and sprinkle some vinyl crackle on top.
static void lofi() {
    float peak = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) peak = max(peak, fabsf(g_scratch[i]));
    if (peak < 1e-6f) return;
    const float a = onePole(3200.0f);
    const float step = peak / 32.0f;
    float l1 = 0.0f, l2 = 0.0f, held = 0.0f, crackle = 0.0f;
    for (uint32_t i = 0; i < g_len; i++) {
        l1 += a * (g_scratch[i] - l1);
        l2 += a * (l1 - l2);
        if ((i & 3) == 0) held = roundf(l2 / step) * step;
        float n = noise();
        if (n > 0.9993f) crackle = peak * 0.18f * (n > 0.9997f ? 1.0f : -1.0f);
        crackle *= 0.86f;
        g_scratch[i] = held + crackle;
    }
}

// ------------------------------------------------------------------- kits

static void build808() {
    beginSound(0.40f, 0xA1B2C3D4); makeKick(115.0f, 46.0f, 0.020f, 0.085f, 0.35f); emit(0, "kick");
    beginSound(0.24f, 0x5EED0001); makeSnare(190.0f, 0.030f, 0.055f, 0.62f);      emit(1, "snare");
    beginSound(0.07f, 0x5EED0002); makeMetal(7000.0f, 0.012f, 0.0004f);           emit(2, "hat");
    beginSound(0.35f, 0x5EED0003); makeMetal(6500.0f, 0.075f, 0.0006f);           emit(3, "open hat");
    beginSound(0.30f, 0x5EED0004); makeClap(4, 9.0f, 0.045f);                     emit(4, "clap");
    beginSound(0.35f, 0x5EED0005); makeTom(110.0f, 0.075f);                       emit(5, "tom lo");
    beginSound(0.30f, 0x5EED0006); makeTom(165.0f, 0.065f);                       emit(6, "tom mid");
    beginSound(0.26f, 0x5EED0007); makeTom(235.0f, 0.055f);                       emit(7, "tom hi");
    beginSound(0.22f, 0x5EED0008); makeCowbell(540.0f, 800.0f, 0.055f);           emit(8, "cowbell");
    beginSound(0.08f, 0x5EED0009); makeClave(2400.0f, 0.012f);                    emit(9, "clave");
    beginSound(0.95f, 0x5EED000A); makeMetal(4200.0f, 0.26f, 0.0025f);            emit(10, "crash");
    beginSound(0.60f, 0x5EED000B); makeBass(55.0f, 0.14f);                        emit(11, "bass");
}

static void buildAcoustic() {
    beginSound(0.50f, 0xACC00000); makeKick(95.0f, 50.0f, 0.010f, 0.16f, 0.9f); lowpass(6000.0f); emit(0, "kick");
    beginSound(0.36f, 0xACC00001); makeSnare(205.0f, 0.025f, 0.13f, 0.82f);    emit(1, "snare");
    beginSound(0.08f, 0xACC00002); makeMetal(8500.0f, 0.018f, 0.0003f);        emit(2, "hat");
    beginSound(0.45f, 0xACC00003); makeMetal(7600.0f, 0.20f, 0.0006f);         emit(3, "open hat");
    beginSound(0.12f, 0xACC00004); makeRim(450.0f, 0.03f);                     emit(4, "rimshot");
    beginSound(0.55f, 0xACC00005); makeTom(100.0f, 0.17f);                     emit(5, "tom lo");
    beginSound(0.50f, 0xACC00006); makeTom(150.0f, 0.15f);                     emit(6, "tom mid");
    beginSound(0.45f, 0xACC00007); makeTom(210.0f, 0.13f);                     emit(7, "tom hi");
    beginSound(0.95f, 0xACC00008); makeRide(5200.0f, 0.45f);                   emit(8, "ride");
    beginSound(0.14f, 0xACC00009); makeShaker(0.02f, 0.05f);                   emit(9, "shaker");
    beginSound(0.95f, 0xACC0000A); makeMetal(3600.0f, 0.42f, 0.003f);          emit(10, "crash");
    beginSound(0.70f, 0xACC0000B); makeTom(72.0f, 0.25f);                      emit(11, "floor tom");
}

static void buildLofi() {
    g_lofi = true;
    beginSound(0.45f, 0x10F10000); makeKick(90.0f, 45.0f, 0.030f, 0.12f, 0.20f); emit(0, "kick");
    beginSound(0.26f, 0x10F10001); makeSnare(180.0f, 0.030f, 0.070f, 0.60f);     emit(1, "snare");
    beginSound(0.08f, 0x10F10002); makeMetal(6000.0f, 0.015f, 0.0006f);          emit(2, "hat");
    beginSound(0.35f, 0x10F10003); makeMetal(6000.0f, 0.090f, 0.0008f);          emit(3, "open hat");
    beginSound(0.30f, 0x10F10004); makeClap(3, 12.0f, 0.06f);                    emit(4, "clap");
    beginSound(0.40f, 0x10F10005); makeTom(95.0f, 0.10f);                        emit(5, "tom lo");
    beginSound(0.35f, 0x10F10006); makeTom(140.0f, 0.09f);                       emit(6, "tom mid");
    beginSound(0.30f, 0x10F10007); makeTom(200.0f, 0.08f);                       emit(7, "tom hi");
    beginSound(0.14f, 0x10F10008); makeRim(520.0f, 0.03f);                       emit(8, "rim");
    beginSound(0.14f, 0x10F10009); makeShaker(0.015f, 0.04f);                    emit(9, "shaker");
    beginSound(0.95f, 0x10F1000A); makeMetal(3800.0f, 0.30f, 0.003f);            emit(10, "crash");
    beginSound(0.60f, 0x10F1000B); makeBass(49.0f, 0.18f);                       emit(11, "bass");
    g_lofi = false;
}

static void buildVocal() {
    beginSound(0.40f, 0x70CA0000); makeKick(150.0f, 48.0f, 0.025f, 0.11f, 0.05f); lowpass(2500.0f); emit(0, "boom");
    beginSound(0.22f, 0x70CA0001); makeSnare(260.0f, 0.012f, 0.07f, 0.92f); lowpass(6000.0f);   emit(1, "psh");
    beginSound(0.07f, 0x70CA0002); makeMetal(6500.0f, 0.018f, 0.0008f);                         emit(2, "ts");
    beginSound(0.30f, 0x70CA0003); makeMetal(5800.0f, 0.13f, 0.004f);                           emit(3, "tss");
    beginSound(0.32f, 0x70CA0004); makeVowel(210.0f, 245.0f, 530.0f, 1840.0f, 2480.0f, 0.16f, 0.06f, 0.05f); emit(4, "hey");
    beginSound(0.55f, 0x70CA0005); makeVowel(196.0f, 196.0f, 730.0f, 1090.0f, 2440.0f, 0.30f, 0.12f, 0.0f);  emit(5, "ah");
    beginSound(0.55f, 0x70CA0006); makeVowel(220.0f, 220.0f, 570.0f, 840.0f, 2410.0f, 0.30f, 0.12f, 0.0f);   emit(6, "oh");
    beginSound(0.55f, 0x70CA0007); makeVowel(262.0f, 262.0f, 270.0f, 2290.0f, 3010.0f, 0.30f, 0.12f, 0.0f);  emit(7, "ee");
    beginSound(0.55f, 0x70CA0008); makeVowel(294.0f, 294.0f, 300.0f, 870.0f, 2240.0f, 0.30f, 0.12f, 0.0f);   emit(8, "oo");
    beginSound(0.16f, 0x70CA0009); makeVowel(150.0f, 140.0f, 640.0f, 1190.0f, 2390.0f, 0.06f, 0.04f, 0.0f);  emit(9, "uh");
    beginSound(0.95f, 0x70CA000A); makeVowel(392.0f, 392.0f, 730.0f, 1090.0f, 2440.0f, 0.50f, 0.20f, 0.0f);  emit(10, "aah");
    beginSound(0.55f, 0x70CA000B); makeVowel(82.0f, 82.0f, 300.0f, 870.0f, 2240.0f, 0.25f, 0.10f, 0.0f);     emit(11, "dum");
}

static const char *kNames[kCount] = {"808", "Acoustic", "Lo-fi", "Vocal"};
static const uint32_t kColours[kCount] = {0xFF6A00, 0xC8843C, 0xB06BFF, 0x00C8C8};

// The synthesis scratch and the kit store are shared, so building is one at a
// time: the background builder and a player switching kits take turns.
static SemaphoreHandle_t g_lock = nullptr;

static bool buildKit(uint8_t k) {
    uint32_t t0 = millis();
    g_building = k;
    switch (k) {
        case 0: build808(); break;
        case 1: buildAcoustic(); break;
        case 2: buildLofi(); break;
        default: buildVocal(); break;
    }
    for (uint8_t s = 0; s < kSlotCount; s++) {
        if (!g_kits[k].data[s]) return false;
    }
    g_kits[k].built = true;
    g_buildMs = millis() - t0;
    Serial.printf("  kit %s built in %u ms\n", kNames[k], (unsigned)g_buildMs);
    return true;
}

bool select(uint8_t k) {
    if (k >= kCount) return false;
    if (!g_kits[k].built) {
        // Normally the background builder has done this long ago; right after
        // power-on it may still be busy, so wait for it rather than race it.
        xSemaphoreTake(g_lock, portMAX_DELAY);
        bool ok = g_kits[k].built || buildKit(k);
        xSemaphoreGive(g_lock);
        if (!ok) return false;
    }
    for (uint8_t s = 0; s < kSlotCount; s++) {
        Slots::setFactory(s, g_kits[k].data[s], g_kits[k].frames[s], g_kits[k].names[s]);
    }
    g_current = k;
    return true;
}

bool begin(uint32_t sampleRate, uint8_t kit) {
    g_sr = sampleRate;
    g_bytes = 0;
    memset(g_kits, 0, sizeof(g_kits));

    // One second of float scratch, kept: later kits are built on demand.
    g_scratchCap = (uint32_t)(kScratchSeconds * g_sr);
    g_scratch = (float *)heap_caps_malloc(g_scratchCap * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!g_scratch) {
        Serial.println("  kit: PSRAM scratch allocation FAILED");
        return false;
    }

    beginSound(0.05f, 0x5EED0100); makeClick(1800.0f, 0.010f); emitClick(0);
    beginSound(0.07f, 0x5EED0101); makeClick(2700.0f, 0.013f); emitClick(1);

    g_lock = xSemaphoreCreateMutex();
    if (!g_lock || !select(kit < kCount ? kit : 0)) return false;

    // Build the other kits in the background, so that switching later is instant
    // instead of freezing the keys for a second or two. It runs on the core the
    // key-scanning loop is not on, at the lowest priority, under the audio tasks.
    xTaskCreatePinnedToCore(
        [](void *) {
            for (uint8_t k = 0; k < kCount; k++) {
                xSemaphoreTake(g_lock, portMAX_DELAY);
                if (!g_kits[k].built) buildKit(k);
                xSemaphoreGive(g_lock);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelete(nullptr);
        },
        "kits", 8192, nullptr, 1, nullptr, 1 - xPortGetCoreID());
    return true;
}

uint8_t current() { return g_current; }
const char *name(uint8_t k) { return k < kCount ? kNames[k] : "?"; }
uint32_t colour(uint8_t k) { return k < kCount ? kColours[k] : 0xFFFFFF; }
uint32_t bytesUsed() { return g_bytes; }
uint32_t buildMillis() { return g_buildMs; }

}  // namespace Kit
