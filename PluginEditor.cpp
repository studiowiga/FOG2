#include "PluginEditor.h"
#include "BinaryData.h"

// =============================================================================
//  Geometry - every coordinate is in background.png pixels (1240 x 928)
// =============================================================================
namespace
{
    constexpr int   kW = 1240, kH = 928;              // background.png
    // Window sizes follow Ring Think: percentages of a 993 x 743 pedal, 67 % = default = minimum
    constexpr float kBaseW = 993.0f, kBaseH = 743.0f;
    constexpr float kDefaultScale = 0.67f;
    constexpr float kMaxScale     = 1.25f;
    const float     kMenuScales[4] = { 0.67f, 0.75f, 1.0f, 1.25f };

    const float slotX[8]  = { 175.9f, 275.6f, 375.4f, 474.4f, 574.9f, 772.3f, 869.3f, 968.4f };
    constexpr float travelTop = 193.0f, travelBot = 448.0f;
    constexpr float faderW = 46.0f, faderY0 = 150.0f, faderY1 = 492.0f;
    constexpr float capW = 30.3f, capH = 60.7f;

    const juce::Point<float> dryLed   { 675.6f, 153.3f },  dryBtn { 675.2f, 250.7f };
    const juce::Point<float> qLed     { 675.6f, 353.8f },  qBtn   { 675.2f, 452.5f };
    constexpr float btnImg = 67.4f, btnR = 32.0f;
    const juce::Point<float> knobC    { 1094.8f, 144.9f };
    constexpr float knobR = 66.0f;
    constexpr float presetLedX = 1091.3f, presetLedY0 = 245.1f, presetLedDY = 38.94f;
    const juce::Point<float> redPreset { 469.2f, 765.2f }, redBypass { 780.3f, 764.9f };
    const juce::Point<float> fsPreset  { 270.9f, 775.7f }, fsBypass  { 982.6f, 782.4f };
    constexpr float fsImg = 73.5f;

    const juce::Colour cream   (0xfff1e9dc);
    const juce::Colour bubbleBg(0xe0201010);

    juce::Image img (const void* d, int s) { return juce::ImageCache::getFromMemory (d, s); }

    juce::String faderText (int i, float v)
    {
        if (i == 5)
        {
            if (v < 0.02f) return "OFF";
            const float s = 0.010f * std::pow (300.0f, (v - 0.02f) / 0.98f);
            return s < 1.0f ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, 2) + " s";
        }
        if (i == 6)
        {
            const float f = 50.0f * std::pow (360.0f, v);
            return f < 1000 ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0f, 1) + " kHz";
        }
        return juce::String (juce::roundToInt (v * 100)) + " %";
    }
}

class PedalContent;
static void showMainMenu (PedalContent&, juce::Component* from);

// =============================================================================
//  Fader: slot + white plastic cap, the cap follows the mouse 1:1
// =============================================================================
struct FaderLnF : public juce::LookAndFeel_V4
{
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        juce::Slider::SliderLayout l;
        l.sliderBounds = juce::Rectangle<int> (0, juce::roundToInt (travelTop - faderY0), s.getWidth(),
                                               juce::roundToInt (travelBot - travelTop));
        return l;
    }
    void drawLinearSlider (juce::Graphics& g, int, int, int w, int, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        if (! cap.isValid()) cap = img (BinaryData::fader_cap_png, BinaryData::fader_cap_pngSize);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (cap, juce::Rectangle<float> (capW, capH).withCentre ({ (float) w * 0.5f, pos }));
    }
    juce::Image cap;
};

class Fader : public juce::Slider
{
public:
    Fader (int i, PedalContent& o) : index (i), owner (o)
    {
        setSliderStyle (LinearVertical);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setSliderSnapsToMousePosition (false);
        setScrollWheelEnabled (true);
        setPaintingIsUnclipped (true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { popup = true; showMainMenu (owner, this); return; }
        popup = false; Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! popup) Slider::mouseDrag (e); }
    void mouseUp   (const juce::MouseEvent& e) override { if (! popup) Slider::mouseUp (e); popup = false; }
    const int index;
private:
    PedalContent& owner; bool popup = false;
};

