# Kotor 2 Efficient Load Times
This mod aims to improve the load times for the steam version of Kotor 2

## Status
The mod is in early development, but currently enables the inital splash screens to be skipped on boot of the game. Other functions and improvements are still being explored.

## Installation
Just copy the `dinput8.dll` into the same directory as your swkotor2.exe

## What has been identified so far?
### Functions
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
- `FUN_007BE4C0` **ModuleChunkLoadCore**: Allocates and parses a module’s sub-chunks, updates the loading screen between groups, and marks the chunk as done.
- `FUN_0073F870` **ModuleChunkLoadWrapperA**: Wrapper for ModuleChunkLoadCore
- `FUN_0078C330` **ModuleChunkLoadWrapperB**: Wrapper for ModuleChunkLoadCore
- `FUN_00747210` **InitializeGameUI**: Constructs and configures the entire in-game user interface
- `FUN_00919723` **AllocateMemoryOrThrow**: General memory allocator
- `FUN_00855F30` **PopulateSaveGameEntry**: Parses a single save file’s metadata and assets—reads playtime, area name, timestamps, hints, corruption flags, screenshots, thumbnails, etc.—and fills in the UI data structure used by the save-selection screen
- `FUN_008C19F0` **DebugMenuContructor**: Makes debug menu?
- `FUN_0085FBE0` **CSWCAnimBase_LoadModel**: Loads an animated model for a world character
- `FUN_00904A80` **CharacterCreationScreen**: Character Creation Screen?
- `FUN_00523870` **LoadModuleEnvironmentAndUI**: Character Creation Screen?
- `FUN_0055A460` **ModuleLoadCoordinator**: Allocates a loader object
- `FUN_00521360` **AreaConstructor**: Constructor for an area/scene object. Initializes vtables, allocates subcomponents, and prepares structures for later resource loading (no direct disk reads).
- `FUN_0055a460` **LoadOrCreateAreaAndInit**: High-level scene/area loader.Decides whether to reuse or create a new area object, initializes it, and begins environment/UI setup. Delegates actual asset streaming to deeper loader functions.
- `FUN_00647050` **EnqueueStreamingRequest**: Builds a small command packet (opcode 0x50) and pushes it into the streaming system’s ring buffer via a vtable call to the streaming manager at DAT_00a1b4a4 + 8. This is the entry point for loading/streaming assets from disk.
- `FUN_00637270` **RequestResourceStream**: Prepares parameters for a resource/asset request, does some validation, and then calls the streaming enqueue function
- `FUN_00401730` **InitResourceManager**: Allocates and initializes the core resource manager structure, creating subcomponents for streaming, resource metadata, and asset caches, and setting up internal state for game resource loading.
- `FUN_0073ef30` **InitClientExoApp**: Sets up the game’s core client application object
- `FUN_00780460` **InitClientCoreSystems**: Allocates and zero-initializes the main client game object, then sets up dozens of subsystems (resource queues, streaming buffers, graphics/audio settings, network structures, and various runtime managers).
- `FUN_00934C70` **OpenGameAsset**: Low Level call to open a game asset
- `FUN_0091caeb` **_fopen**: C standard library
- `FUN_00475ab0` **OpenOrStreamGameFile**: Opens a game asset or streams it?
- `FUN_004762f0` **ReadGameAssetChunk**: Reads a block of game asset data from either an open file stream or an already-loaded memory buffer. If reading from disk, allocates memory and performs I/O. If reading from memory, returns a pointer to the requested chunk and advances the buffer pointer without performing any file I/O.
- `FUN_0045a030` **Gob::LoadFromFileOrStream**: Loads a Gob game object from disk or an in-memory stream, initializes its subcomponents, and registers it with the game world.
- `FUN_007ac470` **UpdatePlayerInputAndTargeting**: Per-frame player control and targeting loop. Reads input devices (mouse/gamepad) for look movement, applies sensitivity/inversion settings, updates camera pitch/yaw, and scans nearby objects for valid interaction/target highlighting. Writes results to HUD and interaction state.
- `FUN_0051eed0` **LoadEventQueueFromConfig**: Iterates the "EventQueue" section, allocates and initializes event nodes from each entry, enqueuing successful ones and freeing failures.
- `FUN_005205f0` **BuildEventFromConfigEntry**: Reads one "EventQueue" entry, fills common fields (tag/ids), and deserializes a typed "EventData" payload based on EventId (allocating and constructing the appropriate struct). Returns 1 on success, 0 for invalid EventId
- `FUN_00423AB0` **ParseTXIAndBuildTextureController**: “Parses TXI directives for a texture: creates the appropriate procedural texture controller from proceduretype and applies all TXI flags/params (mipmaps, clamp, bump, envmap, etc.). Finally lets the controller parse its own extra options.”
- `FUN_00423000` **CAurTextureBasic::Ctor**: Constructs a texture object, sets defaults, stores names, allocates helper state
- `FUN_00424B10` **Texture_ApplyTXIAndBuildController**: Constructs a texture object, sets defaults, stores names, allocates helper state
- `FUN_00704060` **NetLayer::SendMessageToPlayer**: Writes a message into the player’s outgoing network buffer. Validates available space to avoid overflow (logging a detailed dump if it would exceed the buffer), copies the message length and payload into the queue, and advances the write pointer. This is what sends packets to ProcessResourceQueue
- `FUN_00665280` **SendMessageWithSHeader**: This function constructs and sends a network message prefixed with the byte `0x53` to a specific player. It allocates a buffer, copies data from a source function (`FUN_00734010`), and sends it using `NetLayer::SendMessageToPlayer`.
- `FUN_00884450` **SendMessageWithSHeader2**: This function constructs and sends a network message prefixed with the byte `0x73`, which is different from the `0x53` message sent by `FUN_00665280`. It copies the payload from a source function (`FUN_00734010`).
- `FUN_007045b0` **SendBNCSUMessage**: This function sends a network message with a hardcoded, 9-byte header of `0x42 0x4e 0x43 0x53 0x55`. It first checks a state flag to prevent the message from being sent repeatedly. It then copies the payload from `FUN_00734010`, sends the message to a player, and updates several state variables.
- `FUN_00704970` **HandleIncomingBNCRPacket**: This function acts as a message handler. It receives an incoming packet, validates its structure, and then processes it based on a value in the packet's header (`local_14`). It constructs and sends a new, 10-byte response message with the header `0x42 0x4e 0x43 0x52`, and updates various game state flags based on the type of incoming message.
- `FUN_00812350` **HandleNetEvents**: This function acts as a dispatcher for various network events, based on a single character parameter. It handles different cases:
    - `param_1 == 1`: Logs a formatted message and sends a network packet prefixed with `0x73`.
    - `param_1 == 2`: Performs a specific state change by calling `FUN_0073f890`.
    - `param_1 == 3`: Handles a complex multi-step event by checking flags and calling other functions.
