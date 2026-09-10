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

`edgeSides` / `edgeTopBot` inject half-width edge reveals at the
panel bounds (not listed in the reveal UI). L/R uses vertical strips
at X=0 and X=width so adjacent panels form a full groove at the joint.
T/B does the same at Z=0 and Z=height.

`depth` is the base wall. The base is a box of that thickness. Reveal
cells keep their front quads (groove floor). Field cells share one
outer grid at `grooveDepth` with walls only on field/reveal and
field/perimeter edges — a single welded element (no duplicate verts or
overlapping front faces under panels). No reveals / edge insets (or
zero groove size) → just the boxed grid.

Crossing = union of strip cells; those cells are not extruded, so the
plus-shaped floor is the original front grid on reveal cells.

**Verify:**
1. No reveals — box of thickness Depth. Convert to Poly; quads; one
   element.
2. Add Horiz / Add Vert — groove loops on the base front; field regions
   share outer-grid verts; Convert to Poly → Element = **1**.
3. At a former triple-vert corner: one front vert (groove) and one
   outer-grid vert.
4. Groove W / Groove D change every reveal together.
5. Depth is the structural minimum; it does not get carved through.
6. Inset L/R alone — fields start half Groove W in from each side;
   two panels side-by-side meet as one full-width reveal.
7. Inset T/B optional — same for top/bottom.

MAXScript: `.revealAxis`, `.revealPos`, `.grooveWidth`, `.grooveDepth`,
`.edgeSides`, `.edgeTopBot`. Axis 0 = horizontal (Z), 1 = vertical (X).

The Modify rollout remains the interim reveal list UI; preferred authoring
is the floating editor.

## Phase 3 editor + openings

Floating Python editor writes pblock only. Openings are rectangular hole
loops in the same cut-grid pass as reveals:

- Tabs: `openingX`, `openingZ`, `openingW`, `openingH` (lower-left XZ).
- Classification: opening > reveal > field.
- Opening cells omit front/back; solid-adjacent edges get through-wall
  jambs; panel-edge openings omit outer walls (doors).
- Editor: **Draw Opening…** marquee, drag/resize edges, L/R/T/B + % spinners.
- Modify panel keeps size/groove/insets + **Edit Reveal Layout…** only;
  reveal list UI removed (authoring is editor-only).

## Panel colors (prototype)

`panelColors` TYPE_POINT3_TAB — one RGB (0–1) per subpanel region between
reveal dividers (same layout as C++ `BuildRegionDividers`). Applied as
vertex color on outer/front field verts. Editor: Paint Fill, 8 swatches,
Show colors toggle.

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
