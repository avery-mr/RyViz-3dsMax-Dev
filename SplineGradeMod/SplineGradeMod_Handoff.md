# RyViz SplineGradeMod — Project Handoff

> Copied into the repo from `Downloads\SplineGradeMod_Handoff.md` as the
> project reference. Keep the lessons below; they cost real time to find.
>
> **Post-handoff (Cursor, 2026-08-18):** Phase 2 pick crash was addressed
> in-tree after this document was written. `Pick()` no longer uses a
> `MessageBox` bisection; `Filter` no longer calls `EvalWorldState` during
> hit-testing; the pick callback is a static `PickModeCallback` +
> `PickNodeCallback` (SDK sample pattern). `AddSplineEntry` is wired again,
> Tab params have `P_VARIABLE_SIZE`, and `ModifyObject` skips dead MNMesh
> verts. Do not restore the MessageBox-in-`Pick` test. If Add Spline still
> crashes, bisect from the *current* code, not from the "Last action taken"
> section at the bottom of this file.

## What this is

A 3ds Max C++ SDK modifier plugin (`SplineGradeMod`) that deforms a dense
reference mesh (a site-plan terrain surface) toward one or more splines,
simulating road/grading corridors. Splines act like "sculpting tools" —
each has a width, falloff, and strength, and the modifier pulls nearby
mesh vertices toward the spline's elevation, blending smoothly at the
edges. Multiple splines apply in a user-defined order, each deforming the
*result* of the previous one (cascading), so a driveway spline can tie
smoothly into a road spline's shoulder.

This is part of the **RyViz** suite — a set of custom 3ds Max tools (also
includes `FlattenMod`, shipped and working, and `InsetMod`, not yet
implemented) built by Mitch (GitHub: `avery-mr`), targeting 3ds Max 2027,
Visual Studio 2022.

## Origin / motivation

Mitch's site-plan workflow: drape a dense subdivided reference mesh over
survey/topo geometry, sculpt it smooth, then conform actual site geometry
(planting areas, paving, etc.) to that reference surface via `tyConform`.
Editing the reference mesh for new roads/grading (especially sloped ones)
was painful — this tool lets him sculpt grading changes parametrically via
splines instead of manual mesh editing.

## Solution structure

```
C:\Users\mavery\source\MaxDev\RyViz-MaxDev\
  RyViz-MaxDev.sln
  FlattenMod\        <- shipped, working, on GitHub (avery-mr/3dsMax-FlattenMod)
  InsetMod\           <- created via wizard, not yet implemented
  SplineGradeMod\     <- THIS PROJECT, in active development
  Shared\
    RyViz-MaxSDK.props <- shared MSBuild property sheet: lib dir + AdditionalDependencies
                           (core.lib, geom.lib, mesh.lib, etc.) — imported
                           unconditionally by all three projects' .vcxproj
```

All three projects share SDK include paths via Autodesk's own imported
`.props` (`3dsmax.general.project.settings.props`, `3dsmax.cpp.props`,
which define `$(MaxSdkInc)` etc. automatically) plus the custom
`RyViz-MaxSDK.props` for the linker lib list.

