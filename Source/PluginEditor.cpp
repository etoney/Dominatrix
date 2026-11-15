#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

//==============================================================================
namespace pal
{
const juce::Colour ironDark  { 0xff1d1815 };
const juce::Colour iron      { 0xff3a322c };
const juce::Colour rustDeep  { 0xff4a1d0c };
const juce::Colour rust      { 0xff9a4518 };
const juce::Colour rustLight { 0xffc0702a };
const juce::Colour paint     { 0xffd6c7a6 };   // dirty cream stencil paint
const juce::Colour paintDim  { 0xffa8997c };
const juce::Colour signal    { 0xffd2452a };   // rusty red for active things
const juce::Colour signalHot { 0xffff6a3d };
const juce::Colour hazard    { 0xffc99a22 };   // worn safety yellow
const juce::Colour screen    { 0xff0c0a08 };
const juce::Colour leather   { 0xff4a1a16 };   // worn oxblood leather
const juce::Colour leatherDark { 0xff1c0a08 };
}

//==============================================================================
namespace tex
{
inline float hash (int x, int y, int seed)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffffu) / 16777215.0f;
}
inline float smooth (float t) { return t * t * (3.0f - 2.0f * t); }
inline float smoothstep (float a, float b, float x) { return smooth (juce::jlimit (0.0f, 1.0f, (x - a) / (b - a))); }
inline float vnoise (float x, float y, int seed)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float fx = x - (float) xi, fy = y - (float) yi;
    const float a = hash (xi, yi, seed), b = hash (xi + 1, yi, seed), c = hash (xi, yi + 1, seed), d = hash (xi + 1, yi + 1, seed);
    const float u = smooth (fx), v = smooth (fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
inline float fbm (float x, float y, int seed, int octaves)
{
    float s = 0, amp = 0.5f, f = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) { s += amp * vnoise (x * f, y * f, seed + i * 31); norm += amp; f *= 2.03f; amp *= 0.5f; }
    return s / norm;
}
struct RGB { float r, g, b; };
inline RGB rgb (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }
inline RGB mix (RGB a, RGB b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
inline RGB mul (RGB a, float k) { return { a.r * k, a.g * k, a.b * k }; }

inline RGB rustAt (float x, float y, float rustBias, int seed)
{
    const float n      = fbm (x / 170.0f, y / 170.0f, seed, 4);
    const float detail = fbm (x / 36.0f, y / 36.0f, seed + 7, 3);
    const float fine   = vnoise (x / 7.0f, y / 7.0f, seed + 13);
    const float mask   = smoothstep (0.45f, 0.62f, n + (detail - 0.5f) * 0.35f + rustBias);
    RGB base = mix (rgb (pal::ironDark), rgb (pal::iron), detail);
    RGB r    = mix (rgb (pal::rustDeep), rgb (pal::rust), fbm (x / 20.0f, y / 20.0f, seed + 3, 2));
    if (fine > 0.72f) r = mix (r, rgb (pal::rustLight), (fine - 0.72f) * 2.5f);
    return mix (base, r, mask);
}

// Paint-chip a rendered image: knock out chunks of alpha with noise, plus grain.
inline void distress (juce::Image& img, float scale, float wear, int seed)
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            auto c = bd.getPixelColour (x, y);
            if (c.getAlpha() == 0) continue;
            const float n = fbm ((float) x / (7.0f * scale), (float) y / (7.0f * scale), seed, 3);
            const float chip = n < wear ? 0.08f : 1.0f;
            const float grain = 0.7f + 0.3f * hash (x, y, seed + 99);
            bd.setPixelColour (x, y, c.withMultipliedAlpha (chip * grain));
        }
}

inline juce::Font stencil (float size) { return juce::Font (juce::FontOptions ("Impact", size, juce::Font::plain)); }
inline juce::Font plainBold (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }

inline juce::Image distressedText (const juce::String& text, int w, int h, juce::Font font, juce::Colour col,
                                   juce::Justification just, float scale, float wear, int seed, float kerning = 0.0f)
{
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setColour (col);
        g.setFont (font.withExtraKerningFactor (kerning));
        g.drawFittedText (text, 0, 0, w, h, just, 1, 0.55f);   // squeeze rather than clip if the font runs wide
    }
    distress (img, scale, wear, seed);
    return img;
}
} // namespace tex

//==============================================================================
// The Castle Danger crest: a castle silhouette under a blood moon, on a riveted iron medallion.
static void drawCastleCrest (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;

    // iron ring
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff1a1410), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (r.reduced (1.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.39f;
        const float rx = c.x + std::cos (a) * R * 0.88f, ry = c.y + std::sin (a) * R * 0.88f;
        g.setColour (juce::Colour (0xff8a7a68)); g.fillEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f, 0.6f);
    }

    // night sky + blood moon
    const auto inner = r.reduced (R * 0.2f);
    const float ir = inner.getWidth() * 0.5f;
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addEllipse (inner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0c08), c.x, inner.getY(), juce::Colour (0xff080505), c.x, inner.getBottom(), false));
    g.fillEllipse (inner);
    const juce::Rectangle<float> moon (c.x - ir * 0.55f, inner.getY() + ir * 0.12f, ir * 1.1f, ir * 1.1f);
    juce::DropShadow (pal::signalHot.withAlpha (0.55f), (int) (ir * 0.45f), {}).drawForPath (g, [&] { juce::Path p; p.addEllipse (moon); return p; }());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff7a4a), moon.getX() + moon.getWidth() * 0.35f, moon.getY() + moon.getHeight() * 0.3f,
                                             juce::Colour (0xff8a1408), moon.getRight(), moon.getBottom(), true));
    g.fillEllipse (moon);
    g.setColour (juce::Colour (0x40400000));
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.55f, moon.getY() + moon.getHeight() * 0.25f, moon.getWidth() * 0.16f, moon.getWidth() * 0.16f);
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.25f, moon.getY() + moon.getHeight() * 0.55f, moon.getWidth() * 0.12f, moon.getWidth() * 0.12f);

    // castle silhouette
    const float x0 = inner.getX(), y0 = inner.getY(), w = inner.getWidth(), h = inner.getHeight();
    auto X = [&] (float f) { return x0 + f * w; };
    auto Y = [&] (float f) { return y0 + f * h; };
    juce::Path castle;
    auto tower = [&] (float l, float rgt, float top, int merlons)
    {
        castle.addRectangle (X (l), Y (top), X (rgt) - X (l), Y (1.05f) - Y (top));
        const float mw = (rgt - l) / (float) (merlons * 2 - 1);
        for (int i = 0; i < merlons; ++i)
            castle.addRectangle (X (l + (float) i * 2.0f * mw), Y (top - 0.06f), X (l + mw) - X (l), Y (0.06f) - Y (0.0f) + 0.5f);
    };
    tower (0.08f, 0.30f, 0.50f, 3);           // left tower
    tower (0.70f, 0.92f, 0.50f, 3);           // right tower
    tower (0.38f, 0.62f, 0.36f, 3);           // keep
    tower (0.30f, 0.70f, 0.64f, 5);           // curtain wall
    castle.addTriangle (X (0.08f), Y (0.44f), X (0.19f), Y (0.27f), X (0.30f), Y (0.44f));   // spires
    castle.addTriangle (X (0.70f), Y (0.44f), X (0.81f), Y (0.27f), X (0.92f), Y (0.44f));
    castle.setUsingNonZeroWinding (true);
    g.setColour (juce::Colour (0xff070505)); g.fillPath (castle);
    // gate + lit windows
    juce::Path gate;
    gate.addRectangle (X (0.44f), Y (0.84f), X (0.56f) - X (0.44f), Y (0.2f) - Y (0.0f));
    gate.addEllipse (X (0.44f), Y (0.79f), X (0.56f) - X (0.44f), Y (0.1f) - Y (0.0f));
    g.setColour (juce::Colour (0xff2a0c06)); g.fillPath (gate);
    g.setColour (pal::signalHot.withAlpha (0.9f));
    for (auto p : { juce::Point<float> (0.19f, 0.62f), { 0.81f, 0.62f }, { 0.5f, 0.5f } })
        g.fillRect (X (p.x) - w * 0.018f, Y (p.y), w * 0.036f, h * 0.07f);
}

//==============================================================================
DomLookAndFeel::DomLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, pal::paintDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::Label::textColourId, pal::paintDim);
    setColour (juce::ComboBox::textColourId, pal::paint);
    setColour (juce::ComboBox::arrowColourId, pal::signal);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1411));
    setColour (juce::PopupMenu::textColourId, pal::paint);
    setColour (juce::PopupMenu::headerTextColourId, pal::rustLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::rust);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, pal::paint);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f0b09));
    setColour (juce::TextEditor::textColourId, pal::paint);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff5a3420));
    setColour (juce::TextEditor::focusedOutlineColourId, pal::rustLight);
    setColour (juce::TextEditor::highlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, pal::signalHot);

    rustDisc = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Image::BitmapData bd (rustDisc, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            const float n = tex::fbm ((float) x / 22.0f, (float) y / 22.0f, 501, 4);
            const float a = tex::smoothstep (0.5f, 0.7f, n);
            const auto c = tex::rustAt ((float) x * 2.0f, (float) y * 2.0f, 0.3f, 777);
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (c.r, c.g, c.b, a * 0.85f));
        }
}

juce::Font DomLookAndFeel::getComboBoxFont (juce::ComboBox&) { return tex::plainBold (13.0f); }
juce::Font DomLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return tex::stencil (juce::jmin (15.0f, (float) h * 0.62f)).withExtraKerningFactor (0.08f); }

void DomLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (down ? pal::rustDeep : juce::Colour (0xff3a2a20), 0, r.getY(),
                                             down ? juce::Colour (0xff2a120a) : juce::Colour (0xff17110e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hi ? pal::rustLight : juce::Colour (0xff6a3a20)); g.drawRoundedRectangle (r, 3.0f, 1.2f);
}

void DomLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff0f0b09)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.isMouseOver (true) ? pal::rustLight : juce::Colour (0xff6a3a20)); g.drawRoundedRectangle (r, 3.0f, 1.2f);
    juce::Path arrow; const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (pal::signal); g.fillPath (arrow);
}

void DomLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (2.5f, r * 0.11f);
    const float alpha = s.isEnabled() ? 1.0f : 0.3f;

    // tick marks stamped round the dial
    g.setColour (pal::paintDim.withAlpha (0.45f * alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle) - juce::MathConstants<float>::halfPi;
        const float r1 = r + 1.0f, r0 = r - (i % 5 == 0 ? 5.0f : 3.0f);
        g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, i % 5 == 0 ? 1.4f : 0.9f);
    }

    const float ar = r - thick - 4.0f;
    juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff120e0c).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff3a2418).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, angle, true);
    const bool isWidth = s.getName() == "width";
    if (isWidth)
    {
        // mark the danger zone past 150 %
        const float dz = startAngle + 0.75f * (endAngle - startAngle);
        juce::Path zone; zone.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0, dz, endAngle, true);
        g.setColour (pal::signalHot.withAlpha (0.55f * alpha)); g.strokePath (zone, juce::PathStrokeType (2.0f));
    }
    const auto arcCol = (isWidth ? (pos > 0.75f ? pal::signalHot : pal::hazard)
                                 : (s.isMouseOverOrDragging() ? pal::signalHot : pal::signal)).withAlpha (alpha);
    g.setColour (arcCol.withAlpha (0.25f * alpha));
    g.strokePath (val, juce::PathStrokeType (thick + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = ar - thick * 1.4f;
    if (br < 4.0f) return;
    const juce::Rectangle<float> body (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillEllipse (body.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c).withAlpha (alpha), c.x - br * 0.5f, c.y - br * 0.7f,
                                             juce::Colour (0xff17120f), c.x + br * 0.6f, c.y + br, true));
    g.fillEllipse (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (body);
        g.reduceClipRegion (clip);
        const float off = (float) (((juce::uint64) (juce::pointer_sized_int) &s >> 4) % 64);
        g.setOpacity (0.9f * alpha);
        g.drawImage (rustDisc, body.expanded (br * 0.4f).translated (-off * 0.3f, off * 0.2f), juce::RectanglePlacement::stretchToFit);
    }
    g.setColour (juce::Colour (0xff0f0b09).withAlpha (0.8f));
    for (int i = 0; i < 32; ++i)
    {
        const float a = (float) i / 32.0f * juce::MathConstants<float>::twoPi;
        g.drawLine (c.x + std::cos (a) * br * 0.87f, c.y + std::sin (a) * br * 0.87f,
                    c.x + std::cos (a) * br, c.y + std::sin (a) * br, 1.0f);
    }
    g.setColour (juce::Colour (0xff0b0807)); g.drawEllipse (body, 1.2f);

    juce::Path p; p.addRoundedRectangle (-1.8f, -br * 0.92f, 3.6f, br * 0.6f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x + 0.8f, c.y + 1.0f));
    g.setColour (pal::paint.withAlpha (alpha));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

// toggles: an indicator lamp + stencilled label on a rusty plate
void DomLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const bool warn = b.getName() == "match";
    const auto lit = warn ? pal::hazard : pal::signalHot;
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff3a1a10) : juce::Colour (0xff2a221d), 0, r.getY(),
                                             juce::Colour (0xff120d0b), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? lit.withAlpha (0.8f) : (hi ? pal::rustLight : juce::Colour (0xff5a3420))); g.drawRoundedRectangle (r, 3.0f, 1.2f);

    const float d = r.getHeight() * 0.42f;
    auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.55f, r.getCentreY() });
    if (on)
    {
        juce::Path lp; lp.addEllipse (led);
        juce::DropShadow (lit.withAlpha (0.9f), 10, {}).drawForPath (g, lp);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c0), led.getCentreX(), led.getCentreY(), lit, led.getX(), led.getY(), true));
    }
    else g.setColour (juce::Colour (0xff2a1c16));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawEllipse (led, 1.0f);

    g.setColour (on ? pal::paint : pal::paintDim);
    g.setFont (r.getWidth() < 120.0f ? tex::stencil (12.5f).withExtraKerningFactor (0.04f) : tex::stencil (15.0f).withExtraKerningFactor (0.1f));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 1.0f).withTrimmedRight (6).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
}

//==============================================================================
Selector::Selector (juce::RangedAudioParameter& p, juce::StringArray l, juce::StringArray s)
    : labels (std::move (l)), subs (std::move (s)),
      att (p, [this] (float v) { index = juce::roundToInt (v); repaint(); if (onChange) onChange(); })
{
    att.sendInitialUpdate();
}

int Selector::segmentAt (juce::Point<int> p) const
{
    if (labels.isEmpty()) return -1;
    return juce::jlimit (0, labels.size() - 1, p.x * labels.size() / juce::jmax (1, getWidth()));
}

void Selector::mouseDown (const juce::MouseEvent& e) { att.setValueAsCompleteGesture ((float) segmentAt (e.getPosition())); }
void Selector::mouseMove (const juce::MouseEvent& e) { const int h = segmentAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }

void Selector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    const float sw = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * sw, 0, sw, (float) getHeight()).reduced (3.0f, 2.0f);
        const bool on = i == index, hi = i == hover;
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 3.0f);
        if (on)
        {
            juce::Path pth; pth.addRoundedRectangle (r, 3.0f);
            juce::DropShadow (pal::signalHot.withAlpha (0.45f), 12, {}).drawForPath (g, pth);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a2a14), r.getCentreX(), r.getY(), juce::Colour (0xff3a0e06), r.getCentreX(), r.getBottom(), false));
        }
        else
            g.setGradientFill (juce::ColourGradient (hi ? juce::Colour (0xff43332a) : juce::Colour (0xff332822), r.getCentreX(), r.getY(),
                                                     juce::Colour (0xff15100d), r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? pal::signalHot : (hi ? pal::rustLight : juce::Colour (0xff5a3420)));
        g.drawRoundedRectangle (r, 3.0f, on ? 1.6f : 1.1f);
        // little lamp in the corner
        auto lamp = juce::Rectangle<float> (6, 6).withCentre ({ r.getX() + 9, r.getY() + 9 });
        g.setColour (on ? juce::Colour (0xffffd0a0) : juce::Colour (0xff2a1c16)); g.fillEllipse (lamp);
        // grime
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine (r.getRight() - 18, r.getBottom() - 3, r.getRight() - 4, r.getBottom() - 10, 1.0f);

        const bool hasSub = subs.size() > i && subs[i].isNotEmpty();
        auto tr = r.reduced (6, 2);
        g.setColour (on ? juce::Colour (0xfffff0d8) : pal::paint.withAlpha (0.85f));
        g.setFont (tex::stencil (hasSub ? 19.0f : 18.0f).withExtraKerningFactor (0.08f));
        g.drawFittedText (labels[i], (hasSub ? tr.removeFromTop (tr.getHeight() * 0.62f) : tr).toNearestInt(),
                          hasSub ? juce::Justification::centredBottom : juce::Justification::centred, 1, 0.7f);
        if (hasSub)
        {
            g.setColour (on ? pal::paint : pal::paintDim.withAlpha (0.7f));
            g.setFont (tex::plainBold (9.5f).withExtraKerningFactor (0.12f));
            g.drawFittedText (subs[i], tr.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
        }
    }
}

//==============================================================================
// GR mapping on the meter face: 0 dB at the right, 20 dB at the left, spread like a real GR meter
static float grToPos (float gr) { return 1.0f - std::log10 (1.0f + juce::jlimit (0.0f, 20.0f, gr)) / std::log10 (21.0f); }

void VuMeter::setGr (float grDb, float dt)
{
    // spring-and-damper needle: a little overshoot, settles like a real movement
    const float target = active ? grToPos (grDb) : 1.0f;
    const float k = 120.0f, damp = 17.0f;
    const int steps = 4;
    for (int i = 0; i < steps; ++i)
    {
        const float h = dt / (float) steps;
        vel += (k * (target - pos) - damp * vel) * h;
        pos += vel * h;
    }
    pos = juce::jlimit (-0.03f, 1.03f, pos);
    glow += (juce::jlimit (0.0f, 1.0f, grDb / 8.0f) - glow) * 0.15f;
    repaint();
}

