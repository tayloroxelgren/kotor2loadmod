# Resource Manager Reimplementation Design Report

Prepared for: Project Owner  
Prepared by: Codex  
Date: 2026-06-26

## Executive Summary

The design choice to move away from small loading hooks and toward a resource-manager reimplementation is sound. The repeated instability tells us the current hook attempts have been too far downstream: they try to alter loading behavior after the game has already made ownership, lifecycle, and teardown decisions.

The resource manager is the first layer where we can gain real control without immediately owning the whole loading screen, GUI lifecycle, module teardown, parser side effects, or renderer state.

The recommended scope is not a full loading rewrite. It is a controlled resource-manager compatibility layer:

```text
We own:
- resource id decoding
- backend/source prediction
- archive index lookup
- file/archive byte acquisition
- optional mmap/cache/prefetch

The game still owns:
- resource entry allocation
- parser callbacks
- loaded flags/refcounts
- purge policy
- object construction
- module teardown
```

The main place to start is `ResourceLoadFromArchive` in shadow mode. That function is narrow enough to model incrementally, important enough to matter, and structurally simpler than the BIF/KEY slot path.

## Why This Design Choice Makes Sense

The project has already tried smaller optimization points:

- Archive handle pinning.
- Loading-screen frame skipping/debouncing.
- GUI preservation across teardown.
- Other direct loading-path shortcuts.

The common failure pattern is that these hooks modify effects of loading rather than controlling the resource acquisition decision itself. They cross engine lifecycle boundaries without owning enough state to make that safe.

A resource-manager compatibility layer changes the approach:

```text
Old approach:
Patch one side effect and hope engine ownership tolerates it.

New approach:
Reconstruct the resource acquisition model, prove it matches the original,
then replace only deterministic byte acquisition.
```

This is closer to how a professional reverse-engineering team would handle a lost-source port: use the original executable as the oracle, build a shadow implementation, compare behavior, then replace one backend at a time.

## Ghidra Investigation Summary

The following functions were rechecked in Ghidra for this report.

| Address | Function | Role | Replacement Risk |
|---:|---|---|---|
| `0x00711c20` | `ResourceEnsureLoaded` | Top-level dispatcher and refcount bump | Medium-high if replaced first |
| `0x00713bf0` | `ResourceLoadFromArchive` | ERF/MOD/HAK backend | Best first target |
| `0x00713fb0` | `ResourceLoadFromArchiveSlot` | BIF/KEY indexed slot backend | Important, but more indexing surface |
| `0x00713e80` | `ResourceLoadMemoryBacked` | Already-memory-backed backend | Observe only at first |
| `0x007133a0` | `ResourceLoadFromLooseFile` | Loose file backend | Useful later for path resolver |
| `0x00712f30` | `Resource_AllocateLoadBuffer` | Game-owned resource allocation/purge path | Do not replace early |
| `0x00715a60` | `ResourceFinalizeAsyncLoad` | Async completion/finalization | Observe only unless async path appears |
| `0x007295b0` | Shared add-ref/open wrapper | Opens concrete reader on first sync ref | Do not pin/alter |
| `0x00727d10` | Encapsulated release/close | Releases sync reader and closes at ref zero | Do not pin/alter |
| `0x00727d90` | Encapsulated get size | ERF/MOD/HAK table size lookup | Safe to mirror |
| `0x00729370` | Encapsulated read sync | ERF/MOD/HAK seek/read | Safe to mirror eventually |
| `0x00727e30` | Encapsulated open sync handle | Opens ERF/MOD/HAK file wrapper | Avoid mutating |
| `0x00728630` | Encapsulated archive table builder | Reads ERF/MOD/HAK header/table | Reference for our own parser |
| `0x007270a0` | `CExoResFile_AddRefSyncOpen` | BIF/KEY add-ref/open | Do not pin/alter |
| `0x00727390` | `CExoResFile_GetResourceSize` | BIF/KEY table size lookup | Safe to mirror later |
| `0x00727930` | `CExoResFile_ReadResourceSync` | BIF/KEY seek/read | Safe to mirror later |
| `0x00727450` | `CExoResFile_OpenSyncHandle` | Opens BIF file wrapper | Avoid mutating |
| `0x00729790` | `CExoResourceImageFile_LoadImage` | Loads RIM/resource image into memory | Useful architecture model |
| `0x00729ae0` | `CExoResourceImageFile_ReadResourceSync` | Copies from already-loaded image memory | Useful architecture model |
| `0x0073da40` | `LooseFileOpen` | Builds filename and calls `_fopen` | Useful for path mapping |
| `0x0073dd20` | `LooseFileRead` | `_fread` wrapper | Low-level byte read |