- `FUN_0087a350` **SendMessageWithPHeader**: Sends a compact, 3-byte network message with a header of `0x70` and two variable parameters.

- `FUN_00883560` **InitializeAndSyncState**: A major function that performs a series of game state updates, logs a formatted message, and sends it over the network. It also calls `FUN_007045b0` to send a specific message and interacts with a resource scheduler to manage concurrent tasks.

- `FUN_008faca0` **ModuleLoadSynchronization**: A critical state-change function, similar to `FUN_00883560`, that synchronizes game data. It updates multiple game variables, logs a formatted message, sends it over the network, and interacts with the resource scheduler to manage assets and tasks.
- `FUN_00711360` **ResourceStreamer_Init**: This function initializes the resource streaming system. It determines available physical memory to size streaming buffers and launches a dedicated worker thread to handle the loading of game assets from storage into memory.
- `FUN_005363a0` **SubsystemManager_Init**: This function is a core initializer that sets up the game's key subsystems. It allocates memory and constructs various manager objects, including those for a message queue, resource queues, and an in-game virtual machine for scripts. It also establishes file paths for different game directories and logs each step of the initialization process for debugging.
- `FUN_00788d90` **GameClient_Init**: This function performs the comprehensive setup for the game's client-side environment. It reads settings from the swkotor2.ini file to configure graphics options like "FullScreen" and "Texture Quality." It also initializes core game objects, loads localization data, and sets up various other subsystems needed to run the game, such as the resource streamer and other game-specific managers.
- `FUN_00401bc0` **LoadingScreenManager_Init**: This function initializes the loading screen manager. It first checks if a previous manager exists and cleans it up. It then allocates and initializes a new manager object, preparing the game to display and manage loading screens.
- `FUN_00882230` **ModuleLoader**: This function is a core module and resource loader, primarily used when transitioning between different game areas or beginning a new game. It first sets up the necessary file system paths for game resources and then allocates memory for and initializes the main module object. It performs low-level file I/O to load various resources and also uses a tracing system to log its progress. This function is an integral part of the loading sequence, working with the loading screen manager to ensure a smooth transition into the game world.
- `FUN_00880740` **MainMenu_Init**: This function is the constructor for the game's main menu. It initializes the UI, loads the main menu's visual resources, and checks for the existence of game files like GAMEINPROGRESS to enable or disable menu options. It also registers several event handlers, including one for the ModuleLoader, which allows clicking "New Game" or "Load Game" to start the game's loading process. It can also set up a 3D scene for the main menu's background, depending on the game's state.
- `FUN_0070a620` **GameState_Manager**: This function is a core state machine manager that controls the high-level flow of the game. It uses a stack-based system to transition the game between various modes, such as the main menu, loading screen, or an active game session. By pushing and popping states, it ensures that the game's logic and resource management are correctly configured for the current context. This is the central function that dictates what the game is doing at any given moment.
- `FUN_00409750` **Graphics_InitAndMainLoop**: This function is responsible for the game's core startup and primary execution loop. It initializes the graphics context, reads display settings from the swkotor2.ini file, and creates a high-priority worker thread for asynchronous tasks. It also manages the game's window, including disabling the desktop taskbar to ensure a full-screen experience.
- `FUN_00408af0` **Window_CreateRender**: This function creates a secondary, specialized window dedicated to rendering the game's graphics. It registers a new window class with the name "Render Window," then creates the window itself. The handle to this window is stored in a global variable, DAT_00a1b484, and is the surface where the game's visuals are drawn.
- `FUN_004763b0` **ConfigParse**: Takes a filename as a parameter and parses the file. Parses a file named startup.txt, unsure what it is for

### Classes
- `0x009AA224`  **CSWGuiMainCharGen::vftable**: Seems to be the class for character creation
- `0x0099C460`  **CResGFF::vftable**: Generic File Format (GFF) loader used to parse and provide access to resources like UTC, UTI, ARE, etc
- `0x0098B5CC`  **Gob::vftable**: Base game object class used to represent in-world entities. Contains a wide range of virtual functions for lifecycle management, serialization, rendering, and asset loading.  


## Build Instructions
Download [MinHook](https://github.com/TsudaKageyu/minhook)

Place it in the repository directory and include both the src and lib then run

`build.bat`

---
Note:

- Only works for steam version on Windows
- Testing is still minimal as development is early so use at your own risk