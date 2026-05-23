# Gob Class Reference

`Gob` is a large C++ game object class with MSVC RTTI and a 119-entry virtual function table at `0x0098b5cc`.

The object appears to derive from `CAurObject`. Its constructor first installs `CAurObject::vftable`, then replaces it with `Gob::vftable`. The class owns object identity fields, model/resource pointers, transform state, animation queues, render flags, material state, attachment/helper objects, and path/collision-related data.

Most names below are provisional reverse-engineering names. Entries marked as simple accessors are mechanically clear from the decompile, even when the field's game meaning is not fully named yet.

## Important Object Fields

| Offset | Observed Role |
|---:|---|
| `+0x00` | vtable pointer |
| `+0x20`, `+0x22` | 16-bit fields with getter/setter pairs |
| `+0x24`, `+0x25` | byte flags with getter/setter pairs |
| `+0x28..+0x34` | four-value state block, likely color/render/material parameters |
| `+0x3c..+0x44` | three-value state block propagated into model helpers |
| `+0x64` | name/tag/resref-style string field returned by vtable slot 2 |
| `+0x84` | primary model/resource pointer |
| `+0x90` | alternate or loaded resource pointer |
| `+0x94` | resource/helper pointer getter |
| `+0x98` | model tree / child node container pointer |
| `+0xa4..+0xac` | three-float/vector block getter |
| `+0xb0..+0xbc` | four-value block getter; Ghidra mislabels this as an MFC method |
| `+0xc0..+0xc8` | three-float/vector block getter |
| `+0xdc`, `+0xe0` | animation/event node list pointer and count |
| `+0xec` | propagated byte flag |
| `+0xee` | 16-bit render/state flag toggled by two vtable methods |
| `+0x148` | material/state value written by one material helper |
| `+0x180..+0x193` | multiple boolean render/state flags |
| `+0x1a8..+0x1aa` | small state flags |
| `+0x1ac` | float scalar with getter/setter pair |
| `+0x1bc`, `+0x1c0` | dynamic list pointer and count |
| `+0x1d4` | resource/update notification handle |
| `+0x1d8`, `+0x1dc` | scalar fields with getter/setter pairs |

## Vtable Map

