# Agon Light Emulator (work in progress)

This emulator is based off the TI CEmu emulator for the Zilog eZ80. [**TI84 Cemu project**](<https://github.com/CE-Programming/CEmu>)).

ATM it is using just the core eZ80 emulation code. The Agon VDP high-level emulation has been added by me, as has SDL2 library for graphics output and keyboard input.

So far, the emulator runs and displayes the boot-up text for the VDP and MOS, but then hangs as SD card and FAT file system is not yet emulated.

You will need Visual Studio C++ to load the solution and build it. Only the x64\Debug build has been set up so far.

Feel free to contribute. You can also ask questions in the Agon Programmers facebook group.

**Happy hacking.**

