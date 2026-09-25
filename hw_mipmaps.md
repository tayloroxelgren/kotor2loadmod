# GPU mipmap generation (FORCE_HW_MIPMAP_GEN)

Follow-up to `stream_force.md`, where the stack sampler found ~149 ms of each 101PER
reload's stream window inside `gluBuild2DMipmaps`.

## What it changes

`Texture_UploadToGL` (0x00433cd0) uploads uncompressed mipmapped textures one of two ways:

- `GL_CanUseHardwareMipmapGen` (0x00484a60) returns 1: set `GL_GENERATE_MIPMAP`, then
  `glTexImage2D`. The driver builds the mip chain on the GPU.
- It returns 0: `gluBuild2DMipmaps`. GLU rescales to power-of-two sizes if needed and builds
  every level on the CPU.

The check needs `GL_SGIS_generate_mipmap` (bit 0x80000) and requires bit 0x100000 to be
clear. `GL_DetectExtensions` (0x004317d0) sets 0x100000 for `GL_ATI_text_fragment_shader`
and also for `GL_ARB_fragment_program`, so every modern driver gets the CPU path.
`Texture_UploadToGL` is the check's only caller.

With `FORCE_HW_MIPMAP_GEN 1`, a new hook on 0x00484a60 turns a 0 into 1 when the
`GL_SGIS_generate_mipmap` bit is set. Nothing else reads the check.

## New measurements (log only, both builds)

- `LoadPhaseTextures:` one line per load. `mip_checks` / `mip_forced` count the capability
  check (and how many the hook flipped). Per phase (`work` = before finalize, `hs`,
  `stream`, `post`): `uploads_*` / `upload_us_*` count and time `Texture_UploadToGL`;
  `glu_mips_*` / `glu_us_*` do the same for `gluBuild2DMipmaps` (included in the upload time).
- The main-thread sampler now starts when the window arms (the click), so the ~0.84 s before
  finalize is sampled too. `python lp_samples.py --summary` prints a window × category
  table in ms per load: `work`, `handshake`, `gap1`, `gap2`, `rest` against `cpu_mipmaps`,
  `texture_upload`, `texture_queue`, `present_wait`, `other`.

## Run plan

Two DLLs from the same source. Restart the game between them and do the same four 101PER loads.

- `dinput8_mip_off.dll`: `hw_mipmaps=0` (control: today's behaviour plus the wider sampler).
- `dinput8_mip_on.dll`: `hw_mipmaps=1`.

## Prediction (written before the first run)

- Control: `mip_forced=0`, `glu_mips_stream` > 0, `cpu_mipmaps` in gap 1 ≈ 150 ms, as in the
  last run. I don't know how much of `work` is CPU mipmaps; this run measures it. My guess is
  50-300 ms on a reload, since the area and its models load before finalize.
- Fix on: `mip_forced` = `mip_checks` > 0, `glu_mips_*` = 0 in every phase, `cpu_mipmaps` = 0.
  The GPU path still costs something (`glTexImage2D` plus driver mip generation), so gap 1
  shrinks by ~120-145 ms rather than the full 149, and `stream_us` drops from ~0.56 s to
  ~0.42-0.45 s. `work_us` drops by most of whatever the control shows as CPU mipmaps there.
- `upload_us_*` is the direct comparison: the same textures uploaded both ways.
- Visual check: surfaces at a distance and at grazing angles (floors, walls) should look the
  same. Black, white or flickering textures would mean the GPU path is broken on this
  driver. Mip filtering may differ slightly (driver box filter vs GLU's).

## Result

Not run yet.