void VuMeter::buildFace()
{
    const float scale = 2.0f;
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;
    face = juce::Image (juce::Image::ARGB, (int) ((float) W * scale), (int) ((float) H * scale), true);
    {
        // aged, water-stained cream face with rust creeping in from the corners
        juce::Image::BitmapData bd (face, juce::Image::BitmapData::writeOnly);
        const auto cream = tex::rgb (juce::Colour (0xffe2d3a8)), yellowed = tex::rgb (juce::Colour (0xffc7ad72));
        for (int py = 0; py < bd.height; ++py)
            for (int px = 0; px < bd.width; ++px)
            {
                const float x = (float) px / scale, y = (float) py / scale;
                const float n = tex::fbm (x / 40.0f, y / 40.0f, 808, 4);
                auto col = tex::mix (cream, yellowed, tex::smoothstep (0.35f, 0.8f, n));
                const float stain = tex::smoothstep (0.62f, 0.66f, tex::fbm (x / 26.0f, y / 26.0f, 909, 3));
                col = tex::mul (col, 1.0f - 0.12f * stain);
                const float dx = std::min (x, (float) W - x), dy = std::min (y, (float) H - y);
                const float corner = 1.0f - juce::jlimit (0.0f, 1.0f, std::min (dx, dy) / 22.0f);
                const float rustMask = corner * tex::smoothstep (0.4f, 0.65f, tex::vnoise (x / 5.0f, y / 5.0f, 31));
                col = tex::mix (col, tex::rustAt (x * 2.0f, y * 2.0f, 0.4f, 71), rustMask * 0.85f);
                const float grain = 0.93f + 0.07f * tex::hash (px, py, 5);
                col = tex::mul (col, grain);
                bd.setPixelColour (px, py, juce::Colour::fromFloatRGBA (col.r, col.g, col.b, 1.0f));
            }
    }
    juce::Graphics g (face);
    g.addTransform (juce::AffineTransform::scale (scale));
    const float w = (float) W, h = (float) H;
    const juce::Point<float> pivot (w * 0.5f, h * 1.02f);
    const float R = h * 0.80f;
    const float a0 = juce::degreesToRadians (-50.0f), a1 = juce::degreesToRadians (50.0f);
    auto angleFor = [&] (float p) { return a0 + p * (a1 - a0); };
    auto pt = [&] (float ang, float rad) { return juce::Point<float> (pivot.x + std::sin (ang) * rad, pivot.y - std::cos (ang) * rad); };

    // scale arc + red zone
    juce::Path arc; arc.addCentredArc (pivot.x, pivot.y, R, R, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff1a1410)); g.strokePath (arc, juce::PathStrokeType (1.6f));
    juce::Path red; red.addCentredArc (pivot.x, pivot.y, R + 3.5f, R + 3.5f, 0, a0, angleFor (grToPos (10.0f)), true);
    g.setColour (juce::Colour (0xffa8201a)); g.strokePath (red, juce::PathStrokeType (5.0f));
    for (float gr : { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f, 10.f, 14.f, 20.f })
    {
        const float ang = angleFor (grToPos (gr));
        const bool major = gr == 0 || gr == 1 || gr == 2 || gr == 3 || gr == 4 || gr == 6 || gr == 10 || gr == 20;
        g.setColour (juce::Colour (0xff1a1410));
        g.drawLine ({ pt (ang, R), pt (ang, R + (major ? 9.0f : 5.0f)) }, major ? 1.6f : 1.0f);
        if (major)
        {
            const auto lp = pt (ang, R + 18.0f);
            g.setColour (gr >= 10 ? juce::Colour (0xffa8201a) : juce::Colour (0xff1a1410));
            g.setFont (tex::stencil (13.0f));
            g.drawText (juce::String ((int) gr), juce::Rectangle<float> (26, 14).withCentre (lp), juce::Justification::centred);
        }
    }
    for (float gr = 0.5f; gr < 6.0f; gr += 1.0f)
    {
        const float ang = angleFor (grToPos (gr));
        g.setColour (juce::Colour (0xff1a1410).withAlpha (0.7f));
        g.drawLine ({ pt (ang, R), pt (ang, R + 3.5f) }, 0.8f);
    }
    g.setColour (juce::Colour (0xff1a1410));
    g.setFont (tex::stencil (12.0f).withExtraKerningFactor (0.14f));
    g.drawText ("GAIN REDUCTION", juce::Rectangle<float> (0, h * 0.50f, w, 16), juce::Justification::centred);
    g.setFont (tex::plainBold (9.0f).withExtraKerningFactor (0.1f));
    g.drawText ("dB", juce::Rectangle<float> (0, h * 0.50f + 15, w, 12), juce::Justification::centred);
    g.setColour (juce::Colour (0xffa8201a));
    g.setFont (tex::stencil (10.0f).withExtraKerningFactor (0.2f));
    g.drawText ("CASTLE DANGER", juce::Rectangle<float> (10.0f, 8.0f, 120.0f, 12.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff1a1410).withAlpha (0.6f));
    g.drawText ("DMX-1", juce::Rectangle<float> (w - 70.0f, 8.0f, 60.0f, 12.0f), juce::Justification::centredRight);
}

void VuMeter::paint (juce::Graphics& g)
{
    if (! face.isValid()) buildFace();
    auto r = getLocalBounds().toFloat();
    const float w = r.getWidth(), h = r.getHeight();
    g.drawImage (face, r);

    // warm backlight that glows up with the gain reduction
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffb060).withAlpha (0.10f + 0.22f * glow), w * 0.5f, h * 0.75f,
                                             juce::Colours::transparentBlack, w * 0.5f, -h * 0.1f, true));
    g.fillRect (r);

    const juce::Point<float> pivot (w * 0.5f, h * 1.02f);
    const float R = h * 0.80f;
    const float ang = juce::degreesToRadians (-50.0f) + pos * juce::degreesToRadians (100.0f);
    const juce::Point<float> tip (pivot.x + std::sin (ang) * (R + 6.0f), pivot.y - std::cos (ang) * (R + 6.0f));
    const juce::Point<float> base (pivot.x + std::sin (ang) * h * 0.12f, pivot.y - std::cos (ang) * h * 0.12f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawLine ({ base.translated (3.0f, 3.0f), tip.translated (3.0f, 3.0f) }, 2.0f);
    g.setColour (juce::Colour (0xff120c0a));
    g.drawLine ({ base, tip }, 1.8f);
    const juce::Point<float> tip2 (pivot.x + std::sin (ang) * (R - 14.0f), pivot.y - std::cos (ang) * (R - 14.0f));
    g.setColour (juce::Colour (0xffb81d16));
    g.drawLine ({ tip2, tip }, 2.2f);

    // pivot housing
    const float cr = h * 0.19f;
    juce::Path cap; cap.addPieSegment (pivot.x - cr, pivot.y - cr, cr * 2.0f, cr * 2.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, 0.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a302a), pivot.x, pivot.y - cr, juce::Colour (0xff0e0a08), pivot.x, pivot.y, false));
    g.fillPath (cap);
    g.setColour (pal::rust.withAlpha (0.5f)); g.strokePath (cap, juce::PathStrokeType (1.0f));

    // glass: reflection + grime, then the bezel
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0, 0, juce::Colours::transparentWhite, w * 0.45f, h * 0.55f, false));
    juce::Path glare; glare.startNewSubPath (0, 0); glare.lineTo (w * 0.62f, 0); glare.lineTo (0, h * 0.7f); glare.closeSubPath();
    g.fillPath (glare);
    g.setColour (juce::Colours::black); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 2.5f);
    g.setColour (pal::rust.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (3.0f), 3.0f, 1.0f);
}

//==============================================================================
void PilotLamp::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const float lit = juce::jlimit (0.0f, 1.0f, 0.38f + heat * 0.62f);

    // knurled iron bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (2.0f, 3.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7a6a5a), c.x - R * 0.6f, c.y - R * 0.7f, juce::Colour (0xff15100d), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff0c0807));
    for (int i = 0; i < 40; ++i)
    {
        const float a = (float) i / 40.0f * juce::MathConstants<float>::twoPi;
        g.drawLine (c.x + std::cos (a) * R * 0.9f, c.y + std::sin (a) * R * 0.9f, c.x + std::cos (a) * R, c.y + std::sin (a) * R, 1.0f);
    }
    g.setColour (pal::rust.withAlpha (0.6f)); g.drawEllipse (r.reduced (1.0f), 1.5f);

    // the jewel
    const float jr = R * 0.66f;
    const juce::Rectangle<float> jewel (c.x - jr, c.y - jr, jr * 2, jr * 2);
    juce::Path jp; jp.addEllipse (jewel);
    if (lit > 0.4f) juce::DropShadow (juce::Colour (0xffff3020).withAlpha (0.9f * (lit - 0.35f)), (int) (R * 0.9f), {}).drawForPath (g, jp);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff9a70).interpolatedWith (juce::Colour (0xff5a0a06), 1.0f - lit), c.x - jr * 0.25f, c.y - jr * 0.3f,
                                             juce::Colour (0xff2a0403), c.x + jr, c.y + jr, true));
    g.fillPath (jp);
    // facets
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (jp);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.2f;
        g.drawLine (c.x + std::cos (a) * jr * 0.38f, c.y + std::sin (a) * jr * 0.38f, c.x + std::cos (a) * jr, c.y + std::sin (a) * jr, 1.0f);
    }
    juce::Path oct; oct.addPolygon (c, 8, jr * 0.4f, 0.2f);
    g.strokePath (oct, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.35f + 0.3f * lit));
    g.fillEllipse (c.x - jr * 0.55f, c.y - jr * 0.6f, jr * 0.45f, jr * 0.3f);
}

//==============================================================================
static juce::Font ledFont (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }

void RulesDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (r, 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x22ff6a3d), r.getCentreX(), r.getCentreY(), juce::Colours::transparentBlack, r.getX(), r.getY(), true));
    g.fillRoundedRectangle (r, 4.0f);
    // scanlines
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (float y = r.getY() + 1; y < r.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());

    const bool off = discipline <= 0.0005f;
    auto inner = r.reduced (12.0f, 8.0f);
    g.setColour (pal::paintDim.withAlpha (0.8f)); g.setFont (tex::plainBold (9.0f).withExtraKerningFactor (0.14f));
    g.drawText (off ? "DISCIPLINE 0  -  AT EASE" : "SHE SETS THESE. YOU DON'T.", inner.removeFromTop (13.0f), juce::Justification::centredLeft);
    inner.removeFromTop (3.0f);

    const juce::String rows[][2] = {
        { "THRESHOLD", juce::String (thresh, 1) + " dB" },
        { "RATIO",     juce::String (ratio, 1) + " : 1" },
        { "ATTACK",    juce::String (juce::roundToInt (attack)) + " ms" },
        { "RELEASE",   release >= 1000.0f ? juce::String (release / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (release)) + " ms" },
        { "MAKEUP",    (makeup >= 0.05f ? "+" : "") + juce::String (makeup, 1) + " dB" },
    };
    const float rh = inner.getHeight() / 5.0f;
    for (auto& row : rows)
    {
        auto line = inner.removeFromTop (rh);
        g.setColour (pal::paint.withAlpha (off ? 0.3f : 0.75f));
        g.setFont (tex::stencil (13.0f).withExtraKerningFactor (0.1f));
        g.drawText (row[0], line, juce::Justification::centredLeft);
        const auto txt = off ? juce::String ("---") : row[1];
        auto glowCol = off ? pal::signal.withAlpha (0.25f) : pal::signalHot;
        g.setFont (ledFont (14.0f));
        g.setColour (glowCol.withAlpha (0.25f));
        g.drawText (txt, line.translated (0.0f, 0.6f).expanded (1.0f, 0.0f), juce::Justification::centredRight);
        g.setColour (glowCol);
        g.drawText (txt, line, juce::Justification::centredRight);
    }
    g.setColour (juce::Colours::black); g.drawRoundedRectangle (r, 4.0f, 2.0f);
    g.setColour (pal::rust.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.0f);
}

//==============================================================================
void PhaseMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (r, 4.0f);
    auto inner = r.reduced (8.0f, 6.0f);
    auto head = inner.removeFromTop (12.0f);
    g.setColour (pal::paintDim); g.setFont (tex::plainBold (8.5f).withExtraKerningFactor (0.12f));
    g.drawText ("PHASE", head, juce::Justification::centredLeft);
    const auto col = value < 0.0f ? pal::signalHot : (value < 0.3f ? pal::hazard : pal::paint);
    g.setColour (col); g.setFont (ledFont (11.0f));
    g.drawText ((value >= 0 ? "+" : "") + juce::String (value, 2), head, juce::Justification::centredRight);
    auto bar = inner.removeFromTop (12.0f).translated (0.0f, 3.0f);
    g.setColour (juce::Colour (0xff171210)); g.fillRect (bar);
    auto half = bar.withTrimmedRight (bar.getWidth() * 0.5f);
    g.setColour (pal::signal.withAlpha (0.25f)); g.fillRect (half);                       // red zone: below 0
    g.setColour (pal::paintDim.withAlpha (0.5f)); g.fillRect (bar.getCentreX() - 0.5f, bar.getY(), 1.0f, bar.getHeight());
    const float x = bar.getX() + (juce::jlimit (-1.0f, 1.0f, value) + 1.0f) * 0.5f * bar.getWidth();
    juce::Path needle; needle.addRectangle (x - 1.5f, bar.getY() - 2.0f, 3.0f, bar.getHeight() + 4.0f);
    juce::DropShadow (col.withAlpha (0.8f), 6, {}).drawForPath (g, needle);
    g.setColour (col); g.fillPath (needle);
    g.setColour (pal::paintDim.withAlpha (0.6f)); g.setFont (tex::plainBold (7.5f));
    auto lab = inner.removeFromTop (12.0f).translated (0.0f, 4.0f);
    g.drawText ("-1", lab, juce::Justification::centredLeft);
    g.drawText ("0", lab, juce::Justification::centred);
    g.drawText ("+1", lab, juce::Justification::centredRight);
    g.setColour (juce::Colours::black); g.drawRoundedRectangle (r, 4.0f, 2.0f);
    g.setColour (pal::rust.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.0f);
}

//==============================================================================
void LoudnessDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (float y = r.getY() + 1; y < r.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    auto inner = r.reduced (10.0f, 6.0f);
    auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("--.-") : juce::String (v, 1); };

    auto top = inner.removeFromTop (inner.getHeight() * 0.58f);
    g.setColour (pal::paintDim); g.setFont (tex::plainBold (8.5f).withExtraKerningFactor (0.12f));
    g.drawText ("SHORT-TERM", top.removeFromTop (11.0f), juce::Justification::centredLeft);
    g.setFont (ledFont (21.0f)); g.setColour (pal::signalHot);
    g.drawText (fmt (shortTerm), top, juce::Justification::centredLeft);
    g.setFont (tex::stencil (12.0f)); g.setColour (pal::paint.withAlpha (0.8f));
    g.drawText ("LUFS", top, juce::Justification::centredRight);

    g.setColour (pal::paintDim); g.setFont (tex::plainBold (8.5f).withExtraKerningFactor (0.12f));
    g.drawText ("INTEGRATED", inner.removeFromLeft (inner.getWidth() * 0.5f), juce::Justification::centredLeft);
    g.setFont (ledFont (14.0f)); g.setColour (pal::hazard);
    g.drawText (fmt (integrated), inner, juce::Justification::centredRight);

    g.setColour (juce::Colours::black); g.drawRoundedRectangle (r, 4.0f, 2.0f);
    g.setColour ((isMouseOver() ? pal::rustLight : pal::rust).withAlpha (0.45f)); g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.0f);
}

//==============================================================================
void MeterPanel::paint (juce::Graphics& g)
{
    auto m = getLocalBounds().toFloat();
    g.setColour (pal::screen); g.fillRoundedRectangle (m, 3.0f);
    auto bars = m.reduced (10.0f, 8.0f);
    auto labels = bars.removeFromBottom (30.0f);
    const float bw = 28.0f, gap = (bars.getWidth() - bw * 3.0f) / 2.0f;

    auto drawLevel = [&] (juce::Rectangle<float> bar, float lvl, const juce::String& name, juce::Rectangle<float> lab)
    {
        g.setColour (juce::Colour (0xff171210)); g.fillRect (bar);
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
        auto inner = bar.reduced (2.0f);
        const int segs = 24;
        const float sh = inner.getHeight() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const float t = (float) (i + 1) / (float) segs;
            auto seg = juce::Rectangle<float> (inner.getX(), inner.getBottom() - (float) (i + 1) * sh + 1.0f, inner.getWidth(), sh - 1.5f);
            const auto c = t > 0.94f ? pal::signalHot : (t > 0.8f ? pal::hazard : pal::paint.darker (0.15f));
            g.setColour (t <= frac ? c : c.withAlpha (0.07f));
            g.fillRect (seg);
        }
        g.setColour (pal::paint); g.setFont (tex::stencil (13.0f));
        g.drawText (name, lab.removeFromTop (15), juce::Justification::centred);
        g.setColour (pal::paintDim); g.setFont (tex::plainBold (9.5f));
        g.drawText (lvl > 0.00002f ? juce::String (db, 1) : juce::String ("-inf"), lab, juce::Justification::centred);
    };

    auto barIn = bars.removeFromLeft (bw);  bars.removeFromLeft (gap);
    auto barCl = bars.removeFromLeft (bw);  bars.removeFromLeft (gap);
    auto barOut = bars.removeFromLeft (bw);
    auto labIn = labels.withX (barIn.getX() - 14).withWidth (bw + 28);
    auto labCl = labels.withX (barCl.getX() - 14).withWidth (bw + 28);
    auto labOut = labels.withX (barOut.getX() - 14).withWidth (bw + 28);
    drawLevel (barIn, in, "IN", labIn);
    drawLevel (barOut, out, "OUT", labOut);

    // CLIP: how hard the soft clipper is working, hangs down from the top (0..12 dB)
    {
        g.setColour (juce::Colour (0xff171210)); g.fillRect (barCl);
        auto inner = barCl.reduced (2.0f);
        const int segs = 12;
        const float sh = inner.getHeight() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            auto seg = juce::Rectangle<float> (inner.getX(), inner.getY() + (float) i * sh + 0.5f, inner.getWidth(), sh - 1.5f);
            const bool lit = ! clipperOff && clip > (float) i + 0.05f;
            g.setColour (lit ? (i < 2 ? pal::hazard : pal::signalHot) : pal::signal.withAlpha (0.08f));
            g.fillRect (seg);
        }
        if (hold > 0.05f && ! clipperOff)
        {
            const float hy = inner.getY() + std::min (hold, 12.0f) / 12.0f * inner.getHeight();
            g.setColour (juce::Colour (0xfffff0d8)); g.fillRect (inner.getX(), hy - 1.0f, inner.getWidth(), 2.0f);
        }
        g.setColour (clipperOff ? pal::paintDim.withAlpha (0.5f) : pal::signalHot); g.setFont (tex::stencil (13.0f));
        auto lab = labCl;
        g.drawText ("CLIP", lab.removeFromTop (15), juce::Justification::centred);
        g.setColour (pal::paintDim); g.setFont (tex::plainBold (9.5f));
        g.drawText (clipperOff ? juce::String ("OFF") : (hold > 0.05f ? juce::String (hold, 1) : juce::String ("0.0")), lab, juce::Justification::centred);
    }
    g.setFont (tex::plainBold (8.0f));
    for (int db : { 0, -6, -12, -24, -36, -48 })
    {
        const float yy = barIn.reduced (2.0f).getY() + (float) -db / 48.0f * barIn.reduced (2.0f).getHeight();
        g.setColour (pal::paintDim.withAlpha (0.55f));
        g.drawText (juce::String (db), (int) barIn.getRight() + 1, (int) yy - 5, (int) gap - 2, 10, juce::Justification::centred);
    }
    for (int db : { 2, 4, 6, 8, 10 })
    {
        const float yy = barCl.reduced (2.0f).getY() + (float) db / 12.0f * barCl.reduced (2.0f).getHeight();
        g.setColour (pal::paintDim.withAlpha (0.55f));
        g.drawText (juce::String (db), (int) barCl.getRight() + 1, (int) yy - 5, (int) gap - 2, 10, juce::Justification::centred);
    }

    if (match)
    {
        auto w = m.withHeight (16.0f);
        g.setColour (pal::hazard.withAlpha (0.92f)); g.fillRect (w);
        g.setColour (juce::Colours::black); g.setFont (tex::stencil (12.0f).withExtraKerningFactor (0.1f));
        g.drawFittedText ("MATCHED LISTENING " + juce::String (matchDb, 1) + " dB", w.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.6f);
    }
}

