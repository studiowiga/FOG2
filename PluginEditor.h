#pragma once
#include "PluginProcessor.h"

class PedalContent;

class FogEditor : public juce::AudioProcessorEditor
{
public:
    explicit FogEditor (FogProcessor&);
    ~FogEditor() override;
    void resized() override;
    void parentHierarchyChanged() override;
    void setScale (float s);       // 0.67 (default) .. 1.25
    float getScale() const;

private:
    friend class PedalContent;     // son timer réapplique le moteur logiciel (anti-gel Windows)
    void useSoftwareRenderer();
    FogProcessor& proc;
    std::unique_ptr<PedalContent> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FogEditor)
};