// =============================================================================
//  Round white push buttons (DRY FX / Q)
// =============================================================================
class PlasticButton : public juce::Component
{
public:
    PlasticButton (PedalContent& o) : owner (o) {}
    std::function<void()> onPress;
    void paint (juce::Graphics& g) override
    {
        if (! face.isValid()) face = img (BinaryData::button_up_png, BinaryData::button_up_pngSize);
        auto c = getLocalBounds().toFloat().getCentre();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const float d = pressed ? btnImg * 0.93f : btnImg;
        auto r = juce::Rectangle<float> (d, d).withCentre (c.translated (0.0f, pressed ? 1.2f : 0.0f));
        g.drawImage (face, r);
        if (pressed) { g.setColour (juce::Colours::black.withAlpha (0.16f)); g.fillEllipse (r.reduced (btnImg * 0.04f)); }
    }
    juce::Image face;
    bool hitTest (int x, int y) override { return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < btnR + 2; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMainMenu (owner, this); return; }
        pressed = true; repaint();
        if (onPress) onPress();
    }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }
private:
    PedalContent& owner; bool pressed = false;
};

// =============================================================================
//  Preset knob: turn = choose 1-8, click = load, hold 3 s = save
// =============================================================================
class PresetKnob : public juce::Component, private juce::Timer
{
public:
    PresetKnob (FogProcessor& p, PedalContent& o) : proc (p), owner (o)
    {
        angle = targetAngle (proc.getSelected());
        startTimerHz (60);
    }
    static float targetAngle (int sel) { return juce::degreesToRadians (-140.0f + 40.0f * (float) sel); }
    bool isHoldBlinking() const { return down && ! dragged && holdMs() >= 2000 && ! saved; }
    bool isActive() const { return down || hover; }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto c = getLocalBounds().toFloat().getCentre();
        const float r = knobR;
        // cast shadow (light from the top-left, like the picture)
        for (int i = 7; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.05f));
            g.fillEllipse (c.x - r + 5.0f - (float) i, c.y - r + 8.0f - (float) i, (r + (float) i) * 2, (r + (float) i) * 2);
        }
        // translucent outer collar
        ColourGradient collar (Colour (0xfff2eee8), c.x - r, c.y - r, Colour (0xffa59d93), c.x + r, c.y + r, false);
        g.setGradientFill (collar); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        // knurled ring (40 even teeth) : everything turns with the knob,
        // the shading of each tooth stays tied to the light
        const float r0 = r - 12.0f, r1 = r - 1.8f;
        const int teeth = 40;
        for (int k = 0; k < teeth; ++k)
        {
            const float a  = angle + MathConstants<float>::twoPi * (float) k / (float) teeth;
            const float lit = 0.5f + 0.5f * std::cos (a + degreesToRadians (45.0f));
            const bool detent = false;          // uniform knurling: the 8 detents are in the movement only
            Path tooth;
            const float hw = detent ? 0.040f : 0.055f;
            tooth.startNewSubPath (c.x + std::sin (a - hw) * r0, c.y - std::cos (a - hw) * r0);
            tooth.lineTo (c.x + std::sin (a - hw) * r1, c.y - std::cos (a - hw) * r1);
            tooth.lineTo (c.x + std::sin (a + hw) * r1, c.y - std::cos (a + hw) * r1);
            tooth.lineTo (c.x + std::sin (a + hw) * r0, c.y - std::cos (a + hw) * r0);
            tooth.closeSubPath();
            g.setColour (detent ? Colour (0xff4f4740).withAlpha (0.55f + 0.25f * (1.0f - lit))
                                : Colour (0xfffbf8f3).withAlpha (0.25f + 0.55f * lit));
            g.fillPath (tooth);
            if (! detent) { g.setColour (Colour (0xff6f675d).withAlpha (0.18f + 0.25f * (1.0f - lit))); g.strokePath (tooth, PathStrokeType (0.6f)); }
        }
        // top face
        const float tr = r0 - 1.0f;
        ColourGradient top (Colour (0xffffffff), c.x - tr * 0.45f, c.y - tr * 0.55f, Colour (0xffcdc6bb), c.x + tr, c.y + tr, true);
        top.addColour (0.55, Colour (0xfff2eee7));
        g.setGradientFill (top); g.fillEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2);
        g.setColour (Colour (0xff8f877c).withAlpha (0.55f)); g.drawEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2, 1.0f);
        // engraved pointer groove
        const float sa = std::sin (angle), ca = -std::cos (angle);
        const float p0 = 8.0f, p1 = tr - 8.0f;
        g.setColour (Colour (0xff5d564d).withAlpha (0.8f));
        g.drawLine (c.x + sa * p0, c.y + ca * p0, c.x + sa * p1, c.y + ca * p1, 3.4f);
        g.setColour (Colours::white.withAlpha (0.95f));
        g.drawLine (c.x + sa * p0 + 1.3f, c.y + ca * p0 + 1.3f, c.x + sa * p1 + 1.3f, c.y + ca * p1 + 1.3f, 1.1f);
        g.setColour (Colours::white.withAlpha (0.5f));
        g.fillEllipse (c.x - tr * 0.62f, c.y - tr * 0.70f, tr * 0.62f, tr * 0.36f);
    }
    bool hitTest (int x, int y) override { return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < knobR; }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMainMenu (owner, this); popup = true; return; }
        popup = false; down = true; dragged = false; saved = false;
        downTime = juce::Time::getMillisecondCounter();
        startSel = proc.getSelected();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (popup || ! down) return;
        const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
        if (! dragged && std::abs (d) > 4.0f) dragged = true;
        if (! dragged) return;
        const float steps = d / 28.0f;
        const int  s = (int) std::floor (steps);
        const int sel = ((startSel + s) % 8 + 8) % 8;
        if (sel != proc.getSelected()) proc.selectPreset (sel);
        dragFrac = steps - (float) s;
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (popup) { popup = false; return; }
        if (down && ! dragged && ! saved && holdMs() < 2000) proc.loadPreset (proc.getSelected());
        down = false; dragged = false; dragFrac = 0;
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        wheelAcc += (w.deltaY != 0 ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        if (std::abs (wheelAcc) >= 0.12f)
        {
            const int dir = wheelAcc > 0 ? 1 : -1; wheelAcc = 0;
            proc.selectPreset (((proc.getSelected() + dir) % 8 + 8) % 8);
        }
    }
