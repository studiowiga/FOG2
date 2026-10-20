#include "PluginEditor.h"
#include "BinaryData.h"

// =============================================================================
//  Geometry - every coordinate is in background.png pixels (1240 x 928)
// =============================================================================
namespace
{
    constexpr int   kW = 1240, kH = 928;
    constexpr float kDefaultScale = 0.67f;          // same window as Ring Think, and the minimum size
    constexpr float kMaxScale     = 1.25f;
    const float     kMenuScales[3] = { 0.67f, 0.75f, 1.0f };

    const float slotX[8]  = { 181.5f, 278.4f, 375.4f, 473.4f, 570.4f, 765.3f, 861.3f, 958.2f };
    constexpr float slotTop = 163.8f, slotBot = 468.1f, travelTop = 191.7f, travelBot = 439.3f;
    constexpr float faderW = 46.0f, faderY0 = 150.0f, faderY1 = 482.0f;

    const juce::Point<float> dryLed   { 669.4f, 159.7f },  dryBtn { 669.4f, 249.4f };
    const juce::Point<float> qLed     { 669.4f, 349.5f },  qBtn   { 669.4f, 442.4f };
    constexpr float btnR = 34.0f;
    const juce::Point<float> knobC    { 1085.1f, 148.4f };
    constexpr float knobR = 62.5f;
    constexpr float presetLedX = 1078.9f, presetLedY0 = 241.2f, presetLedDY = 38.31f;
    const juce::Point<float> redPreset { 468.2f, 751.8f }, redBypass { 772.6f, 753.9f };
    const juce::Point<float> fsPreset  { 270.2f, 782.8f }, fsBypass  { 976.8f, 777.6f };

    const juce::Colour cream   (0xfff4efe6);
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
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        using namespace juce;
        const float cx = (float) w * 0.5f;
        const float t = slotTop - faderY0, b = slotBot - faderY0;