## Core Architecture Found In Ghidra

### Resource Entry Layout

From the backend loaders, the resource entry behaves like this:

```cpp
struct ResourceEntry {
    void* vtable;        // +0x00, parse callback at vtable + 0x10
    int16_t refCount;    // +0x04, incremented by ResourceEnsureLoaded
    int16_t unknown06;   // +0x06, async finalizer writes zero here
    uint32_t packedId;   // +0x08
    uint32_t flags;      // +0x0c, loaded flag 0x04, async/pending flag 0x10
    void* data;          // +0x10
    void* metadata;      // +0x14, used by loose-file path/type handling
    uint32_t size;       // +0x18
    uint32_t mode20;     // +0x20, affects allocation prefix
    uint32_t mode24;     // +0x24, affects allocation padding
};
```

This is not a final struct definition, but it is accurate enough for phase-1 shadow logging.

### `ResourceEnsureLoaded`

`ResourceEnsureLoaded` does four important things:

1. Rejects null entries or entries with `packedId == -1`.
2. If not loaded, dispatches by the top two bits of `packedId`.
3. If load succeeds, removes the entry from a linked list in some cases.
4. Increments the resource entry refcount and returns `entry->data`.

Backend dispatch:

```cpp
switch (entry->packedId >> 30) {
case 0:
    ResourceLoadFromArchiveSlot(entry, 0);
    break;
case 1:
    ResourceLoadMemoryBacked(entry, 0);
    break;
case 2:
    ResourceLoadFromArchive(entry, 0);
    break;
case 3:
    ResourceLoadFromLooseFile(entry, 0);
    break;
}
```

Design implication:

`ResourceEnsureLoaded` is not the first replacement target. It owns refcount behavior and async finalization decisions. We should observe it, but leave it intact until backend replacements are proven.

### `ResourceLoadFromArchive`

This is the best first target.

It handles ERF/MOD/HAK-style encapsulated archives. It:

1. Validates the resource entry is non-null and not already loaded.
2. Walks a linked list of resource source nodes.
3. Finds a source where:

```cpp
(source->archiveId & 0x0fffffff) == ((entry->packedId & 0x000fc000) >> 14)
```

4. Resolves the concrete archive reader from `source + 0x30`.
5. Calls the reader add-ref/open vfunc.
6. Gets resource size from reader table.
7. Calls `Resource_AllocateLoadBuffer`.
8. Reads bytes into `entry->data`.
9. Releases/closes the reader.
10. Calls the resource parse callback at `entry->vtable + 0x10`.
11. Sets loaded flag `0x04` if parse succeeds.

Key packed id fields:

```cpp
backend       = packedId >> 30;
slotIndex     = (packedId & 0x3ff00000) >> 20; // used by ArchiveSlot path
archiveId     = (packedId & 0x000fc000) >> 14;
resourceIndex = packedId & 0x00003fff;
```

Design implication:

This function gives us a deterministic, bounded first model:

```text
packed id -> archive id -> source node -> reader -> table index -> offset/size
```

That can be shadowed without changing behavior.

### `ResourceLoadFromArchiveSlot`

This is the BIF/KEY path. It is similar to `ResourceLoadFromArchive`, but adds an extra slot index:

