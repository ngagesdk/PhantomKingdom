# Legal notes

This project is an independent reimplementation of the Pocket Kingdom
engine for the purpose of interoperability and preservation: running a
game you own on current systems and connecting it to PhantomLink, a
replacement for the discontinued N-Gage Arena service.

What the repository contains:

- Original C99 source code written for this project. Structure, names
  and comments are the project's own. The original program was studied
  with Ghidra to understand its behaviour (file formats, timing, state
  machines, network protocol); its code was not copied, and decompiler
  output is not part of the repository.
- Facts needed for interoperability: file format descriptions, the
  addresses at which the original keeps certain tables, numeric
  constants such as timings, screen coordinates and protocol fields.
- Tools to repeat the analysis on your own copy (`tools/re`).

What it does not contain, and must never contain:

- Any game file: no executable, bitmaps, sounds, music, text tables or
  other data, and nothing extracted from them (no converted images,
  no dumped tables, no decompiled listings).
- Nokia, SEGA or N-Gage trademarks used as branding for this project.

How game data is used at run time: the user points the program at the
files of their own copy (the contents of `\System\Apps\6RAH\`). Bitmaps,
sounds and text are read from there. Layout tables that the original
keeps inside its executable (sprite frames, animations) are read from
the user's `6RAH.APP` while the program runs; they are not copied into
this project. Nothing is written back to the game files. The program
refuses to start without a complete data set and explains why.

Pocket Kingdom is a trademark of its respective owners. N-Gage and Nokia
are trademarks of Nokia Corporation. SEGA is a trademark of SEGA
Corporation. This project is not affiliated with or endorsed by them.
This note is not legal advice; laws on reverse engineering and
interoperability differ between countries.
