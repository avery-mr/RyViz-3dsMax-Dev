/*
	RyViz_SplineGradeProto.ms  (v2 -- multi-spline)

	Prototype / proof-of-concept for the SplineGradeMod algorithm.
	Supports multiple ordered splines applied sequentially -- each spline
	deforms the mesh starting from the RESULT of the previous spline's pass
	(i.e. later entries in the list see the stack result, not the original
	base mesh). This validates the ordering/cascading design before porting
	to the C++ SDK modifier.

	Usage:
		1. Select your dense reference mesh (Editable_Poly) FIRST.
		2. Run this script.
		3. Click "Use Selected Mesh".
		4. Click "Add Spline" and pick a spline in the viewport -- repeat
		   for each road/spline you want to add. New entries append to the
		   BOTTOM of the list (applied last).
		5. Select an entry in the list to edit its Width / Falloff /
		   Strength / Enabled in the "Selected Spline" group.
		6. Use "Move Up" / "Move Down" to change apply order.
		7. Click "Apply All" to run the full sequence.
		8. Click "Restore Original" to reset the mesh to its pre-tool state.

	Notes:
		- Mesh must be Editable_Poly (uses polyop.getVert / polyop.setVert).
		- Vertex coordinates are handled via EXPLICIT matrix transform
		  (obj.transform / inverse obj.transform) -- do NOT switch to the
		  polyop node: keyword, it was empirically unreliable during
		  prototyping and produced pivot-distance-dependent errors.
		- interpCurve3D already returns WORLD-SPACE points for a picked
		  node -- do NOT multiply by shapeNode.transform, that double-
		  applies the offset (also empirically confirmed during prototyping).
		- Closed-shape "area mode" is NOT implemented -- open-spline
		  centerline logic only.
		- Known limitation: tight self-overlapping curves (hairpins where
		  width+falloff exceeds the gap between two strands of the same
		  spline) will show pinched/distorted seams -- this is an inherent
		  limitation of the global-nearest-segment approach. Documented,
		  not fixed, in this prototype. Future fix: inverse-distance blend
		  across multiple candidate regions instead of winner-take-all
		  nearest match.
*/

global RyViz_SplineGrade_Rollout