//==============================================================================
DominatrixEditor::DominatrixEditor (DominatrixProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      typeSel (*p.apvts.getParameter ("satType"), { "TAPE", "TUBE", "IRON", "CONSOLE", "WHIP" },
               { "WARM & SMOOTH", "EVEN HARMONICS", "FAT LOW END", "SUBTLE SUMMING", "BITE & EDGE" }),
      ceilSel (*p.apvts.getParameter ("ceiling"), { "-1 dB", "-6 dB", "OFF" }, { "LOUD MASTER", "HEADROOM", "NO CLIPPER" }),
      qualitySel (*p.apvts.getParameter ("quality"), { "2x", "4x", "8x" })
{
    setLookAndFeel (&lnf);

    disciplineKnob = &makeKnob ("discipline", "DISCIPLINE",
        "How hard she holds the mix together. Threshold, ratio, attack, release and makeup are all set for you, and follow the music.");
    satKnob    = &makeKnob ("saturation", "SATURATION", "Harmonic saturation. Pick the flavour with SATURATION TYPE above. 0 = clean.");
    analogKnob = &makeKnob ("analog", "ANALOG",
        "Old-gear character: tape wow & flutter, head bump, soft top end, L/R mismatch, crosstalk, a little hiss and drift. 0 = digital clean.");
    mixKnob    = &makeKnob ("mix", "MIX", "Parallel blend: 100 = all Dominatrix, lower = blend the uncompressed mix back in.");
    outKnob    = &makeKnob ("outputDb", "OUTPUT",
        "Drives the soft clipper. With CEILING at -1 or -6 dB, the more you turn up, the more it clips (and it never goes past the ceiling). With CEILING OFF it's a plain output level.");
    widthKnob  = &makeKnob ("width", "WIDTH",
        "Stereo width. 100 = unchanged, 0 = mono, up to 200 = wider. Only the sides change; the centre (kick, bass, vocal) stays put, and the lows below ~120 Hz are never widened. Past 150 % mixes start to sound phasey: watch the PHASE meter.");
    widthKnob->slider.setName ("width");
    disciplineKnob->label.setFont (tex::stencil (21.0f).withExtraKerningFactor (0.12f));

    for (auto* s : { &typeSel, &ceilSel, &qualitySel }) addAndMakeVisible (*s);
    typeSel.setTooltip ("TAPE: warm, smooth, softens the top.  TUBE: rich even harmonics.  IRON: transformer weight in the low end.  CONSOLE: subtle summing glue.  WHIP: bite and edge.");
    ceilSel.setTooltip ("Replaces your limiter. -1 dB: loud master for streaming.  -6 dB: leaves headroom (e.g. for a mastering engineer).  OFF: no clipper, OUTPUT is plain gain.");
    qualitySel.setTooltip ("Oversampling for the saturation and clipper. Higher = cleaner top end, more CPU. 4x is plenty for most sessions; use 8x for the final bounce.");

    matchButton.setName ("match");
    matchButton.setTooltip ("Level-matched listening: turns the output down (or up) to the same loudness as the input, so you hear what Dominatrix does, not just 'louder'. Turn it off before you bounce!");
    addAndMakeVisible (matchButton);
    bassMonoButton.setName ("bassMono");
    bassMonoButton.setTooltip ("Makes everything below ~120 Hz mono, so the kick and bass sit dead centre and play right on any system (and on vinyl).");
    addAndMakeVisible (bassMonoButton);
    bassMonoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bassMono", bassMonoButton);
    addAndMakeVisible (phase);
    phase.setInterceptsMouseClicks (false, false);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "match", matchButton);

    for (auto* c : std::initializer_list<juce::Component*> { &vu, &lamp, &rules, &meters })
    {
        addAndMakeVisible (*c);
        c->setInterceptsMouseClicks (false, false);
    }
    addAndMakeVisible (lufs);
    lufs.setTooltip ("Loudness of the output (before MATCH). Click to reset the integrated reading.");
    lufs.onReset = [this] { proc.getEngine().getLoudness().requestReset(); };

    addAndMakeVisible (presetBox);
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id >= kUserIdBase)
        {
            if (id - kUserIdBase < userFiles.size()) proc.loadUserPreset (userFiles[id - kUserIdBase]);
        }
        else if (id - 1 != proc.getCurrentProgram() || proc.getCurrentPresetName() != proc.getProgramName (id - 1))
            proc.setCurrentProgram (id - 1);
    };
    for (auto* b : { &prevButton, &nextButton, &saveButton, &deleteButton }) addAndMakeVisible (*b);
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.onClick = [this] { promptSave(); };
    deleteButton.onClick = [this] { confirmDelete(); };

    refreshPresetBox();
    setSize (1060, 620);
    startTimerHz (30);
}

DominatrixEditor::~DominatrixEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

DominatrixEditor::Knob& DominatrixEditor::makeKnob (const juce::String& id, const juce::String& text, const juce::String& tip)
{
    auto k = std::make_unique<Knob>();
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (tex::stencil (15.0f).withExtraKerningFactor (0.1f));
    k->label.setColour (juce::Label::textColourId, pal::paint);
    k->label.setMinimumHorizontalScale (0.6f);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->slider.setColour (juce::Slider::textBoxTextColourId, pal::paint.withAlpha (0.85f));
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a2418));
    k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0x90000000));
    k->slider.setMouseDragSensitivity (220);
    k->slider.setTooltip (tip);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    k->slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void DominatrixEditor::placeKnob (Knob& k, juce::Rectangle<int> cell)
{
    k.label.setBounds (cell.removeFromTop (20));
    k.slider.setBounds (cell);
}

void DominatrixEditor::resized()
{
    plates.clear();
    lamp.setBounds (16, 12, 88, 88);
    logoArea  = { 112, 8, 360, 70 };
    badgeArea = { 548, 14, 260, 86 };
    crestArea = { 556, 19, 76, 76 };
    presetArea = { 842, 14, 200, 86 };
    presetBox.setBounds (842, 36, 200, 26);
    prevButton.setBounds (842, 70, 28, 24);
    nextButton.setBounds (874, 70, 28, 24);
    saveButton.setBounds (908, 70, 62, 24);
    deleteButton.setBounds (976, 70, 66, 24);
    stripeArea = { 0, 110, getWidth(), 18 };

    plates.push_back ({ "SATURATION TYPE", { 16, 138, 640, 86 }, true });
    typeSel.setBounds (26, 162, 620, 54);
    plates.push_back ({ "CEILING", { 666, 138, 232, 86 } });
    ceilSel.setBounds (676, 162, 212, 54);
    plates.push_back ({ "QUALITY", { 908, 138, 136, 86 } });
    qualitySel.setBounds (916, 168, 120, 44);

    plates.push_back ({ "GAIN REDUCTION", { 16, 234, 300, 214 } });
    vu.setBounds (28, 258, 276, 178);
    plates.push_back ({ "THE RULES", { 16, 458, 300, 146 } });
    rules.setBounds (28, 482, 276, 110);

    plates.push_back ({ "CONTROLS", { 326, 234, 500, 370 } });
    placeKnob (*disciplineKnob, { 336, 272, 180, 266 });
    const int gx = 528, gw = 98, gh = 168;
    placeKnob (*satKnob,    { gx,          260, gw, gh });
    placeKnob (*analogKnob, { gx + gw,     260, gw, gh });
    placeKnob (*widthKnob,  { gx + gw * 2, 260, gw, gh });
    placeKnob (*mixKnob,    { gx,          260 + gh + 4, gw, gh });
    placeKnob (*outKnob,    { gx + gw,     260 + gh + 4, gw, gh });
    phase.setBounds (gx + gw * 2 + 4, 462, gw - 8, 58);
    bassMonoButton.setBounds (gx + gw * 2 + 2, 530, gw - 4, 34);

    plates.push_back ({ "METERS", { 836, 234, 208, 370 } });
    meterArea = { 848, 260, 184, 206 };
    meters.setBounds (meterArea);
    lufs.setBounds (848, 474, 184, 72);
    matchButton.setBounds (848, 556, 184, 36);

    buildBackdrop();
}

