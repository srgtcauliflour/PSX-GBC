// gui.h - ROM select menu (PSn00bSDK version).
//
// REWRITE NOTE: replaces the original Psy-Q/GsLib-based gui.h (splash
// screen, animated menu roll-in, TIM-image-indexed asset table - see git
// history or /tmp/old_gui.h during the session this was written in). None
// of that has a PSn00bSDK equivalent (see the psx.c/psx.h rewrite note
// from earlier this session) and a from-scratch rewrite was always the
// plan - this is deliberately a plain, functional text menu using
// PSn00bSDK's built-in debug font (FntPrint), not a recreation of the
// original's splash screens/animations. Visual polish can layer on top
// of this later; a working way to actually pick a game from a multi-game
// disc could not wait on that.
#ifndef GUI_H
#define GUI_H

// Shows the ROM select menu, letting the player pick a .GB/.GBC file from
// the disc's root directory with Up/Down and confirm with Cross or
// Start. Copies the chosen file's name (without the ISO9660 ";1" version
// suffix - LoadROMFromCD() adds it back) into selectedFilename, which
// must be at least 16 bytes. Returns 1 on a real selection, or 0 if no
// .GB/.GBC files were found on the disc at all (nothing to select).
int ShowROMMenu(char *selectedFilename, int maxLen);

#endif
