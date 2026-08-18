/*
	SplineGradeMod.h

	Phase 2: multi-spline list management, porting the ordering/cascading
	behavior validated in RyViz_SplineGradeProto.ms (v2) into the C++
	modifier. Each entry (spline node, width, falloff, strength, enabled)
	lives in parallel Tab<> paramblock arrays, applied in list order --
	later entries deform the RESULT of earlier ones (cascading stack
	behavior), same as the prototype.

	ARCHITECTURE CHANGE FROM PHASE 1: P_AUTO_UI is dropped entirely. Auto-UI
	binds one control directly to one scalar parameter -- it has no notion
	of "edit whichever list entry is currently selected," which is exactly
	what a variable-length spline list needs. Instead:
		- The rollout dialog is created manually (ip->AddRollupPage) with a
		  classic DialogProc, not an auto-bound ParamMap2 dialog.
		- Spinners are wired up manually via GetISpinner()/SetupFloatSpinner
		  on dialog controls -- NOT via p_ui tags in the paramblock.
		- The "Add Spline" button uses a hand-written PickModeCallback
		  (CommandMode-based node picking) instead of TYPE_PICKNODEBUTTON,
		  since that auto-UI tag doesn't apply here either.
		- Selecting a listbox entry pushes that entry's Tab values into the
		  spinners; editing a spinner writes back into the Tab at the
		  selected index. This mirrors the prototype's loadSelectedEntryToUI
		  / spnEntryWidth-changed-handler pattern exactly, just in C++.

	CONFIDENCE NOTE: the paramblock Tab plumbing and ModifyObject cascade
	loop are a direct, well-understood port of validated logic. The manual
	dialog proc / spinner control wiring is comparatively unfamiliar
	territory for this project -- expect a real build-and-fix cycle here,
	same as Phase 1's paramblock/reference-system debugging, not a
	first-try compile.

	KNOWN LIMITATION carried over unchanged from Phase 1: moving the host
	mesh after applying this modifier does not re-track the spline(s) --
	standard OSMs don't re-trigger ModifyObject on a pure node move. See
	Phase 1 source history for the full writeup. A Space Warp is the
	correct SDK tool for genuinely world-space-reactive behavior; deferred.

	Coordinate-space rule, unchanged from Phase 1: spline geometry pulled
	via EvalWorldState is LOCAL space -- multiply by splineNode->GetNodeTM(t)
	to get world space. Mesh vertices: prefer node->GetNodeTM(t) when node
	is non-null (tracks the object's real position), fall back to mc.tm /
	identity only when node is null (rare background evaluation).
*/

#pragma once

#include "3dsmaxsdk_preinclude.h"
#include "max.h"
#include "iparamm2.h"
#include "iparamb2.h"
#include "shape.h"
#include "simpobj.h"
#include "resource.h"

#define SPLINEGRADEMOD_CLASS_ID Class_ID(0x1a2b3c4d, 0x5e6f7081)	// PLACEHOLDER -- see Phase 1 note.

extern HINSTANCE hInstance;
extern ClassDesc2* GetSplineGradeModDesc();

enum { splinegrademod_params };

enum
{
	pb_splineNodes,		// TYPE_INODE_TAB  -- one entry per spline, in apply order
	pb_widths,			// TYPE_FLOAT_TAB  -- parallel to pb_splineNodes
	pb_falloffs,		// TYPE_FLOAT_TAB  -- parallel
	pb_strengths,		// TYPE_FLOAT_TAB  -- parallel
	pb_enabled,			// TYPE_INT_TAB    -- parallel, 0/1 (TYPE_BOOL_TAB may not exist in this SDK -- verify; INT is a safe fallback)
	pb_samples			// TYPE_INT        -- single global value, NOT per-entry (matches prototype)
};

