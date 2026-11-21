#include "layout/Transpose.h"

namespace msd
{
namespace
{

int semitonesAboveC(Step step)
{
    static constexpr int semitones[] = { 0, 2, 4, 5, 7, 9, 11 }; // C D E F G A B
    return semitones[static_cast<int>(step)];
}

int midiNumber(const Pitch& pitch) { return (pitch.octave + 1) * 12 + semitonesAboveC(pitch.step) + pitch.alter; }

int floorDiv(int a, int b) { return a / b - (a % b != 0 && (a < 0) != (b < 0) ? 1 : 0); }

} // namespace

Pitch writtenPitch(const Pitch& sounding, const std::optional<Transposition>& transposition)
{
    if (!transposition)
        return sounding;
    const int diatonic = -(transposition->diatonic + 7 * transposition->octave);
    const int chromatic = -(transposition->chromatic + 12 * transposition->octave);
    const int index = sounding.octave * 7 + static_cast<int>(sounding.step) + diatonic;
    Pitch written;
    written.octave = floorDiv(index, 7);
    written.step = static_cast<Step>(index - written.octave * 7);
    const Pitch natural { .step = written.step, .alter = 0, .octave = written.octave };
    written.alter = midiNumber(sounding) + chromatic - midiNumber(natural);
    return written;
}

int writtenKey(int soundingAccidentals, const std::optional<Transposition>& transposition)
{
    if (!transposition)
        return soundingAccidentals;
    // Each semitone up adds 7 sharps, each diatonic step up takes 12 away (mod 12).
    int result = soundingAccidentals - (7 * transposition->chromatic - 12 * transposition->diatonic);
    while (result > 7)
        result -= 12;
    while (result < -7)
        result += 12;
    return result;
}

} // namespace msd
