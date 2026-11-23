# Input: supers with two thumbs (Chord Assist)

The game's own command reader (`foit_rt.cpp`, `composeSample`) counts **PP / KK / PPP / KKK** (the buttons of every super) only when the
buttons go down within **one tick** of each other.  On a keyboard that is easy; with two thumbs on a phone the buttons usually land 2-3
ticks apart (33-50 ms), so the super did not come out and the first button's normal did instead.

Measured with the real Filia (Fenrir Drive, `623 + PP`), the two punches `n` ticks apart:

| ticks apart | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| before | super | super | normal | normal | normal | normal |
| Chord Assist (3) | super | super | super | super | normal | normal |

`src/core/chord_assist.*` sits between the pad and the fight (the raw buttons of one local player, once per tick; the simulation and what a
netplay peer receives are untouched):

* only while the fighter has **at least one bar** of meter (no bar = no delay at all, no super to miss);
* the first attack button of a group (punches LP/MP/HP, kicks LK/MK/HK) that goes down alone is held back up to **3 ticks**;
* another button of the same group inside that time -> both go out on the same tick (a chord);
* otherwise the held-back press goes out when the time is up; a tap shorter than that is still delivered (one tick);
* directions, Start, the other group and a second button while the first is already out are never delayed.

The cost is 3 ticks (50 ms at 60 ticks/s) on a lone attack press while a bar is available.  Setting: `ZD_CHORD_ASSIST` in the `[options]` section
of the save, `0` (off) to `6` ticks, default `3`.  Tests: `zdroid_chord_tests` (logic; with `ZDROID_TEST_CHARS=<characters folder>` it also
drives the real Filia super through the real fight).