private:
    juce::uint32 holdMs() const { return juce::Time::getMillisecondCounter() - downTime; }
    void timerCallback() override
    {
        if (down && ! dragged && ! saved && holdMs() >= 3000) { proc.savePreset (proc.getSelected()); saved = true; }
        // continuous angle between detents while dragging, eased otherwise
        float target = targetAngle (proc.getSelected());
        if (down && dragged) target += juce::degreesToRadians (7.0f) * std::sin (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, dragFrac)) * (dragFrac < 0.5f ? 1.0f : -1.0f);
        // wrap from 8 to 1 the short way
        const float diff = target - angle;
        if (std::abs (diff) > 0.0005f) { angle += diff * (down && dragged ? 0.55f : 0.35f); repaint(); }
    }
    FogProcessor& proc; PedalContent& owner;
    float angle = 0, dragFrac = 0, wheelAcc = 0;
    bool down = false, dragged = false, saved = false, popup = false, hover = false;
    int startSel = 0; juce::uint32 downTime = 0;
};

// =============================================================================
//  Footswitch (metal cap, pressed image)
// =============================================================================
class Footswitch : public juce::Component
{
public:
    Footswitch (int w, PedalContent& o) : which (w), owner (o)
    {
        up = w == 1 ? img (BinaryData::fs_preset_up_png, BinaryData::fs_preset_up_pngSize)
                    : img (BinaryData::fs_bypass_up_png, BinaryData::fs_bypass_up_pngSize);
    }
    std::function<void()> onPress;
    std::function<void (juce::Component*)> onMenu;
    void paint (juce::Graphics& g) override
    {
        auto c = getLocalBounds().toFloat().getCentre();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const float d = pressed ? fsImg * 0.95f : fsImg;
        auto r = juce::Rectangle<float> (d, d).withCentre (c.translated (0.0f, pressed ? 1.2f : 0.0f));
        g.drawImage (up, r);
        if (pressed) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillEllipse (r.reduced (fsImg * 0.03f)); }
    }
    bool hitTest (int x, int y) override { return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < 40.0f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu (this); return; }
        pressed = true; repaint(); if (onPress) onPress();
    }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }
    const int which;