| Slot | Address | Provisional Name | Behavior |
|---:|---|---|---|
| 0 | `0x00459d00` | `Gob::DeletingDestructor` | Calls the full destructor/cleanup routine, then frees the object when the low bit of the delete flag is set. |
| 1 | `0x004558a0` | `ReturnFalse` | Default stub returning `0`. |
| 2 | `0x004596f0` | `GetNameField` | Returns `this + 0x64`, likely a name/tag/resref string field. |
| 3 | `0x004615d0` | `GetModelDataOffset8` | Returns `model + 8` when `this + 0x84` exists, otherwise `0`. |
| 4 | `0x004b3fe0` | `Render` | Main draw routine. Updates cached transform fields, pushes OpenGL matrix state, applies transform/scale, renders the model, and handles render propagation. |
| 5 | `0x004e1d40` | `UpdateAnimationAndModelState` | Large per-frame update routine. Advances animation nodes, updates model state, handles visibility/render gating, and invokes model animation callbacks. |
| 6 | `0x004e3270` | `PlayOrQueueAnimation` | Starts, queues, stops, or updates named animation entries with flags, speed, and start position. |
| 7 | `0x004e3a20` | `FindAnimationState` | Finds an active animation node by name and optionally returns current and total timing values. Falls back to model animation data when no active node exists. |
| 8 | `0x0045a030` | `LoadFromFileOrStream` | Loads model/resource data from an existing resource or from a file, then initializes object/model data. |
| 9 | `0x0045a150` | `ClearOverrideResource` | Releases/clears the resource pointer at `+0x90`. |
| 10 | `0x004dd140` | `CreateTrackedNode` | Allocates a small tracked node/helper object and registers it in an object list. |
| 11 | `0x004dd1f0` | `RemoveTrackedNode` | Finds a tracked node in the `+0x1bc` list, removes it, and frees it. |
| 12 | `0x00460c20` | `ApplyDataToChildNodes` | Converts a ushort array into a temporary int array, iterates selected model children, and applies data to each child. |
| 13 | `0x00460910` | `ApplyNamedCommandToChildren` | Applies a named/default command to child nodes; notifies the update handle when present. |
| 14 | `0x00460e50` | `ApplyDataToSelectedSubtree` | Finds a selected child/subtree, then applies data to nodes below it. |
| 15 | `0x00460ae0` | `PropagatePoseToChildren` | Iterates child nodes and propagates pose/animation data. Decompiler parameter recovery is poor here. |
| 16 | `0x00461120` | `SetPropagatedFlag` | If the flag at `+0xec` changes, stores the new value and propagates it through the model tree. |
| 17 | `0x00461230` | `ApplyResourceNameToChildRefs` | Enumerates resource references from the model tree and applies a named resource operation to each. |
| 18 | `0x00461300` | `SetNamedResource` | Resolves or loads a named resource, stores it into the object/update handle, and notifies children when needed. |
| 19 | `0x0046d440` | `SetAttachmentOrResourceHandle` | Wrapper around a helper setter, returning success. |
| 20 | `0x00461670` | `CreateAttachmentFromObject` | Uses another object/resource to create one of several attachment/helper object types, then stores it through slot 22/23-style state. |
| 21 | `0x004618b0` | `CreateAttachmentFromNode` | Creates one of several attachment/helper object types from an explicit node/resource pointer. |
| 22 | `0x00461600` | `ClearAttachment` | Clears attachment/helper state. |
| 23 | `0x00461490` | `AttachmentHelperA` | Attachment/model helper operation. Needs more naming work. |
| 24 | `0x004614e0` | `AttachmentHelperB` | Attachment/model helper operation. Needs more naming work. |
| 25 | `0x00459710` | `GetVectorA` | Copies three values from `+0xa4..+0xac` into an out vector. |
| 26 | `0x00459740` | `GetFourValueBlockA` | Copies four values from `+0xb0..+0xbc` into an out block. Ghidra's `CRichEditView::GetMargins` label is false. |
| 27 | `0x00459780` | `GetVectorB` | Copies three values from `+0xc0..+0xc8` into an out vector. |
| 28 | `0x004597b0` | `GetField1D8` | Returns dword field `+0x1d8`. |
| 29 | `0x004597d0` | `SetField1D8` | Sets dword field `+0x1d8`. |
| 30 | `0x004597f0` | `GetField1DC` | Returns dword field `+0x1dc`. |
| 31 | `0x00459810` | `SetField1DC` | Sets dword field `+0x1dc`. |
| 32 | `0x00461b30` | `ModelHelper32` | Model/helper routine. Needs individual analysis. |
| 33 | `0x00461580` | `ModelHelper33` | Model/helper routine. Needs individual analysis. |
| 34 | `0x0045a510` | `ModelResourceHelper34` | Model/resource helper routine. Needs individual analysis. |
| 35 | `0x0045b1c0` | `ModelResourceHelper35` | Model/resource helper routine. Needs individual analysis. |
| 36 | `0x00459830` | `SetFlag1A9` | Sets byte flag `+0x1a9`. |
| 37 | `0x004626d0` | `ModelTraversal37` | Model/child traversal routine. Needs individual analysis. |
| 38 | `0x00462b90` | `ModelTraversal38` | Model/child traversal routine. Needs individual analysis. |
| 39 | `0x00462c50` | `ModelTraversal39` | Model/child traversal routine. Needs individual analysis. |
| 40 | `0x00462cd0` | `ModelTraversal40` | Model/child traversal routine. Needs individual analysis. |
| 41 | `0x00462d50` | `ModelTraversal41` | Model/child traversal routine. Needs individual analysis. |
| 42 | `0x004636d0` | `SerializeOrState42` | Serialization/state routine; Ghidra's GUI label is false. Needs individual analysis. |
| 43 | `0x00462020` | `ModelHelper43` | Model/helper routine. Needs individual analysis. |
| 44 | `0x00462170` | `ModelHelper44` | Model/helper routine. Needs individual analysis. |
| 45 | `0x0045c750` | `MaterialOrModelState45` | Material/model state helper. Needs individual analysis. |
| 46 | `0x0045c7b0` | `MaterialOrModelState46` | Material/model state helper. Needs individual analysis. |
| 47 | `0x00459850` | `ClearFlagEE` | Writes `0` to 16-bit field `+0xee`. |
| 48 | `0x00459870` | `SetFlagEE` | Writes `1` to 16-bit field `+0xee`. |
| 49 | `0x0045c580` | `MaterialState49` | Material/model state helper. Needs individual analysis. |
| 50 | `0x0045c690` | `MaterialState50` | Material/model state helper. Needs individual analysis. |
| 51 | `0x00463340` | `ModelTraversal51` | Model traversal/helper routine. Needs individual analysis. |
| 52 | `0x0045bed0` | `MaterialState52` | Material/model state helper. Needs individual analysis. |
| 53 | `0x0045bf10` | `MaterialState53` | Material/model state helper. Needs individual analysis. |
| 54 | `0x0045be90` | `MaterialState54` | Material/model state helper. Needs individual analysis. |
| 55 | `0x00459d60` | `SimpleAccessor55` | Small accessor/setter-style method. Needs field naming. |
| 56 | `0x00459d80` | `SimpleAccessor56` | Small accessor/setter-style method. Needs field naming. |
| 57 | `0x00459f70` | `SimpleAccessor57` | Small accessor/setter-style method. Needs field naming. |
| 58 | `0x00459f90` | `SimpleAccessor58` | Small accessor/setter-style method. Needs field naming. |
| 59 | `0x00459fb0` | `SimpleAccessor59` | Small accessor/setter-style method. Needs field naming. |
| 60 | `0x00460040` | `StateHelper60` | State/model helper routine. Needs individual analysis. |
| 61 | `0x004600a0` | `StateHelper61` | State/model helper routine. Needs individual analysis. |
| 62 | `0x004599f0` | `SetFlag1A8` | Writes `1` to byte flag `+0x1a8`. |
| 63 | `0x00459a10` | `ClearFlag1A8` | Writes `0` to byte flag `+0x1a8`. |
| 64 | `0x004598f0` | `MarkPrimaryModelDirty` | If primary model/resource exists, ORs flag `4` into its `+0x50` flags. |
| 65 | `0x00459930` | `GetField94` | Returns dword field `+0x94`. |
| 66 | `0x00461530` | `ModelHelper66` | Model/helper routine. Needs individual analysis. |
| 67 | `0x004587f0` | `InheritedOrBaseHelper67` | Helper routine near construction code. Needs individual analysis. |
| 68 | `0x004637a0` | `ModelTreeHelper68` | Model tree helper. Needs individual analysis. |
| 69 | `0x00459950` | `SetFlag180` | Sets byte flag `+0x180`. |
| 70 | `0x004dca80` | `AnimationResourceHelper70` | Animation/resource helper. Needs individual analysis. |
| 71 | `0x004dcce0` | `AnimationResourceHelper71` | Animation/resource helper. Needs individual analysis. |
| 72 | `0x004dc920` | `AnimationResourceHelper72` | Animation/resource helper. Needs individual analysis. |
| 73 | `0x004dca10` | `AnimationResourceHelper73` | Animation/resource helper. Needs individual analysis. |
| 74 | `0x004e3110` | `AnimationStateHelper74` | Animation state helper. Needs individual analysis. |
| 75 | `0x004e3160` | `AnimationStateHelper75` | Animation state helper. Needs individual analysis. |
| 76 | `0x00459dc0` | `SimpleAccessor76` | Small accessor/setter-style method. Needs field naming. |
| 77 | `0x00459de0` | `SimpleAccessor77` | Small accessor/setter-style method. Needs field naming. |
| 78 | `0x004cdc60` | `PathOrCollisionHelper78` | Path/collision/render helper. Needs individual analysis. |
| 79 | `0x00459e00` | `SimpleAccessor79` | Small accessor/setter-style method. Needs field naming. |
| 80 | `0x00459e40` | `SimpleAccessor80` | Small accessor/setter-style method. Needs field naming. |
| 81 | `0x00459970` | `SetFlag181` | Sets byte flag `+0x181`. |
| 82 | `0x00459990` | `GetFlag181` | Returns byte flag `+0x181`. |
| 83 | `0x004b3c90` | `RenderHelper83` | Render helper. Needs individual analysis. |
| 84 | `0x0045bf50` | `ApplyMaterialDirectiveToChildren` | Parses a texture/material directive and applies render-state values to child material structures. |
| 85 | `0x0045c490` | `ClearMaterialDirectiveState` | If dirty flag `+0x15` is set, clears related child material state and resets `+0x52`. |
| 86 | `0x004599b0` | `SetFloat1AC` | Stores a scalar at `+0x1ac`. |
| 87 | `0x004599d0` | `GetFloat1AC` | Returns scalar at `+0x1ac`. |
| 88 | `0x00459a30` | `SetField20` | Sets 16-bit field `+0x20`. |
| 89 | `0x00459a50` | `GetField20` | Returns 16-bit field `+0x20`. |
| 90 | `0x00459a70` | `SetField22` | Sets 16-bit field `+0x22`. |
| 91 | `0x00459a90` | `GetField22` | Returns 16-bit field `+0x22`. |
| 92 | `0x00459ab0` | `SetFourValueBlock28` | Sets four dwords/floats at `+0x28..+0x34`. |
| 93 | `0x00459af0` | `GetFourValueBlock28` | Copies four dwords/floats from `+0x28..+0x34`. |
| 94 | `0x00459b30` | `SetFlag25` | Sets byte flag `+0x25`. |
| 95 | `0x00459b50` | `GetFlag25` | Returns byte flag `+0x25`. |
| 96 | `0x00459b70` | `SetFlag24` | Sets byte flag `+0x24`. |
| 97 | `0x00459b90` | `GetFlag24` | Returns byte flag `+0x24`. |
| 98 | `0x00459bb0` | `PropagateFlagToModelA` | Passes a byte flag to helper `FUN_004637d0` on model tree pointer `+0x98`. |
| 99 | `0x00459be0` | `PropagateFlagToModelB` | Passes a byte flag to helper `FUN_00463840` on model tree pointer `+0x98`. |
| 100 | `0x00459c10` | `GetFlag38` | Returns byte flag `+0x38`. |
| 101 | `0x00459c30` | `SetVector3CAndPropagate` | Stores values at `+0x3c..+0x44` and propagates a byte flag through the model tree. |
| 102 | `0x00459c70` | `GetFlag39` | Returns byte flag `+0x39`. |
| 103 | `0x00459c90` | `GetVector3C` | Copies three dwords/floats from `+0x3c..+0x44`. |
| 104 | `0x00459cc0` | `SetFlag48` | Sets byte flag `+0x48`. |
| 105 | `0x00459ce0` | `GetFlag48` | Returns byte flag `+0x48`. |
| 106 | `0x004558a0` | `ReturnFalse` | Default stub returning `0`. |
| 107 | `0x00458940` | `BaseOrIdentityHelper107` | Helper near base object code. Needs individual analysis. |
| 108 | `0x004558a0` | `ReturnFalse` | Default stub returning `0`. |
| 109 | `0x004dd260` | `DispatchNamedEvent` | Called by destructor with `"Dying"`; likely dispatches a named script/event callback. |
| 110 | `0x00459da0` | `SimpleAccessor110` | Small accessor/setter-style method. Needs field naming. |
| 111 | `0x00459890` | `GetFlag1AA` | Returns byte flag `+0x1aa`. |
| 112 | `0x0045a000` | `StateHelper112` | State/model helper. Needs individual analysis. |
| 113 | `0x004598b0` | `ClearFlag11` | Writes `0` to byte flag `+0x11`. |
| 114 | `0x004598d0` | `GetFlag11` | Returns byte flag `+0x11`. |
| 115 | `0x0045d030` | `StateHelper115` | State/model helper. Needs individual analysis. |
| 116 | `0x00460810` | `StateHelper116` | State/model helper. Needs individual analysis. |
| 117 | `0x00459d30` | `SimpleAccessor117` | Small accessor/setter-style method. Needs field naming. |
| 118 | `0x00459ee0` | `SimpleAccessor118` | Small accessor/setter-style method. Needs field naming. |