```cpp
slotIndex = (entry->packedId & 0x3ff00000) >> 20;
reader = *(source->readerArray + slotIndex * 4);
```

Then it uses the same vtable shape:

```text
+0x04 AddRefSyncOpen
+0x14 ReleaseSyncClose
+0x24 GetResourceSize
+0x34 ReadResourceSync
```

BIF/KEY table layout from `CExoResFile_ReadResourceSync`:

```cpp
resourceCount = *(reader->header + 0x08);
offset        = *(reader->table + 0x04 + resourceIndex * 0x10);
size          = *(reader->table + 0x08 + resourceIndex * 0x10);
```

Design implication:

This backend probably matters because it has high call count, but it should come after `ResourceLoadFromArchive`. It has more indirection: source node, reader array, slot index, and a different table layout.

### `ResourceLoadMemoryBacked`

This backend does not read from disk. It:

1. Walks the same source list.
2. Finds matching archive/source id.
3. Gets resource size from reader vtable `+0x24`.
4. Gets a data pointer from reader vtable `+0x44`.
5. Writes directly into `entry->size` and `entry->data`.
6. Marks the loaded flag.
7. Calls the parse callback.

Design implication:

This should be observed but not replaced early. It is already memory-backed and had negligible measured time.

### `ResourceLoadFromLooseFile`

This backend is more complex than the archive path because it builds a filename using metadata from the resource entry. It:

1. Walks the same source list.
2. Builds a path/name using `entry->metadata`.
3. Opens a loose file through `CExoFile_OpenByResourceType`.
4. Checks file validity.
5. Gets file size.
6. Allocates with `Resource_AllocateLoadBuffer`.
7. Reads with `LooseFileRead`.
8. Closes/frees the file wrapper.
9. Calls the parse callback.
10. Sets loaded flag from parse result.

Design implication:

Loose files are a later target. The call count was low in the latest data, and path reconstruction is more string-heavy.

### `Resource_AllocateLoadBuffer`

This function is game-owned and should remain game-owned.

It:

1. Checks the resource memory budget at resource-manager `+0x08`.
2. Calls purge until enough memory is available.
3. Decrements the memory budget by `entry->size`.
4. Allocates using game allocator.
5. Adjusts `entry->data` depending on entry fields at `+0x20` and `+0x24`.
6. Restores budget if allocation fails.

Design implication:

Do not replace this early. Our replacement loader should call it exactly like the original. This keeps purge policy, allocator, and data-pointer quirks inside the game.

### `ResourceFinalizeAsyncLoad`

This finalizes the async path. The latest timing has no samples for it, but Ghidra shows it:

1. Finds the source for `manager + 0x28` resource entry.
2. Releases the backend async reader based on backend bits.
3. Clears async flag `0x10`.
4. Sets loaded flag `0x04`.
5. Calls parse callback.
6. Clears async state at manager `+0x28` and `+0x2c`.

Design implication:

Keep async out of scope until the sync backend replacement is stable. If `asyncFlag != 0`, fall back to original.

## Reader Layouts That Matter

### ERF/MOD/HAK Encapsulated Reader

From `CExoEncapsulatedFile_GetResourceSize` and `ReadResourceSync`:

```cpp
struct EncapsulatedReader {
    void* vtable;          // +0x00
    ExoString name;        // +0x04
    void* syncFile;        // +0x14
    int syncRefCount;      // +0x1c
    int syncOpen;          // +0x24
    int tableBuilt;        // +0x2c
    void* header;          // +0x38
    ErfTableEntry* table;  // +0x3c
    uint8_t archiveType;   // +0x40
};

struct ErfTableEntry {
    uint32_t offset;
    uint32_t size;
};
```

Read logic:

```cpp
resourceIndex = packedId & 0x3fff;
resourceCount = *(reader->header + 0x10);
offset = reader->table[resourceIndex].offset;
size   = reader->table[resourceIndex].size;
```

Archive type mapping in `OpenSyncHandle`:

