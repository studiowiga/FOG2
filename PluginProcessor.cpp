#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
//  Factory sounds  (dry, -2, -1, +1, +2, attack, LP, detune | dryFx, Q)
// ============================================================================
static FogPreset mk (std::array<float, 8> v, int dryFx, int q) { FogPreset p; p.valid = true; p.v = v; p.dryFx = dryFx; p.q = q; return p; }

const std::vector<FactoryPreset>& getFactoryPresets()
{
    //                                  dry   -2    -1    +1    +2   attack  LP  detune | dryFx Q
    static const std::vector<FactoryPreset> list {
        { "Organ Pad",             mk ({ 0.60f, 0.26f, 0.70f, 0.62f, 0.30f, 0.45f, 0.62f, 0.35f }, 3, 0) },
        { "Baritone Growl",        mk ({ 0.35f, 0.30f, 0.85f, 0.00f, 0.00f, 0.00f, 0.55f, 0.00f }, 0, 2) },
        { "Theremin-ish",          mk ({ 0.00f, 0.00f, 0.00f, 0.92f, 0.00f, 0.56f, 0.50f, 0.88f }, 0, 1) },
        { "Cello",                 mk ({ 0.00f, 0.00f, 0.86f, 0.10f, 0.00f, 0.50f, 0.40f, 0.12f }, 1, 1) },
        { "Lower + Upper Clean",   mk ({ 0.80f, 0.00f, 0.62f, 0.58f, 0.00f, 0.00f, 1.00f, 0.00f }, 0, 0) },
        { "Organ Drawbars",        mk ({ 0.70f, 0.56f, 0.70f, 0.66f, 0.52f, 0.00f, 0.80f, 0.28f }, 2, 0) },
        { "Glass Choir",           mk ({ 0.56f, 0.00f, 0.40f, 0.68f, 0.82f, 0.60f, 0.74f, 0.40f }, 1, 1) },
        { "Sub Synth Bass",        mk ({ 0.20f, 0.66f, 0.85f, 0.00f, 0.00f, 0.08f, 0.30f, 0.00f }, 2, 3) },
        { "12-String Shimmer",     mk ({ 0.80f, 0.00f, 0.00f, 0.62f, 0.00f, 0.00f, 1.00f, 0.22f }, 0, 0) },
        { "Reverse Swell",         mk ({ 0.62f, 0.00f, 0.92f, 0.84f, 0.30f, 0.80f, 0.78f, 0.18f }, 1, 0) },
        { "Leslie Rotary",         mk ({ 0.62f, 0.00f, 0.66f, 0.66f, 0.48f, 0.00f, 0.70f, 0.85f }, 3, 0) },
        { "Fog Horn",              mk ({ 0.00f, 0.80f, 0.55f, 0.25f, 0.00f, 0.30f, 0.25f, 0.15f }, 2, 3) },
    };
    return list;
}

// ============================================================================
static float levelGain (float s)      { return s * s * 1.5625f; }          // unity at 80 %
static float attackSeconds (float s)  { return s < 0.02f ? 0.0f : 0.010f * std::pow (300.0f, (s - 0.02f) / 0.98f); }
static float cutoffHz (float s)       { return 50.0f * std::pow (360.0f, s); }
static const float qValues[4] = { 0.707f, 1.5f, 3.2f, 7.5f };
// transparent below -1 dBFS, then a smooth knee: all voices flat out never hard-clip
static inline float softLimit (float x)
{
    const float a = std::abs (x), t = 0.89f;
    if (a <= t) return x;
    const float y = t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t));
    return x < 0 ? -y : y;
}

FogProcessor::FogProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FOG2", createLayout())
{
    for (int i = 0; i < 8; ++i) raw[(size_t) i] = apvts.getRawParameterValue (FogIDs::faders[i]);
    rawDryFx = apvts.getRawParameterValue (FogIDs::dryFx);
    rawQ     = apvts.getRawParameterValue (FogIDs::q);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (FogIDs::bypass));
    restoreFactorySlots();
    loaded = 0;
}

