# Symphony Series Percussion VST Rules

## General
- don't generate multiple notes wioth tremolo, keep one and use CC1 for dynamics
  - except for mallets
  - measured tremolos are written out as hits, for every instrument
- sfp / fp: CC1 at piano right away, velocity 0.9
  - on a roll, a single stroke too: drums play it together with the roll, timpani play
    a 32nd stroke and then the roll, the suspended cymbal a long hit as long as the roll
- hairpins: exponential (cresc slow first, dim fast first); suspended cymbal: ease
- each instrument has a dynamic range (velocity for hits, CC1 for rolls), to tune by ear
- damp keys are held from the rest until the next note or one beat, then let go (back to hits)
  - timpani, platti and suspended cymbal: also right after a staccato note ends
- a roll tied into notes without tremolo ends where the tremolo ends
- a drum kit in one part plays the notes the file gives its sounds (General MIDI, channel 10)

## Snare Drum / Bass Drum
- Normal Hits: C2 (48) D2 (50)
- Edge Hits: E2 (52) F2 (53) (Snare Drum only, use when accent)
- Tremolo: G2 (55) 
- Crescendo: A2 (57) (Usually not used)
- Flam: B2 (59) (Usually not used)

## Timpani
- Keyswitches
  - Hits: C1 (36)
  - Tremolo: D1 (38)
  - Damp: A1 (45) (temp, use when rest)
- MIDI pitch is an octave up of real pitch

## Suspended Cymbal
- Long Hits: C2 (48) D2 (50) (Use this as default)
- Short Hits: E2 (52) F2 (53)
- Keyswitches
  - Hard: C1 (36) (Use this when accent)
  - Soft: D1 (38) (use this as default)
- Tremolo: G2 (55) (Used with short crescendos longer than 2s. Use CC1 ease curve for dynamics)
- Crescendo: A2 (57) (Used with short crescendos less than 2s. Fixed length 1.25s before peak)

## Platti
- Long Hits: C2 (48) D2 (50) (Use this as default)
- Short Hits: E2 (52) F2 (53)
- Keyswitches
  - Hits: C1 (36)
  - Damp: A1 (45) (temp, use when rest)

## Sticks
- Hits: C2 (48) D2 (50)
- used for a snare part's rim clicks and side sticks, on a track of its own

## Toms
- Tom 4 (lowest): G2-A2 (43, 45)
- Tom 3: C3-D3 (48, 50)
- Tom 2: E3-F3 (52, 53)
- Tom 1: G3-A3 (55, 57)
- the part's drums from low to high (by staff line) go to Tom 4 upwards

## Bongos
- no bongos in this library: each drum or play technique of the part gets its own key,
  white keys from C2 (48) upwards, low to high

## Wood Blocks
- only one wood block in this library, so use Temple Blocks instead
- C2 (48), D2 (50), E2 (52), F2 (53), G2 (55), low to high
- fewer blocks are spread over the five (two blocks: C2 and G2)

## Drum kit in one part
- split into one instrument (and one MIDI track) each:
  bass drum, snare drum, sticks (rim / side stick), toms, cymbals (suspended cymbal), others

## Glockenspiel
- MIDI pitch is an octave below of real pitch

## Other mallets
No keyswitches for mallets, so expand tremolos.

## Others
Most follow the same keyswitches and same keys for variation of hits (two white keys each)