# Cinematic Studio VST Rules
- legato always on
- marcato repetition overlay always on
- CC1 is dynamics (except where a rule below takes it over)
- every track in the DAW template has a -66ms track delay; notes are not moved ahead for it
- piccolo plays an octave lower than it sounds; tuba, contrabass and timpani an octave higher
- saxophones use Cinematic Studio Strings

## Key switches
| note | strings (CSS)          | woodwinds (CSW)       | brass (CSB)   |
|------|------------------------|-----------------------|---------------|
| 24   | sustain                | sustain               | sustain       |
| 25   | tremolo                |                       |               |
| 26   | measured tremolo       | measured repetitions  | double tongue |
| 27   | trill                  | trill                 | trill         |
| 28   |                        | flutter tongue        | flutter tongue|
| 29   | staccato (velocity 40) | staccato              | staccato      |
| 30   | marcato                | marcato               | marcato       |
| 31   | pizzicato              |                       |               |

## Phrasing
- default is connected, for every family: unslurred notes that touch are played as if slurred
- a connected run breaks at a rest, a staccato, an accent, a repeated pitch, a chord,
  a written slur, or a tremolo
- a written slur breaks at a repeated pitch too: the repeat is played detached, on its beat,
  and starts a new phrase
- words in the score switch it from there on
  - connected: cantabile, espr., dolce, legato, cantando
  - detached: maestoso, marc., non legato, détaché, secco

## Detached (non slurs)
- note duration 0.95x
- velocity follows dynamics
- CC1 follow dynamics

## Slow slurs
- use sustain
- inner and last note come in early
  - strings: 150ms
  - others: 75ms
- first and inner note end late (25ms later), so that they overlap the next note
- last note duration 0.95x
- first note velocity follow dynamics
- inner and last note velocities is fixed (0.75)
- CC1 follow dynamics

## Fast slurs
- condition: when this OR previous note is short enough (160ms or shorter, in real time)
  - the first note of a slur never looks at the note before it
- use marcato
- first and inner note end late (25ms later)
- inner note do not come in early
- last note duration 0.95x
- first note velocity is fixed (0.1)
- inner and last note velocities is fixed (0.75)
- CC1 follow dynamics

## Slur starting with an accent
- first note uses marcato, velocity follows dynamics plus 0.2 (not the fast slur's 0.1)
- the notes after it follow the slow / fast slur rules (back to sustain if slow)

## staccato / spiccato
- use staccato
- CC1 follows note duration, the shorter the lower (150ms or shorter → 0, 800ms or longer → 127)
- velocity follows dynamics
- if it is at end of slur, ignore above and
  - use original articulation
  - duration 0.5x
  - velocity fixed (0.75)
  - CC1 follow dynamics

## staccatissimo
- same as staccato, duration 0.25x

## accent / marcato
- use marcato
- velocity follows dynamics plus 0.2
- CC1 follow dynamics

## tenuto
- note duration 0.99x

## accent + staccato
- use staccato (repetitions, CC1 fixed = 0)
- velocity follows dynamics plus 0.2

## sfp / fp
- use marcato
- velocity fixed (0.9)
- CC1 starts at piano (0.2) right away

## A technique's own CC1
- holds until the next note starts, not just until the note ends
- then the dynamics come back (or the next note's own value)

## Tremolo (unmeasured)
- strings: tremolo
- woodwinds and brass: flutter tongue

## Measured tremolo
- one note of its written length; the library plays the repetitions
- brass (double tongue)
  - CC1 fixed = 127 ("Hold": repeats while the key is down)
  - duration 0.4x

## Trills
- use trill
- both notes start together and end together (the library needs them within 25ms)
- tremolo between two notes: both notes, for the length of the two
- trill mark: adds the note above
  - half or whole step, from an accidental on the trill, else one earlier in the bar,
    else the key signature

## Swells (not written, added to long notes)
- only on notes longer than 160ms; on slurs, the rise only from a first note and the fall only into a last note longer than the fast-slur limit
- one swell for the whole slur
- CC1 starts and ends at 75% of the dynamics
- rise: 25% of the note (slur: 50% of the first note), ease-out (power -2)
- fall: 25% of the note (slur: 50% of the last note), ease-in (power 2)
- between notes CC1 stays down until the next note
- a hairpin or a dynamic change inside the note wins: the rise or fall over it is left out
- velocity is not changed

## Hairpins
- cresc: exponential, slow first (exp 4)
- dim: exponential, fast first (exp -4)
