polykeyboard
============

A simple qwerty/console-interface 4-mode synthesizer implemented using Perry Cook's mass-spring ODE solution.

To play the synth, four keyboard rows are mapped to each of the four voices: row 1...0, row q...p, row a...l and row z.../ (case doesnt matter).

By default, each of the rows control a different voice that uses a different spring mass and spring constant, although you can lock or unlock the spring-constants together using the left-shift key so that the differences between voices will only be the mass.

The ESC key quits.

Compile with:

> gcc polykeyboard.c -o polykeyboard -ffast-math -fomit-frame-pointer -msseregparm -mfpmath=sse -msse2 `sdl-config --cflags --libs`

The program requires an installation of SDL, so install that first.

The code was tested using x86 with sse2 extensions on an old 2Ghz Core 2 duo. Without the sse2 the synth just doesnt fill the buffer fast enough and the playback thread will skip.
