#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include "OctaveEngine.h"

// order used everywhere (faders, presets)
namespace FogIDs
{
    static const char* const faders[8] = { "dry", "octm2", "octm1", "octp1", "octp2", "attack", "lpf", "detune" };
    static const char* const faderNames[8] = { "DRY OUTPUT", "-2 OCTAVES", "-1 OCTAVE", "+1 OCTAVE", "+2 OCTAVES", "ATTACK", "LP FILTER", "DETUNE" };
    static const char* const dryFx  = "dryfx";
    static const char* const q      = "q";
    static const char* const bypass = "bypass";
}

struct FogPreset
{
    bool  valid = false;
    std::array<float, 8> v { 0.8f, 0.0f, 0.6f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f };
    int dryFx = 0, q = 0;
};

struct FactoryPreset { const char* name; FogPreset p; };
const std::vector<FactoryPreset>& getFactoryPresets();

class FogProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    FogProcessor();
    ~FogProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Fog Z"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---------------- preset system (message thread) ----------------
    void selectPreset (int idx);              // knob turned
    void loadPreset (int idx);                // knob pressed / footswitch
    void savePreset (int idx);                // knob held 3 s
    void presetFootswitch();                  // POG2 logic: load, or next if already loaded
    void toggleBypass();
    void applyFactory (int factoryIndex);     // load a factory sound into the faders
    void restoreFactorySlots();               // slots 1-8 = factory 1-8
    bool isModified() const;
    FogPreset captureCurrent() const;

    int  getSelected() const { return selected.load(); }
    int  getLoaded()   const { return loaded.load(); }
    juce::uint32 getSaveBlinkStart() const { return saveBlinkStart.load(); }
    bool isBypassed() const { return bypassParam->get(); }

    // ---------------- MIDI footswitches ----------------
    std::atomic<int> ccPreset { 80 }, ccBypass { 81 };
    std::atomic<int> learnTarget { 0 };           // 0 none, 1 preset footswitch, 2 bypass footswitch
    std::atomic<bool> latchingPedals { false };    // pedal sends 127 / 0 on alternate presses
    std::atomic<float> uiScale { 0.67f };          // window scale (0.67 = default, never smaller)

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;
    void setParam (const char* id, float plainValue);

    std::array<FogPreset, 8> slots;
    std::atomic<int> selected { 0 }, loaded { -1 };
    std::atomic<juce::uint32> saveBlinkStart { 0 };
    std::atomic<int> pendingPresetPress { 0 }, pendingBypass { 0 }, pendingProgram { -1 }, pendingFactory { -1 }, currentProgram { 0 };
    int lastCcVal[128] {};

    std::array<std::atomic<float>*, 8> raw {};
    std::atomic<float>* rawDryFx = nullptr; std::atomic<float>* rawQ = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    // DSP
    OctaveEngine engine;
    ResonantLP lpf;
    Detuner detUp, detDryL, detDryR;
    juce::AudioBuffer<float> work;           // mono in + 4 voices
    std::array<juce::SmoothedValue<float>, 8> sm;
    juce::SmoothedValue<float> bypassMix;
    float dryEnv = 1.0f, fastEnv = 0, slowEnv = 0, dryHold = 0; bool dryRetrig = false;
    float rmsEnv = 0.0f, peakHold = 0.0f, susGain = 1.0f;
    float stopFast = 0.0f, stopSlow = 0.0f, stopGain = 1.0f;
    double sr = 48000.0;
    int preparedBlock = 512;
    float hpState = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FogProcessor)
};
