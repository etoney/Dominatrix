#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

namespace dom
{
struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };

// satType: 0 Tape, 1 Tube, 2 Iron, 3 Console, 4 Whip | ceiling: 0 = -1 dB, 1 = -6 dB, 2 = Off | quality: 0 2x, 1 4x, 2 8x
// Anything not listed uses the default (DISCIPLINE 30, SATURATION 20 Tape, ANALOG 15, MIX 100, OUTPUT 0, CEILING -1 dB).
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> p {
        { "Init",                      { } },
        { "Gentle Discipline",         { { "discipline", 22 }, { "saturation", 10 }, { "analog", 10 } } },
        { "Mix Bus Glue (-6 Headroom)",{ { "discipline", 35 }, { "satType", 3 }, { "saturation", 15 }, { "analog", 15 }, { "ceiling", 1 } } },
        { "Streaming Master",          { { "discipline", 40 }, { "saturation", 20 }, { "analog", 10 }, { "outputDb", 3.0f } } },
        { "Loud & Proud",              { { "discipline", 55 }, { "saturation", 30 }, { "analog", 20 }, { "outputDb", 5.0f } } },
        { "Tube Warmth",               { { "discipline", 35 }, { "satType", 1 }, { "saturation", 45 }, { "analog", 25 } } },
        { "Iron Fist",                 { { "discipline", 60 }, { "satType", 2 }, { "saturation", 50 }, { "analog", 30 }, { "outputDb", 3.0f } } },
        { "Console Summing",           { { "discipline", 25 }, { "satType", 3 }, { "saturation", 35 }, { "analog", 40 } } },
        { "Leather & Lace",            { { "discipline", 40 }, { "saturation", 30 }, { "analog", 35 }, { "mix", 70 } } },
        { "Crack the Whip",            { { "discipline", 50 }, { "satType", 4 }, { "saturation", 40 }, { "analog", 20 }, { "outputDb", 3.0f } } },
        { "Tape Machine",              { { "discipline", 20 }, { "saturation", 45 }, { "analog", 70 } } },
        { "Vintage Broadcast",         { { "discipline", 75 }, { "satType", 1 }, { "saturation", 35 }, { "analog", 60 }, { "outputDb", 2.0f } } },
        { "New York Parallel",         { { "discipline", 90 }, { "satType", 2 }, { "saturation", 30 }, { "mix", 45 }, { "outputDb", 2.0f } } },
        { "Total Control",             { { "discipline", 100 }, { "satType", 1 }, { "saturation", 25 }, { "analog", 20 }, { "outputDb", 4.0f } } },
        { "Safe Word (Clipper Only)",  { { "discipline", 0 }, { "saturation", 0 }, { "analog", 0 } } },
    };
    return p;
}
} // namespace dom