//==============================================================================
void DominatrixEditor::buildBackdrop()
{
    const float scale = 2.0f;
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;
    const int PW = (int) ((float) W * scale), PH = (int) ((float) H * scale);
    juce::Image base (juce::Image::RGB, W, H, false);

    std::vector<juce::Rectangle<float>> pr;
    for (auto& s : plates) pr.push_back (s.bounds.toFloat());
    pr.push_back (badgeArea.toFloat());
    const auto stripe = stripeArea.toFloat();

    {
        juce::Image::BitmapData bd (base, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < H; ++py)
        {
            const float y = (float) py;
            for (int px = 0; px < W; ++px)
            {
                const float x = (float) px;
                tex::RGB col;
                int inside = -1; float edge = 1e9f; bool shadow = false;
                for (int i = 0; i < (int) pr.size(); ++i)
                {
                    const auto& p = pr[(size_t) i];
                    if (p.contains (x, y))
                    {
                        inside = i;
                        edge = std::min ({ x - p.getX(), p.getRight() - x, y - p.getY(), p.getBottom() - y });
                        break;
                    }
                    if (p.translated (3.0f, 4.0f).contains (x, y)) shadow = true;
                }
                if (stripe.contains (x, y))
                {
                    // worn oxblood leather strap
                    const float grainN = tex::fbm (x / 3.0f, y / 3.0f, 3030, 3);
                    const float crease = tex::smoothstep (0.55f, 0.62f, tex::fbm (x / 18.0f, y / 2.0f, 4040, 2));
                    col = tex::mix (tex::rgb (pal::leatherDark), tex::rgb (pal::leather), grainN);
                    col = tex::mul (col, 1.0f - crease * 0.35f);
                    const float wearSpot = tex::smoothstep (0.6f, 0.75f, tex::fbm (x / 40.0f, y / 10.0f, 5050, 3));
                    col = tex::mix (col, tex::rgb (juce::Colour (0xff6a3a2a)), wearSpot * 0.4f);
                    const float t = (y - stripe.getY()) / stripe.getHeight();
                    col = tex::mul (col, 0.75f + 0.35f * std::sin (t * juce::MathConstants<float>::pi));
                }
                else if (inside >= 0)
                {
                    // dark diamond-plate steel, rust creeping in from the edges
                    const float d = tex::fbm (x / 30.0f, y / 30.0f, 211, 3);
                    tex::RGB steel = tex::mix (tex::rgb (juce::Colour (0xff1e1a17)), tex::rgb (juce::Colour (0xff332c27)), d);
                    const float u = std::fmod (x + y + 2000.0f, 16.0f) / 16.0f, v = std::fmod (x - y + 2000.0f, 16.0f) / 16.0f;
                    const float tread = (std::abs (u - 0.5f) < 0.08f && std::abs (v - 0.25f) < 0.18f) || (std::abs (v - 0.5f) < 0.08f && std::abs (u - 0.75f) < 0.18f) ? 1.0f : 0.0f;
                    steel = tex::mix (steel, tex::rgb (juce::Colour (0xff4a4038)), tread * 0.55f);
                    const float edgeRust = (1.0f - juce::jlimit (0.0f, 1.0f, edge / 14.0f)) * tex::smoothstep (0.35f, 0.6f, tex::vnoise (x / 6.0f, y / 6.0f, 17));
                    const float bloom = tex::smoothstep (0.6f, 0.75f, tex::fbm (x / 55.0f, y / 55.0f, 404, 3)) * 0.7f;
                    col = tex::mix (steel, tex::rustAt (x, y, 0.15f, 1337), juce::jlimit (0.0f, 1.0f, edgeRust + bloom));
                    col = tex::mul (col, 0.88f);
                }
                else
                {
                    col = tex::rustAt (x, y, 0.1f, 42);
                    if (shadow) col = tex::mul (col, 0.45f);
                }

                const float streak = tex::fbm (x / 9.0f, y / 240.0f, 909, 3);
                const float drip = tex::smoothstep (0.58f, 0.78f, streak) * (0.25f + 0.5f * y / (float) H);
                col = tex::mul (col, 1.0f - drip * 0.6f);
                const float runoff = tex::smoothstep (0.66f, 0.82f, tex::fbm (x / 5.0f, y / 120.0f, 31337, 2));
                col = tex::mix (col, tex::rgb (pal::rustDeep), runoff * 0.35f);
                const float oil = tex::smoothstep (0.7f, 0.8f, tex::fbm (x / 70.0f, y / 50.0f, 555, 3));
                col = tex::mul (col, 1.0f - oil * 0.45f);
                if (tex::hash (px, py, 5) > 0.988f) col = tex::mul (col, 0.45f);
                const float grime = 0.72f + 0.28f * tex::fbm (x / 260.0f, y / 260.0f, 77, 2);
                const float dx = (x / (float) W - 0.5f) * 2.0f, dy = (y / (float) H - 0.5f) * 2.0f;
                const float vig = 1.0f - 0.45f * std::min (1.0f, (dx * dx + dy * dy) * 0.6f);
                col = tex::mul (col, grime * vig);
                bd.setPixelColour (px, py, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, col.r), juce::jlimit (0.0f, 1.0f, col.g),
                                                                        juce::jlimit (0.0f, 1.0f, col.b), 1.0f));
            }
        }
    }

    backdrop = juce::Image (juce::Image::RGB, PW, PH, false);
    juce::Graphics g (backdrop);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (base, 0, 0, PW, PH, 0, 0, W, H);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random rnd (1991);

    auto rivet = [&] (float cx, float cy, float rad)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (cx - rad + 1, cy - rad + 1.5f, rad * 2, rad * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a7a68), cx - rad * 0.4f, cy - rad * 0.5f, juce::Colour (0xff2a201a), cx + rad, cy + rad, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
        g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (cx - rad, cy - rad, rad * 2, rad * 2, 1.2f);
        g.setColour (pal::rustDeep.withAlpha (0.35f)); g.fillRect (cx - 0.8f, cy + rad, 1.6f, 3.0f + rnd.nextFloat() * 9.0f);
    };
    auto plateFrame = [&] (juce::Rectangle<float> p, bool hot)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawLine (p.getX() + 2, p.getY() + 1, p.getRight() - 2, p.getY() + 1, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.drawRect (p, 1.0f);
        if (hot) { g.setColour (pal::signal.withAlpha (0.45f)); g.drawRect (p.reduced (2.0f), 1.5f); }
        rivet (p.getX() + 6, p.getY() + 6, 3.0f); rivet (p.getRight() - 6, p.getY() + 6, 3.0f);
        rivet (p.getX() + 6, p.getBottom() - 6, 3.0f); rivet (p.getRight() - 6, p.getBottom() - 6, 3.0f);
    };

    // scratches
    for (int i = 0; i < 300; ++i)
    {
        const float x = rnd.nextFloat() * (float) W, y = rnd.nextFloat() * (float) H;
        const float len = 6.0f + rnd.nextFloat() * 70.0f, ang = rnd.nextFloat() * juce::MathConstants<float>::pi;
        g.setColour ((rnd.nextBool() ? juce::Colour (0xffd0c0a8) : juce::Colours::black).withAlpha (0.04f + rnd.nextFloat() * 0.1f));
        g.drawLine (x, y, x + std::cos (ang) * len, y + std::sin (ang) * len, 0.5f + rnd.nextFloat() * 0.6f);
    }

    int seed = 10;
    for (auto& s : plates)
    {
        const auto p = s.bounds.toFloat();
        plateFrame (p, s.hot);
        auto title = tex::distressedText (s.title, s.bounds.getWidth() - 40, 18, tex::stencil (15.0f), s.hot ? pal::signalHot : pal::paint,
                                          juce::Justification::centredLeft, scale, 0.2f, seed++, 0.12f);
        g.setOpacity (1.0f);
        g.drawImage (title, juce::Rectangle<float> (p.getX() + 14, p.getY() + 3, (float) s.bounds.getWidth() - 40, 18.0f));
    }
    // stamped divider between DISCIPLINE and the small knobs, and the hint under the big knob
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawLine (522.0f, 258.0f, 522.0f, 592.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawLine (523.5f, 258.0f, 523.5f, 592.0f, 1.0f);
    {
        auto hint1 = tex::distressedText ("ONE KNOB.", 180, 14, tex::stencil (13.0f), pal::signalHot.withAlpha (0.9f), juce::Justification::centred, scale, 0.18f, 61, 0.2f);
        auto hint2 = tex::distressedText ("SHE HANDLES THE REST.", 180, 14, tex::plainBold (9.5f), pal::paint.withAlpha (0.75f), juce::Justification::centred, scale, 0.18f, 62, 0.14f);
        g.setOpacity (1.0f);
        g.drawImage (hint1, juce::Rectangle<float> (336.0f, 550.0f, 180.0f, 14.0f));
        g.drawImage (hint2, juce::Rectangle<float> (336.0f, 566.0f, 180.0f, 14.0f));
    }
    {   // a stamped STEREO tag over the phase meter + bass mono switch
        auto st = tex::distressedText ("STEREO", 90, 16, tex::stencil (15.0f), pal::paint, juce::Justification::centred, scale, 0.18f, 63, 0.12f);
        g.setOpacity (1.0f);
        g.drawImage (st, juce::Rectangle<float> ((float) phase.getX() - 1.0f, 436.0f, 90.0f, 16.0f));
    }
    // screen wells
    for (auto rr : { meterArea, vu.getBounds(), rules.getBounds(), lufs.getBounds(), phase.getBounds() })
    {
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRoundedRectangle (rr.toFloat().expanded (4.0f), 5.0f);
    }

    // leather strap: stitching + chrome studs
    {
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawLine (0, stripe.getY(), (float) W, stripe.getY(), 1.5f);
        g.drawLine (0, stripe.getBottom(), (float) W, stripe.getBottom(), 1.5f);
        juce::Path stitch;
        for (float yy : { stripe.getY() + 3.0f, stripe.getBottom() - 3.0f })
        {
            stitch.startNewSubPath (0, yy); stitch.lineTo ((float) W, yy);
        }
        juce::Path dashed; const float dl[] = { 5.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, stitch, dl, 2);
        g.setColour (pal::paint.withAlpha (0.45f)); g.fillPath (dashed);
        for (float x = 22.0f; x < (float) W; x += 34.0f)
        {
            const float cy = stripe.getCentreY(), sr = 4.2f;
            g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (x - sr + 1.0f, cy - sr + 1.5f, sr * 2, sr * 2);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e0d4), x - sr * 0.4f, cy - sr * 0.5f, juce::Colour (0xff3a3430), x + sr, cy + sr, true));
            g.fillEllipse (x - sr, cy - sr, sr * 2, sr * 2);
            if (rnd.nextFloat() < 0.5f) { g.setColour (pal::rust.withAlpha (0.5f)); g.drawEllipse (x - sr, cy - sr, sr * 2, sr * 2, 1.0f); }
            g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillEllipse (x - sr * 0.45f, cy - sr * 0.55f, sr * 0.6f, sr * 0.45f);
        }
    }

    // CASTLE DANGER MUSIC COMPANY badge with the castle crest
    const auto badge = badgeArea.toFloat();
    plateFrame (badge, false);
    g.setColour (pal::paint.withAlpha (0.22f)); g.drawRect (badge.reduced (5.0f), 1.0f);
    drawCastleCrest (g, crestArea.toFloat());
    const float tx = (float) crestArea.getRight() + 8.0f, tw = badge.getRight() - tx - 10.0f;
    auto l1 = tex::distressedText ("CASTLE DANGER", (int) tw, 28, tex::stencil (25.0f), juce::Colour (0xffeadfc4), juce::Justification::centredLeft, scale, 0.24f, 4242, 0.08f);
    auto l2 = tex::distressedText ("MUSIC COMPANY", (int) tw, 20, tex::stencil (17.0f), pal::rustLight, juce::Justification::centredLeft, scale, 0.24f, 4343, 0.2f);
    auto l3 = tex::distressedText ("WE MAKE MUSIC", (int) tw, 14, tex::plainBold (9.0f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 4444, 0.3f);
    g.setColour (juce::Colours::black);
    g.setOpacity (0.7f); g.drawImage (l1, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 13.0f, tw, 28.0f));
    g.setOpacity (1.0f); g.drawImage (l1, juce::Rectangle<float> (tx, badge.getY() + 11.0f, tw, 28.0f));
    g.setOpacity (0.7f); g.drawImage (l2, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 40.0f, tw, 20.0f));
    g.setOpacity (1.0f); g.drawImage (l2, juce::Rectangle<float> (tx, badge.getY() + 38.0f, tw, 20.0f));
    g.drawImage (l3, juce::Rectangle<float> (tx + 1.0f, badge.getY() + 60.0f, tw, 14.0f));

    // stamped subtitle under the logo
    auto sub = tex::distressedText ("MASTER BUS GLUE COMPRESSOR  //  V" + juce::String (JucePlugin_VersionString),
                                    350, 14, tex::plainBold (10.5f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 99, 0.14f);
    g.setOpacity (1.0f);
    g.drawImage (sub, juce::Rectangle<float> (114.0f, 86.0f, 350.0f, 14.0f));

    // PRESET label
    auto pl = tex::distressedText ("PRESET", 120, 16, tex::stencil (14.0f), pal::paint, juce::Justification::centredLeft, scale, 0.2f, 7, 0.15f);
    g.setOpacity (1.0f);
    g.drawImage (pl, juce::Rectangle<float> ((float) presetArea.getX() + 2, (float) presetArea.getY() + 2, 120.0f, 16.0f));

    // the logo: DOMINATRIX in rusty red with a cracking whip lash under it (drawn live so it can flicker)
    {
        const int lw = logoArea.getWidth(), lh = logoArea.getHeight() + 10;
        logoImage = juce::Image (juce::Image::ARGB, (int) ((float) lw * scale), (int) ((float) lh * scale), true);
        juce::Graphics lg (logoImage);
        lg.addTransform (juce::AffineTransform::scale (scale));

        // the lash: a tapered curve sweeping under the word, cracking at the tip
        {
            juce::Path spine;
            spine.startNewSubPath (4.0f, 58.0f);
            spine.cubicTo ((float) lw * 0.30f, 74.0f, (float) lw * 0.62f, 50.0f, (float) lw - 26.0f, 66.0f);
            juce::Path lash;
            const int segs = 60;
            juce::Array<juce::Point<float>> top, bot;
            for (int i = 0; i <= segs; ++i)
            {
                const float t = (float) i / (float) segs;
                const auto p0 = spine.getPointAlongPath (t * spine.getLength());
                const auto p1 = spine.getPointAlongPath (juce::jmin (1.0f, t + 0.01f) * spine.getLength());
                auto dir = p1 - p0; const float len = std::max (0.001f, dir.getDistanceFromOrigin());
                const juce::Point<float> nrm (-dir.y / len, dir.x / len);
                const float wdt = 4.6f * (1.0f - t) + 0.5f;
                top.add (p0 + nrm * wdt); bot.add (p0 - nrm * wdt);
            }
            lash.startNewSubPath (top[0]);
            for (auto& p : top) lash.lineTo (p);
            for (int i = bot.size() - 1; i >= 0; --i) lash.lineTo (bot[i]);
            lash.closeSubPath();
            lg.setColour (juce::Colours::black.withAlpha (0.6f)); lg.fillPath (lash, juce::AffineTransform::translation (1.5f, 2.0f));
            lg.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a3020), 0, 52, juce::Colour (0xff1e0c08), 0, 74, false));
            lg.fillPath (lash);
            lg.setColour (pal::rustLight.withAlpha (0.45f)); lg.strokePath (lash, juce::PathStrokeType (0.6f));
            // braid marks
            lg.setColour (juce::Colours::black.withAlpha (0.55f));
            for (int i = 2; i < segs - 10; i += 2)
                lg.drawLine (juce::Line<float> (top[i], bot[i + 1]), 0.6f);
            // the crack
            const auto tipPt = spine.getPointAlongPath (spine.getLength());
            juce::Random cr (9);
            for (int i = 0; i < 9; ++i)
            {
                const float a = -0.9f + cr.nextFloat() * 1.8f, l = 5.0f + cr.nextFloat() * 9.0f;
                lg.setColour ((i % 2 ? pal::hazard : pal::signalHot).withAlpha (0.85f));
                lg.drawLine (tipPt.x + std::cos (a) * 3.0f, tipPt.y + std::sin (a) * 3.0f, tipPt.x + std::cos (a) * l, tipPt.y + std::sin (a) * l, 1.1f);
            }
        }

        auto f = tex::stencil (60.0f).withExtraKerningFactor (0.05f);
        {
            juce::GlyphArrangement all; all.addLineOfText (f, "DOMINATRIX", 0, 0);
            const float wAll = all.getBoundingBox (0, -1, true).getWidth();
            if (wAll > (float) lw - 6.0f) f = f.withHeight (60.0f * ((float) lw - 6.0f) / wAll);
        }
        juce::Image word (juce::Image::ARGB, (int) ((float) lw * scale), (int) ((float) lh * scale), true);
        {
            juce::Graphics wg (word);
            wg.addTransform (juce::AffineTransform::scale (scale));
            wg.setFont (f);
            wg.setColour (pal::signal);
            wg.drawText ("DOMINATRIX", 0, 0, lw, 58, juce::Justification::centredLeft, false);
        }
        tex::distress (word, scale, 0.3f, 666);
        lg.drawImage (word, juce::Rectangle<float> (0.0f, 0.0f, (float) lw, (float) lh));
    }
}

