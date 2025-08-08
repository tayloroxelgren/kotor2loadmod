# Kotor 2 Efficient Load Times
This mod aims to improve the load times for the steam version of Kotor 2

## Status
The mod is in early development, but currently enables the inital splash screens to be skipped on boot of the game. Other functions and improvements are still being explored.

## Installation
Just copy the `dinput8.dll` into the same directory as your swkotor2.exe

## What has been identified so far?
- `FUN_00533830` **LoadingScreen**: This appears to be the main function that initiates the loading screens in the game

- `FUN_0051c470` **LoadingScreenWrapper**: Seems to just be a wrapper to call LoadingScreen
- `FUN_00409ed0` **LoadingScreenUpdateFrame**: Draws and presents one loading-screen frame, then advances asset streaming by ticking the resource queue.
- `FUN_005582f0` **LoadAndInitialize**: This function likely handles the loading and initialization of mod resources and game scenario data, including mod information, game parameters (time, player data), and setting up various in-game scripts.
- `FUN_00407920` **GameMain**: Most likely the main function of the game
- `FUN_00781be0` **Engine**: Seems to have the main engine logic
- `FUN_00703f30` **ProcessResourceQueue**: Empties a 64 KB ring buffer of loading-time “packets,” handing each packet to the right handler (special handler if the packet starts with BN, otherwise the generic resource loader) until the queue is empty.
- `FUN_00781840` **ResourceQueue_UnpackAndTrace**: Unpacks a single resource‐queue packet with full tracer setup/flush; wraps the call in the Concurrency scheduler and records timing/log data before invoking the real dispatch.
- `FUN_005314e0` **ResourcePacketDispatcher**: Core dispatcher that examines the first byte of each packet (e.g. 's' vs. 'p'), checks the subsystem’s enable flags, and forwards the packet to the appropriate handler routine.
- `FUN_00810cf0` **PPacketHandler**: Handles all 'P'‐prefix packets, dispatching on major/minor subtype, with buffer checks and tracing.
- `FUN_00884530` **SPacketHandler**: Likewise, handles all 'S'‐prefix packets (e.g. scripts or static data), routing to the appropriate subsystem.
- `FUN_00718e40` **LoadResourceBlockOrFallback**: Tries to load a resource block by type & ID; if it’s missing, invalid, or too small, it copies a default 4-word fallback instead and clears the “found” flag; only when valid data exists does it set the flag and process the payload.
- `FUN_00717820` **GetResourceDataPtr**: Checks that the resource manager and entry are valid and in bounds; if so, returns a pointer to the entry’s data block and writes its byte-length into the out parameter, otherwise returns 0.
- `FUN_0053a0c0` **InitGraphicsCache**: During loading screen, this ticks its timer, loops over every object, and pulls in any missing textures or meshes into GPU memory. It also handles one-off setup like shadow-map building.
- `FUN_0053a8b0` **InitShadowCache**: grabs every object in the current scene (once the renderer’s up) and uploads each one’s shadow/lighting buffers into the GPU.
- `FUN_007047b0` **EnsureSubsystemReady**: When you hit the special marker (0x400000) and the “pending” flag is set, it checks the subsystem’s status—if it isn’t “ready” (code 2), it forces a reset. In practice this makes sure any background loader or decompressor is actually up and running before moving on
- `FUN_00733540` **ResetTracer**: Takes a two-word slot and zeroes both values—basically “start a fresh trace section” by clearing its ID and counter.
- `FUN_00733780` **FlushTracer**": If the trace section has a nonzero ID, it clears the counter
- `FUN_009196fd` **FreeTracer**: Frees the tracer memory
- `FUN_0073F2D0` **Trace_InitContext**: Grabs a per-thread “tracer” slot so the loader knows where to send its log lines.
- `FUN_00734270` **Trace_FormatMessage**: Builds a log message by running a printf-style format into a resizable temp buffer and copying it to a new heap string.
- `FUN_00739AA0` **Trace_WriteMessage**: Takes the formatted string and writes it out (via fwrite) to the tracer’s output file.
- `FUN_0052f610` **SaveGame**: Creates save game file
- `FUN_007DE110` **CaptureScreenThumb**: Grabs current frame buffer, measures its brightness (skips totally dark frames), scales it to thumbnail size, and calls into the renderer to write out the 4-component pixel data
- `FUN_00704880` **HandleBNPacket**: Checks a “BN…” packet to see if it’s a BN-CS (decompress or verify data) or BN-CR (compile or initialize data) message—if so, calls the right specialized handler; otherwise it hands the packet off to the generic loader with a flag marking it as a BN packet.
- `FUN_0073f050` **PreloadInitialAssetsWrapper**: Wrapper to preload inital assets like the splash screen
- `FUN_007813a0` **PreloadInitialAssets**: Preloads inital assets like the splash screen
- `FUN_0073f230` **PreloadAssetsWrapper**: Wrapper to preload assets
- `FUN_00781590` **PreloadAssets**: Loads assets
- `FUN_00811450` **ModuleHandler**: Loads and initializes each game module, bringing in level data, assets, and scripts needed for the world.
- `FUN_0080deb0` **GameObjUpdate**: Processes incoming object-update packets, creating or refreshing in-game entities and syncing their state during the loading phase.
- `FUN_007BE4C0` **ModuleChunkLoadCore**: Wrapper Allocates and parses a module’s sub-chunks, updates the loading screen between groups, and marks the chunk as done.
- `FUN_0073F870` **ModuleChunkLoadWrapperA**: Wrapper for ModuleChunkLoadCore
- `FUN_0078C330` **ModuleChunkLoadWrapperB**: Wrapper for ModuleChunkLoadCore
- `FUN_00747210` **InitializeGameUI**: Constructs and configures the entire in-game user interface


## Build Instructions
Download [MinHook](https://github.com/TsudaKageyu/minhook)

Place it in the repository directory and include both the src and lib then run

`build.bat`

---
Note:

- Only works for steam version on Windows
- Testing is still minimal as development is early so use at your own risk