# Kotor 2 Efficient Load Times
A `dinput8.dll` proxy mod that shortens load times in the Steam version of
Star Wars: Knights of the Old Republic II. It doesn't change any game files.
It hooks a few engine functions at runtime and either skips work the game
doesn't need, or stops the game from waiting when it doesn't have to.

## Enhancements

Savings were measured on a quicksave reload in Peragus (101PER), four loads per
build, with the game running at about 170 fps. Bigger areas load more objects and textures, so they
should save more.

### Skip intros
When the game starts, `PreloadInitialAssetsWrapper` (0x0073f050) loads the
intro/splash-screen assets before you reach the main menu. The mod turns that
call into a no-op, so the game goes straight on to the menu.
Toggle: `SKIP_PRELOAD_INITIAL_ASSETS_WRAPPER`.

### Forced area streaming
KOTOR 2 runs a client and a server inside the same process, even in single
player. When a load finishes, the server sends the player's client the area's
objects in small messages of about 2 KB each. The loading screen stays up
until the last one arrives. The server sends at most one message every
200 ms (`Server_UpdateClient_Throttle200ms`, 0x00537590). That limit makes
sense over a network, but here client and server share one process. Peragus
needs 9 messages, so the loading screen sat idle for about 1.9 s while the
actual sending took about 2 ms.

The engine's update function already has a "send now" flag, and the engine
uses it itself elsewhere. The mod sets that flag only while the player's area
is still loading (`player+0x24 == 1`), so one message goes out per frame
instead of one per 200 ms. Once the client reports the area as loaded, normal
gameplay keeps the original 200 ms rate. Only the waits between messages
change: the same data is sent, just sooner.
Toggle: `FORCE_AREA_STREAM_DURING_LOAD`. Details: [load-pipeline notes](https://github.com/tayloroxelgren/kotor2-engine-internals/tree/main/load-pipeline).

**Estimated saving:** about **1.3 s per load** (1.90 s → 0.56 s).

### GPU mipmap generation
Each texture needs a set of smaller copies (mipmaps) for surfaces seen at a
distance. The game can have the graphics driver build them on the GPU, but it
only does so if a startup check passes (`GL_CanUseHardwareMipmapGen`,
0x00484a60). That check rejects any driver that supports
`GL_ARB_fragment_program`, which is every modern GPU, probably as a workaround
for an old ATI driver bug. So the game falls back to `gluBuild2DMipmaps`, which
builds every mipmap level on the CPU while the loading screen waits.

The mod makes the check pass whenever the driver supports GPU mipmap
generation (`GL_SGIS_generate_mipmap`). Uploading the 28 textures that
Peragus loads here went from 147 ms to under 1 ms of main-thread time.
Toggle: `FORCE_HW_MIPMAP_GEN`. Details: [load-pipeline notes](https://github.com/tayloroxelgren/kotor2-engine-internals/tree/main/load-pipeline).

**Estimated saving:** about **145 ms per load**.

### Total
Together these take roughly **1.4 s** off every load in 101PER, going by the
mod's own profiler. In everyday play it feels like a lot more than that at times potentially resulting in a 30-40% improvement.


## Building it yourself
You need:

- Windows, with Visual Studio 2022 (the "Desktop development with C++"
  workload). `build.bat` expects `vcvars32.bat` in the default Community
  install path, so edit that line if yours is elsewhere.
- [MinHook](https://github.com/TsudaKageyu/minhook): clone or extract it
  into a `minhook` folder in this repository, so that `minhook\include` and
  `minhook\src` exist.

Then run this from the repository folder in any shell:

```
build.bat
```

It sets up the 32-bit compiler itself and produces
`dinput8.dll`, which logs the hooks it installs to `kotor2_log.txt` in the game
folder. Each enhancement can be switched off at the top of `dinput8.cpp`
by setting its toggle to 0.

For a release build with no logging, add `-release`:

```
build.bat -release
```

That defines `LOGGING_ENABLED=0`: the DLL installs the same hooks but never
creates `kotor2_log.txt`.

To build a DLL with no hooks at all, for an unmodified-engine baseline, add
`-nohooks`:

```
build.bat -nohooks
```

That defines `NO_HOOKS`: the DLL only forwards `DirectInput8Create` to the real
`dinput8.dll`. It installs no hooks, applies none of the enhancements and writes
no log. If `-release` is also given, `-nohooks` wins.

## Installation
Just copy the `dinput8.dll` into the same directory as your swkotor2.exe

## Reverse-engineering notes

The function table, class notes, code paths and load-pipeline analysis behind these hooks live in
[kotor2-engine-internals](https://github.com/tayloroxelgren/kotor2-engine-internals).

Note:

- Only works for steam version on Windows
- Testing is still minimal as development is early so use at your own risk
