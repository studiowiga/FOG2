#pragma once
#include "PluginProcessor.h"

class PedalContent;

class FogEditor : public juce::AudioProcessorEditor
{
public:
    explicit FogEditor (FogProcessor&);
    ~FogEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    void setScale (float s);       // 0.67 (default) .. 1.25
    float getScale() const;

private:
    friend class PedalContent;     // son timer surveille le moteur de rendu (anti-gel v2)
    void useSoftwareRenderer();    // applique le moteur logiciel (Windows) ; repaint si la fenêtre hôte a changé
    void checkRenderer();          // depuis le timer : seulement si la fenêtre a changé ou au plus 1 fois / s
    void resetRenderer();          // chien de garde : oublie la fenêtre mémorisée et réapplique tout
    void* lastPeer = nullptr;
    double lastRendererCheckMs = 0.0;
    FogProcessor& proc;
    std::unique_ptr<PedalContent> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FogEditor)
};