private:
    PedalContent& owner; juce::Image up; bool pressed = false;
};

// =============================================================================
//  The pedal (drawn at design size, scaled by the editor)
// =============================================================================
class PedalContent : public juce::Component, private juce::Timer
{
public:
    PedalContent (FogProcessor& p, FogEditor& e) : proc (p), editor (e)
    {
        bg       = img (BinaryData::background_png,   BinaryData::background_pngSize);
        setSize (kW, kH);
        setOpaque (true);

        for (int i = 0; i < 8; ++i)
        {
            auto* f = faders.add (new Fader (i, *this));
            f->setLookAndFeel (&lnf);
            f->setBounds (juce::roundToInt (slotX[i] - faderW * 0.5f), (int) faderY0, (int) faderW, (int) (faderY1 - faderY0));
            addAndMakeVisible (f);
            atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, FogIDs::faders[i], *f));
            f->setDoubleClickReturnValue (true, proc.apvts.getParameter (FogIDs::faders[i])->getDefaultValue());
            f->onValueChange = [this, i] { showValue (i); };
            f->onDragStart   = [this, i] { activeFader = i; dragging = true; showValue (i); };
            f->onDragEnd     = [this]    { dragging = false; bubbleUntil = juce::Time::getMillisecondCounter() + 900; };
        }
        auto cycle = [this] (const char* id, const juce::String& label, const juce::StringArray& names)
        {
            auto* prm = proc.apvts.getParameter (id);
            const int cur = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
            const int nxt = (cur + 1) % 4;
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) nxt)); prm->endChangeGesture();
            flash (label + ": " + names[nxt]);
        };
        dryBtnC = std::make_unique<PlasticButton> (*this);
        dryBtnC->onPress = [cycle] { cycle (FogIDs::dryFx, "DRY FX", { "DRY CLEAN", "ATTACK", "ATTACK + LP", "ATTACK + LP + DETUNE" }); };
        qBtnC = std::make_unique<PlasticButton> (*this);
        qBtnC->onPress = [cycle] { cycle (FogIDs::q, "FILTER Q", { "1 (FLAT)", "2", "3", "4 (PEAK)" }); };
        for (auto [b, c] : { std::pair<PlasticButton*, juce::Point<float>> { dryBtnC.get(), dryBtn }, { qBtnC.get(), qBtn } })
        {
            b->setBounds (juce::Rectangle<int> (76, 76).withCentre (c.toInt()));
            addAndMakeVisible (b);
        }
        knob = std::make_unique<PresetKnob> (proc, *this);
        knob->setBounds (juce::Rectangle<int> (160, 160).withCentre (knobC.toInt()));
        addAndMakeVisible (*knob);

        fsPre = std::make_unique<Footswitch> (1, *this);
        fsByp = std::make_unique<Footswitch> (2, *this);
        fsPre->onPress = [this] { proc.presetFootswitch(); };
        fsByp->onPress = [this] { proc.toggleBypass(); };
        fsPre->onMenu = [this] (juce::Component* c) { showFootswitchMenu (1, c); };
        fsByp->onMenu = [this] (juce::Component* c) { showFootswitchMenu (2, c); };
        fsPre->setBounds (juce::Rectangle<int> (90, 90).withCentre (fsPreset.toInt()));
        fsByp->setBounds (juce::Rectangle<int> (90, 90).withCentre (fsBypass.toInt()));
        addAndMakeVisible (*fsPre); addAndMakeVisible (*fsByp);

        startTimerHz (30);
    }
    ~PedalContent() override
    {
        stopTimer();
        atts.clear();
        for (auto* f : faders) f->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageAt (bg, 0, 0);
        drawLeds (g);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto now = juce::Time::getMillisecondCounter();
        juce::String text; juce::Point<float> at;
        if (activeFader >= 0 && (dragging || hoverFader == activeFader || now < bubbleUntil))
        {
            auto* f = faders[activeFader];
            const float v = (float) f->getValue();
            text = juce::String (FogIDs::faderNames[activeFader]) + "  " + faderText (activeFader, v);
            const float y = faderY0 + (float) f->getPositionOfValue (v);
            at = { slotX[activeFader], y - 40.0f };
        }
        else if (knob->isActive() || now < knobBubbleUntil)
        {
            text = "PRESET " + juce::String (proc.getSelected() + 1)
                 + (knob->isHoldBlinking() ? "  - SAVING..." : (proc.getLoaded() == proc.getSelected() ? "  (LOADED)" : "  - CLICK TO LOAD"));
            at = { 1000.0f, 575.0f };
        }
        else if (now < flashUntil) { text = flashText; at = { 675.0f, 545.0f }; }
        if (text.isEmpty()) { lastBubble = {}; return; }

        juce::Font f (juce::FontOptions (17.0f, juce::Font::bold));
        g.setFont (f);
        const float w = juce::GlyphArrangement::getStringWidth (f, text) + 22.0f;
        auto r = juce::Rectangle<float> (w, 28.0f).withCentre (at);
        r = r.withX (juce::jlimit (6.0f, (float) kW - w - 6.0f, r.getX()));
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (r.translated (1.5f, 2.5f), 7.0f);
        g.setColour (bubbleBg); g.fillRoundedRectangle (r, 7.0f);
        g.setColour (cream.withAlpha (0.55f)); g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (cream); g.drawText (text, r, juce::Justification::centred);
        lastBubble = r.expanded (4.0f).toNearestInt();
    }

    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) showMainMenu (*this, this); }
    void mouseMove (const juce::MouseEvent&) override {}

    void flash (const juce::String& t) { flashText = t; flashUntil = juce::Time::getMillisecondCounter() + 1400; repaint(); }
    void showValue (int i) { activeFader = i; repaint(); }
    void setHoverFader (int i) { hoverFader = i; if (i >= 0) activeFader = i; repaint(); }

    FogProcessor& proc;
    FogEditor& editor;

    void showFootswitchMenu (int which, juce::Component* from);