// host preset menu (the drop-down of the plug-in window) = the 12 factory sounds
int FogProcessor::getNumPrograms() { return (int) getFactoryPresets().size(); }
const juce::String FogProcessor::getProgramName (int i)
{
    const auto& l = getFactoryPresets();
    return juce::isPositiveAndBelow (i, (int) l.size()) ? juce::String (i + 1) + ". " + l[(size_t) i].name : juce::String();
}
void FogProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumPrograms()) || i == currentProgram.load()) return;
    currentProgram = i;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) applyFactory (i);
    else { pendingFactory = i; triggerAsyncUpdate(); }
}

juce::AudioProcessorValueTreeState::ParameterLayout FogProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto& f0 = getFactoryPresets()[0].p;      // a fresh pedal = preset 1
    float defs[8]; for (int i = 0; i < 8; ++i) defs[i] = f0.v[(size_t) i];
    const char* names[8] = { "Dry Output", "-2 Octaves", "-1 Octave", "+1 Octave", "+2 Octaves", "Attack", "LP Filter", "Detune" };
    for (int i = 0; i < 8; ++i)
    {
        std::function<String (float, int)> txt;
        if (i == 5)      txt = [] (float v, int) { auto s = attackSeconds (v); return s <= 0 ? String ("Off") : (s < 1 ? String (roundToInt (s * 1000)) + " ms" : String (s, 2) + " s"); };
        else if (i == 6) txt = [] (float v, int) { auto f = cutoffHz (v); return f < 1000 ? String (roundToInt (f)) + " Hz" : String (f / 1000.0f, 1) + " kHz"; };
        else             txt = [] (float v, int) { return String (roundToInt (v * 100)) + " %"; };
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { FogIDs::faders[i], 1 }, names[i],
                                                      NormalisableRange<float> (0.0f, 1.0f), defs[i],
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (txt)));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { FogIDs::dryFx, 1 }, "Dry FX",
                                                   StringArray { "Dry clean", "Dry > Attack", "Dry > Attack + LP", "Dry > Attack + LP + Detune" }, getFactoryPresets()[0].p.dryFx));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { FogIDs::q, 1 }, "Filter Q",
                                                   StringArray { "Q 1 (flat)", "Q 2", "Q 3", "Q 4 (peak)" }, getFactoryPresets()[0].p.q));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { FogIDs::bypass, 1 }, "Bypass", false));
    return l;
}

bool FogProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto& in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FogProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    preparedBlock = juce::jmax (64, samplesPerBlock);
    engine.prepare (sampleRate, preparedBlock);
    lpf.prepare (sampleRate);
    detUp.prepare (sampleRate); detDryL.prepare (sampleRate); detDryR.prepare (sampleRate);
    work.setSize (5, samplesPerBlock, false, true, true);
    for (int i = 0; i < 8; ++i) { sm[(size_t) i].reset (sampleRate, 0.03); sm[(size_t) i].setCurrentAndTargetValue (raw[(size_t) i]->load()); }
    bypassMix.reset (sampleRate, 0.015);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
    dryEnv = 1.0f; fastEnv = slowEnv = 0.0f; dryHold = 0; dryRetrig = false; rmsEnv = peakHold = 0.0f; susGain = 1.0f; stopFast = stopSlow = 0.0f; stopGain = 1.0f; idleLvl = 0.0f; idleFloor = 3.0e-3f; idleGain = 0.0f; idleFreeze = 0; gateOpen = false; gateHold = 0;
}

void FogProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (n == 0) return;
    if (work.getNumSamples() < n) work.setSize (5, n, false, true, true);

    // ---------------- MIDI: footswitch CCs + program change ----------------
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            const int cc = m.getControllerNumber(), val = m.getControllerValue();
            const int lt = learnTarget.load();
            if (lt != 0 && val >= 64)
            {
                if (lt == 1) ccPreset = cc; else ccBypass = cc;
                if (ccPreset.load() == ccBypass.load()) { if (lt == 1) ccBypass = -1; else ccPreset = -1; }
                learnTarget = 0; lastCcVal[cc] = val; triggerAsyncUpdate(); continue;
            }
            const bool wasOn = lastCcVal[cc] >= 64, isOn = val >= 64;
            lastCcVal[cc] = val;
            const bool fire = latchingPedals.load() ? (wasOn != isOn) : (isOn && ! wasOn);
            if (fire && cc == ccPreset.load()) { ++pendingPresetPress; triggerAsyncUpdate(); }
            if (fire && cc == ccBypass.load()) { ++pendingBypass;      triggerAsyncUpdate(); }
        }
        else if (m.isProgramChange() && m.getProgramChangeNumber() < 8)
        {
            pendingProgram = m.getProgramChangeNumber(); triggerAsyncUpdate();
        }
    }

    // ---------------- parameters ----------------
    for (int i = 0; i < 8; ++i) sm[(size_t) i].setTargetValue (raw[(size_t) i]->load());
    const int dryFx = juce::jlimit (0, 3, (int) std::lround (rawDryFx->load()));
    const int qIdx  = juce::jlimit (0, 3, (int) std::lround (rawQ->load()));
    bypassMix.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    const float attSec = attackSeconds (raw[5]->load());
    const float qv = qValues[qIdx];
    const float qComp = 1.0f / (1.0f + 0.11f * (qv - 0.707f));

    // ---------------- mono sum -> octave engine ----------------
    float* mono = work.getWritePointer (0);
    if (numIn >= 2)
    {
        auto* l = buffer.getReadPointer (0); auto* r = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i) mono[i] = 0.5f * (l[i] + r[i]);
    }
    else juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), n);

    float* voices[4] = { work.getWritePointer (1), work.getWritePointer (2), work.getWritePointer (3), work.getWritePointer (4) };
    for (int off = 0; off < n; off += preparedBlock)       // hosts may send bigger blocks than announced
    {
        float* v[4] = { voices[0] + off, voices[1] + off, voices[2] + off, voices[3] + off };
        engine.process (mono + off, juce::jmin (preparedBlock, n - off), attSec, v);
    }

    // dry attack detector constants
    const float fastC = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
    const float slowC = 1.0f - std::exp (-1.0f / (0.120f * (float) sr));
    const float duck  = 1.0f / (0.003f * (float) sr);
    const float rise  = attSec > 0 ? 1.0f / (attSec * (float) sr) : 1.0f;
    const int   hold  = (int) (0.08 * sr);
    const float hpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 16.0f / (float) sr);
    const float rmsC = 1.0f - std::exp (-1.0f / (0.040f * (float) sr));
    const float stopFastC     = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));
    const float stopSlowDecay = std::exp (-1.0f / (0.150f * (float) sr));
    const float stopDnC     = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
    const float stopUpC     = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));
    const float idleC     = 1.0f - std::exp (-1.0f / (0.020f * (float) sr));
    const float idleFallC = 1.0f - std::exp (-1.0f / (0.080f * (float) sr));
    const float idleRise  = std::pow (10.0f, 1.0f / 20.0f / (float) sr);                // floor may rise 1 dB/s
    const float gateUpC   = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
    const float gateDnC   = 1.0f - std::exp (-1.0f / (0.040f * (float) sr));
    const int   gateHoldLen = (int) (0.060 * sr);
    const int   idleFreezeLen = (int) (2.0 * sr);
    const float susC = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
    const float holdDecay = std::pow (10.0f, -4.0f / 20.0f / (float) sr);        // the held level fades 4 dB/s
    const float attNorm = juce::jlimit (0.0f, 1.0f, (raw[5]->load() - 0.02f) / 0.98f);
    const float susMax = 1.0f + 2.0f * attNorm;                                    // up to about +9.5 dB

    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    float lastCut = -1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float gDry = levelGain (sm[0].getNextValue());
        const float gM2  = levelGain (sm[1].getNextValue());
        const float gM1  = levelGain (sm[2].getNextValue());
        const float gP1  = levelGain (sm[3].getNextValue());
        const float gP2  = levelGain (sm[4].getNextValue());
        sm[5].getNextValue();
        const float cut  = sm[6].getNextValue();
        const float det  = sm[7].getNextValue();
        if ((i & 15) == 0 || cut != lastCut) { if (std::abs (cut - lastCut) > 1.0e-4f) lpf.setParams (cutoffHz (cut), qv); lastCut = cut; }

        // ---- dry attack envelope (global onset detector) ----
        const float ax = std::abs (mono[i]);
        fastEnv += (ax - fastEnv) * fastC;
        slowEnv += (ax - slowEnv) * slowC;
        if (attSec > 0.0f && dryFx >= 1)
        {
            if (dryHold > 0) dryHold -= 1.0f;
            else if (fastEnv > 2.2f * slowEnv + 1.0e-4f && fastEnv > 0.004f) { dryRetrig = true; dryHold = (float) hold; }
            if (dryRetrig) { dryEnv -= duck; if (dryEnv <= 0) { dryEnv = 0; dryRetrig = false; } }
            else dryEnv = std::min (1.0f, dryEnv + rise);
        }
        else { dryEnv = 1.0f; dryRetrig = false; }

        // ---- swell sustain: with ATTACK on, the decaying note is held up while it swells in,
        //      so the swell ends loud (like a compressed volume-pedal swell) ----
        float sus = 1.0f;
        if (attSec > 0.0f)
        {
            rmsEnv += (ax * ax - rmsEnv) * rmsC;
            const float lev = std::sqrt (rmsEnv);
            if (fastEnv > 2.2f * slowEnv + 1.0e-4f && fastEnv > 0.004f) peakHold = lev;     // new note
            peakHold = std::max (lev, peakHold * holdDecay);
            const float target = lev > 2.0e-4f ? juce::jlimit (1.0f, susMax, peakHold / lev) : 1.0f;
            susGain += (target - susGain) * susC;
            sus = susGain;
        }
        else susGain = 1.0f;

        // ---- stop follower: when the guitar stops (palm mute, staccato, string damping) the narrow
        //      band filters would keep ringing ~80 ms as pure tones ("beeps"); duck the octaves
        //      as fast as the input drops ----
        stopFast += (ax * ax - stopFast) * stopFastC;                 // 10 ms RMS: no ripple on low notes
        const float fr = std::sqrt (stopFast);
        stopSlow = std::max (fr, stopSlow * stopSlowDecay);           // recent level, falls 150 ms
        const float ratio = fr / (stopSlow + 1.0e-9f);
        const float stopTarget = juce::jlimit (0.0f, 1.0f, (ratio - 0.45f) / 0.35f);        // full at -2 dB, shut at -7 dB
        stopGain += (stopTarget - stopGain) * (stopTarget < stopGain ? stopDnC : stopUpC);
        sus *= stopGain;

        // ---- idle gate: guitar plugged in but not played (hum, buzz, hiss) -> octaves muted.
        //      The idle floor is learnt continuously; the octaves open only 12 dB above it. ----
        idleLvl += (ax * ax - idleLvl) * idleC;
        const float lv = std::sqrt (idleLvl);
        if (fastEnv > 2.2f * slowEnv + 1.0e-4f && fastEnv > 0.002f) idleFreeze = idleFreezeLen;   // a note was picked
        if (lv < idleFloor) idleFloor += (lv - idleFloor) * idleFallC;
        else if (idleFreeze > 0) --idleFreeze;                 // while playing the floor doesn't climb
        else idleFloor *= idleRise;
        const float openThr  = std::max (4.0f * idleFloor, 3.0e-4f);       // +12 dB over the floor, never below -70 dBFS
        const float closeThr = std::max (2.0f * idleFloor, 1.5e-4f);
        if (lv > openThr) { gateOpen = true; gateHold = gateHoldLen; }
        else if (lv < closeThr) { if (gateHold > 0) --gateHold; else gateOpen = false; }
        idleGain += ((gateOpen ? 1.0f : 0.0f) - idleGain) * (gateOpen ? gateUpC : gateDnC);
        sus *= idleGain;

        float low = (gM2 * voices[0][i] + gM1 * voices[1][i]) * sus;
        hpState += (low - hpState) * hpCoef; low -= hpState;          // 16 Hz rumble filter on the sub octaves
        const float up  = (gP1 * voices[2][i] + gP2 * voices[3][i]) * sus;
        float upL, upR; detUp.process (up, det, upL, upR);

        float dL = inL[i] * gDry, dR = inR[i] * gDry;
        if (dryFx >= 1) { const float sw = dryEnv * std::sqrt (dryEnv) * sus; dL *= sw; dR *= sw; }   // same swell curve as the octaves
        if (dryFx == 3) { float t; detDryL.process (dL, det, dL, t); detDryR.process (dR, det, t, dR); }

        const bool dryThroughLp = dryFx >= 2;
        if (outR == nullptr) { upL = 0.5f * (upL + upR); dL = 0.5f * (dL + dR); }
        const float yL = lpf.process (0, (low + upL) * qComp + (dryThroughLp ? dL * qComp : 0.0f)) + (dryThroughLp ? 0.0f : dL);
        const float yR = outR != nullptr
                           ? lpf.process (1, (low + upR) * qComp + (dryThroughLp ? dR * qComp : 0.0f)) + (dryThroughLp ? 0.0f : dR)
                           : yL;

        const float b = bypassMix.getNextValue();
        const float xl = inL[i], xr = inR[i];
        outL[i] = softLimit (yL) + (xl - softLimit (yL)) * b;
        if (outR != nullptr) outR[i] = softLimit (yR) + (xr - softLimit (yR)) * b;
    }
}