(
	struct SplineEntry (splineNode, width, falloff, strength, enabled)

	local targetMesh = undefined
	local splineEntries = #()			-- array of SplineEntry, in apply order
	local selectedIndex = 0			-- 1-based index into splineEntries, 0 = none selected
	local basePositions = #()			-- cached original vertex positions (Point3 array, world space)
	local loadingSelection = false		-- guard flag to prevent feedback loops when loading UI from a selected entry

	-- ---------------------------------------------------------------
	-- Sample a shape node into a polyline of WORLD-SPACE Point3s.
	-- interpCurve3D already returns world-space coords for a picked
	-- node -- confirmed empirically; do not re-transform.
	-- ---------------------------------------------------------------
	function sampleSplineToPolyline shapeNode numSamples =
	(
		local pts = #()
		local splineIndex = 1

		for i = 0 to numSamples do
		(
			local u = i as float / numSamples as float
			local worldPt = interpCurve3D shapeNode splineIndex u
			append pts worldPt
		)
		pts
	)

	-- ---------------------------------------------------------------
	-- Given a world-space point P (only .x/.y used) and a polyline
	-- (array of world-space Point3), find:
	--   - targetZ: interpolated Z of the closest point on the polyline
	--   - lateralDist: XY-only distance from P to that closest point
	-- ---------------------------------------------------------------
	function closestPointOnPolylineXY P polyline =
	(
		local bestDistSq = 1e30
		local bestZ = P.z
		local n = polyline.count

		if n < 2 then return #(P.z, 1e30)

		for i = 1 to (n - 1) do
		(
			local A = polyline[i]
			local B = polyline[i + 1]

			local a_x = A.x
			local a_y = A.y
			local b_x = B.x
			local b_y = B.y

			local dx = b_x - a_x
			local dy = b_y - a_y
			local segLenSq = (dx * dx) + (dy * dy)

			local t = 0.0
			if segLenSq > 0.00001 then
			(
				t = (((P.x - a_x) * dx) + ((P.y - a_y) * dy)) / segLenSq
				t = amax 0.0 (amin 1.0 t)
			)

			local c_x = a_x + (t * dx)
			local c_y = a_y + (t * dy)
			local distSq = ((P.x - c_x) * (P.x - c_x)) + ((P.y - c_y) * (P.y - c_y))

			if distSq < bestDistSq then
			(
				bestDistSq = distSq
				bestZ = A.z + (t * (B.z - A.z))
			)
		)

		#(bestZ, sqrt bestDistSq)
	)

	-- ---------------------------------------------------------------
	-- Smoothstep falloff: 1.0 inside halfWidth, eases to 0.0 over
	-- falloffDist, 0.0 beyond.
	-- ---------------------------------------------------------------
	function falloffWeight lateralDist halfWidth falloffDist =
	(
		if lateralDist <= halfWidth then return 1.0
		if falloffDist <= 0.0 then return 0.0

		local outerEdge = halfWidth + falloffDist
		if lateralDist >= outerEdge then return 0.0

		local u = (lateralDist - halfWidth) / falloffDist
		local s = 1.0 - u
		(s * s * (3.0 - (2.0 * s)))
	)

	-- ---------------------------------------------------------------
	-- Debug: spawn small point helpers at every sampled polyline
	-- location for ALL entries (color-coded by index) so sampling
	-- correctness can be checked visually.
	-- ---------------------------------------------------------------
	function clearDebugPoints =
	(
		local toDelete = for o in objects where (matchPattern o.name pattern:"RyViz_SampPt_*") collect o
		for o in toDelete do delete o
	)

	local debugColors = #((color 255 0 0), (color 0 255 0), (color 0 128 255), (color 255 255 0), (color 255 0 255), (color 0 255 255))

	function visualizeAllSamples sampleCount =
	(
		if splineEntries.count == 0 then
		(
			messageBox "Add at least one spline first."
			return false
		)
		clearDebugPoints()
		local total = 0
		for e = 1 to splineEntries.count do
		(
			local entry = splineEntries[e]
			local pts = sampleSplineToPolyline entry.splineNode sampleCount
			local col = debugColors[((e - 1) mod debugColors.count) + 1]
			for i = 1 to pts.count do
			(
				local p = Point pos:pts[i] size:5 cross:true box:false wirecolor:col
				p.name = "RyViz_SampPt_" + (e as string) + "_" + (i as string)
			)
			total += pts.count
		)
		format "Spawned % debug sample points across % spline(s)\n" total splineEntries.count
		true
	)

	-- ---------------------------------------------------------------
	-- Cache the target mesh's current vertex positions (world space,
	-- via explicit matrix) as the base to deform from.
	-- ---------------------------------------------------------------
	function cacheBasePositions obj =
	(
		basePositions = #()
		local vc = polyop.getNumVerts obj
		local tm = obj.transform
		for i = 1 to vc do
			append basePositions ((polyop.getVert obj i) * tm)
		basePositions
	)

	-- ---------------------------------------------------------------
	-- Apply the full ordered sequence of enabled splines. Each spline
	-- deforms from the RESULT of the previous spline's pass (cascading
	-- stack behavior), starting from the cached base positions.
	-- ---------------------------------------------------------------
	function applyAllSplines obj sampleCount =
	(
		if obj == undefined then return false
		if basePositions.count == 0 then cacheBasePositions obj
		if splineEntries.count == 0 then return false

		-- working copy we mutate progressively through the spline stack
		local working = for p in basePositions collect p

		for entry in splineEntries do
		(
			if entry.enabled and entry.splineNode != undefined then
			(
				local polyline = sampleSplineToPolyline entry.splineNode sampleCount
				local halfWidth = entry.width / 2.0

				for i = 1 to working.count do
				(
					local baseP = working[i]
					local result = closestPointOnPolylineXY baseP polyline
					local targetZ = result[1]
					local lateralDist = result[2]

					local w = falloffWeight lateralDist halfWidth entry.falloff
					w = w * entry.strength

					local newZ = baseP.z + (w * (targetZ - baseP.z))
					working[i] = [baseP.x, baseP.y, newZ]
				)
			)
		)

		-- write final result to mesh
		local invTm = inverse obj.transform
		for i = 1 to working.count do
			polyop.setVert obj i (working[i] * invTm)

		update obj
		true
	)

	-- ---------------------------------------------------------------
	-- Restore mesh to cached base positions.
	-- ---------------------------------------------------------------
	function restoreOriginal obj =
	(
		if obj == undefined or basePositions.count == 0 then return false
		local vc = basePositions.count
		local invTm = inverse obj.transform
		for i = 1 to vc do
			polyop.setVert obj i (basePositions[i] * invTm)
		update obj
		true
	)

	-- ---------------------------------------------------------------
	-- UI
	-- ---------------------------------------------------------------
	rollout RyViz_SplineGrade_Rollout "RyViz Spline Grade (Prototype v2)" width:280
	(
		group "Target"
		(
			button btnPickMesh "Use Selected Mesh" width:260
			label lblMeshName "Mesh: <none>" align:#left
		)

		group "Splines (apply order top to bottom)"
		(
			listbox lbxSplines "" height:6 width:260
			pickbutton btnAddSpline "Add Spline" width:260
			button btnRemoveSpline "Remove Selected" width:260
			button btnMoveUp "Move Up" width:126 across:2
			button btnMoveDown "Move Down" width:126
		)

		group "Selected Spline"
		(
			spinner spnEntryWidth "Width:    " range:[0.0, 100000.0, 120.0] type:#float fieldWidth:70
			spinner spnEntryFalloff "Falloff:  " range:[0.0, 100000.0, 60.0] type:#float fieldWidth:70
			spinner spnEntryStrength "Strength: " range:[0.0, 1.0, 1.0] type:#float fieldWidth:70
			checkbox chkEntryEnabled "Enabled" checked:true
		)

		group "Global"
		(
			spinner spnSamples "Samples per spline: " range:[4, 2000, 100] type:#integer fieldWidth:70
		)

		button btnApplyAll "Apply All" width:260 height:28
		button btnRestore "Restore Original" width:260
		button btnRecache "Re-cache Base (after manual edits)" width:260
		button btnVisualize "Visualize All Spline Samples" width:260
		button btnClearDebug "Clear Debug Points" width:260

		-- -----------------------------------------------------------
		-- helpers local to the rollout
		-- -----------------------------------------------------------
		fn refreshListbox =
		(
			local names = for e in splineEntries collect (e.splineNode.name + "  [w:" + (e.width as string) + "]" + (if e.enabled then "" else "  (disabled)"))
			lbxSplines.items = names
			if selectedIndex > 0 and selectedIndex <= splineEntries.count then
				lbxSplines.selection = selectedIndex
		)

		fn loadSelectedEntryToUI =
		(
			if selectedIndex > 0 and selectedIndex <= splineEntries.count then
			(
				loadingSelection = true
				local e = splineEntries[selectedIndex]
				spnEntryWidth.value = e.width
				spnEntryFalloff.value = e.falloff
				spnEntryStrength.value = e.strength
				chkEntryEnabled.checked = e.enabled
				loadingSelection = false
			)
		)

		on btnPickMesh pressed do
		(
			if selection.count == 1 and (classOf selection[1] == Editable_Poly) then
			(
				targetMesh = selection[1]
				lblMeshName.text = "Mesh: " + targetMesh.name
				cacheBasePositions targetMesh
			)
			else
			(
				messageBox "Select exactly one Editable_Poly object first."
			)
		)

		on btnAddSpline picked obj do
		(
			if (superClassOf obj == shape) then
			(
				local newEntry = SplineEntry splineNode:obj width:120.0 falloff:60.0 strength:1.0 enabled:true
				append splineEntries newEntry
				selectedIndex = splineEntries.count
				refreshListbox()
				loadSelectedEntryToUI()
			)
			else
			(
				messageBox "Pick a spline/shape object."
			)
		)

		on lbxSplines selected sel do
		(
			selectedIndex = sel
			loadSelectedEntryToUI()
		)

		on btnRemoveSpline pressed do
		(
			if selectedIndex > 0 and selectedIndex <= splineEntries.count then
			(
				deleteItem splineEntries selectedIndex
				selectedIndex = amin selectedIndex splineEntries.count
				refreshListbox()
				loadSelectedEntryToUI()
			)
		)

		on btnMoveUp pressed do
		(
			if selectedIndex > 1 then
			(
				local tmp = splineEntries[selectedIndex - 1]
				splineEntries[selectedIndex - 1] = splineEntries[selectedIndex]
				splineEntries[selectedIndex] = tmp
				selectedIndex = selectedIndex - 1
				refreshListbox()
			)
		)

		on btnMoveDown pressed do
		(
			if selectedIndex > 0 and selectedIndex < splineEntries.count then
			(
				local tmp = splineEntries[selectedIndex + 1]
				splineEntries[selectedIndex + 1] = splineEntries[selectedIndex]
				splineEntries[selectedIndex] = tmp
				selectedIndex = selectedIndex + 1
				refreshListbox()
			)
		)

		-- edits to the selected entry's params write straight back into the struct
		on spnEntryWidth changed val do
		(
			if not loadingSelection and selectedIndex > 0 then
			(
				splineEntries[selectedIndex].width = val
				refreshListbox()
			)
		)
		on spnEntryFalloff changed val do
		(
			if not loadingSelection and selectedIndex > 0 then splineEntries[selectedIndex].falloff = val
		)
		on spnEntryStrength changed val do
		(
			if not loadingSelection and selectedIndex > 0 then splineEntries[selectedIndex].strength = val
		)
		on chkEntryEnabled changed state do
		(
			if not loadingSelection and selectedIndex > 0 then
			(
				splineEntries[selectedIndex].enabled = state
				refreshListbox()
			)
		)

		on btnApplyAll pressed do
		(
			applyAllSplines targetMesh spnSamples.value
		)

		on btnRestore pressed do
		(
			restoreOriginal targetMesh
		)

		on btnRecache pressed do
		(
			if targetMesh != undefined then cacheBasePositions targetMesh
		)

		on btnVisualize pressed do
		(
			visualizeAllSamples spnSamples.value
		)

		on btnClearDebug pressed do
		(
			clearDebugPoints()
		)
	)

	createDialog RyViz_SplineGrade_Rollout
)