void DominatrixEditor::paint (juce::Graphics& g)
{
    if (backdrop.isValid()) g.drawImage (backdrop, getLocalBounds().toFloat());
    else g.fillAll (pal::ironDark);

    if (logoImage.isValid())
    {
        const float alpha = juce::jlimit (0.45f, 1.0f, 1.0f - flicker * 0.6f);
        const auto la = logoArea.toFloat().withHeight ((float) logoArea.getHeight() + 10.0f);
        g.setColour (juce::Colours::black);
        g.setOpacity (0.65f); g.drawImage (logoImage, la.translated (2.5f, 3.5f));
        g.setOpacity (alpha); g.drawImage (logoImage, la);
        g.setOpacity (1.0f);
    }
}

void DominatrixEditor::timerCallback()
{
    auto& e = proc.getEngine();
    const float in = e.inPeak.exchange (0.0f), out = e.outPeak.exchange (0.0f);
    const float cl = e.clipDb.exchange (0.0f), gr = e.grDb.exchange (0.0f);
    inMeter = std::max (in, inMeter * 0.86f);
    outMeter = std::max (out, outMeter * 0.86f);
    clipMeter = std::max (cl, clipMeter - 0.5f);
    if (cl >= clipHold) { clipHold = cl; holdTimer = 45; }
    else if (--holdTimer <= 0) clipHold = std::max (0.0f, clipHold - 0.3f);
    grSmooth = std::max (gr, grSmooth * 0.8f);

    const float disc = proc.apvts.getRawParameterValue ("discipline")->load() * 0.01f;
    vu.active = disc > 0.0f;
    vu.setGr (grSmooth, 1.0f / 30.0f);
    const float targetHeat = juce::jlimit (0.0f, 1.0f, grSmooth / 6.0f + clipMeter * 0.08f);
    lamp.heat += (targetHeat - lamp.heat) * 0.25f;
    lamp.repaint();

    rules.thresh = e.threshDb.load(); rules.ratio = e.ratioNow.load(); rules.attack = e.attackMs.load();
    rules.release = e.releaseMs.load(); rules.makeup = e.makeupDb.load(); rules.discipline = disc;
    rules.repaint();

    meters.in = inMeter; meters.out = outMeter; meters.clip = clipMeter; meters.hold = clipHold;
    meters.clipperOff = (int) proc.apvts.getRawParameterValue ("ceiling")->load() == dom::ceilOff;
    meters.match = matchButton.getToggleState();
    meters.matchDb = e.matchDb.load();
    meters.repaint();

    const float corr = e.correlation.load();
    phaseSmooth += (corr - phaseSmooth) * 0.3f;
    phase.value = phaseSmooth;
    phase.repaint();

    auto& L = e.getLoudness();
    lufs.shortTerm = L.getShortTerm();
    lufs.integrated = L.computeIntegrated();
    lufs.repaint();

    auto& rng = juce::Random::getSystemRandom();
    const float before = flicker;
    flicker = (rng.nextFloat() < 0.015f + juce::jlimit (0.0f, 0.25f, clipMeter * 0.04f)) ? rng.nextFloat() : flicker * 0.5f;
    if (std::abs (flicker - before) > 0.01f) repaint (logoArea.expanded (6, 8));

    if (proc.presetVersion.load() != lastPresetVersion) refreshPresetBox();
}

