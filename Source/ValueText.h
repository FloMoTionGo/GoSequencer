#pragma once

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

//==============================================================================
/** Typed values back into numbers: what a parameter makes of the text someone
    types into its value box, in the editor or in the host.

    Without these JUCE falls back to getFloatValue() / getIntValue(), which read
    "50" on the gate as 50 (so 100 % once clamped), a note name as 0, "+7" as 0
    and "hold" as 0. Each function here returns nothing for text that is not a
    value, so the caller can keep what it had rather than jump to the minimum.

    Plain C++ and JUCE-free, so tests/GoRulesTests.cpp checks it directly. */
namespace valuetext
{
    inline std::string trimmedLower (const std::string& text)
    {
        const auto first = text.find_first_not_of (" \t\r\n");

        if (first == std::string::npos)
            return {};

        const auto last = text.find_last_not_of (" \t\r\n");
        std::string out = text.substr (first, last - first + 1);

        for (auto& c : out)
            if (c >= 'A' && c <= 'Z')
                c = (char) (c - 'A' + 'a');

        return out;
    }

    inline bool endsWith (const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size()
            && text.compare (text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    /** A plain decimal - optional sign, digits, optional point - followed by at
        most one of the given unit words ("50%", "+7 st", "96 bpm"). */
    inline std::optional<double> number (const std::string& text, const std::vector<std::string>& units = {})
    {
        auto t = trimmedLower (text);

        for (const auto& unit : units)
        {
            if (endsWith (t, unit))
            {
                t = trimmedLower (t.substr (0, t.size() - unit.size()));
                break;
            }
        }

        if (t.empty())
            return std::nullopt;

        size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0;
        int digits = 0, points = 0;

        for (; i < t.size(); ++i)
        {
            if (t[i] >= '0' && t[i] <= '9')  ++digits;
            else if (t[i] == '.')            ++points;
            else                             return std::nullopt;
        }

        if (digits == 0 || points > 1)
            return std::nullopt;

        return std::strtod (t.c_str(), nullptr);
    }

    /** As number(), rounded to the nearest whole one. */
    inline std::optional<int> wholeNumber (const std::string& text, const std::vector<std::string>& units = {})
    {
        if (auto v = number (text, units))
            return (int) std::lround (*v);

        return std::nullopt;
    }

    /** A MIDI note: a number, or a name - C3, C#3, Db3, e-1 - with middle C
        (60) in octave 3, the way JUCE writes them here. */
    inline std::optional<int> note (const std::string& text)
    {
        if (auto v = wholeNumber (text))
            return *v;

        const auto t = trimmedLower (text);

        if (t.size() < 2 || t[0] < 'a' || t[0] > 'g')
            return std::nullopt;

        static constexpr int pitchClass[7] = { 9, 11, 0, 2, 4, 5, 7 };     //  a b c d e f g
        int pitch = pitchClass[t[0] - 'a'];
        size_t i = 1;

        if (t[i] == '#')        { ++pitch; ++i; }
        else if (t[i] == 'b' && i + 1 < t.size())
                                { --pitch; ++i; }

        const auto octave = wholeNumber (t.substr (i));

        if (! octave || t.find ('.', i) != std::string::npos)
            return std::nullopt;

        return (*octave + 2) * 12 + pitch;
    }

    /** A fraction typed as a percentage: "50" and "50%" are a half, and so is
        "0.5" - a value with a point and no sign of a percent, at most 1. */
    inline std::optional<double> percent (const std::string& text)
    {
        const auto t = trimmedLower (text);
        const auto v = number (t, { "%" });

        if (! v)
            return std::nullopt;

        const bool fraction = t.find ('%') == std::string::npos && t.find ('.') != std::string::npos && *v <= 1.0;
        return fraction ? *v : *v / 100.0;
    }

    /** Stone life: a count of steps, or "hold" for the top of the range. */
    inline std::optional<int> life (const std::string& text, int holdValue)
    {
        const auto t = trimmedLower (text);

        if (t == "hold" || t == "h" || t == "inf")
            return holdValue;

        return wholeNumber (t, { "placements", "placement", "steps", "step" });
    }

    /** One of a choice parameter's names, ignoring case and spaces: "1/8t" is
        "1/8T", "2bars" is "2 bars". */
    inline std::optional<int> choice (const std::string& text, const std::vector<std::string>& names)
    {
        const auto squash = [] (const std::string& s)
        {
            std::string out;

            for (char c : trimmedLower (s))
                if (c != ' ')
                    out += c;

            return out;
        };

        const auto wanted = squash (text);

        for (size_t i = 0; i < names.size(); ++i)
            if (squash (names[i]) == wanted)
                return (int) i;

        return std::nullopt;
    }
}