Output extension is `.dlm` (not `.dlo`), output dir is
`C:\Program Files\Autodesk\3ds Max 2027\Plugins\`.

## Development process used so far

Two-track validation approach, deliberately:
1. **MAXScript prototype first**
   (`SplineGradeMod/splinegrade_mod_prototype.ms`, internally titled
   `RyViz_SplineGradeProto.ms` v2) — validated the core algorithm cheaply
   before committing to C++. This went through several real bugs
   (documented below) and ended up fully working, including multi-spline
   cascading with reorder.
2. **C++ port**, phased:
   - **Phase 1 (COMPLETE, working):** single-spline version. Compiles,
     applies in Max, deforms correctly, UI (pick button + spinners) works
     via `P_AUTO_UI`.
   - **Phase 2 (IN PROGRESS, currently debugging a crash):** multi-spline
     list version — parallel `Tab<>` paramblock params, hand-built dialog
     (listbox + add/remove/move buttons), pick-mode callback for "Add
     Spline". See "Current status" below.

## The validated algorithm (unchanged from prototype through both C++ phases)

Per spline, per vertex:
1. Sample the spline into a polyline (world space).
2. Find the closest point on that polyline to the vertex, **XY distance
   only** (ignore Z when finding closest point).
3. Interpolate the polyline's Z at that closest point → `targetZ`.
4. Compute `lateralDist` = XY-only distance to that closest point.
5. Weight: 1.0 inside `width/2`, smoothstep falloff to 0.0 over
   `falloff` distance beyond that, 0.0 beyond `width/2 + falloff`.
6. `newZ = lerp(vertex.z, targetZ, weight * strength)`.

Multi-spline: apply entries **in list order**, each entry deforming the
**result** of the previous entry (not the original base mesh) — this is
what lets a later spline's shoulder blend into an earlier spline's result.

**Known accepted limitation (documented, not fixed):** tight
self-overlapping curves (hairpins where `width+falloff` exceeds the gap
between two strands of the same spline) produce pinched/distorted seams.
This is inherent to the "global nearest-segment" approach — a vertex
between two strands can flip discontinuously to whichever strand is
closer. Future fix (if ever needed): inverse-distance blend across
multiple candidate regions instead of winner-take-all nearest match.

## Critical lessons learned (READ BEFORE DEBUGGING FURTHER — these cost real time to find)

### Coordinate space (biggest recurring source of bugs)
- **MAXScript's `interpCurve3D`** already returns **world-space** points
  for a picked node — do NOT multiply by the node's transform, that
  double-applies the offset. (Confirmed empirically after a long bug hunt
  — symptom was error scaling with distance between object pivots.)
- **C++'s spline sampling** (`ShapeObject::MakePolyShape` /
  `EvalWorldState`) returns **local/object-space** points — the OPPOSITE
  convention. You MUST multiply by `splineNode->GetNodeTM(t)` to get world
  space here. Getting this backwards reproduces the same pivot-distance
  bug.
- **MAXScript mesh vertices:** `polyop.getVert`/`setVert` with the
  `node:` keyword was UNRELIABLE (empirically produced pivot-distance-
  dependent errors even though it's supposed to give world space). Fixed
  by using explicit matrix math instead: `localPos * obj.transform` /
  `worldPos * (inverse obj.transform)`.
- **C++ mesh vertices in `ModifyObject`:** `node->GetNodeTM(t)` is the
  object's REAL current world transform — use this when `node` is
  non-null. `ModContext::tm` (`mc.tm`, a `Matrix3*`) is a DIFFERENT thing
  (the modifier's own local/gizmo context) — using `mc.tm` alone caused
  deformations to move rigidly WITH the object instead of tracking the
  spline correctly when the object was moved after applying the modifier.
  Correct pattern:
  ```cpp
  Matrix3 meshTM = node ? node->GetNodeTM(t) : (mc.tm ? *mc.tm : Matrix3());
  ```

### `INode* node` in `ModifyObject` is NOT reliably non-null
Confirmed empirically — it was null on essentially every evaluation
during testing (not just obscure background polling as initially assumed).
Always null-check before use, or use the `mc.tm` fallback pattern above.

### Position-lock limitation (ACCEPTED, not fixed)
Moving the host mesh **after** applying the modifier does NOT re-trigger
`ModifyObject` — standard Object-Space Modifiers are architecturally
position-independent; Max's pipeline doesn't re-evaluate on a pure node
move. Confirmed this is NOT fixable by choosing a different transform
variable (tried both `mc.tm` and `node->GetNodeTM`, neither helps — the
function simply isn't re-entered). The correct SDK tool for genuinely
world-space-reactive deformation is a **Space Warp** (`WSMObject` base
class, object-binding model — see Max's built-in Conform / Path Deform
(WSM) as prior art). Deliberately deferred as a v2 architectural decision.

### ParamBlockDesc2 / paramblock gotchas
- Macro terminator is **`p_end`**, not `end` (SDK version difference from
  older docs/samples).
- The **4th constructor argument is the owning `ClassDesc*`** — passing
  `nullptr` here causes `MakeAutoParamBlocks` to **silently fail** (no
  compile error, no crash — `pblock` just stays null forever, breaking
  MAXScript property exposure, the UI, and everything downstream). Must
  be `&yourClassDescInstance`, which means the ClassDesc must be defined
  **before** the paramblock descriptor in the file.
- `Modifier` requires implementing `NumRefs()`, `GetReference()`,
  `SetReference()`, `NotifyRefChanged()` — easy to forget since
  `P_AUTO_CONSTRUCT` handles paramblock *creation* but not this reference-
  system plumbing. Omitting them → `C2259 cannot instantiate abstract
  class`.
- `Modifier` derives from `BaseObject` in this SDK → also requires
  `GetCreateMouseCallBack()` (return `nullptr`, modifiers aren't created
  via viewport mouse-drag).
- `GetObjectName`'s exact signature matters for `override` to match:
  `const MCHAR* GetObjectName(bool localized) const override` — note the
  **trailing `const`** and **`MCHAR*`** (not `TCHAR*`) — missing the
  `const` silently breaks the override match (falls back to showing
  "Object" in the modifier stack instead of your class name).

### Debugging output gotchas
- `DebugPrint()` goes to `OutputDebugString` — visible only under an
  attached debugger, NOT the MAXScript Listener.
- `mprintf()` needs MAXScript SDK headers not included by default —
  didn't compile as used.
- **`MessageBox()` is the reliable fallback** for quick diagnostics — no
  special headers, unambiguous. BUT: **do not put blocking MessageBox
  calls in the constructor** — Max instantiates a throwaway reference
  instance of every plugin class during startup class-registration scan,
  and a modal dialog firing that early can hang Max's init sequence
  before its message loop is running, leaving a **suspended zombie
  process** that locks the output `.dlm`/`.pdb` (causes `LNK1201`/
  `LNK1104` on next build). Put diagnostics in `ModifyObject` instead
  (guarded with a static counter, not a one-shot bool, so you don't miss
  the state after user interaction if the first eval happens before
  that). Also do **not** put `MessageBox` in `HitTest` / `Filter` / `Pick`
  — those run inside Max's pick command mode; a modal dialog there
  re-enters the message loop and can crash.
- If a build fails with `LNK1201`/`LNK1104` "access denied" / "error
  writing to pdb": check Task Manager's **Details** tab for a suspended
  `3dsmax.exe` and **End Process Tree** it — simple Task Manager "End
  Task" sometimes isn't enough for a truly hung/suspended process.

### `PickModeCallback` vs `PickNodeCallback`
These are **two different classes**. `Filter(INode*)` belongs to
`PickNodeCallback`, NOT `PickModeCallback`. Current in-tree pattern
(after the post-handoff pick fix) is one class inheriting both, matching
Linked XForm / Skin Wrap:

```cpp
class SplineGradeAddSplinePick : public PickModeCallback, public PickNodeCallback {
    BOOL Filter(INode* node) override;   // FindBaseObject + SHAPE_CLASS_ID; no EvalWorldState
    BOOL HitTest(...) override;          // ip->PickNode(hWnd, m, this)
    BOOL Pick(...) override;             // vpt->IsAlive(); GetClosestHit(); fallback hitNode
    PickNodeCallback* GetFilter() override { return this; }
};
```

`Filter` must not call `EvalWorldState` during pick hit-testing (pipeline
re-entry). `ip->PickNode(HWND, IPoint2, PickNodeCallback*)` returns
`INode*` directly (not bool).

### General debugging technique that worked well
When stuck on a signature/API guess: **have the user right-click the
symbol → "Go to Definition" in VS and paste back the real declaration**
directly from the SDK header. This resolved `GetObjectName`,
`GetCreateMouseCallBack`, `PickNodeCallback::Filter`, `PickNode`, and
`IParamBlock2::GetValue` — much faster and more reliable than guessing
from training-data memory, especially since SDK signatures vary by Max
version. **Use this early, not as a last resort** — several crash/rebuild
cycles could have been avoided by checking signatures before writing code
that depends on them, especially for anything beyond the very well-worn
paramblock/spinner patterns.

## Current status (as of original handoff)

**Phase 2 had an unresolved crash** at handoff time: click "Add Spline"
→ enter pick mode → click a spline in viewport → full application crash.

The original next-step was a `MessageBox` bisection inside `Pick()`. That
test was written but not rebuilt. **Do not resume from that step** — see
the post-handoff note at the top of this file. Remaining suspects if a
crash persists after rebuild:

- `AddSplineEntry()` `SetCount` / `SetValue` on parallel `TYPE_INODE_TAB`
  params (reference bookkeeping).
- UI refresh (`RefreshListbox` / `LoadSelectedEntryToUI`) after the pblock
  write.
- `ModifyObject` running immediately after the node is added.

## Files

All live under `SplineGradeMod\` in this repo (not `/mnt/user-data/outputs/`):

- `SplineGradeMod.h` — Phase 2 header, multi-spline Tab-based paramblock
  IDs, manual UI method declarations, pick callback class.
- `SplineGradeMod.cpp` — Phase 2 implementation.
- `SplineGradeMod.rc` — Phase 2 dialog: listbox + 4 buttons + per-entry
  spinner group + global samples spinner.
- `resource.h` — Phase 2 control IDs.
- `DllEntry.cpp` — `GetString()` forward declaration present.
- `splinegrade_mod_prototype.ms` — validated MAXScript prototype
  (multi-spline v2, including debug visualization). Source of truth for
  cascading order, XY-closest Z, and coord-space rules.

## Suggested next steps after unblocking the crash

1. Finish Phase 2 functional testing: add/remove/reorder multiple
   splines, confirm cascading behavior matches the prototype, confirm
   enabled/disabled toggle works, confirm width/falloff/strength edits on
   a selected entry write back correctly.
2. Remove any remaining temporary bisection/debug code once confirmed
   working.
3. Tighten `LocalValidity` (currently `FOREVER` — forces re-eval every
   frame; fine for development, worth revisiting for performance once
   stable).
4. Consider whether `Class_ID` needs to be finalized (generate a real one
   via Max's Class_ID generator utility before this touches any scene
   file you intend to keep).
5. Longer-term, not urgent: closed-shape "area mode" for splines (never
   implemented, prototype only did open-spline centerline logic); Space
   Warp port if the position-lock limitation proves disruptive in real
   use; performance work if the brute-force closest-point scan is too
   slow on production-density meshes (spatial partitioning was flagged
   early as a "later" optimization, never revisited).