//==============================================================================
void DominatrixEditor::refreshPresetBox()
{
    lastPresetVersion = proc.presetVersion.load();
    userFiles = DominatrixProcessor::getUserPresets();
    const auto current = proc.getCurrentPresetName();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.addSeparator();
    presetBox.addSectionHeading ("User Presets");
    if (userFiles.isEmpty()) { presetBox.addItem ("(none saved yet)", 999); presetBox.setItemEnabled (999, false); }
    for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), kUserIdBase + i);

    int sel = 0;
    for (int i = 0; i < userFiles.size(); ++i)
        if (userFiles[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (current)) sel = kUserIdBase + i;
    if (sel == 0 && proc.getProgramName (proc.getCurrentProgram()) == current) sel = proc.getCurrentProgram() + 1;
    if (sel != 0) presetBox.setSelectedId (sel, juce::dontSendNotification);
    else presetBox.setText (current, juce::dontSendNotification);
    deleteButton.setEnabled (sel >= kUserIdBase);
}

void DominatrixEditor::stepPreset (int delta)
{
    std::vector<int> ids;
    for (int i = 0; i < proc.getNumPrograms(); ++i) ids.push_back (i + 1);
    for (int i = 0; i < userFiles.size(); ++i) ids.push_back (kUserIdBase + i);
    auto it = std::find (ids.begin(), ids.end(), presetBox.getSelectedId());
    const int pos = it == ids.end() ? 0 : (int) (it - ids.begin());
    const int n = (int) ids.size();
    presetBox.setSelectedId (ids[(size_t) (((pos + delta) % n + n) % n)], juce::sendNotificationSync);
}

void DominatrixEditor::showDialog (const juce::String& title, const juce::String& message, bool withTextField, const juce::String& initialText,
                                 juce::StringArray buttons, std::function<void (int, const juce::String&)> onResult)
{
    juce::Component::SafePointer<DominatrixEditor> safe (this);
    dialog = std::make_unique<DomDialog> (title, message, withTextField, initialText, buttons,
        [safe, onResult] (int index, const juce::String& text)
        {
            juce::MessageManager::callAsync ([safe, onResult, index, text]
            {
                if (safe == nullptr) return;
                safe->dialog.reset();
                if (onResult) onResult (index, text);
            });
        });
    addAndMakeVisible (*dialog);
    dialog->setBounds (getLocalBounds());
}

void DominatrixEditor::showMessage (const juce::String& title, const juce::String& text)
{
    showDialog (title, text, false, {}, { "OK" }, {});
}

void DominatrixEditor::promptSave()
{
    const bool onUser = presetBox.getSelectedId() >= kUserIdBase;
    juce::Component::SafePointer<DominatrixEditor> safe (this);
    auto doSave = [safe] (const juce::String& name)
    {
        if (safe == nullptr) return;
        if (! safe->proc.saveUserPreset (name))
            safe->showMessage ("COULDN'T SAVE", "Dominatrix couldn't write the preset file to:\n" + DominatrixProcessor::getUserPresetFolder().getFullPathName());
        else safe->refreshPresetBox();
    };
    showDialog ("SAVE PRESET", "Name your preset:", true, onUser ? proc.getCurrentPresetName() : juce::String(), { "Save", "Cancel" },
        [safe, doSave] (int button, const juce::String& text)
        {
            if (button != 0 || safe == nullptr) return;
            const auto name = text.trim();
            if (name.isEmpty()) { safe->showMessage ("SAVE PRESET", "Please enter a name for the preset."); return; }
            for (int i = 0; i < safe->proc.getNumPrograms(); ++i)
                if (safe->proc.getProgramName (i).equalsIgnoreCase (name))
                {
                    safe->showMessage ("SAVE PRESET", "\"" + name + "\" is a factory preset name. Please choose another name.");
                    return;
                }
            if (DominatrixProcessor::fileForPresetName (name).existsAsFile())
            {
                safe->showDialog ("REPLACE PRESET?", "A preset named \"" + name + "\" already exists. Replace it with the current settings?",
                                  false, {}, { "Replace", "Cancel" },
                                  [doSave, name] (int b, const juce::String&) { if (b == 0) doSave (name); });
                return;
            }
            doSave (name);
        });
}

void DominatrixEditor::confirmDelete()
{
    const int id = presetBox.getSelectedId();
    if (id < kUserIdBase || id - kUserIdBase >= userFiles.size()) return;
    const auto file = userFiles[id - kUserIdBase];
    juce::Component::SafePointer<DominatrixEditor> safe (this);
    showDialog ("DELETE PRESET?", "Delete \"" + file.getFileNameWithoutExtension() + "\"? This can't be undone.", false, {},
                { "Delete", "Cancel" }, [safe, file] (int b, const juce::String&)
                {
                    if (b != 0 || safe == nullptr) return;
                    file.deleteFile();
                    safe->proc.setCurrentProgram (0);
                    safe->refreshPresetBox();
                });
}

//==============================================================================
DomDialog::DomDialog (const juce::String& t, const juce::String& m, bool withTextField, const juce::String& initialText,
                        juce::StringArray names, std::function<void (int, const juce::String&)> cb)
    : title (t), message (m), hasField (withTextField), onClose (std::move (cb))
{
    setWantsKeyboardFocus (true);
    if (hasField)
    {
        field.setText (initialText, false);
        field.setInputRestrictions (60);
        field.setSelectAllWhenFocused (true);
        field.setFont (tex::plainBold (15.0f));
        field.setIndents (8, 6);
        field.onReturnKey = [this] { finish (0); };
        field.onEscapeKey = [this] { finish ((int) buttons.size() - 1); };
        addAndMakeVisible (field);
    }
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { finish (i); };
        addAndMakeVisible (b);
    }
}

void DomDialog::parentHierarchyChanged()
{
    if (isShowing())
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<DomDialog> (this)]
        {
            if (sp == nullptr) return;
            if (sp->hasField) sp->field.grabKeyboardFocus(); else sp->grabKeyboardFocus();
        });
}

void DomDialog::finish (int index)
{
    if (onClose == nullptr) return;
    auto cb = std::move (onClose);
    onClose = nullptr;
    cb (index, field.getText());
}

bool DomDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey) { finish (0); return true; }
    if (k == juce::KeyPress::escapeKey) { finish ((int) buttons.size() - 1); return true; }
    return true;
}

void DomDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (400, hasField ? 196 : 166);
    auto r = panel.reduced (22);
    r.removeFromTop (30);
    r.removeFromTop (hasField ? 22 : 50);
    if (hasField) { field.setBounds (r.removeFromTop (32)); r.removeFromTop (16); }
    auto row = r.removeFromBottom (28);
    for (int i = (int) buttons.size() - 1; i >= 0; --i) { buttons[i]->setBounds (row.removeFromRight (96)); row.removeFromRight (8); }
}

void DomDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    auto p = panel.toFloat();
    g.setColour (juce::Colour (0xff1a1411)); g.fillRect (p);
    g.setGradientFill (juce::ColourGradient (pal::rustDeep.withAlpha (0.5f), p.getX(), p.getY(), juce::Colours::transparentBlack, p.getX(), p.getY() + p.getHeight() * 0.6f, false));
    g.fillRect (p);
    g.setColour (pal::rust); g.drawRect (p, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawRect (p.reduced (2.0f), 1.0f);
    for (auto pt : { juce::Point<float> (p.getX() + 8, p.getY() + 8), { p.getRight() - 8, p.getY() + 8 }, { p.getX() + 8, p.getBottom() - 8 }, { p.getRight() - 8, p.getBottom() - 8 } })
    {
        g.setColour (juce::Colour (0xff6a5a4c)); g.fillEllipse (pt.x - 3, pt.y - 3, 6, 6);
        g.setColour (pal::rust.withAlpha (0.6f)); g.drawEllipse (pt.x - 3, pt.y - 3, 6, 6, 1.0f);
    }
    auto r = panel.reduced (22);
    g.setColour (pal::signalHot); g.setFont (tex::stencil (19.0f).withExtraKerningFactor (0.1f));
    g.drawText (title, r.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (pal::paint); g.setFont (tex::plainBold (13.0f));
    g.drawFittedText (message, r.removeFromTop (hasField ? 22 : 50), juce::Justification::topLeft, 3);
}