## Constructor Summary

`Gob::constructor` at `0x00458b70` performs broad initialization:

- installs base and derived vtables
- initializes multiple vector blocks and scalar fields to `0`, `1.0`, or defaults
- initializes several embedded dynamic arrays / helper containers
- assigns a unique name if a requested name already exists
- creates a helper object at `+0x69`
- registers the object in a global name/index system
- increments global live-object counters

The constructor is a good source for object layout because it writes most fields once.

## Destructor Summary

The main destructor body is `FUN_0045a190`.

It:

- restores `Gob::vftable`
- decrements global live-object counters
- dispatches a `"Dying"` event
- clears animation/model state
- releases attachment/helper objects
- frees dynamic node lists
- releases model/resource pointers
- destructs embedded containers in reverse construction order
- restores `CAurObject::vftable`
- calls the base destructor

## High-Confidence Method Groups

### Rendering

`Render` at `0x004b3fe0` is the main draw routine. It checks global render enable flags, synchronizes cached transform fields, pushes an OpenGL matrix, applies object transform and scale, renders the owned model, optionally propagates render events, and restores OpenGL state.

### Animation

Slots 5-7 and 70-75 are animation-heavy. The class maintains a list of active animation nodes and uses named animation resources from either the primary or alternate model resource. Animation nodes track current time, previous time, flags, blend weights, and completion status.

### Model Resource Management

Slots 8-24 and 32-54 mostly operate on the primary model/resource tree at `+0x84`, alternate resource at `+0x90`, model tree pointer at `+0x98`, and attachment/helper objects.

### Accessors and Flags

Slots 25-31, 47-48, 62-65, 69, and 81-118 include many direct field accessors. These are useful for reconstructing layout but should not all be given gameplay names until their call sites are studied.

