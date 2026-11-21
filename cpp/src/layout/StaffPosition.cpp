#include "layout/StaffPosition.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace msd
{
namespace
{

// Diatonic steps from C0: C4 = 28, G4 = 32.
int diatonicStep(Step step, int octave) { return octave * 7 + static_cast<int>(step); }

struct ClefReference
{
    double y = 0;      // where the reference pitch sits
    int step = 0;      // the reference pitch, as a diatonic step
};

ClefReference reference(const Clef& clef, int lines)
{
    const int octaveShift = clef.octaveChange * 7;
    switch (clef.type)
    {
    case ClefType::F: return { static_cast<double>(lines - clef.line), diatonicStep(Step::F, 3) + octaveShift };
    case ClefType::C: return { static_cast<double>(lines - clef.line), diatonicStep(Step::C, 4) + octaveShift };
    case ClefType::G: return { static_cast<double>(lines - clef.line), diatonicStep(Step::G, 4) + octaveShift };
    case ClefType::Percussion: break;
    }
    return { static_cast<double>(lines - 2), diatonicStep(Step::G, 4) }; // as a treble clef
}

double stepY(int step, const ClefReference& ref) { return ref.y - (step - ref.step) * 0.5; }

} // namespace

double middleLineY(int lines) { return (lines - 1) * 0.5; }

double clefY(const Clef& clef, int lines)
{
    if (clef.type == ClefType::Percussion)
        return middleLineY(lines);
    return static_cast<double>(lines - clef.line);
}

double pitchY(const Pitch& pitch, const Clef& clef, int lines)
{
    return stepY(diatonicStep(pitch.step, pitch.octave), reference(clef, lines));
}

double unpitchedY(int displayPitch, int lines) { return middleLineY(lines) - displayPitch * 0.5; }

std::vector<double> keySignatureYs(int accidentals, const Clef& clef, int lines)
{
    static constexpr Step sharps[] = { Step::F, Step::C, Step::G, Step::D, Step::A, Step::E, Step::B };
    static constexpr Step flats[] = { Step::B, Step::E, Step::A, Step::D, Step::G, Step::C, Step::F };

    const ClefReference ref = reference(clef, lines);

    // How far this clef moves the treble pattern: where F sits, folded into one octave
    // around the top line. Treble 0, bass 1, alto 0.5, tenor -0.5.
    double shift = std::fmod(stepY(diatonicStep(Step::F, 5), ref), 3.5);
    if (shift >= 1.75)
        shift -= 3.5;
    if (shift < -1.75)
        shift += 3.5;

    // Each accidental goes in the one octave whose position falls in a window of
    // 7 steps: treble sharps from G5 (y -0.5) down to A4, flats from E5 down to F4.
    double windowTop = (accidentals > 0 ? -0.5 : 0.5) + shift;
    if (accidentals > 0 && windowTop < -0.5)
        windowTop += 1; // tenor clef: sharps start low, not above the staff

    std::vector<double> ys;
    const int count = std::min(std::abs(accidentals), 7);
    for (int i = 0; i < count; ++i)
    {
        const Step step = accidentals > 0 ? sharps[i] : flats[i];
        double y = stepY(diatonicStep(step, 4), ref);
        while (y < windowTop - 1e-9)
            y += 3.5;
        while (y > windowTop + 3 + 1e-9)
            y -= 3.5;
        ys.push_back(y);
    }
    return ys;
}

} // namespace msd
