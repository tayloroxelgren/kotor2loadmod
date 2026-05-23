# Kotor 2 Efficient Load Times
This mod aims to improve the load times for the steam version of Kotor 2

## Status
The mod is in early development, but some small improvements have been made

### Improvements
- Initial spalsh screens have been skipped by no oping `PreloadInitialAssetsWrapper`

#### Estimated Loading improvment at about: 0%

## Installation
Just copy the `dinput8.dll` into the same directory as your swkotor2.exe

## What has been identified so far?
### Functions
| Address | Name | Description | Timed |
|---|---|---|---|
|`FUN_00533830`| **LoadingScreen** | This appears to be the main function that initiates the loading screens in the game | yes |
|`FUN_0051c470` | **LoadingScreenWrapper** | Seems to just be a wrapper to call LoadingScreen | yes |
|`FUN_00409ed0` | **LoadingScreenUpdateFrame** | Draws and presents one loading-screen frame, then advances asset streaming by ticking the resource queue. | yes |
|`FUN_005582f0`| **LoadAndInitialize** | This function likely handles the loading and initialization of mod resources and game scenario data, including mod information, game parameters (time, player data), and setting up various in-game scripts. | yes |
|`FUN_00407920`| **GameMain** | Most likely the main function of the game | no |
|`FUN_00781be0` | **Engine** | Seems to have the main engine logic | no |
|`FUN_00703f30` | **ProcessResourceQueue** | Empties a 64 KB ring buffer of loading-time "packets," handing each packet to the right handler (special handler if the packet starts with BN, otherwise the generic resource loader) until the queue is empty. | yes |
|`FUN_00781840` | **ResourceQueue_UnpackAndTrace** | Traces one queued packet, then dispatches `P` packets to `PPacketHandler` and `S` packets to `SPacketHandler`. | yes |
|`FUN_005314e0` | **ResourcePacketDispatcher** | Core dispatcher that examines the first byte of each packet (e.g. 's' vs. 'p'), checks the subsystem's enable flags, and forwards the packet to the appropriate handler routine. | yes |
|`FUN_00810cf0` | **PPacketHandler** | Handles all 'P'-prefix packets, dispatching on major/minor subtype, with buffer checks and tracing. | yes |
|`FUN_00884530` | **SPacketHandler** | Likewise, handles all 'S'-prefix packets (e.g. scripts or static data), routing to the appropriate subsystem. | yes |
|`FUN_00718e40` | **LoadResourceBlockOrFallback** | Tries to load a resource block by type & ID; if it's missing, invalid, or too small, it copies a default 4-word fallback instead and clears the "found" flag; only when valid data exists does it set the flag and process the payload. | yes |
|`FUN_00717820` | **GetResourceDataPtr** | Checks that the resource manager and entry are valid and in bounds; if so, returns a pointer to the entry's data block and writes its byte-length into the out parameter, otherwise returns 0. | no |
|`FUN_0053a0c0` | **InitGraphicsCache** | During loading screen, this ticks its timer, loops over every object, and pulls in any missing textures or meshes into GPU memory. It also handles one-off setup like shadow-map building. | no |
|`FUN_0053a8b0` | **InitShadowCache** | Grabs every object in the current scene (once the renderer's up) and uploads each one's shadow/lighting buffers into the GPU. | yes |
|`FUN_007047b0` | **EnsureSubsystemReady** | When you hit the special marker (0x400000) and the "pending" flag is set, it checks the subsystem's status—if it isn't "ready" (code 2), it forces a reset. In practice this makes sure any background loader or decompressor is actually up and running before moving on | no |
|`FUN_00733540` | **ResetTracer** | Takes a two-word slot and zeroes both values—basically "start a fresh trace section" by clearing its ID and counter. | no |
|`FUN_00733780` | **FlushTracer** | If the trace section has a nonzero ID, it clears the counter | yes |
|`FUN_009196fd` | **FreeTracer** | Frees the tracer memory | no |
|`FUN_0073F2D0` | **Trace_InitContext** | Grabs a per-thread "tracer" slot so the loader knows where to send its log lines. | no |
|`FUN_00734270` | **Trace_FormatMessage** | Builds a log message by running a printf-style format into a resizable temp buffer and copying it to a new heap string. | no |
|`FUN_00739AA0` | **Trace_WriteMessage** | Takes the formatted string and writes it out (via fwrite) to the tracer's output file. | no |
|`FUN_0052f610` | **SaveGame** | Creates save game file | no |
|`FUN_007DE110` | **CaptureScreenThumb** | Grabs current frame buffer, measures its brightness (skips totally dark frames), scales it to thumbnail size, and calls into the renderer to write out the 4-component pixel data | no |
|`FUN_00704880` | **HandleBNPacket** | Checks a "BN…" packet to see if it's a BN-CS (decompress or verify data) or BN-CR (compile or initialize data) message—if so, calls the right specialized handler; otherwise it hands the packet off to the generic loader with a flag marking it as a BN packet. | yes |
|`FUN_0073f050` | **PreloadInitialAssetsWrapper** | Wrapper to preload initial assets like the splash screen | no |
|`FUN_007813a0` | **PreloadInitialAssets** | Preloads initial assets like the splash screen | no |
|`FUN_0073f230` | **PreloadAssetsWrapper** | Wrapper to preload assets | no |
|`FUN_00781590` | **PreloadAssets** | Loads assets | no |
|`FUN_00811450` | **ModuleHandler** | Loads and initializes each game module, bringing in level data, assets, and scripts needed for the world. | yes |
|`FUN_0080deb0` | **GameObjUpdate** | Processes incoming object-update packets, creating or refreshing in-game entities and syncing their state during the loading phase. | yes |
|`FUN_007BE4C0` | **ModuleChunkLoadCore** | Allocates and parses a module's sub-chunks, updates the loading screen between groups, and marks the chunk as done. | yes |
|`FUN_0073fea0` | **AppState_GetGuiContext** | Returns the GUI/context pointer stored at offset `0x274` inside the app-state child object. `ModuleChunkLoadCore` uses this once to populate its GUI context before constructing UI chunks; other loading/UI code also calls it. | yes |
|`FUN_00740c60` | **AppState_GetLoadProgressByte** | Reads one byte from the app-state child object's load-progress table at `0x3e4 + index`. `ModuleChunkLoadCore` calls this with indexes `3`, `1`, and `0` before each loading-frame update to compute the next load-bar value. | yes |
|`FUN_0091c860` | **Runtime_FloatToInt_ST0** | Compiler/runtime helper that converts the current x87 `ST0` floating-point value to an integer. In `ModuleChunkLoadCore`, it rounds the computed load-bar value before it is applied. | yes |
|`FUN_007405d0` | **AppState_SetLoadBarValue** | Wrapper around the load-bar setter. It forwards the computed value and update flag to the app-state child object, then traces `Load Bar = %d` when a load-bar GUI object is present. | yes |
|`FUN_007bc8f0` | **CSWGuiFade_SetTransitionState** | Updates a fade GUI object's target color/vector, screen dimensions, alpha/progress fields, mode flag, duration, and start timestamp. `ModuleChunkLoadCore` uses this on the conditional 3D scene/fade path. | yes |
|`FUN_0040dac0` | **LoadingScreenFadeUpdateFrame** | Creates the loading-screen fade GUI if needed, updates its 3D scene state, sets fade alpha/progress, draws one loading-screen frame, and exits its guarded loading-screen section. | yes |
|`FUN_008c0cb0` | **CSWGuiLoadModuleDebugMenu_Ctor** | Constructs and initializes the Load Module Debug Menu. Sets the vftable, initializes standard debug labels (e.g., LB_OPTIONS, LBL_BUILD), formats build/version text, scans module directories, filters valid module files (e.g., _s.rim), merges/sorts results, and populates the module selection list. | yes |
|`FUN_008bfb60` | **CSWGuiPowersFeatsSkillsDebugMenu_Ctor** | Constructs and initializes the Powers/Feats/Skills Debug Menu. Sets the vftable, initializes debug UI labels and build info, and performs menu-specific setup via internal initialization routines. | yes |
|`FUN_008bba80` | **CSWGuiDialogCinematic_Ctor** | Constructs and initializes the cinematic dialog GUI. Sets the CSWGuiDialogCinematic vftable, loads dialog_p resources, initializes the replies/message labels (LB_REPLIES, LBL_MESSAGE), configures multiple reply-related child widgets, creates auxiliary dialog state via FUN_008bb5d0, and marks the GUI instance as active. | yes |
|`FUN_008bd910` | **CSWGuiDialogComputerCamera_Ctor** | Constructs and initializes the computer-camera dialog GUI. Sets the CSWGuiDialogComputerCamera vftable, initializes an embedded CSWGuiBlackenedLabel, loads computercam_p resources, creates the LBL_RETURN label, and finalizes the layout/state for the computer-camera dialog screen. | yes |
|`FUN_008bc620` | **CSWGuiDialogComputer_Ctor** | Constructs the terminal interaction GUI. Sets the vftable, loads computer_p resources, and initializes terminal-specific labels for Computer Spikes (LBL_COMP_SPIKES), Repair Parts, and the 10-slot status bar. | yes |
|`FUN_0089c5f0` | **CSWGuiSkillInfoBox_Ctor** | Constructs the Skill Info UI box. Sets the vftable, loads skillinfo_p resources, and maps the skills list (LB_SKILLS), info message (LBL_MESSAGE), and confirmation button (BTN_OK). Also initializes a 20-slot array (0x14 iterations) for skill data entries and registers interaction callbacks. | yes |
|`FUN_008b1ea0` | **CSWGuiContainer_Ctor** | Constructs the loot/container GUI. Sets the vftable, loads container_p resources, and maps list/button elements including LB_ITEMS, BTN_GIVEITEMS (Take All), and BTN_OK. It also calculates UI scaling factors and registers button callbacks (B/X/Cancel) for inventory interaction. | yes |
|`FUN_008becf0` | **CSWGuiExamine_Ctor** | Constructs the Examine/Description GUI. Sets the CSWGuiExamine vftable, initializes the base class, and sets up the internal text buffer (0x62E size) used to display item descriptions and lore | yes |
|`FUN_008bee10` | **CSWGuiCreateDebugItemSubMenu_Ctor** | Constructs the Item Creation Debug menu. Sets the vftable, loads debug_p resources, and initializes the LB_OPTIONS list and LBL_BUILD text label. It also pulls a "Build: " string (likely for versioning or debug tracking) and registers UI tracing for the submenu. | yes |
|`FUN_00898ca0` | **CSWGuiTutorialBox_Ctor** | Constructs the Tutorial Message GUI. Sets the vftable, initializes the base class via FUN_0075ae40, and loads the fnt_d16x16 font resource. It also sets default background transparency/colors and defines the internal text buffer properties. | yes |
|`FUN_0073F870` | **ModuleChunkLoadWrapperA** | Wrapper for ModuleChunkLoadCore | no |
|`FUN_0078C330` | **ModuleChunkLoadWrapperB** | Wrapper for ModuleChunkLoadCore | no |
|`FUN_00747210` | **InitializeGameUI** | Constructs and configures the entire in-game user interface | yes |
|`FUN_00919723` | **AllocateMemoryOrThrow** | General memory allocator | yes |
|`FUN_00855F30` | **PopulateSaveGameEntry** | Parses a single save file's metadata and assets—reads playtime, area name, timestamps, hints, corruption flags, screenshots, thumbnails, etc.—and fills in the UI data structure used by the save-selection screen | no |
|`FUN_008C19F0` | **DebugMenuConstructor** | Makes debug menu | yes |
|`FUN_0085FBE0` | **CSWCAnimBase_LoadModel** | Loads an animated model for a world character | no |
|`FUN_00904A80` | **CharacterCreationScreen** | Character Creation Screen | no |
|`FUN_00523870` | **LoadModuleEnvironmentAndUI** | Loads module environment and UI | no |
|`FUN_0055A460` | **ModuleLoadCoordinator** | Allocates a loader object | no |
|`FUN_00521360` | **AreaConstructor** | Constructor for an area/scene object. Initializes vtables, allocates subcomponents, and prepares structures for later resource loading (no direct disk reads). | yes |
|`FUN_0055a460` | **LoadOrCreateAreaAndInit** | High-level scene/area loader. Decides whether to reuse or create a new area object, initializes it, and begins environment/UI setup. Delegates actual asset streaming to deeper loader functions. | no |
|`FUN_00647050` | **EnqueueStreamingRequest** | Builds a small command packet (opcode 0x50) and pushes it into the streaming system's ring buffer via a vtable call to the streaming manager at DAT_00a1b4a4 + 8. This is the entry point for loading/streaming assets from disk. | no |
|`FUN_00637270` | **RequestResourceStream** | Prepares parameters for a resource/asset request, does some validation, and then calls the streaming enqueue function | no |
|`FUN_00401730` | **InitResourceManager** | Allocates the root resource-manager object: client app at `+0x4`, later-filled loading/resource manager pointer at `+0x8`, two 0x184-byte resource tables at `+0xc/+0x10`, load-state object at `+0x14`, tick count at `+0x18`, and a 0x40000-byte scratch/queue buffer at `+0x0`. | no |
|`FUN_00401cf0` | **ResourceRoot_EnsureScratchBuffer** | Lazily allocates the root object's 0x40000-byte buffer at offset `+0x0`. | no |
|`FUN_0073ef30` | **InitClientExoApp** | Sets up the game's core client application object | no |
|`FUN_00780460` | **InitClientCoreSystems** | Allocates and zero-initializes the main client game object, then sets up dozens of subsystems (resource queues, streaming buffers, graphics/audio settings, network structures, and various runtime managers). | no |
|`FUN_0073f7f0` | **CClientExoApp_GetResourceQueue** | `CClientExoApp` vtable slot `+0x10`; returns the queue pointer at `clientCore + 0x10`, which is passed to `ProcessResourceQueue`. | no |
|`FUN_0073f930` | **CClientExoApp_QueuePacketDispatch** | `CClientExoApp` vtable slot `+0x4`; forwards packet work into `ResourceQueue_UnpackAndTrace`. | no |
|`FUN_00934C70` | **OpenGameAsset** | Low Level call to open a game asset | no |
|`FUN_0091caeb` | **_fopen** | C standard library | yes |
|`FUN_00475ab0` | **OpenOrStreamGameFile** | Opens a game asset or streams it | no |
|`FUN_004762f0` | **ReadGameAssetChunk** | Reads a block of game asset data from either an open file stream or an already-loaded memory buffer. If reading from disk, allocates memory and performs I/O. If reading from memory, returns a pointer to the requested chunk and advances the buffer pointer without performing any file I/O. | no |
|`FUN_00458b70` | **Gob::constructor** | Constructs a game object | yes |
|`FUN_0045a030` | **Gob::LoadFromFileOrStream** | Loads a Gob game object from disk or an in-memory stream, initializes its subcomponents, and registers it with the game world. | no |
|`FUN_007ac470` | **UpdatePlayerInputAndTargeting** | Per-frame player control and targeting loop. Reads input devices (mouse/gamepad) for look movement, applies sensitivity/inversion settings, updates camera pitch/yaw, and scans nearby objects for valid interaction/target highlighting. Writes results to HUD and interaction state. | no |
|`FUN_0051eed0` | **LoadEventQueueFromConfig** | Iterates the "EventQueue" section, allocates and initializes event nodes from each entry, enqueuing successful ones and freeing failures. | no |
|`FUN_005205f0` | **BuildEventFromConfigEntry** | Reads one "EventQueue" entry, fills common fields (tag/ids), and deserializes a typed "EventData" payload based on EventId (allocating and constructing the appropriate struct). Returns 1 on success, 0 for invalid EventId | no |
|`FUN_00423AB0` | **ParseTXIAndBuildTextureController** | Parses TXI directives for a texture: creates the appropriate procedural texture controller from proceduretype and applies all TXI flags/params (mipmaps, clamp, bump, envmap, etc.). Finally lets the controller parse its own extra options. | yes |
|`FUN_00423000` | **CAurTextureBasic::Ctor** | Constructs a texture object, sets defaults, stores names, allocates helper state. | no |
|`FUN_00424B10` | **Texture_ApplyTXIAndBuildController** | Opens or reads a texture's TXI sidecar/in-memory TXI text, parses each directive with `ParseTXIAndBuildTextureController`, and builds/removes the procedural texture controller state. | yes |
|`FUN_0045BF50` | **Texture_ApplyTXIBlendingMode** | Opens or reads the same TXI sidecar family, extracts the `blending` directive, and applies additive/punchthrough render state across related mesh/material children. | yes |
|`FUN_004DA3F0` | **Texture_ApplyTXIMaterialDirectives** | Opens or reads a texture TXI sidecar/in-memory TXI text and feeds each trimmed line into the material directive parser at `FUN_004dab20`. | yes |
|`FUN_00704060` | **NetLayer::SendMessageToPlayer** | Writes a message into the player's outgoing network buffer. Validates available space to avoid overflow (logging a detailed dump if it would exceed the buffer), copies the message length and payload into the queue, and advances the write pointer. This is what sends packets to ProcessResourceQueue | no |
|`FUN_00665280` | **SendMessageWithSHeader** | This function constructs and sends a network message prefixed with the byte 0x53 to a specific player. It allocates a buffer, copies data from a source function (FUN_00734010), and sends it using NetLayer::SendMessageToPlayer. | no |
|`FUN_00884450` | **SendMessageWithSHeader2** | This function constructs and sends a network message prefixed with the byte 0x73, which is different from the 0x53 message sent by FUN_00665280. It copies the payload from a source function (FUN_00734010). | no |
|`FUN_007045b0` | **SendBNCSUMessage** | This function sends a network message with a hardcoded, 9-byte header of 0x42 0x4e 0x43 0x53 0x55. It first checks a state flag to prevent the message from being sent repeatedly. It then copies the payload from FUN_00734010, sends the message to a player, and updates several state variables. | no |
|`FUN_00704970` | **HandleIncomingBNCRPacket** | This function acts as a message handler. It receives an incoming packet, validates its structure, and then processes it based on a value in the packet's header (local_14). It constructs and sends a new, 10-byte response message with the header 0x42 0x4e 0x43 0x52, and updates various game state flags based on the type of incoming message. | no |
|`FUN_00812350` | **HandleNetEvents** | This function acts as a dispatcher for various network events, based on a single character parameter. It handles different cases: param_1 == 1 logs a formatted message and sends a network packet prefixed with 0x73; param_1 == 2 performs a specific state change by calling FUN_0073f890; param_1 == 3 handles a complex multi-step event by checking flags and calling other functions. | no |
|`FUN_0087a350` | **SendMessageWithPHeader** | Sends a compact, 3-byte network message with a header of 0x70 and two variable parameters. | no |
|`FUN_00883560` | **InitializeAndSyncState** | A major function that performs a series of game state updates, logs a formatted message, and sends it over the network. It also calls FUN_007045b0 to send a specific message and interacts with a resource scheduler to manage concurrent tasks. | no |
|`FUN_008faca0` | **ModuleLoadSynchronization** | A critical state-change function, similar to FUN_00883560, that synchronizes game data. It updates multiple game variables, logs a formatted message, sends it over the network, and interacts with the resource scheduler to manage assets and tasks. | no |
|`FUN_00711360` | **ResourceStreamer_Init** | This function initializes the resource streaming system. It determines available physical memory to size streaming buffers and launches a dedicated worker thread to handle the loading of game assets from storage into memory. | no |
|`FUN_005363a0` | **SubsystemManager_Init** | This function is a core initializer that sets up the game's key subsystems. It allocates memory and constructs various manager objects, including those for a message queue, resource queues, and an in-game virtual machine for scripts. It also establishes file paths for different game directories and logs each step of the initialization process for debugging. | no |
|`FUN_00788d90` | **GameClient_Init** | This function performs the comprehensive setup for the game's client-side environment. It reads settings from the swkotor2.ini file to configure graphics options like "FullScreen" and "Texture Quality." It also initializes core game objects, loads localization data, and sets up various other subsystems needed to run the game, such as the resource streamer and other game-specific managers. | no |
|`FUN_0073fad0` | **GameClient_Init_Wrapper** | Wrapper function for GameClient_Init | no |
|`FUN_00401bc0` | **LoadingScreenManager_Init** | This function initializes the loading screen manager. It first checks if a previous manager exists and cleans it up. It then allocates and initializes a new manager object, preparing the game to display and manage loading screens. | no |
|`FUN_00882230` | **ModuleLoader** | This function is a core module and resource loader, primarily used when transitioning between different game areas or beginning a new game. It first sets up the necessary file system paths for game resources and then allocates memory for and initializes the main module object. It performs low-level file I/O to load various resources and also uses a tracing system to log its progress. This function is an integral part of the loading sequence, working with the loading screen manager to ensure a smooth transition into the game world. | no |
|`FUN_00880740` | **MainMenu_Init** | This function is the constructor for the game's main menu. It initializes the UI, loads the main menu's visual resources, and checks for the existence of game files like GAMEINPROGRESS to enable or disable menu options. It also registers several event handlers, including one for the ModuleLoader, which allows clicking "New Game" or "Load Game" to start the game's loading process. It can also set up a 3D scene for the main menu's background, depending on the game's state. | no |
|`FUN_0070a620` | **GameState_Manager** | This function is a core state machine manager that controls the high-level flow of the game. It uses a stack-based system to transition the game between various modes, such as the main menu, loading screen, or an active game session. By pushing and popping states, it ensures that the game's logic and resource management are correctly configured for the current context. This is the central function that dictates what the game is doing at any given moment. | no |
|`FUN_00409750` | **Graphics_InitAndMainLoop** | This function is responsible for the game's core startup and primary execution loop. It initializes the graphics context, reads display settings from the swkotor2.ini file, and creates a high-priority worker thread for asynchronous tasks. It also manages the game's window, including disabling the desktop taskbar to ensure a full-screen experience. | no |
|`FUN_00408af0` | **Window_CreateRender** | This function creates a secondary, specialized window dedicated to rendering the game's graphics. It registers a new window class with the name "Render Window," then creates the window itself. The handle to this window is stored in a global variable, DAT_00a1b484, and is the surface where the game's visuals are drawn. | no |
|`FUN_004763b0` | **ConfigParse** | Takes a filename as a parameter and parses the file. Parses a file named startup.txt, unsure what it is for | no |
|`FUN_00462320` | **LevelLoaderAndInitializer** | This function acts as a level loader and object factory. It takes a filename or resource ID and loads a primary game resource (e.g., a level or scene file). It then optionally loads two related variant resources (suffixed with _x and _z) and binds all of them to a newly created game object. This function serves as the central entry point for creating and initializing a major game entity based on external resource files. | yes |
|`FUN_00475c60` | **FindFileInCache** | This function attempts to retrieve a file from a pre-loaded cache. It takes a file identifier and other parameters as input. If the file is found in the cache, it allocates a small data structure, populates it with information about the cached file (like its memory address and size), and returns a pointer to this structure. If the file is not found, the function returns 0 | no |
|`FUN_00401c70` | **SubsystemDeinitializer** | This function de-initializes a game subsystem. It first checks a flag to determine if the subsystem is active. If so, it calls multiple cleanup functions and frees memory before setting the active flag to zero. A final, generic cleanup routine is always executed. | no |
|`FUN_00537870` | **QueueManagerAndTimerReset** | This function manages the cleanup of a game's resource or event queue. It stops a game component, then processes and removes items from the queue. The cleanup method for each item depends on specific parameters. After clearing the queue, it resets a high-precision timer, with the timer value depending on the current game state, in preparation for the next phase. | no |
|`FUN_00410530` | **GUI_Update3DSceneView** | Initializes or updates the state of a 3D scene view GUI element. It handles rendering flags, increments the active view counter, and triggers the loading and rendering of 3D assets. It can either perform a full load for a new scene or a quick update for an existing one. | yes |
|`FUN_0070ed50` | **Audio_Related** | Something to do with audio - would need to look into further but out of scope for now | no |
|`FUN_00410890` | **FindObjectInList** | Linear searches for an object within a list | no |
|`FUN_0083ea60` | **ArrayAdd** | Adds an element to an array and resizes it if it is too small | yes |
|`FUN_00423860` | **SortedInsertTexture** | Inserts a sorted texture into an array | no |
|`FUN_00427810` | **TexturePoolCleanupAndRefresh** | Cleanup and initialization routine for a texture or resource pool | no |
|`FUN_00737540` | **ModuleDirectoryScanner** | Walks through the file system and for each file in a directory does something | yes |
|`FUN_006310d0` | **LoadGame** | Loads a save game | no |
|`FUN_005308b0` | **GameSaveLoadManager** | Central save/load system dispatcher for the game. It uses a single-byte parameter to determine which action to perform | no |
|`FUN_00711750` | **ResourceLoader** | Main function that loads resources | no |
|`FUN_00711690` | **ResourceLoaderWrapper** | Wrapper for resource loader | no |
|`FUN_00711c20` | **ResourceEnsureLoaded** | Ensures a resource entry has resident data. If the entry is not loaded, dispatches to the correct backend based on the high bits of the packed resource id, handles async completion waits/finalization, bumps the resource refcount, and returns the loaded data pointer. | yes |
|`FUN_00713fb0` | **ResourceLoadFromArchiveSlot** | Loads a resource from an indexed archive/resource-manager slot. Allocates a destination buffer, asks the archive vtable for size/read operations, and marks the resource loaded when the read and parse callback succeed. | yes |
|`FUN_00713e80` | **ResourceLoadMemoryBacked** | Resolves a resource whose data is already memory-backed by the resource manager. Sets the resource size/data pointer, marks it loaded, and runs the resource parse callback without a loose-file read. | yes |
|`FUN_00713bf0` | **ResourceLoadFromArchive** | Loads a resource through the default archive/resource-manager backend. Similar to `ResourceLoadFromArchiveSlot`, but uses the archive object directly rather than a selected archive slot. | yes |
|`FUN_007133a0` | **ResourceLoadFromLooseFile** | Loads a resource from the loose-file backend. Opens the file, measures it, allocates the resource buffer, reads file bytes, and invokes the resource parse callback. | yes |
|`FUN_00712f30` | **Resource_AllocateLoadBuffer** | Allocates the destination buffer for an archive or loose resource load. If the resource-memory budget is tight, this path can purge unused loaded resources before allocating. | yes |
|`FUN_007270a0` | **CExoResFile_AddRefSyncOpen** | BIF/KEY reader sync open/reference wrapper used by archive loads before size/read work. Calls the concrete open handle routine on the first active reference. | yes |
|`FUN_007270f0` | **CExoResFile_AddRefAsyncOpen** | BIF/KEY reader async open/reference wrapper used by async archive loads. | yes |
|`FUN_00727450` | **CExoResFile_OpenSyncHandle** | Opens the BIF/KEY packed-file handle for sync reads. Timed separately from the wrapper to expose actual file-open cost. | yes |
|`FUN_007275b0` | **CExoResFile_OpenAsyncHandle** | Opens the BIF/KEY packed-file handle for async reads. | yes |
|`FUN_00727390` | **CExoResFile_GetResourceSize** | Returns the resource byte size from the BIF/KEY reader table. | yes |
|`FUN_00727930` | **CExoResFile_ReadResourceSync** | Seeks to a BIF/KEY resource offset and reads its bytes into the destination buffer. | yes |
|`FUN_007279f0` | **CExoResFile_ReadResourceAsync** | Queues or performs the async BIF/KEY resource read path. | yes |
|`FUN_007272f0` | **CExoResFile_ReleaseSyncClose** | Releases the sync BIF/KEY reader reference and closes the handle when the reference count reaches zero. | yes |
|`FUN_00727340` | **CExoResFile_ReleaseAsyncClose** | Releases the async BIF/KEY reader reference and closes the handle when the reference count reaches zero. | yes |
|`FUN_007295b0` | **ArchiveReaderShared_AddRefSyncOpen** | Shared sync open/reference wrapper used by encapsulated/RIM-style archive readers. Calls the concrete image/open routine on the first active reference. | yes |
|`FUN_00727bb0` | **CExoEncapsulatedFile_AddRefAsyncOpen** | ERF/MOD/HAK reader async open/reference wrapper. | yes |
|`FUN_00727e30` | **CExoEncapsulatedFile_OpenSyncHandle** | Opens and parses the ERF/MOD/HAK archive header and resource table for sync reads. | yes |
|`FUN_00728230` | **CExoEncapsulatedFile_OpenAsyncHandle** | Opens and parses the ERF/MOD/HAK archive header and resource table for async reads. | yes |
|`FUN_00727d90` | **CExoEncapsulatedFile_GetResourceSize** | Returns the resource byte size from the encapsulated archive table. | yes |
|`FUN_00729370` | **CExoEncapsulatedFile_ReadResourceSync** | Seeks to an ERF/MOD/HAK resource offset and reads bytes into the destination buffer. | yes |
|`FUN_00729420` | **CExoEncapsulatedFile_ReadResourceAsync** | Queues or performs the async encapsulated archive resource read path. | yes |
|`FUN_00727d10` | **CExoEncapsulatedFile_ReleaseSyncClose** | Releases the sync encapsulated-file reader reference and closes the handle when needed. | yes |
|`FUN_00727d50` | **CExoEncapsulatedFile_ReleaseAsyncClose** | Releases the async encapsulated-file reader reference and closes the handle when needed. | yes |
|`FUN_00729790` | **CExoResourceImageFile_LoadImage** | Loads a RIM/resource-image file into memory; after this, individual resource reads are memory copies rather than file reads. | yes |
|`FUN_007296e0` | **CExoResourceImageFile_GetResourceSize** | Returns the resource byte size from the loaded RIM/image table. | yes |
|`FUN_00729ae0` | **CExoResourceImageFile_ReadResourceSync** | Copies a resource from the already loaded RIM/image memory block into the destination buffer. | yes |
|`FUN_00729b80` | **CExoResourceImageFile_ReadResourceAsync** | Async wrapper for RIM/image resource reads. | yes |
|`FUN_00729650` | **CExoResourceImageFile_ReleaseSyncClose** | Releases the sync RIM/image reader reference. | yes |
|`FUN_0073da40` | **LooseFileOpen** | Builds a loose-resource filename/mode pair and calls `_fopen`, storing the resulting `FILE*` in the loose-file wrapper object. | yes |
|`FUN_0073dd20` | **LooseFileRead** | Reads bytes from an already opened loose-resource `FILE*` with `_fread`, handling `ferror` and `feof` failure cases. | yes |
|`FUN_00715a60` | **ResourceFinalizeAsyncLoad** | Finalizes a completed async resource request: closes/releases backend handles, clears async flags, marks the resource loaded, invokes the resource parse callback, and clears async state. | yes |
|`FUN_0040fe60` | **SetResourceStateAndTriggerUpdate** | Toggles a state flag for a resource and, if the flag is enabled, triggers a batch update and logging process. | no |
|`FUN_007178e0` | **GFF_LookupFieldLabelByName** | Finds the slot index of a named field within the specific GFF struct/list selected by `param_1` (a pointer to a struct index). It walks that struct's field entries, then compares each entry's label index against the global 16-byte label table at `gffPtr+0x54`. Returns -1 when no matching field is found. | yes |
|`FUN_0040ee40` | **GUI_InitWidgetFromGFF** | Loads and initializes a GUI widget from its GFF layout resource. Creates a `CResGFF` object, opens the `.gui` file via the resource streamer, then parses `COLOR`/`BORDER`/`BACKGROUND`/`ALPHA`/`CONTROLS` fields and scales coordinates to screen resolution. Guards against double-init via a flag bit. | yes |
|`FUN_00418df0` | **GUI_FindAndBindControlByTag** | Searches a GUI `CONTROLS` list for a child control whose `TAG` field matches the requested widget name. For each control, it reads the `TAG` string through the GFF field APIs; when a match is found, it calls the panel's bind vfunc with that GFF node and returns. | yes |
|`FUN_0040f620` | **GUI_BindNamedWidget** | Binds a named GUI control from a loaded GFF layout into a member slot on the panel object. Calls `GUI_FindAndBindControlByTag` to locate the control by tag, then reads its position/size, applies screen-resolution scaling, handles `LBL_BAR*` special state, and registers the control with the panel. | yes |
|`FUN_00418da0` | **GUI_BindChildStructByName** | Looks up a named child struct inside the current GFF control node and, if present, dispatches the panel's bind vfunc for that child. Used by complex controls for nested pieces such as `SCROLLBAR` and `PROTOITEM`. | yes |
|`FUN_004188e0` | **GUI_BaseControlSetup** | Applies common movement/base setup for a GUI control, including optional `MOVETO` data, then calls the shared base binder. | yes |
|`FUN_00418f20` | **GUI_CommonBaseBinder** | Shared base-control binder. Applies common control data, sets bounds through the control vfunc, reads object id/parent metadata, and attaches the control to its parent when needed. | yes |
|`FUN_00420180` | **GUI_ListBoxBindProtoItem** | Listbox-specific prototype item setup. Reads `PROTOITEM` and `CONTROLTYPE`, allocates the prototype row/control object, then recursively binds the `PROTOITEM` child. | yes |
|`FUN_00718a00` | **GFF_ReadBoolFieldByName** | Reads a boolean field by label from a GFF struct, returning a default when the field is missing or not a bool. | yes |
|`FUN_00718c80` | **GFF_ReadIntFieldByName** | Reads an integer field by label from a GFF struct, returning a default when the field is missing or not an int. | yes |
|`FUN_00719520` | **GFF_ReadVector3FieldByName** | Reads a 3-value vector/color field by label from a GFF struct, falling back to a provided default vector when missing or malformed. | yes |
|`FUN_008bdc90` | **CSWGuiBarkBubble_Ctor** | Constructs the bark bubble overlay — the small floating speech bubble that appears above characters during ambient dialogue. Minimal setup: sets vftable, initializes a single text label. | yes |
|`FUN_0075ae40` | **CSWGuiMessageBox_Ctor** | Constructs a general-purpose modal confirmation dialog (OK/Cancel). Used for confirmations throughout the game UI. Also invoked as the base constructor for CSWGuiControllerLossBox and CSWGuiTutorialBox. | yes |
|`FUN_0075b370` | **CSWGuiMessageBoxVariant_Ctor** | Variant of CSWGuiMessageBox_Ctor that supports datapad-style messages with a slightly different layout. | yes |
|`FUN_008ba980` | **CSWGuiDialogLetterbox_Ctor** | Constructs the letterbox overlay used during cinematic dialogue sequences (black bars top/bottom). Sets the vftable and zero-initializes the overlay state. | yes |
|`FUN_007bc600` | **CSWGuiFade_Ctor** | Constructs the screen-fade overlay GUI element. Loads the "fade_p" resource and initializes the fade state. | yes |
|`FUN_00754ed0` | **CSWGuiInGameMenu_Ctor** | Constructs the main in-game pause/options menu. Sets up UI elements including labels, buttons, and status bars. | yes |
|`FUN_008b91f0` | **CSWGuiInGamePause_Ctor** | Constructs the pause-screen overlay, including the pause reason label and unpause button. | yes |
|`FUN_008b8c40` | **CSWGuiInGameSoloModeQuery_Ctor** | Constructs the solo-mode confirmation dialog shown when switching party members in or out. Uses a base dialog template. | yes |
|`FUN_008b82e0` | **CSWGuiInGameAreaTransition_Ctor** | Constructs the area-transition notification screen that displays the location name on area change. Loads the "areatrans_p" resource template. | yes |
|`FUN_00757c40` | **CSWGuiInGameMessages_Ctor** | Constructs the in-game combat log / message display. Initializes message bars and filter buttons. | yes |
|`FUN_008b4270` | **CSWGuiStore_Ctor** | Constructs the merchant/shop UI with elements for buying and selling items. | yes |
|`FUN_008a92d0` | **CSWGuiInGameEquip_Ctor** | Constructs the character equipment screen. Initializes weapon slots, equipment list slots, and supporting UI labels. | yes |
|`FUN_008a6170` | **CSWGuiInGameInventory_Ctor** | Constructs the player inventory/item management screen with filter buttons and item category lists. | yes |
|`FUN_0084c3a0` | **CSWGuiInGameCharacter_Ctor** | Constructs the full character sheet UI for stats, attributes, skills, alignment, and saving throws. | yes |
|`FUN_0075cec0` | **CSWGuiStatusSummary_Ctor** | Constructs the status summary / journal overview panel with status labels. | yes |
|`FUN_00893950` | **CSWGuiInGameMap_Ctor** | Constructs the local area map display, including the map rendering surface and navigation buttons. | yes |
|`FUN_008a25c0` | **CSWGuiInGameAbilities_Ctor** | Constructs the abilities/feats/powers/skills screen, including the tab buttons, list rows, description panes, and supporting labels. | yes |
|`FUN_007fae60` | **CSWGuiInGameJournal_Ctor** | Constructs the player journal screen and allocates its internal message data structure. | yes |
|`FUN_008a1170` | **CSWGuiInGameOptions_Ctor** | Constructs the in-game options menu for load, save, quit, and settings actions. | yes |
|`FUN_0089cf30` | **CSWGuiPartySelection_Ctor** | Constructs the party member selection screen with party slots and a 3D character preview. | yes |
|`FUN_008973d0` | **CSWGuiInGameGalaxyMap_Ctor** | Constructs the galaxy/world map with dual 3D scene views and planet buttons. | yes |
|`FUN_007d0760` | **CSWGuiInGamePanel_LazyInitTab** | Lazy-init dispatcher for the 8-tab in-game panel (Equip/Inventory/Character/Abilities/Party/Journal/Map/Options). Deactivates the previous tab slot and constructs the new tab's GUI object only if its slot is null. `ModuleChunkLoadCore` pre-warms all slots during loading to avoid per-tab stutter during gameplay. | no |
|`FUN_007c9df0` | **CSWGuiInGamePanel_Open** | Opens the in-game panel to a given tab index (0–7). Fires the `k_sup_guiopen` script event, calls `CSWGuiInGamePanel_LazyInitTab` to construct the tab if needed, shows the panel, and sets the visible flag. | no |
|`FUN_007ca060` | **CSWGuiInGamePanel_Close** | Hides the in-game panel. Calls `CSWGuiInGamePanel_LazyInitTab` with -1 as the new tab to deactivate the current tab without switching. Clears the visible flag and triggers scene-view cleanup. | no |
|`FUN_007ca550` | **CSWGuiInGamePanel_GoToTab** | Switches directly to a specific tab index (0–7) if it differs from the current tab. Lazy-constructs the destination tab via `CSWGuiInGamePanel_LazyInitTab`, then shows and focuses it. | no |
|`FUN_007ca230` | **CSWGuiInGamePanel_NextTab** | Advances the active tab index by 1, wrapping from 7 back to 0, then switches via `CSWGuiInGamePanel_LazyInitTab`. | no |
|`FUN_007ca3c0` | **CSWGuiInGamePanel_PrevTab** | Decrements the active tab index by 1, wrapping from 0 back to 7, then switches via `CSWGuiInGamePanel_LazyInitTab`. | no |
|`FUN_00411170` | **UpdateObjectCollectionsAndTrace** | Iterates through collections of objects, updates their state, and logs the process | no |
|`FUN_00715c00` | **Worker_ProcessJob** | Main worker-side job processor that runs after the thread is resumed, likely consuming the shared job fields and performing the actual resource lookup/loading work before the worker goes idle again. | no |
|`FUN_00711600` | **Worker_SubmitJob** | Waits for the worker slot to become free, writes job parameters into the shared worker state, marks the worker busy, and wakes the suspended worker thread. | no |
|`FUN_0069cdb0` | **CExoStats::SerializeCombatInfo** | A GFF serialization function that packs a character's live combat statistics—including attack/damage modifiers, critical hit ranges, and equipped items—into a structured "CombatInfo" field for saving to a file or syncing over the network. | no |
|`FUN_00638bd0` | **GameSaveLoad_Core** | The central dispatcher for the save/load state machine. It coordinates high-level transitions (New Game, Save, or Area Load) by driving the mass-serialization of Gob objects via CExoStats routines. Once data is gathered, it hands the resulting GFF packets to the streaming system via Worker_SubmitJob to be written to disk. | no |
|`FUN_0065f8a0` | **NetPacketMajorDispatcher** | A high-level packet router that parses incoming "p-prefix" buffers. It identifies the packet's Major Type and dispatches it to the appropriate subsystem handler (e.g., Inventory, Dialog, or CharList). It includes strict overflow/underflow checks and wraps every dispatch in a Tracer logging block | no |
|`FUN_0048b6f0` | **Mesh_ParseMaterialAndGeometry** | A high-level material state machine that maps textures (texture0/1), defines mesh buffers (verts, colors, faces), and calculates real-time UV transformations for animated effects like scrolling or jitter. | no |
|`FUN_0093b650` | **VFS_RegisterHandlers** | The core initialization routine for the Virtual File System (VFS). It populates a global table of function pointers that define the engine's I/O interface, including file discovery, metadata retrieval, and archive management. | no |

### Classes
- `0x009AA224`  **CSWGuiMainCharGen::vftable**: Seems to be the class for character creation
- `0x0099C460`  **CResGFF::vftable**: Generic File Format (GFF) loader used to parse and provide access to resources like UTC, UTI, ARE, etc
- `0x0098B5CC`  **Gob::vftable**: Base game object class used to represent in-world entities. Contains a wide range of virtual functions for lifecycle management, serialization, rendering, and asset loading. 
- `0x009a7a74`  **CSWGuiInGameAreaTransition::vftable**: Gui for area transition display during loading screen
- `0x0099493c`  **CSWSModule::vftable::vftable**: Module class
- `0x00992398`  **CServerExoApp::vftable**: The virtual function table for the internal game server. This class manages the world simulation "heartbeat," background task scheduling via the Microsoft Concurrency Runtime, and serves as the primary owner of the world logic that receives and processes network packets from the client.

### Ring buffer connects client and server arch?

### Code Paths
- ProcessResourceQueue
    - HandleBNPacket
    - ResourcePacketDispatcher
    - ResourceQueue_UnpackAndTrace
        - SPacketHandler
        - PPacektHandler
            - ModuleHandler
                - ModuleChunkLoadWrapperA
                    - ModuleChunkLoadCore
                        - InitializeGameUI

#### Resource manager object graph

`GameMain` allocates the root object with `InitResourceManager`, stores it in `DAT_00a1b4a4`, and the main loop then repeatedly consults its `+0x4`, `+0x8`, and `+0x14` children during loading.

Known root layout:

| Offset | Meaning | Evidence |
|---|---|---|
| `+0x0` | 0x40000-byte root scratch/queue buffer | `InitResourceManager` clears it, then `ResourceRoot_EnsureScratchBuffer` allocates it if null. |
| `+0x4` | `CClientExoApp*` | Allocated as 8 bytes by `InitClientExoApp`; vtable is `CClientExoApp::vftable` at `0x99d684`, child core object at `+0x4`. |
| `+0x8` | Loading/resource manager pointer | Starts null in `InitResourceManager`; later `loadingscreen`, `Engine`, and `EnqueueStreamingRequest` use it heavily. It owns loading-state helpers and an embedded queue at `+0x10040`. |
| `+0xc` | 0x184-byte resource table/cache | Allocated by `FUN_0051b830`, which zeroes 0x60 dwords and clears `+0x180`. |
| `+0x10` | Second 0x184-byte resource table/cache | Same constructor as `+0xc`; likely paired active/secondary resource table. |
| `+0x14` | 0x3c-byte load-state object | Constructed by `FUN_004018c0`; fields drive load state, module indexes, resource names, and completion flag checks. |
| `+0x18` | `GetTickCount()` value | Stored during root construction. |
| `+0x1c` | Temporary graphics/loading flag | `FUN_0040bc40` stores the previous state of flag `2` here and `FUN_0040bcf0` restores/toggles it. |

`CClientExoApp` vtable entries that matter for the queue path:

| Vtable offset | Function | Meaning |
|---|---|---|
| `+0x4` | `FUN_0073f930` | Wrapper around `ResourceQueue_UnpackAndTrace`. This is the generic packet-dispatch target reached from `ProcessResourceQueue` through the queue object's handler pointer. |
| `+0x10` | `FUN_0073f7f0` | Returns `*(client + 0x4) + 0x10`, the queue object that `LoadingScreenUpdateFrame` passes to `ProcessResourceQueue`. |

#### Resource queue flow

`ProcessResourceQueue(queue, enabled)` is a drain, not a one-packet tick. If `enabled != 0`, it loops until `queue + 0x20000` (read offset) equals `queue + 0x20004` (write offset). Each packet is stored as:

```text
0x00000..0x0ffff  packet ring buffer bytes
+0x20000          read offset
+0x20004          write offset
+0x20008          generic packet handler object
+0x2000c          paired outgoing queue target used by NetLayer::SendMessageToPlayer
```

For each packet, the consumer reads a 4-byte payload length, checks for a `BN` prefix, and either calls `HandleBNPacket` or dispatches through `(*(queue + 0x20008))->vtable[+0x4]`, which currently resolves to the `ResourceQueue_UnpackAndTrace` path for normal resource packets.

Producer side:

```text
RequestResourceStream / many FUN_0064xxxx helpers
  -> EnqueueStreamingRequest
      writes opcode 0x50 and subtype bytes
      calls (*(DAT_00a1b4a4 + 8))->vtable[+0x10] as a prep/translation hook
      calls NetLayer::SendMessageToPlayer
          writes [length][payload] into the paired queue ring buffer
```

Pump side:

```text
GameMain idle frame:
  if DAT_00a1b4a4 + 8 exists:
      loadingscreenwrapper(DAT_00a1b4a4 + 8)
      break early only when (*(DAT_00a1b4a4 + 0x14))[0] == 1

LoadingScreenUpdateFrame(param2 == 1):
  if DAT_00a1b4a4 + 8 exists:
      loop once:
          loadingscreenwrapper(DAT_00a1b4a4 + 8)
          ProcessResourceQueue(CClientExoApp_GetResourceQueue(DAT_00a1b4a4 + 4), 0)  ; no drain
  ProcessResourceQueue(CClientExoApp_GetResourceQueue(DAT_00a1b4a4 + 4), 0)
```

The artificial limit is therefore not inside `ProcessResourceQueue`; when the flag is `1`, the function drains everything currently available. The limit is around how often the outer loading/update frame asks the loading manager to produce/drain real work. The explicit calls from `LoadingScreenUpdateFrame` pass `0`, so their current value is probably timing/side-effect noise rather than queue throughput.

#### ModuleChunkLoadCore timing read
Current parse run:

```text
python loadingscreen_timeparse.py "D:\SteamLibrary\steamapps\common\Knights of the Old Republic II\kotor2_log.txt"
```

`ModuleChunkLoadCore` totals 2347.15ms over 5 calls, averaging 469.43ms per call. Its parent chain is much larger: `ModuleHandler` totals 2684.47ms over 15 samples, while `PpacketHandler` totals 6337.81ms over 294 samples and `ResourceQueue_UnpackAndTrace` totals 6341.12ms over 305 samples. The outer loading screen hook totals 5322.90ms over 6030 samples.

These timings are inclusive hook totals, not exclusive flamegraph time. Nested work is counted in both parent and child hooks, so phase totals should be read as attribution clues rather than values that add exactly to `ModuleChunkLoadCore`.

The current strongest nested signals are:

- `ResourceQueue_UnpackAndTrace`: 6341.12ms total over 305 calls, averaging 20.79ms per call.
- `PpacketHandler`: 6337.81ms total over 294 calls, averaging 21.56ms per call.
- `ProcessResourceQueue`: 6393.71ms total over 12564 calls, averaging 0.51ms per call.
- `ResourceEnsureLoaded`: 3755.67ms total over 9502 calls, averaging 0.40ms per call.
- `LooseFileRead`: 3272.76ms total over 30878 calls, averaging 0.11ms per call.
- `OpenOrStreamGameFile`: 2850.51ms total over 3235 calls, averaging 0.88ms per call.
- `GUI_BindNamedWidget`: 717.59ms total over 2997 calls, averaging 0.24ms per call.
- `GUI_FindAndBindControlByTag`: 670.19ms total over 2997 calls, averaging 0.22ms per call.
- `fopen`: 391.77ms total over 10652 calls, averaging 0.04ms per call.

`GUI_BindNamedWidget` and `GUI_FindAndBindControlByTag` are nested, not additive. `GUI_BindNamedWidget` calls `GUI_FindAndBindControlByTag` first, then performs position/size scaling, `LBL_BAR*` special handling, and control registration. The high total is mostly from call volume and GFF tag lookup work.


#### ModuleChunkLoadCore phase breakdown
Ghidra shows five `LoadingScreenUpdateFrame(_DAT_00986da8, 0, 0)` calls inside `ModuleChunkLoadCore`. The current run also has five `ModuleChunkLoadCore` samples, so the constructor totals line up cleanly as "one pass per module load" signals. The five update-frame calls themselves total 2270.60ms over 190 calls, averaging 11.95ms each, which means the frame/update path is no longer just a marker; it is a meaningful part of the load.

| Phase | Ghidra boundary | Main work | Current direct timing signal |
|---|---|---|---|
| Phase 0 | Start to first `LoadingScreenUpdateFrame` | `DebugMenuConstructor`, `CSWGuiLoadModuleDebugMenu_Ctor`, `CSWGuiPowersFeatsSkillsDebugMenu_Ctor` | ~0ms |
| Phase 1 | First to second `LoadingScreenUpdateFrame` | Debug/item/dialog/message box setup: `CSWGuiCreateDebugItemSubMenu_Ctor`, `CSWGuiExamine_Ctor`, `CSWGuiBarkBubble_Ctor`, `CSWGuiContainer_Ctor`, `CSWGuiDialogCinematic_Ctor`, `CSWGuiDialogComputerCamera_Ctor`, `CSWGuiMessageBox_Ctor`, `CSWGuiMessageBoxVariant_Ctor`, `CSWGuiSkillInfoBox_Ctor` | ~153ms direct constructor time. Largest signals are `CSWGuiMessageBox_Ctor` at 73.28ms, `CSWGuiContainer_Ctor` at 36.20ms, and `CSWGuiBarkBubble_Ctor` at 31.37ms. |
| Phase 2 | Second to third `LoadingScreenUpdateFrame` | Mid-size in-game UI: `CSWGuiFade_Ctor`, `CSWGuiInGameMenu_Ctor`, `CSWGuiInGamePause_Ctor`, `CSWGuiInGameSoloModeQuery_Ctor`, `CSWGuiInGameAreaTransition_Ctor`, optional `CSWGuiInGameMessages_Ctor`, `CSWGuiStore_Ctor`, `CSWGuiInGameEquip_Ctor`, `CSWGuiInGameInventory_Ctor` | ~414ms direct constructor time. Biggest pieces are `CSWGuiInGameMessages_Ctor` at 84.67ms, `CSWGuiInGameMenu_Ctor` at 82.56ms, `CSWGuiStore_Ctor` at 74.81ms, and `CSWGuiInGameSoloModeQuery_Ctor` at 70.05ms. |
| Phase 3 | Third to fourth `LoadingScreenUpdateFrame` | Character/status/main interface: `CSWGuiInGameCharacter_Ctor`, `CSWGuiStatusSummary_Ctor`, `InitializeGameUI` | ~415ms direct constructor/init time: `CSWGuiInGameCharacter_Ctor` at 208.06ms, `InitializeGameUI` at 183.52ms, and `CSWGuiStatusSummary_Ctor` at 23.48ms. Because `InitializeGameUI` may overlap nested GUI work, do not add this phase directly against other GUI totals. |
| Phase 4 | Fourth to fifth `LoadingScreenUpdateFrame` | Late in-game panels: `CSWGuiInGameMap_Ctor`, `CSWGuiInGameAbilities_Ctor`, `CSWGuiInGameJournal_Ctor`, `CSWGuiInGameOptions_Ctor`, `CSWGuiPartySelection_Ctor`, `CSWGuiInGameGalaxyMap_Ctor` | ~292ms direct constructor time, led by `CSWGuiInGameGalaxyMap_Ctor` at 129.63ms. The rest are medium-cost repeated panels: journal 34.66ms, map 31.98ms, party selection 31.03ms, abilities 40.09ms, options 24.53ms. |

Current direct constructor ranking inside the phases:

- `CSWGuiInGameCharacter_Ctor`: 208.06ms total, 41.61ms average over 5 calls.
- `InitializeGameUI`: 183.52ms total, 36.70ms average over 5 calls.
- `CSWGuiInGameGalaxyMap_Ctor`: 129.63ms total, 25.93ms average over 5 calls.
- `CSWGuiInGameMessages_Ctor`: 84.67ms total, 8.47ms average over 10 calls.
- `CSWGuiInGameMenu_Ctor`: 82.56ms total, 16.51ms average over 5 calls.
- `CSWGuiStore_Ctor`: 74.81ms total, 14.96ms average over 5 calls.
- `CSWGuiMessageBox_Ctor`: 73.28ms total, 7.33ms average over 10 calls.
- `CSWGuiInGameSoloModeQuery_Ctor`: 70.05ms total, 14.01ms average over 5 calls.

Current hypothesis: the best optimization target is probably still not one GUI constructor's own logic. The latest run is heavily resource/file I/O shaped (`ResourceEnsureLoaded`, `ResourceLoadFromArchiveSlot`, `LooseFileRead`, `OpenOrStreamGameFile`) while `GUI_BindNamedWidget` / `GUI_FindAndBindControlByTag` remain the biggest repeated nested GUI path. Phase work is useful for attribution, but the larger win is likely reducing repeated resource reads, loose-file/archive load cost, or repeated widget lookup/bind work.

#### Archive resource load breakdown

`ResourceLoadFromArchive` and `ResourceLoadFromArchiveSlot` now have simple direct hooks for the concrete archive-reader vtable calls seen in Ghidra:

- open/reference wrappers: `CExoResFile_AddRefSyncOpen`, `ArchiveReaderShared_AddRefSyncOpen`, async variants
- concrete open/image load routines: `CExoResFile_OpenSyncHandle`, `CExoEncapsulatedFile_OpenSyncHandle`, `CExoResourceImageFile_LoadImage`
- size lookup: `*_GetResourceSize`
- destination allocation: `Resource_AllocateLoadBuffer`
- data transfer: `*_ReadResourceSync` / `*_ReadResourceAsync`
- close/release wrappers: `*_ReleaseSyncClose` / `*_ReleaseAsyncClose`

## Build Instructions
Download [MinHook](https://github.com/TsudaKageyu/minhook)

Place it in the repository directory and include both the src and lib then run

`build.bat`

---
Note:

- Only works for steam version on Windows
- Testing is still minimal as development is early so use at your own risk
