#include "layout/Accidentals.h"

#include <algorithm>
#include <cstdlib>

namespace msd
{

int keyAlter(Step step, int keyAccidentals)
{
    static constexpr Step sharps[] = { Step::F, Step::C, Step::G, Step::D, Step::A, Step::E, Step::B };
    static constexpr Step flats[] = { Step::B, Step::E, Step::A, Step::D, Step::G, Step::C, Step::F };
    const int count = std::min(std::abs(keyAccidentals), 7);
    const Step* order = keyAccidentals > 0 ? sharps : flats;
    for (int i = 0; i < count; ++i)
        if (order[i] == step)
            return keyAccidentals > 0 ? 1 : -1;
    return 0;
}

std::string accidentalGlyph(int alter)
{
    switch (alter)
    {
    case -2: return "accidentalDoubleFlat";
    case -1: return "accidentalFlat";
    case 1: return "accidentalSharp";
    case 2: return "accidentalDoubleSharp";
    default: return "accidentalNatural";
    }
}

std::optional<AccidentalMark> MeasureAccidentals::next(const Pitch& pitch, std::optional<AccidentalDisplay> display,
                                                       bool continuesTie)
{
    const int step = pitch.octave * 7 + static_cast<int>(pitch.step);
    const auto it = altered.find(step);
    const int inForce = it != altered.end() ? it->second : keyAlter(pitch.step, key);

    bool show = !continuesTie && pitch.alter != inForce;
    bool parentheses = false;
    if (display == AccidentalDisplay::Show)
        show = true;
    else if (display == AccidentalDisplay::Hide)
        show = false;
    else if (display == AccidentalDisplay::Cautionary)
        show = parentheses = true;

    // A tied-over note carries its alteration without restating it, so the next note
    // on that step still needs its own accidental.
    if (!continuesTie)
        altered[step] = pitch.alter;

    if (!show)
        return std::nullopt;
    return AccidentalMark { pitch.alter, parentheses };
}

} // namespace msd