private:
    // ---------------------------------------------------------------- LEDs
    // lights a LED of the picture (unlit = the picture itself)
    static void lightLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float level)
    {
        using namespace juce;
        if (level <= 0.01f) return;
        for (int i = 0; i < 7; ++i)
        {
            const float gr = r + 2.0f + (float) i * 3.6f;
            g.setColour (col.withAlpha (0.11f * level * (1.0f - (float) i / 7.0f)));
            g.fillEllipse (c.x - gr, c.y - gr, gr * 2, gr * 2);
        }
        ColourGradient dome (col.brighter (0.9f).withAlpha (0.95f * level), c.x - r * 0.2f, c.y - r * 0.25f,
                             col.darker (0.4f).withAlpha (0.9f * level), c.x + r, c.y + r, true);
        g.setGradientFill (dome); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (Colours::white.withAlpha (0.65f * level)); g.fillEllipse (c.x - r * 0.35f, c.y - r * 0.38f, r * 0.7f, r * 0.62f);
        g.setColour (Colours::white.withAlpha (0.7f)); g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.72f, r * 0.48f, r * 0.30f);
    }

    void drawLeds (juce::Graphics& g)
    {
        using namespace juce;
        const auto now = Time::getMillisecondCounter();
        const int df = st.dryFx;
        const Colour dfCol = df == 1 ? Colour (0xffff2a1a) : df == 2 ? Colour (0xff2cff4a) : Colour (0xffffa514);
        lightLed (g, dryLed, 12.5f, dfCol, df == 0 ? 0.0f : 1.0f);
        const float ql[4] = { 0.0f, 0.35f, 0.66f, 1.0f };
        lightLed (g, qLed, 12.0f, Colour (0xffffa20c), ql[st.q]);

        const bool holdBlink = knob->isHoldBlinking();
        const bool saveBlink = now - proc.getSaveBlinkStart() < 450;
        const bool blinkOn = ((now / 125) % 2) == 0;
        for (int i = 0; i < 8; ++i)
        {
            bool on = (i == st.selected);
            if (holdBlink || saveBlink) on = blinkOn;
            if (on) lightLed (g, { presetLedX, presetLedY0 + presetLedDY * (float) i }, 10.5f, Colour (0xffffae1a), 1.0f);
        }
        bool pre = st.loaded >= 0 && st.loaded == st.selected;
        if (pre && st.modified) pre = ((now / 90) % 2) == 0;
        if (pre)          lightLed (g, redPreset, 13.5f, Colour (0xffff2414), 1.0f);
        if (! st.bypassed) lightLed (g, redBypass, 13.5f, Colour (0xffff2414), 1.0f);
    }

    struct State
    {
        int dryFx = 0, q = 0, selected = 0, loaded = -1; bool modified = false, bypassed = false, blink = false;
        bool operator!= (const State& o) const
        { return dryFx != o.dryFx || q != o.q || selected != o.selected || loaded != o.loaded || modified != o.modified || bypassed != o.bypassed || blink != o.blink; }
    };

    void timerCallback() override
    {
        State s;
        s.dryFx = roundToIntSafe (proc.apvts.getRawParameterValue (FogIDs::dryFx)->load());
        s.q     = roundToIntSafe (proc.apvts.getRawParameterValue (FogIDs::q)->load());
        s.selected = proc.getSelected(); s.loaded = proc.getLoaded();
        s.modified = proc.isModified(); s.bypassed = proc.isBypassed();
        const auto now = juce::Time::getMillisecondCounter();
        const bool anyBlink = knob->isHoldBlinking() || now - proc.getSaveBlinkStart() < 500 || (s.modified && s.loaded == s.selected);
        s.blink = anyBlink ? (((now / 90) % 2) == 0) : false;
        if (s != st || anyBlink)
        {
            if (s.selected != st.selected) knobBubbleUntil = now + 1200;
            st = s;
            repaint (juce::Rectangle<float> (presetLedX - 24, presetLedY0 - 24, 48, presetLedDY * 8 + 30).toNearestInt());
            repaint (juce::Rectangle<int> (60, 60).withCentre (dryLed.toInt()));
            repaint (juce::Rectangle<int> (60, 60).withCentre (qLed.toInt()));
            repaint (juce::Rectangle<int> (56, 56).withCentre (redPreset.toInt()));
            repaint (juce::Rectangle<int> (56, 56).withCentre (redBypass.toInt()));
        }
        // hover tracking for the value bubble
        int hv = -1;
        for (auto* f : faders) if (f->isMouseOverOrDragging()) hv = f->index;
        const bool bubbleWanted = hv >= 0 || dragging || knob->isActive() || now < bubbleUntil || now < knobBubbleUntil || now < flashUntil;
        if (hv != hoverFader) { hoverFader = hv; if (hv >= 0) activeFader = hv; }
        if (bubbleWanted || hadBubble)
        {
            if (! lastBubble.isEmpty()) repaint (lastBubble);
            repaint (juce::Rectangle<int> (0, 100, kW, 460));       // fader / knob zone
            repaint (juce::Rectangle<int> (420, 480, 500, 80));
        }
        hadBubble = bubbleWanted;
    }
    static int roundToIntSafe (float v) { return juce::jlimit (0, 3, juce::roundToInt (v)); }

    juce::Image bg;
    FaderLnF lnf;
    juce::OwnedArray<Fader> faders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
    std::unique_ptr<PlasticButton> dryBtnC, qBtnC;
    std::unique_ptr<PresetKnob> knob;
    std::unique_ptr<Footswitch> fsPre, fsByp;
    State st;
    int activeFader = -1, hoverFader = -1; bool dragging = false, hadBubble = false;
    juce::uint32 bubbleUntil = 0, knobBubbleUntil = 0, flashUntil = 0;
    juce::String flashText;
    juce::Rectangle<int> lastBubble;
};