// ============================================================================
//  presets
// ============================================================================
void FogProcessor::setParam (const char* id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

FogPreset FogProcessor::captureCurrent() const
{
    FogPreset p; p.valid = true;
    for (int i = 0; i < 8; ++i) p.v[(size_t) i] = raw[(size_t) i]->load();
    p.dryFx = (int) std::lround (rawDryFx->load());
    p.q     = (int) std::lround (rawQ->load());
    return p;
}

static void applyToParams (FogProcessor& pr, const FogPreset& p, std::function<void (const char*, float)> set)
{
    juce::ignoreUnused (pr);
    for (int i = 0; i < 8; ++i) set (FogIDs::faders[i], p.v[(size_t) i]);
    set (FogIDs::dryFx, (float) p.dryFx);
    set (FogIDs::q, (float) p.q);
}

void FogProcessor::selectPreset (int idx) { selected = juce::jlimit (0, 7, idx); }

void FogProcessor::loadPreset (int idx)
{
    idx = juce::jlimit (0, 7, idx);
    selected = idx;
    const auto& p = slots[(size_t) idx];
    if (! p.valid) { loaded = -1; return; }
    applyToParams (*this, p, [this] (const char* id, float v) { setParam (id, v); });
    loaded = idx;
}

void FogProcessor::savePreset (int idx)
{
    idx = juce::jlimit (0, 7, idx);
    slots[(size_t) idx] = captureCurrent();
    selected = idx; loaded = idx;
    saveBlinkStart = juce::Time::getMillisecondCounter();
}

void FogProcessor::presetFootswitch()
{
    const int l = loaded.load(), s = selected.load();
    if (l < 0 || l != s)        loadPreset (s);           // load the selected number
    else if (isModified())       loadPreset (l);           // edited -> reload it
    else                         loadPreset ((l + 1) % 8); // already loaded -> next one
}

void FogProcessor::toggleBypass()
{
    bypassParam->beginChangeGesture();
    bypassParam->setValueNotifyingHost (bypassParam->get() ? 0.0f : 1.0f);
    bypassParam->endChangeGesture();
}

void FogProcessor::applyFactory (int fi)
{
    const auto& list = getFactoryPresets();
    if (fi < 0 || fi >= (int) list.size()) return;
    applyToParams (*this, list[(size_t) fi].p, [this] (const char* id, float v) { setParam (id, v); });
    loaded = -1;
}

void FogProcessor::restoreFactorySlots()
{
    const auto& list = getFactoryPresets();
    for (int i = 0; i < 8; ++i) slots[(size_t) i] = list[(size_t) i].p;
}

bool FogProcessor::isModified() const
{
    const int l = loaded.load();
    if (l < 0) return false;
    const auto cur = captureCurrent();
    const auto& p = slots[(size_t) l];
    for (int i = 0; i < 8; ++i) if (std::abs (cur.v[(size_t) i] - p.v[(size_t) i]) > 0.006f) return true;
    return cur.dryFx != p.dryFx || cur.q != p.q;
}

void FogProcessor::handleAsyncUpdate()
{
    const int pf = pendingFactory.exchange (-1);
    if (pf >= 0) applyFactory (pf);
    const int pg = pendingProgram.exchange (-1);
    if (pg >= 0) loadPreset (pg);
    for (int k = pendingPresetPress.exchange (0); k > 0; --k) presetFootswitch();
    for (int k = pendingBypass.exchange (0); k > 0; --k) toggleBypass();
}

// ============================================================================
//  state
// ============================================================================
void FogProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree ps ("Presets");
    for (int i = 0; i < 8; ++i)
    {
        const auto& p = slots[(size_t) i];
        juce::ValueTree t ("Slot");
        t.setProperty ("valid", p.valid, nullptr);
        for (int k = 0; k < 8; ++k) t.setProperty (FogIDs::faders[k], p.v[(size_t) k], nullptr);
        t.setProperty ("dryfx", p.dryFx, nullptr);
        t.setProperty ("q", p.q, nullptr);
        ps.appendChild (t, nullptr);
    }
    state.removeChild (state.getChildWithName ("Presets"), nullptr);
    state.appendChild (ps, nullptr);
    state.setProperty ("selected", selected.load(), nullptr);
    state.setProperty ("loaded", loaded.load(), nullptr);
    state.setProperty ("uiScale2", uiScale.load(), nullptr);
    state.setProperty ("ccPreset", ccPreset.load(), nullptr);
    state.setProperty ("ccBypass", ccBypass.load(), nullptr);
    state.setProperty ("latching", latchingPedals.load(), nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FogProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;

    auto ps = state.getChildWithName ("Presets");
    if (ps.isValid())
    {
        for (int i = 0; i < 8 && i < ps.getNumChildren(); ++i)
        {
            auto t = ps.getChild (i);
            FogPreset p;
            p.valid = (bool) t.getProperty ("valid", false);
            for (int k = 0; k < 8; ++k) p.v[(size_t) k] = (float) t.getProperty (FogIDs::faders[k], p.v[(size_t) k]);
            p.dryFx = (int) t.getProperty ("dryfx", 0);
            p.q     = (int) t.getProperty ("q", 0);
            slots[(size_t) i] = p;
        }
        state.removeChild (ps, nullptr);
    }
    selected   = juce::jlimit (0, 7, (int) state.getProperty ("selected", 0));
    loaded     = juce::jlimit (-1, 7, (int) state.getProperty ("loaded", -1));
    uiScale    = juce::jlimit (0.67f, 1.25f, (float) state.getProperty ("uiScale2", 0.67f));
    ccPreset   = (int) state.getProperty ("ccPreset", 80);
    ccBypass   = (int) state.getProperty ("ccBypass", 81);
    latchingPedals = (bool) state.getProperty ("latching", false);
    currentProgram = (int) state.getProperty ("program", 0);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* FogProcessor::createEditor() { return new FogEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FogProcessor(); }