// Dialog control IDs the companion .rc needs to define (in addition to/
// replacing Phase 1's IDC_ set):
//   IDC_SPLINE_LIST      -- native listbox
//   IDC_ADD_SPLINE       -- button, triggers pick mode
//   IDC_REMOVE_SPLINE    -- button
//   IDC_MOVE_UP          -- button
//   IDC_MOVE_DOWN        -- button
//   IDC_ENTRY_WIDTH_EDIT / IDC_ENTRY_WIDTH_SPIN
//   IDC_ENTRY_FALLOFF_EDIT / IDC_ENTRY_FALLOFF_SPIN
//   IDC_ENTRY_STRENGTH_EDIT / IDC_ENTRY_STRENGTH_SPIN
//   IDC_ENTRY_ENABLED    -- checkbox
//   IDC_SAMPLES_EDIT / IDC_SAMPLES_SPIN  -- global, reused from Phase 1

class SplineGradeMod : public Modifier
{
public:
	IParamBlock2* pblock;
	static IObjParam* ip;

	int selectedIndex;		// currently selected listbox entry, -1 = none
	HWND hPanel;			// dialog HWND, valid only while the rollout is open

	SplineGradeMod();
	~SplineGradeMod();

	void DeleteThis() override { delete this; }
	Class_ID ClassID() override { return SPLINEGRADEMOD_CLASS_ID; }
	SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
	void GetClassName(MSTR& s, bool localized = true) const override { s = MSTR(_M("SplineGradeMod")); }

	int NumParamBlocks() override { return 1; }
	IParamBlock2* GetParamBlock(int i) override { return pblock; }
	IParamBlock2* GetParamBlockByID(BlockID id) override { return (pblock->ID() == id) ? pblock : nullptr; }

	int NumRefs() override { return 1; }
	RefTargetHandle GetReference(int i) override { return pblock; }
	void SetReference(int i, RefTargetHandle rtarg) override { pblock = (IParamBlock2*)rtarg; }
	RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID, RefMessage message, BOOL propagate = TRUE) override;

	int NumSubs() override { return 1; }
	Animatable* SubAnim(int i) override { return pblock; }
	MSTR SubAnimName(int i, bool localized = true) override { return MSTR(_M("Parameters")); }

	void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
	void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;

	ChannelMask ChannelsUsed() override { return GEOM_CHANNEL | TOPO_CHANNEL; }
	ChannelMask ChannelsChanged() override { return GEOM_CHANNEL; }
	Class_ID InputType() override { return defObjectClassID; }
	void ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node) override;
	Interval LocalValidity(TimeValue t) override;

	CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
	const MCHAR* GetObjectName(bool localized) const override { return _M("RyViz Spline Grade"); }

	RefTargetHandle Clone(RemapDir& remap) override;

	// -- UI helpers, called from the dialog proc (see .cpp) --
	void RefreshListbox();
	void LoadSelectedEntryToUI();
	void AddSplineEntry(INode* splineNode);
	void RemoveSelectedEntry();
	void MoveSelectedEntry(int direction);	// -1 = up, +1 = down

private:
	void SampleSplineToPolyline(INode* splineNode, TimeValue t, int numSamples, Tab<Point3>& outPts);
	float ClosestPointOnPolylineXY(const Point3& p, const Tab<Point3>& polyline, float& outTargetZ);
	float FalloffWeight(float lateralDist, float halfWidth, float falloffDist);
};

// PickModeCallback + PickNodeCallback on the same object is the SDK
// sample pattern (Linked XForm, Skin Wrap, Morpher). Filter must NOT
// call EvalWorldState -- that re-enters the pipeline during hit-testing
// and is the usual crash when clicking a spline in pick mode.
class SplineGradeAddSplinePick : public PickModeCallback, public PickNodeCallback
{
public:
	SplineGradeMod* mod;
	INode* hitNode;

	SplineGradeAddSplinePick() : mod(nullptr), hitNode(nullptr) {}

	BOOL HitTest(IObjParam* ip, HWND hWnd, ViewExp* vpt, IPoint2 m, int flags) override;
	BOOL Pick(IObjParam* ip, ViewExp* vpt) override;
	BOOL Filter(INode* node) override;
	PickNodeCallback* GetFilter() override { return this; }
	BOOL RightClick(IObjParam* /*ip*/, ViewExp* /*vpt*/) override { return TRUE; }
	void EnterMode(IObjParam* ip) override;
	void ExitMode(IObjParam* ip) override;
};