// =============================================================================
//  Menus
// =============================================================================
static void showMainMenu (PedalContent& pc, juce::Component* from)
{
    using namespace juce;
    auto& proc = pc.proc;
    auto* ed = &pc.editor;
    PopupMenu m;
    m.addSectionHeader ("Fog Z  -  Studio Wiga");

    PopupMenu size;
    const float cur = ed->getScale();
    for (int i = 0; i < 4; ++i)
        size.addItem (100 + i, String (roundToInt (kMenuScales[i] * 100)) + " %" + (i == 0 ? " (default)" : ""),
                      true, std::abs (cur - kMenuScales[i]) < 0.01f);
    m.addSubMenu ("Plugin size", size);

    PopupMenu fac;
    const auto& list = getFactoryPresets();
    for (int i = 0; i < (int) list.size(); ++i) fac.addItem (200 + i, String (i + 1) + ". " + list[(size_t) i].name);
    m.addSubMenu ("Factory sounds", fac);
    m.addItem (300, "Restore factory sounds in presets 1-8");

    PopupMenu midi;
    auto ccName = [] (int cc) { return cc < 0 ? String ("none") : "CC " + String (cc); };
    midi.addItem (401, "Preset footswitch: " + ccName (proc.ccPreset.load()) + "  -  learn...", true, proc.learnTarget.load() == 1);
    midi.addItem (402, "Bypass footswitch: " + ccName (proc.ccBypass.load()) + "  -  learn...", true, proc.learnTarget.load() == 2);
    midi.addItem (403, "Latching MIDI pedals (127 / 0 on each press)", true, proc.latchingPedals.load());
    midi.addSeparator();
    midi.addItem (404, "Program Change 1-8 loads presets 1-8", false, false);
    m.addSubMenu ("MIDI", midi);

    Component::SafePointer<Component> sp (from);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (from), [sp, ed, &proc] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r >= 100 && r < 104)      ed->setScale (kMenuScales[r - 100]);
        else if (r >= 200 && r < 300) proc.applyFactory (r - 200);
        else if (r == 300)            proc.restoreFactorySlots();
        else if (r == 401)            proc.learnTarget = proc.learnTarget.load() == 1 ? 0 : 1;
        else if (r == 402)            proc.learnTarget = proc.learnTarget.load() == 2 ? 0 : 2;
        else if (r == 403)            proc.latchingPedals = ! proc.latchingPedals.load();
    });
}