        // --- slot: light printed rim, black well, inner shadow ---
        g.setColour (Colour (0xffe9d9cf).withAlpha (0.92f));
        g.fillRoundedRectangle (cx - 11.6f, t, 23.2f, b - t, 11.6f);
        g.setColour (Colour (0xff3a0606));
        g.fillRoundedRectangle (cx - 9.8f, t + 1.8f, 19.6f, b - t - 3.6f, 9.8f);
        ColourGradient well (Colour (0xff050303), cx - 8.0f, 0, Colour (0xff1c1414), cx + 8.0f, 0, false);
        g.setGradientFill (well);
        g.fillRoundedRectangle (cx - 8.4f, t + 3.0f, 16.8f, b - t - 6.0f, 8.4f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawLine (cx + 5.5f, t + 12.0f, cx + 5.5f, b - 12.0f, 1.2f);

        // --- cap ---
        const float cw = 31.0f, ch = 47.0f;
        Rectangle<float> cap (cx - cw * 0.5f, pos - ch * 0.5f, cw, ch);
        // stem shadow inside the slot under the cap
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (cx - 8.0f, pos + ch * 0.5f - 6.0f, 16.0f, 12.0f, 6.0f);
        // drop shadow (light comes from the top-left)
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.07f));
            g.fillEllipse (cap.translated (2.2f, 4.0f).expanded ((float) i * 1.1f));
        }
        ColourGradient body (Colour (0xffffffff), cap.getX() + cw * 0.32f, cap.getY() + ch * 0.25f,
                             Colour (0xffbdb6ac), cap.getRight() + 2.0f, cap.getBottom(), true);
        body.addColour (0.55, Colour (0xfff0ece5));
        g.setGradientFill (body);
        g.fillEllipse (cap);
        // lower lip: the cap is a little cylinder seen from above
        g.setColour (Colour (0xff9d958a).withAlpha (0.55f));
        g.drawEllipse (cap.reduced (0.6f), 1.1f);
        Path lip; lip.addCentredArc (cx, pos + 1.0f, cw * 0.5f - 1.2f, ch * 0.5f - 1.2f, 0.0f,
                                     MathConstants<float>::pi * 0.62f, MathConstants<float>::pi * 1.38f, true);
        g.setColour (Colour (0xff7a7268).withAlpha (0.45f));
        g.strokePath (lip, PathStrokeType (2.0f));
        // specular highlight
        g.setColour (Colours::white.withAlpha (0.85f));
        g.fillEllipse (cap.getX() + cw * 0.22f, cap.getY() + ch * 0.12f, cw * 0.30f, ch * 0.20f);
        ignoreUnused (s);
    }
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
        using namespace juce;
        auto c = getLocalBounds().toFloat().getCentre();
        const float r = btnR;
        // recess ring printed around the button
        g.setColour (Colour (0xff2a0505).withAlpha (0.85f));
        g.fillEllipse (c.x - r - 1.0f, c.y - r - 0.5f, (r + 1.0f) * 2, (r + 1.0f) * 2);
        g.setColour (Colour (0xffdcb7ae).withAlpha (0.6f));
        g.drawEllipse (c.x - r - 1.8f, c.y - r - 1.3f, (r + 1.8f) * 2, (r + 1.8f) * 2, 1.0f);

        const float rr = pressed ? r - 2.6f : r - 1.6f;
        const float dy = pressed ? 1.2f : 0.0f;
        if (! pressed)
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (Colours::black.withAlpha (0.10f));
                g.fillEllipse (c.x - rr + 1.5f, c.y - rr + 2.5f, rr * 2 + (float) i, rr * 2 + (float) i);
            }
        ColourGradient face (pressed ? Colour (0xffe9e5de) : Colour (0xffffffff), c.x - rr * 0.35f, c.y - rr * 0.45f + dy,
                             pressed ? Colour (0xffb3aca2) : Colour (0xffcdc6bc), c.x + rr, c.y + rr + dy, true);
        face.addColour (0.6, pressed ? Colour (0xffd6d1c9) : Colour (0xfff1ede7));
        g.setGradientFill (face);
        g.fillEllipse (c.x - rr, c.y - rr + dy, rr * 2, rr * 2);
        g.setColour (Colour (0xff8d857b).withAlpha (0.6f));
        g.drawEllipse (c.x - rr, c.y - rr + dy, rr * 2, rr * 2, 1.0f);
        if (! pressed)
        {
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillEllipse (c.x - rr * 0.62f, c.y - rr * 0.70f, rr * 0.55f, rr * 0.32f);
        }
    }
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
        // soft cast shadow (light from top-left)
        for (int i = 6; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.055f));
            g.fillEllipse (c.x - r + 4.0f - (float) i, c.y - r + 7.0f - (float) i, (r + (float) i) * 2, (r + (float) i) * 2);
        }
        // skirt
        ColourGradient skirt (Colour (0xfff7f4ee), c.x - r, c.y - r, Colour (0xffa9a196), c.x + r, c.y + r, false);
        g.setGradientFill (skirt);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        // knurled rim : grooves turn with the knob, their shading stays tied to the light
        const int grooves = 48;
        for (int k = 0; k < grooves; ++k)
        {
            const float a = angle + MathConstants<float>::twoPi * (float) k / (float) grooves;
            const float lit = 0.5f + 0.5f * std::cos (a - degreesToRadians (-45.0f) - MathConstants<float>::halfPi);
            const float x0 = c.x + std::sin (a) * (r - 6.5f), y0 = c.y - std::cos (a) * (r - 6.5f);
            const float x1 = c.x + std::sin (a) * (r - 0.8f), y1 = c.y - std::cos (a) * (r - 0.8f);
            g.setColour (Colour (0xff6e665c).withAlpha (0.10f + 0.22f * (1.0f - lit)));
            g.drawLine (x0, y0, x1, y1, 1.6f);
            g.setColour (Colours::white.withAlpha (0.10f + 0.30f * lit));
            g.drawLine (x0 + 1.1f * std::cos (a), y0 + 1.1f * std::sin (a), x1 + 1.1f * std::cos (a), y1 + 1.1f * std::sin (a), 0.8f);
        }
        // top face
        const float tr = r - 8.0f;
        ColourGradient top (Colour (0xffffffff), c.x - tr * 0.45f, c.y - tr * 0.55f, Colour (0xffd2cbc0), c.x + tr, c.y + tr, true);
        top.addColour (0.55, Colour (0xfff3efe8));
        g.setGradientFill (top);
        g.fillEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2);
        g.setColour (Colour (0xff9a9186).withAlpha (0.55f));
        g.drawEllipse (c.x - tr, c.y - tr, tr * 2, tr * 2, 1.0f);
        // engraved pointer
        const float sa = std::sin (angle), ca = -std::cos (angle);
        const float p0 = 12.0f, p1 = tr - 7.0f;
        g.setColour (Colour (0xff5c554c).withAlpha (0.75f));
        g.drawLine (c.x + sa * p0, c.y + ca * p0, c.x + sa * p1, c.y + ca * p1, 3.2f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.drawLine (c.x + sa * p0 + 1.2f, c.y + ca * p0 + 1.2f, c.x + sa * p1 + 1.2f, c.y + ca * p1 + 1.2f, 1.0f);
        // highlight
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillEllipse (c.x - tr * 0.62f, c.y - tr * 0.68f, tr * 0.62f, tr * 0.38f);
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
        if (down && dragged) target += juce::degreesToRadians (40.0f) * juce::jlimit (0.0f, 0.95f, dragFrac) * 0.6f;
        // wrap from 8 to 1 the short way
        const float diff = target - angle;
        if (std::abs (diff) > 0.0005f) { angle += diff * 0.28f; repaint(); }
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
        up   = img (BinaryData::footswitch_up_png,   BinaryData::footswitch_up_pngSize);
        down = img (BinaryData::footswitch_down_png, BinaryData::footswitch_down_pngSize);
    }
    std::function<void()> onPress;
    std::function<void (juce::Component*)> onMenu;
    void paint (juce::Graphics& g) override
    {
        auto c = getLocalBounds().toFloat().getCentre();
        const float d = pressed ? 66.5f : 69.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (pressed ? down : up, juce::Rectangle<float> (d, d).withCentre (c.translated (0, pressed ? 1.0f : 0.0f)));
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
    PedalContent& owner; juce::Image up, down; bool pressed = false;
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
        redOn    = img (BinaryData::led_red_on_png,   BinaryData::led_red_on_pngSize);
        redOff   = img (BinaryData::led_red_off_png,  BinaryData::led_red_off_pngSize);
        amberOn  = img (BinaryData::led_amber_on_png, BinaryData::led_amber_on_pngSize);
        amberOff = img (BinaryData::led_amber_off_png,BinaryData::led_amber_off_pngSize);
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
            b->setBounds (juce::Rectangle<int> (84, 84).withCentre (c.toInt()));
            addAndMakeVisible (b);
        }
        knob = std::make_unique<PresetKnob> (proc, *this);
        knob->setBounds (juce::Rectangle<int> (150, 150).withCentre (knobC.toInt()));
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
            at = { 1010.0f, 566.0f };
        }
        else if (now < flashUntil) { text = flashText; at = { 669.4f, 520.0f }; }
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
    static void drawDomeLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour lit, float level, juce::Colour offTint)
    {
        using namespace juce;
        // chrome bezel
        ColourGradient bez (Colour (0xfff3eee8), c.x - r - 3, c.y - r - 3, Colour (0xff6d6259), c.x + r + 3, c.y + r + 3, false);
        g.setGradientFill (bez);
        g.fillEllipse (c.x - r - 3.2f, c.y - r - 3.2f, (r + 3.2f) * 2, (r + 3.2f) * 2);
        g.setColour (Colour (0xff3a0808)); g.fillEllipse (c.x - r - 0.8f, c.y - r - 0.8f, (r + 0.8f) * 2, (r + 0.8f) * 2);
        // glow
        if (level > 0.01f)
            for (int i = 0; i < 6; ++i)
            {
                const float gr = r + 3.0f + (float) i * 4.0f;
                g.setColour (lit.withAlpha (0.10f * level * (1.0f - (float) i / 6.0f)));
                g.fillEllipse (c.x - gr, c.y - gr, gr * 2, gr * 2);
            }
        const Colour base = offTint.interpolatedWith (lit, level);
        ColourGradient dome (base.brighter (0.6f + 0.6f * level), c.x - r * 0.25f, c.y - r * 0.3f,
                             base.darker (0.8f - 0.5f * level), c.x + r, c.y + r, true);
        g.setGradientFill (dome);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        if (level > 0.01f)
        {
            g.setColour (Colours::white.withAlpha (0.55f * level));
            g.fillEllipse (c.x - r * 0.35f, c.y - r * 0.35f, r * 0.7f, r * 0.7f);
        }
        g.setColour (Colours::white.withAlpha (0.75f));
        g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.72f, r * 0.52f, r * 0.34f);
    }

    void drawLeds (juce::Graphics& g)
    {
        using namespace juce;
        const auto now = Time::getMillisecondCounter();
        // DRY FX : off / red / green / amber
        const int df = st.dryFx;
        const Colour dfCol = df == 1 ? Colour (0xffff2a1a) : df == 2 ? Colour (0xff2cff4a) : Colour (0xffffa514);
        drawDomeLed (g, dryLed, 12.5f, dfCol, df == 0 ? 0.0f : 1.0f, Colour (0xffb9bcb6));
        // Q : brightness grows with resonance
        const float ql[4] = { 0.0f, 0.38f, 0.68f, 1.0f };
        drawDomeLed (g, qLed, 13.0f, Colour (0xffffa20c), ql[st.q], Colour (0xff9a6510));

        // preset LEDs 1..8
        const bool holdBlink = knob->isHoldBlinking();
        const bool saveBlink = now - proc.getSaveBlinkStart() < 450;
        const bool blinkOn = ((now / 125) % 2) == 0;
        for (int i = 0; i < 8; ++i)
        {
            bool on = (i == st.selected);
            if (holdBlink || saveBlink) on = blinkOn;
            const float y = presetLedY0 + presetLedDY * (float) i;
            const auto r = Rectangle<float> (31.0f, 31.0f).withCentre ({ presetLedX, y });
            if (on) g.drawImage (amberOn, r);
            else
            {   // unlit: amber lens, like the real pedal
                g.drawImage (amberOff, r);
                g.setOpacity (0.22f); g.drawImage (amberOn, r); g.setOpacity (1.0f);
            }
        }
        // PRESET red LED : solid = preset loaded, fast blink = edited
        bool pre = st.loaded >= 0 && st.loaded == st.selected;
        if (pre && st.modified) pre = ((now / 90) % 2) == 0;
        g.drawImage (pre ? redOn : redOff, Rectangle<float> (40.0f, 40.0f).withCentre (redPreset));
        g.drawImage (! st.bypassed ? redOn : redOff, Rectangle<float> (40.0f, 40.0f).withCentre (redBypass));
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

    juce::Image bg, redOn, redOff, amberOn, amberOff;
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
    m.addSectionHeader ("Fog2  -  Studio Wiga");

    PopupMenu size;
    const float cur = ed->getScale();
    for (int i = 0; i < 3; ++i)
        size.addItem (100 + i, String (roundToInt (kMenuScales[i] * 100)) + " %" + (i == 0 ? " (default)" : ""),
                      true, std::abs (cur - kMenuScales[i]) < 0.01f);
    m.addSubMenu ("Plugin size", size);

    PopupMenu fac;
    const auto& list = getFactoryPresets();
    for (int i = 0; i < (int) list.size(); ++i) fac.addItem (200 + i, list[(size_t) i].name);
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
        if (r >= 100 && r < 103)      ed->setScale (kMenuScales[r - 100]);
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
    const int minW = juce::roundToInt (kW * kDefaultScale), minH = juce::roundToInt (kH * kDefaultScale);
    setResizable (true, true);
    setResizeLimits (minW, minH, juce::roundToInt (kW * kMaxScale), juce::roundToInt (kH * kMaxScale));
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setScale (proc.uiScale.load());
}

FogEditor::~FogEditor() = default;

void FogEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale = juce::jlimit (kDefaultScale, kMaxScale, s);
}

void FogEditor::setScale (float s)
{
    s = juce::jlimit (kDefaultScale, kMaxScale, s);
    setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
}

float FogEditor::getScale() const { return (float) getWidth() / (float) kW; }