| Reader `+0x40` | File Type |
|---:|---:|
| `0` | `0x7db` |
| `1` | `0x809` |
| `2` | `0x270d` |
| `3` | `0x80d` |
| `4` | `0x80e` |

These correspond to ERF/MOD/HAK-style resources.

### BIF/KEY Reader

From `CExoResFile_GetResourceSize` and `ReadResourceSync`:

```cpp
struct BifReader {
    void* vtable;        // +0x00
    ExoString name;      // +0x04
    void* syncFile;      // +0x14
    int syncRefCount;    // +0x1c
    int syncOpen;        // +0x24
    void* header;        // +0x30
    BifTableEntry* table;// +0x34
};

struct BifTableEntry {
    uint32_t unknown00;
    uint32_t offset;
    uint32_t size;
    uint32_t unknown0c;
};
```

Read logic:

```cpp
resourceCount = *(reader->header + 0x08);
offset = *(reader->table + 0x04 + resourceIndex * 0x10);
size   = *(reader->table + 0x08 + resourceIndex * 0x10);
```

### Resource Image Reader

`CExoResourceImageFile_LoadImage` loads an entire resource image into memory at `reader + 0x30`.

`CExoResourceImageFile_ReadResourceSync` later does:

```cpp
memcpy(destination, reader->imageBase + resourceOffset, size);
```

Design implication:

This is the strongest internal precedent for our desired architecture. The game already has a reader mode where resource reads become memory copies. We want an external version of this for archives, without mutating the game's reader lifecycle.

## Recommended Incremental Scope

The work can be done incrementally if each stage has a fallback and a correctness check.

### Stage 1: Shadow `ResourceLoadFromArchive`

Goal:

Observe and predict the ERF/MOD/HAK backend without changing behavior.

Implementation point:

```cpp
Hook_ResourceLoadFromArchive
```

Before calling original:

1. Read `entry->packedId`.
2. Decode:

```cpp
backend       = packedId >> 30;
archiveId     = (packedId & 0x000fc000) >> 14;
resourceIndex = packedId & 0x00003fff;
```

3. Walk the same source list using the known linked-list layout:

```text
list node + 0x04 -> next
list node + 0x08 -> payload/source
source + 0x28    -> archive id
source + 0x30    -> reader holder
```

4. Resolve the encapsulated reader.
5. Predict:

```cpp
offset = reader->table[resourceIndex].offset;
size   = reader->table[resourceIndex].size;
```

Then call original.

After original:

1. Compare predicted size to `entry->size`.
2. Log final `entry->data`.
3. Log parse result.
4. Log final flags.

Do not change return value.

Deliverable:

```text
shadow_archive_log.txt

packedId
archiveId
resourceIndex
sourceNode
reader
readerType
predictedOffset
predictedSize
actualEntrySize
dataPtr
parseResult
flagsBefore
flagsAfter
match/mismatch
```

Success criteria:

- 99%+ prediction match for sync `ResourceLoadFromArchive`.
- All mismatches explainable.
- No behavior changes.

### Stage 2: Add A Standalone Archive Reader

Goal:

Implement our own ERF/MOD/HAK byte resolver in C++.

This does not hook replacement yet. It should be testable from logs.

Inputs:

```text
archive filename/type
resourceIndex
```

Outputs:

```text
offset
size
optional byte hash
```

Approach:

1. Use Ghidra's `FUN_00728630` as the reference for archive header/table parsing.
2. Parse:
   - 0xa0-byte header.
   - magic/version.
   - localized string area if present.
   - resource table entries.
3. Store table in our own C++ structures.
4. Compare our offsets/sizes against the game reader table.

Deliverable:

```text
OurArchiveReader predicts same offset/size as game reader for observed loads.
```

### Stage 3: Read-Only Byte Comparison

Goal:

Prove our archive reader returns the same bytes as the game.

Implementation:

1. Before original call, use our reader to read the expected resource bytes into a temporary buffer.
2. Let original run.
3. After original parse, do not assume `entry->data` still contains raw bytes because parsers may transform data.
4. Instead, compare at the lower `CExoEncapsulatedFile_ReadResourceSync` hook:
   - Let original read into destination.
   - Compute hash of destination bytes immediately after original read.
   - Compute hash of our read bytes.
   - Log match/mismatch.

Deliverable:

```text
raw byte hash match for ERF/MOD/HAK reads
```

### Stage 4: Replace Only `CExoEncapsulatedFile_ReadResourceSync`

Goal:

First behavior-changing replacement, but still narrow.

In `Hook_CExoEncapsulatedFile_ReadResourceSync`:

1. Validate:
   - `reader` is known.
   - `reader->syncOpen != 0` or we intentionally allow our read independent of it.
   - destination is non-null.
   - resource index is in range.
   - our archive cache has a valid source.
2. Read from our own handle/mmap into destination.
3. Return the number of bytes original would return.
4. Fall back to original on any uncertainty.

This does not avoid original add-ref/open/close yet, so speedup may be limited. But it proves replacement byte acquisition safely.

Success criteria:

- No crashes.
- Byte match already proven in Stage 3.
- All fallback cases logged.

### Stage 5: Replace Sync Branch Of `ResourceLoadFromArchive`

Goal:

Avoid original reader open/seek/read path for ERF/MOD/HAK while preserving allocation and parser semantics.

Implementation:

```text
Hook_ResourceLoadFromArchive
  if asyncFlag != 0: fallback original
  if entry invalid or already loaded: fallback original
  if shadow prediction fails: fallback original
  get predicted size
  entry->size = predicted size
  if !Resource_AllocateLoadBuffer(manager, entry): return 0
  read bytes with our archive reader into entry->data
  if read size mismatch: return 0 or fallback before mutation where possible
  parseResult = entry->vtable[0x10](...)
  set loaded flag exactly as original:
      entry->flags = (entry->flags & ~4) | (parseResult ? 4 : 0)
  return parseResult
```

Important:

This stage should not touch game archive reader refcounts. It should not call `AddRefSyncOpen` or `ReleaseSyncClose`.

This is the first stage likely to produce meaningful speedup for `ResourceLoadFromArchive`.

### Stage 6: Shadow And Replace `ResourceLoadFromArchiveSlot`

Goal:

Repeat the same method for BIF/KEY.

Start in shadow mode because the indexing is more complex:

```cpp
slotIndex     = (packedId & 0x3ff00000) >> 20;
archiveId     = (packedId & 0x000fc000) >> 14;
resourceIndex = packedId & 0x00003fff;
reader         = *(source->readerArray + slotIndex * 4);
```

Table layout:

```cpp
offset = reader->table[resourceIndex].offset;
size   = reader->table[resourceIndex].size;
```

This backend has more calls than `ResourceLoadFromArchive`, so it may be important for total speedup. But it should come second because there is more surface area.

### Stage 7: Top-Level Dispatch Replacement

Goal:

Only after backend replacements are stable, consider replacing `ResourceEnsureLoaded`.

At this stage, our dispatcher can choose:

```text
cache hit -> our fast path
known sync backend -> our backend replacement
unknown/async/edge case -> original ResourceEnsureLoaded
```

This is the point where the project starts becoming a true resource manager replacement rather than a backend accelerator.

## Main Place To Start

Start here:

```cpp
Hook_ResourceLoadFromArchive
```

But start in shadow mode.

This is the correct first seam because:

- It is already hooked.
- It was the largest resource backend in the latest focused timing.
- It uses a direct archive reader, not a reader array.
- Its resource index and archive id are simple packed-id bitfields.
- Its table layout is simple 8-byte entries: offset and size.
- The original parser callback and allocation can remain untouched.
- It allows fallback to original at every stage.

Do not start by replacing:

