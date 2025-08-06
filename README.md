# Kotor 2 Efficient Load Times
This mod aims to improve the load times for the steam version of Kotor 2

## Status
The mod is in early development, with only initial timing of the LoadingScreen function set up. Currently still exploring and investigating functions in the game.

## What has been identified so far?
- `FUN_00533830` **LoadingScreen**: This appears to be the main function that initiates the loading screens in the game

- `FUN_0051c470` **LoadingScreenWrapper**: Seems to just be a wrapper to call LoadingScreen

- `FUN_005582f0` **LoadAndInitialize**: This function likely handles the loading and initialization of mod resources and game scenario data, including mod information, game parameters (time, player data), and setting up various in-game scripts.
- `FUN_00407920` **GameMain**: Most likely the main function of the game
- `FUN_00703f30` **ProcessResourceQueue**: Empties a 64 KB ring buffer of loading-time “packets,” handing each packet to the right handler (special handler if the packet starts with BN, otherwise the generic resource loader) until the queue is empty.
- `FUN_00718e40` **LoadResourceBlockOrFallback**: Tries to load a resource block by type & ID; if it’s missing, invalid, or too small, it copies a default 4-word fallback instead and clears the “found” flag; only when valid data exists does it set the flag and process the payload.
- `FUN_00717820` **GetResourceDataPtr**: Checks that the resource manager and entry are valid and in bounds; if so, returns a pointer to the entry’s data block and writes its byte-length into the out parameter, otherwise returns 0.
- `FUN_0053a0c0` **InitGraphicsCache**: During loading screen, this ticks its timer, loops over every object, and pulls in any missing textures or meshes into GPU memory. It also handles one-off setup like shadow-map building.
- `FUN_0053a8b0` **InitShadowCache**: grabs every object in the current scene (once the renderer’s up) and uploads each one’s shadow/lighting buffers into the GPU.
- `FUN_007047b0` **EnsureSubsystemReady**: When you hit the special marker (0x400000) and the “pending” flag is set, it checks the subsystem’s status—if it isn’t “ready” (code 2), it forces a reset. In practice this makes sure any background loader or decompressor is actually up and running before moving on
- `FUN_00733540` **ResetTracer**: Takes a two-word slot and zeroes both values—basically “start a fresh trace section” by clearing its ID and counter.
- `FUN_00733780` **FlushTracer**": If the trace section has a nonzero ID, it clears the counter
- `FUN_009196fd` **FreeTracer**: Frees the tracer memory
- `FUN_0052f610` **SaveGame**: Creates save game file
- `FUN_007DE110` **CaptureScreenThumb**: Grabs current frame buffer, measures its brightness (skips totally dark frames), scales it to thumbnail size, and calls into the renderer to write out the 4-component pixel data

## Currently Identifying:
- `FUN_007178e0`
- `FUN_007108a0`
- `FUN_00717770`
- `FUN_00717720`
- `FUN_007186c0`

## Build Instructions
Download [MinHook](https://github.com/TsudaKageyu/minhook)

Place it in the repository directory and include both the src and lib then run

`build.bat`