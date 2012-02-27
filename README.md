            _______________________
           /\                      \
           \_|         Arx         |
             |      Libertatis     |
             |   __________________|__
              \_/____________________/


Cross-platform port of Arx Fatalis

Arx Libertatisis based on the publicly released [Arx Fatalis source code](http://www.arkane-studios.com/uk/arx_downloads.php).
GPLv3 - read ARX_PUBLIC_LICENSE.txt

## Contact

Website: [http://arx-libertatis.org](http://arx-libertatis.org)

Bug Tracker: [https://bugs.arx-libertatis.org/](https://bugs.arx-libertatis.org/)

IRC: \#arxfatalis on irc.freenode.net

Wiki: [http://arx.parpg.net/](http://arx.parpg.net/)

Reddit: [http://www.reddit.com/r/ArxFatalis/](http://www.reddit.com/r/ArxFatalis/)

## Dependencies

* DevIL
* zlib
* Boost (headers, `program_options` library)
* Freetype
* OpenAL 1.1 **and/or** DirectSound 9

There are rendering backends for both OpenGL and Direct3D. You need either:

* SDL 1.2
* OpenGL 1.5 (OpenGL 2.1 or newer is recommended)
* GLEW

**and/or**

* DirectInput 8
* Direct3D 9

Both OpenGL and Direct3D backends can be built at the same time.

Systems without Win32 or POSIX filesystem support will also need Boost 1.44 or newer including the `filesystem` and `system` libraries.

Arx Libertatis comes with an optional gui crash reporter which has additional dependencies:

* Qt 4 (`QtCore` and `QtGui` libraries)
* OpenSSL

## Compile and install

For Linux run:

    $ mkdir build && cd build && cmake ..
    $ make

To install the binaries system-wide, run as root:

    # make install

Alternatively you can run the game by specifying the full path to the `arx` binary in the `build` directory.

The wiki has more detailed instructions on [compiling under linux](http://arx.parpg.net/Downloading_and_Compiling_under_Linux).

Getting all the dependencies set up for Windows is more tricky. Pre-build dependencies are available in the [ArxWindows repository](https://github.com/arx/ArxWindows) and [instructions on how to use them](http://arx.parpg.net/Downloading_and_Compiling_under_Windows) are available on the wiki.

Build options:

* `BUILD_TOOLS` (default=ON): Build tools
* `BUILD_CRASHREPORTER` (default=ON): Build the Qt crash reporter gui
* `UNITY_BUILD` (default=OFF): Unity build (faster build, better optimizations but no incremental build)
* `CMAKE_BUILD_TYPE` (default=Release): Set to `Debug` for debug binaries
* `CMAKE_INSTALL_PREFIX` (default: `/usr/local` on UNIX and `C:/Program Files` on Windows): Where to install Arx Libertatis.
* `DEBUG_EXTRA` (default=OFF): Expensive debug options
* `USE_OPENAL` (default=ON): Build the OpenAL audio backend
* `USE_OPENGL` (default=ON): Build the OpenGL renderer backend
* `USE_SDL` (default=ON): Build the SDL windowing and input backends
* `USE_NATIVE_FS` (default=ON): Use the native filesystem backend (POSIX / Win32) if available and not boost::filesystem.

Windows-only options (always OFF for non-windows platforms):

* `USE_DSOUND` (default=ON): Build the DirectSound audio backend
* `USE_D3D9` (default=ON): Build the Direct3D 9 renderer backend
* `USE_DINPUT8` (default=ON): Build the DirectInput 8 input backend

Enable by passing `-D<option>=1` to cmake, disable using `-D<option>=0`

Backends that are not available are disabled by default. The `cmake` run should display a summary of the enabled backends at the end.

## Data file, config and savegame locations

You will need to get either the full game or demo data of Arx Fatalis. See http://arx.parpg.net/Getting_the_game_data

Where arx will look for data files and write config and save files depends on the operating system and environment - the full algorithm is described at http://arx.parpg.net/Data_directories

To print all directories considered by arx, run

    $ arx --list-dirs

If you don't have a system-wide installation of the Arx Fatalis data files, you can just run arx from the directory containing the .pak files:

    $ arx

Arx Libertatis will then put the config and save files in the same directory. If you have a system-wide installation, but still want to run from the current directory, use the `--no-data-dir --user-dir=.` command-line options.

Where arx will look for a system-wide installation depends on the OS:

Under **Windows**, the locations for data and user (config and savegame) files can be set by the `{HKCU,HKLM}\Software\ArxLibertatis\DataDir` and `{HKCU,HKLM}\Software\ArxLibertatis\UserDir` registry keys. If not specified by a registry key, the user files are stored at `%USERPROFILE%\My Documents\My Games\Arx Libertatis` on XP and `%USERPROFILE%\Saved Games\Arx Libertatis` on Vista and up.

For other systems like **Linux**, the data files can be in `/usr/local/share/arx` and `/usr/share/arx` as well as other locations like `/usr/share/games/arx` depending on your linux distribution. Config and save files are normally located in `~/.local/share/arx`

## Run

Run from the directory containing the .pak files (or from anywhere in case of a system-wide installation):

    $ arx

The game will try to automatically rename all used files in the user directory (but not the data directory) to lowercase on the first run if possible. System-wide installations always need to manually rename the files to lowercase.

You can close it with `Alt + F4` or `killall arx.exe.so`

## Tools

* `arxunpak <pakfile> [<pakfile>...]` - Extracts the .pak files containing the game assets.

* `arxsavetool <command> <savefile> [<options>...]` - commands are:
  * `extract <savefile>` - Extract the contents of the given savefile to the current directly.
  * `add <savefile> [<files>...]` - Add files to a savefile, create it if needed.
  * `fix <savefile>` - Fix savegame issues created by previous builds of Arx Libertatis
  * `view <savefile> <ident>` - Print savegame information.

## Build documentation

To build developer documentation (doxygen), run this from the build directory:

    $ make doc