- `ResourceEnsureLoaded`: too central and owns refcount/async behavior.
- `Resource_AllocateLoadBuffer`: owns purge/allocator quirks.
- `ReleaseSyncClose`/`AddRefSyncOpen`: prior attempts proved lifecycle-sensitive.
- GUI/module teardown: unrelated to deterministic byte acquisition.
- `ResourceLoadFromArchiveSlot`: valuable, but more complex than ERF/MOD/HAK.

## Incremental Feasibility

Yes, this can be done incrementally.

The safe incremental ladder is:

```text
1. Log fields.
2. Predict backend/source/offset/size.
3. Compare prediction to original.
4. Implement standalone archive parser.
5. Compare our parser to original reader tables.
6. Compare raw bytes at read-hook boundary.
7. Replace low-level read for one backend.
8. Replace full sync backend for one backend.
9. Repeat for BIF/KEY.
10. Only then replace top-level dispatch.
```

The key discipline:

Every stage must have:

- A fallback path.
- A mismatch log.
- A narrow enable macro.
- No modification of game ownership state unless that stage explicitly requires it.

Recommended macros:

```cpp
#define SHADOW_RESOURCE_MANAGER 1
#define SHADOW_ARCHIVE_BACKEND 1
#define REPLACE_ENCAPSULATED_READ_SYNC 0
#define REPLACE_RESOURCE_LOAD_FROM_ARCHIVE 0
#define REPLACE_ARCHIVE_SLOT_BACKEND 0
#define REPLACE_RESOURCE_ENSURE_LOADED 0
```

## Expected Performance Reality

This design gives us control, but it does not guarantee a large speedup by itself.

The latest focused resource numbers were:

| Function | Total |
|---|---:|
| `ResourceEnsureLoaded` | 613.13 ms |
| `ResourceLoadFromArchive` | 310.91 ms |
| `ResourceLoadFromArchiveSlot` | 204.71 ms |
| `ResourceLoadFromLooseFile` | 28.49 ms |
| `ResourceLoadMemoryBacked` | 0.12 ms |

So if these numbers represent the true wall-clock resource bottleneck, a resource-manager reimplementation might save hundreds of milliseconds, not multiple seconds.

However, the value of this design is bigger than the immediate timing:

- It gives us control over caching.
- It enables prefetch.
- It avoids fragile lifecycle hooks.
- It creates a stable foundation for larger replacement later.
- It tells us whether resource acquisition is actually the limiting factor.

If resource acquisition is not the dominant wall-clock cost after replacement, we will know that with much higher confidence.

## Recommended First Work Package

The first implementation package should be:

```text
Shadow ResourceLoadFromArchive Model
```

Concrete tasks:

1. Add small C++ structs for `ResourceEntry`, `SourceNode`, and `EncapsulatedReaderView`.
2. Add packed-id decode helpers.
3. Add source-list walker mirroring the original linked-list behavior.
4. In `Hook_ResourceLoadFromArchive`, before original:
   - build a prediction record.
5. Call original.
6. After original:
   - compare `predictedSize` to `entry->size`.
   - log `entry->data`, flags, result, and mismatch state.
7. Keep all behavior unchanged.

Example first log line:

```text
ShadowArchiveLoad packed=0x81234567 backend=2 archive=0x09 index=0x0567
source=0x12345678 reader=0x23456789 offset=0x0012a000 predictedSize=3840
actualSize=3840 data=0x1b9c4000 flags=0x00000004 result=1 match=1
```

This gets the project rolling without betting stability on a replacement too early.

## Final Recommendation

The resource-manager reimplementation is the right strategic direction, but it should be treated as a compatibility-layer project, not a full rewrite from day one.

The correct starting point is:

```text
Shadow ResourceLoadFromArchive -> standalone ERF/MOD/HAK reader -> byte comparison -> read replacement -> backend replacement
```

This gives us real control while keeping the game responsible for the dangerous parts: allocation, parser side effects, refcounts, and lifecycle ownership.

The project can be incremental if we preserve this rule:

```text
Model first. Compare second. Replace third.
```

That is the path most likely to produce a stable performance improvement instead of another unstable loading hook.