void PedalContent::showFootswitchMenu (int which, juce::Component* from)
{
    using namespace juce;
    PopupMenu m;
    const int cc = which == 1 ? proc.ccPreset.load() : proc.ccBypass.load();
    m.addSectionHeader (which == 1 ? "PRESET footswitch" : "BYPASS footswitch");
    m.addItem (1, "MIDI learn (move a pedal / button now)", true, proc.learnTarget.load() == which);
    m.addItem (2, cc < 0 ? String ("No MIDI CC assigned") : "Clear MIDI CC " + String (cc), cc >= 0);
    m.addItem (3, "Latching MIDI pedals", true, proc.latchingPedals.load());
    m.addSeparator();
    m.addItem (4, "More...");
    Component::SafePointer<PedalContent> sp (this);
    Component::SafePointer<Component> src (from);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (from), [sp, src, which] (int r)
    {
        if (sp == nullptr) return;
        auto& p = sp->proc;
        if (r == 1) { p.learnTarget = p.learnTarget.load() == which ? 0 : which; sp->flash ("MIDI LEARN: PRESS YOUR PEDAL"); }
        if (r == 2) { if (which == 1) p.ccPreset = -1; else p.ccBypass = -1; }
        if (r == 3) p.latchingPedals = ! p.latchingPedals.load();
        if (r == 4 && src != nullptr) showMainMenu (*sp, src);
    });
}

// =============================================================================
//  Editor
// =============================================================================
FogEditor::FogEditor (FogProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    content = std::make_unique<PedalContent> (p, *this);
    addAndMakeVisible (*content);
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (kBaseW * kDefaultScale), juce::roundToInt (kBaseH * kDefaultScale),
                     juce::roundToInt (kBaseW * kMaxScale),     juce::roundToInt (kBaseH * kMaxScale));
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setScale (proc.uiScale.load());
}

FogEditor::~FogEditor() = default;

void FogEditor::resized()
{
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
    proc.uiScale = juce::jlimit (kDefaultScale, kMaxScale, getScale());
}

void FogEditor::setScale (float s)
{
    s = juce::jlimit (kDefaultScale, kMaxScale, s);
    setSize (juce::roundToInt (kBaseW * s), juce::roundToInt (kBaseH * s));
}

float FogEditor::getScale() const { return (float) getWidth() / kBaseW; }
