# RyViz TiltUpPanel — Handoff

Read `TiltUpPanel_ProjectSpec.md` for the full design. This file records
decisions and crash-adjacent lessons as they are paid for.

## What this is

A 3ds Max **object plugin** (`.dlo`, not `.dlm`) — a parametric tilt-up
precast wall panel primitive. Class name in the Create panel is
**RyViz Tilt-Up Panel**. MAXScript / pymxs name is `RyViz_TiltUpPanel`.

## Phase 2 (current)

MNMesh cut-grid. Reveals are axis + position only (`revealAxis`,
`revealPos`). Groove width and depth are **global** (`grooveWidth`,
`grooveDepth`).

`depth` is the base wall. The base is always a complete box of that
thickness with the full front cut-grid (every reveal loop stays on that
front). Field cells then get **new** extrusion boxes sitting on those
front quads (`grooveDepth`). Original front faces under the panels are
kept. No reveals (or zero groove size) → just the boxed grid.

Crossing = union of strip cells; those cells are not extruded, so the
plus-shaped floor is the original front grid.

**Verify:**
1. No reveals — box of thickness Depth. Convert to Poly; quads.
2. Add Horiz / Add Vert — original front loops still visible in the
   grooves and under the panels; each field is a separate extrusion.
3. Groove W / Groove D change every reveal together.
4. Depth is the structural minimum; it does not get carved through.

MAXScript: `.revealAxis`, `.revealPos`, `.grooveWidth`, `.grooveDepth`.
Axis 0 = horizontal (Z), 1 = vertical (X).

Phase 3 is still the floating Python 2D editor (modeless, pblock in/out,
no geometry writes). This rollout is the interim authoring UI.

## Coordinates

Panel-local, origin at the lower-left-**back** corner:

- **X** = width (vertical reveals sit at a constant X)
- **Y** = thickness (back `y = 0`, base front `y = depth`, fields
  `y = depth + grooveDepth`)
- **Z** = height (horizontal reveals sit at a constant Z)

Created standing, same rhythm as Box: drag width × depth on the
construction plane, then click height. Node origin is the min-corner of
the drag so the mesh lives in +X +Y +Z.

The 2D editor canvas is width × height (X × Z).

## Locked for now (spec open decisions)

- Reveal positions stay **fixed world units** on panel resize; clamp if a
  reveal falls outside the new bounds. (Phase 2+)
- Editor is a **modeless floating** window. Not docked. (Phase 3)
- No auto re-target on selection change. (not v1)
- Groove is a **rectangular relief**: fields extrude out from the base
  wall. Chamfer/bevel is later.
- Create-panel category is **RyViz** (same as SplineGradeMod).

## Hard rules

- Do not run MSBuild or copy the `.dlo`. User builds **Hybrid | x64** from
  Visual Studio.
- `BuildMesh()` is the only geometry author. No cached / boolean mesh.
- ParamBlockDesc2 4th arg is `&tiltUpPanelDesc`, terminator is `p_end`.
  Tab params (Phase 2) need `P_VARIABLE_SIZE`.
- Verify unknown SDK signatures with Go to Definition in this Max 2027 SDK.
- `InsetMod` is prior art for Phase 2 carve, not a dependency. FlattenMod
  is shipped and off-limits. SplineGradeMod is a separate plugin.

## Class_ID

`Class_ID(0x7b3e1a90, 0x2c8d4f61)` — generated for this plugin. Do not
reuse a modifier Class_ID.
