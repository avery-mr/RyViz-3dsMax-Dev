// Cursor sync check — file is live in the RyViz-MaxDev workspace.

/*
	SplineGradeMod.cpp

	Phase 2: multi-spline cascading list. See header for the architecture
	writeup (manual UI, no P_AUTO_UI) and coordinate-space rules carried
	over from Phase 1.
*/

#include "SplineGradeMod.h"
#include "iparamm2.h"
#include "polyobj.h"
#include "triobj.h"
#include "mnmesh.h"
#include "shape.h"
#include "custcont.h"

IObjParam* SplineGradeMod::ip = nullptr;
static WNDPROC s_oldDlgProc = nullptr;
static SplineGradeMod* s_editMod = nullptr;

static void WriteEntrySpinnersToPblock(SplineGradeMod* mod, TimeValue t)
{
	if (!mod || !mod->pblock) return;
	mod->SyncSelectionFromList();
	if (mod->selectedIndex < 0) return;
	if (mod->spinWidth)
		mod->pblock->SetValue(pb_widths, t, mod->spinWidth->GetFVal(), mod->selectedIndex);
	if (mod->spinFalloff)
		mod->pblock->SetValue(pb_falloffs, t, mod->spinFalloff->GetFVal(), mod->selectedIndex);
	if (mod->spinStrength)
		mod->pblock->SetValue(pb_strengths, t, mod->spinStrength->GetFVal(), mod->selectedIndex);
	mod->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
	if (SplineGradeMod::ip)
		SplineGradeMod::ip->RedrawViews(t);
}

static bool IsEntrySpinnerCtrl(int ctrlID)
{
	return ctrlID == IDC_ENTRY_WIDTH_SPIN || ctrlID == IDC_ENTRY_WIDTH_EDIT ||
		ctrlID == IDC_ENTRY_FALLOFF_SPIN || ctrlID == IDC_ENTRY_FALLOFF_EDIT ||
		ctrlID == IDC_ENTRY_STRENGTH_SPIN || ctrlID == IDC_ENTRY_STRENGTH_EDIT;
}

// ParamMap2 owns the dialog wndproc. It does not reliably forward WM_INITDIALOG
// or clicks on non-p_ui controls, which is why the entry spinners stayed blank
// and Move Up/Down did nothing. Chain-subclass after the map is created.
static LRESULT CALLBACK EntrySubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = s_oldDlgProc ? CallWindowProc(s_oldDlgProc, hWnd, msg, wParam, lParam) : 0;
	SplineGradeMod* mod = s_editMod;
	if (!mod)
		return result;

	TimeValue t = SplineGradeMod::ip ? SplineGradeMod::ip->GetTime() : 0;

	if (msg == WM_COMMAND)
	{
		int ctrlID = LOWORD(wParam);
		int notifyCode = HIWORD(wParam);
		if (ctrlID == IDC_MOVE_UP && notifyCode == BN_CLICKED)
		{
			mod->SyncSelectionFromList();
			mod->MoveSelectedEntry(-1);
		}
		else if (ctrlID == IDC_MOVE_DOWN && notifyCode == BN_CLICKED)
		{
			mod->SyncSelectionFromList();
			mod->MoveSelectedEntry(1);
		}
		else if (ctrlID == IDC_SPLINE_LIST && notifyCode == LBN_SELCHANGE)
		{
			mod->SyncSelectionFromList();
			mod->LoadSelectedEntryToUI();
		}
		else if (ctrlID == IDC_ENTRY_ENABLED && notifyCode == BN_CLICKED)
		{
			mod->SyncSelectionFromList();
			if (mod->selectedIndex >= 0)
			{
				BOOL checked = IsDlgButtonChecked(hWnd, IDC_ENTRY_ENABLED);
				mod->pblock->SetValue(pb_enabled, t, checked ? 1 : 0, mod->selectedIndex);
				mod->NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
				if (SplineGradeMod::ip)
					SplineGradeMod::ip->RedrawViews(t);
			}
		}
	}
	else if (msg == CC_SPINNER_CHANGE || msg == CC_SPINNER_BUTTONUP || msg == WM_CUSTEDIT_ENTER)
	{
		if (IsEntrySpinnerCtrl(LOWORD(wParam)))
			WriteEntrySpinnersToPblock(mod, t);
	}

	return result;
}

class SplineGradeModClassDesc : public ClassDesc2
{
public:
	int IsPublic() override { return TRUE; }
	void* Create(BOOL loading = FALSE) override { return new SplineGradeMod(); }
	const TCHAR* ClassName() override { return _T("RySplineGrade"); }
	const TCHAR* NonLocalizedClassName() override { return _T("RySplineGrade"); }
	SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
	Class_ID ClassID() override { return SPLINEGRADEMOD_CLASS_ID; }
	const TCHAR* Category() override { return _T("RyViz"); }
	const TCHAR* InternalName() override { return _T("RySplineGrade"); }
	HINSTANCE HInstance() override { return hInstance; }
};

static SplineGradeModClassDesc splinegrademodDesc;
ClassDesc2* GetSplineGradeModDesc() { return &splinegrademodDesc; }

// Keep width/falloff/strength/enabled tabs the same length as splineNodes.
// TYPE_NODELISTBOX only mutates the INode tab; we pad/trim the rest here.
class SplineListAccessor : public PBAccessor
{
public:
	void TabChanged(tab_changes changeCode, Tab<PB2Value>* /*tab*/, ReferenceMaker* owner,
		ParamID id, int tabIndex, int count) override
	{
		if (id != pb_splineNodes) return;
		SplineGradeMod* mod = (SplineGradeMod*)owner;
		if (!mod || !mod->pblock) return;

		switch (changeCode)
		{
		case tab_append:
		case tab_insert:
			mod->SyncParallelTabs();
			mod->selectedIndex = mod->pblock->Count(pb_splineNodes) - 1;
			break;
		case tab_delete:
		case tab_ref_deleted:
			if (tabIndex >= 0 && count > 0)
			{
				if (tabIndex < mod->pblock->Count(pb_widths))
					mod->pblock->Delete(pb_widths, tabIndex, count);
				if (tabIndex < mod->pblock->Count(pb_falloffs))
					mod->pblock->Delete(pb_falloffs, tabIndex, count);
				if (tabIndex < mod->pblock->Count(pb_strengths))
					mod->pblock->Delete(pb_strengths, tabIndex, count);
				if (tabIndex < mod->pblock->Count(pb_enabled))
					mod->pblock->Delete(pb_enabled, tabIndex, count);
			}
			{
				int n = mod->pblock->Count(pb_splineNodes);
				if (mod->selectedIndex >= n)
					mod->selectedIndex = n - 1;
			}
			break;
		case tab_setcount:
			mod->SyncParallelTabs();
			break;
		default:
			break;
		}

		mod->InitEntryControls();
		// Do not NotifyDependents here -- adding the INode already dirties
		// the reference graph. Doing it inside the pick callback re-enters
		// ModifyObject (EvalWorldState on the spline being picked) and can
		// freeze Max.
	}
};

static SplineListAccessor splineListAccessor;

// ---------------------------------------------------------------------
// ParamBlock2 -- P_AUTO_UI so TYPE_NODELISTBOX owns Add/Remove pick.
// That is Max's built-in picker (same as Phase 1 TYPE_PICKNODEBUTTON).
// ---------------------------------------------------------------------
static ParamBlockDesc2 splinegrademod_paramblock(
	splinegrademod_params, _T("Parameters"), 0, &splinegrademodDesc,
	P_AUTO_CONSTRUCT + P_AUTO_UI, 0,
	IDD_SPLINEGRADE_PANEL, IDS_PARAMS, 0, 0, NULL,

	pb_splineNodes, _T("splineNodes"), TYPE_INODE_TAB, 0, P_AUTO_UI | P_VARIABLE_SIZE, IDS_SPLINENODE,
		p_ui, TYPE_NODELISTBOX, IDC_SPLINE_LIST, IDC_ADD_SPLINE, 0, IDC_REMOVE_SPLINE,
		p_sclassID, SHAPE_CLASS_ID,
		p_prompt, IDS_PICK_SPLINE,
		p_accessor, &splineListAccessor,
		p_end,

	pb_widths, _T("widths"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, IDS_WIDTH,
		p_end,

	pb_falloffs, _T("falloffs"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, IDS_FALLOFF,
		p_end,

	pb_strengths, _T("strengths"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, IDS_STRENGTH,
		p_end,

	pb_enabled, _T("enabled"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, 0,
		p_end,

	pb_samples, _T("samples"), TYPE_INT, 0, IDS_SAMPLES,
		p_default, 100,
		p_range, 4, 2000,
		p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_SAMPLES_EDIT, IDC_SAMPLES_SPIN, 1.0f,
		p_end,

	p_end
);

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------
SplineGradeMod::SplineGradeMod()
	: pblock(nullptr), selectedIndex(-1), hPanel(nullptr),
	  spinWidth(nullptr), spinFalloff(nullptr), spinStrength(nullptr)
{
	GetSplineGradeModDesc()->MakeAutoParamBlocks(this);
}

SplineGradeMod::~SplineGradeMod()
{
}

RefTargetHandle SplineGradeMod::Clone(RemapDir& remap)
{
	SplineGradeMod* newMod = new SplineGradeMod();
	newMod->ReplaceReference(0, remap.CloneRef(pblock));
	BaseClone(this, newMod, remap);
	return newMod;
}

Interval SplineGradeMod::LocalValidity(TimeValue t)
{
	Interval valid = FOREVER;
	if (pblock)
		pblock->GetValidity(t, valid);
	return valid;
}

RefResult SplineGradeMod::NotifyRefChanged(const Interval& /*changeInt*/, RefTargetHandle hTarget,
	PartID& /*partID*/, RefMessage message, BOOL /*propagate*/)
{
	if (message == REFMSG_CHANGE && hTarget == pblock && pblock)
	{
		IParamMap2* map = pblock->GetMap();
		if (map)
			map->Invalidate(pblock->LastNotifyParamID());
	}
	return REF_SUCCEED;
}

// ParamMap2 user dlg proc -- InitEntryControls is the real setup.
// Clicks on non-p_ui controls are handled by EntrySubclassProc.
class SplineGradeDlgProc : public ParamMap2UserDlgProc
{
public:
	SplineGradeMod* mod;
	SplineGradeDlgProc(SplineGradeMod* m) : mod(m) {}
	void DeleteThis() override { delete this; }

	INT_PTR DlgProc(TimeValue /*t*/, IParamMap2* /*map*/, HWND /*hWnd*/, UINT msg, WPARAM /*wParam*/, LPARAM /*lParam*/) override
	{
		if (msg == WM_INITDIALOG)
			mod->InitEntryControls();
		return FALSE;
	}
};

void SplineGradeMod::InitEntryControls()
{
	if (!pblock) return;
	IParamMap2* map = pblock->GetMap();
	if (!map) return;
	HWND hWnd = map->GetHWnd();
	if (!hWnd) return;

	hPanel = hWnd;
	s_editMod = this;

	if (!spinWidth)
	{
		spinWidth = SetupUniverseSpinner(hWnd, IDC_ENTRY_WIDTH_SPIN, IDC_ENTRY_WIDTH_EDIT, 0.0f, 100000.0f, 120.0f);
		spinWidth->SetAutoScale(TRUE);
	}
	if (!spinFalloff)
	{
		spinFalloff = SetupUniverseSpinner(hWnd, IDC_ENTRY_FALLOFF_SPIN, IDC_ENTRY_FALLOFF_EDIT, 0.0f, 100000.0f, 60.0f);
		spinFalloff->SetAutoScale(TRUE);
	}
	if (!spinStrength)
		spinStrength = SetupFloatSpinner(hWnd, IDC_ENTRY_STRENGTH_SPIN, IDC_ENTRY_STRENGTH_EDIT, 0.0f, 1.0f, 1.0f, 0.01f);

	if (!s_oldDlgProc)
	{
		s_oldDlgProc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)EntrySubclassProc);
	}

	if (selectedIndex < 0 && pblock->Count(pb_splineNodes) > 0)
		selectedIndex = pblock->Count(pb_splineNodes) - 1;
	SyncSelectionFromList();
	if (selectedIndex < 0 && pblock->Count(pb_splineNodes) > 0)
		selectedIndex = pblock->Count(pb_splineNodes) - 1;
	LoadSelectedEntryToUI();
}

void SplineGradeMod::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
	this->ip = ip;
	TimeValue t = ip->GetTime();
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_BEGIN_EDIT);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_MOD_DISPLAY_ON);
	SetAFlag(A_MOD_BEING_EDITED);

	GetSplineGradeModDesc()->BeginEditParams(ip, this, flags, prev);
	splinegrademod_paramblock.SetUserDlgProc(new SplineGradeDlgProc(this));
	InitEntryControls();
}

void SplineGradeMod::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
	if (hPanel && s_oldDlgProc)
	{
		SetWindowLongPtr(hPanel, GWLP_WNDPROC, (LONG_PTR)s_oldDlgProc);
		s_oldDlgProc = nullptr;
	}
	s_editMod = nullptr;

	if (spinWidth) { ReleaseISpinner(spinWidth); spinWidth = nullptr; }
	if (spinFalloff) { ReleaseISpinner(spinFalloff); spinFalloff = nullptr; }
	if (spinStrength) { ReleaseISpinner(spinStrength); spinStrength = nullptr; }

	GetSplineGradeModDesc()->EndEditParams(ip, this, flags, next);
	hPanel = nullptr;

	TimeValue t = ip->GetTime();
	ClearAFlag(A_MOD_BEING_EDITED);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_END_EDIT);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_MOD_DISPLAY_OFF);
	this->ip = nullptr;
}

// =======================================================================
// UI helper methods
// =======================================================================
void SplineGradeMod::RefreshListbox()
{
	if (!pblock) return;
	IParamMap2* map = pblock->GetMap();
	if (map)
		map->Invalidate(pb_splineNodes);
	if (hPanel && selectedIndex >= 0)
		SendDlgItemMessage(hPanel, IDC_SPLINE_LIST, LB_SETCURSEL, selectedIndex, 0);
}

void SplineGradeMod::SyncParallelTabs()
{
	if (!pblock) return;
	int n = pblock->Count(pb_splineNodes);
	int oldW = pblock->Count(pb_widths);
	pblock->SetCount(pb_widths, n);
	pblock->SetCount(pb_falloffs, n);
	pblock->SetCount(pb_strengths, n);
	pblock->SetCount(pb_enabled, n);
	for (int i = oldW; i < n; i++)
	{
		pblock->SetValue(pb_widths, 0, 120.0f, i);
		pblock->SetValue(pb_falloffs, 0, 60.0f, i);
		pblock->SetValue(pb_strengths, 0, 1.0f, i);
		pblock->SetValue(pb_enabled, 0, 1, i);
	}
}

void SplineGradeMod::LoadSelectedEntryToUI()
{
	if (!hPanel || selectedIndex < 0) return;

	float w = 120.0f, f = 60.0f, s = 1.0f;
	int en = 1;
	pblock->GetValue(pb_widths, 0, w, FOREVER, selectedIndex);
	pblock->GetValue(pb_falloffs, 0, f, FOREVER, selectedIndex);
	pblock->GetValue(pb_strengths, 0, s, FOREVER, selectedIndex);
	pblock->GetValue(pb_enabled, 0, en, FOREVER, selectedIndex);

	if (spinWidth) spinWidth->SetValue(w, FALSE);
	if (spinFalloff) spinFalloff->SetValue(f, FALSE);
	if (spinStrength) spinStrength->SetValue(s, FALSE);

	CheckDlgButton(hPanel, IDC_ENTRY_ENABLED, en ? BST_CHECKED : BST_UNCHECKED);
}

int SplineGradeMod::SyncSelectionFromList()
{
	if (!hPanel) return selectedIndex;
	int sel = (int)SendDlgItemMessage(hPanel, IDC_SPLINE_LIST, LB_GETCURSEL, 0, 0);
	selectedIndex = (sel == LB_ERR) ? -1 : sel;
	return selectedIndex;
}

void SplineGradeMod::AddSplineEntry(INode* splineNode)
{
	if (!splineNode || !pblock) return;
	pblock->Append(pb_splineNodes, 1, &splineNode);
}

void SplineGradeMod::RemoveSelectedEntry()
{
	if (selectedIndex < 0 || !pblock) return;
	int count = pblock->Count(pb_splineNodes);
	if (selectedIndex >= count) return;
	pblock->Delete(pb_splineNodes, selectedIndex, 1);
}

void SplineGradeMod::MoveSelectedEntry(int direction)
{
	int count = pblock->Count(pb_splineNodes);
	int a = selectedIndex;
	int b = selectedIndex + direction;
	if (a < 0 || b < 0 || b >= count) return;

	// swap via read/write -- Tabs don't have a built-in swap
	INode *nA = nullptr, *nB = nullptr;
	float wA, wB, fA, fB, sA, sB;
	int eA, eB;

	pblock->GetValue(pb_splineNodes, 0, nA, FOREVER, a);
	pblock->GetValue(pb_splineNodes, 0, nB, FOREVER, b);
	pblock->GetValue(pb_widths, 0, wA, FOREVER, a);
	pblock->GetValue(pb_widths, 0, wB, FOREVER, b);
	pblock->GetValue(pb_falloffs, 0, fA, FOREVER, a);
	pblock->GetValue(pb_falloffs, 0, fB, FOREVER, b);
	pblock->GetValue(pb_strengths, 0, sA, FOREVER, a);
	pblock->GetValue(pb_strengths, 0, sB, FOREVER, b);
	pblock->GetValue(pb_enabled, 0, eA, FOREVER, a);
	pblock->GetValue(pb_enabled, 0, eB, FOREVER, b);

	INode* none = nullptr;
	pblock->SetValue(pb_splineNodes, 0, none, a);
	pblock->SetValue(pb_splineNodes, 0, nA, b);
	pblock->SetValue(pb_splineNodes, 0, nB, a);
	pblock->SetValue(pb_widths, 0, wB, a);
	pblock->SetValue(pb_widths, 0, wA, b);
	pblock->SetValue(pb_falloffs, 0, fB, a);
	pblock->SetValue(pb_falloffs, 0, fA, b);
	pblock->SetValue(pb_strengths, 0, sB, a);
	pblock->SetValue(pb_strengths, 0, sA, b);
	pblock->SetValue(pb_enabled, 0, eB, a);
	pblock->SetValue(pb_enabled, 0, eA, b);

	selectedIndex = b;
	RefreshListbox();
	LoadSelectedEntryToUI();
	NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
	if (ip)
		ip->RedrawViews(ip->GetTime());
}

// =======================================================================
// Sampling / math
// Closed splines: XY point-in-polygon → full weight inside; interior Z
// from a least-squares XY plane fit through boundary samples. Open
// splines: centerline width/falloff. Closing segment included when closed.
// =======================================================================
void SplineGradeMod::SampleSplineToPolyline(INode* splineNode, TimeValue t, int numSamples, Tab<Point3>& outPts, bool& outClosed)
{
	outPts.ZeroCount();
	outClosed = false;
	if (!splineNode) return;

	ObjectState os = splineNode->EvalWorldState(t);
	if (!os.obj || os.obj->SuperClassID() != SHAPE_CLASS_ID) return;

	ShapeObject* shapeObj = static_cast<ShapeObject*>(os.obj);

	int steps = numSamples;
	if (steps < 1) steps = 1;

	PolyShape polyShape;
	shapeObj->MakePolyShape(t, polyShape, steps, FALSE);

	if (polyShape.numLines < 1) return;

	Matrix3 nodeTM = splineNode->GetNodeTM(t);

	PolyLine& line = polyShape.lines[0];
	int numPts = line.numPts;
	if (numPts < 2) return;

	outClosed = line.IsClosed() ? true : false;

	outPts.SetCount(numPts);
	for (int i = 0; i < numPts; i++)
	{
		Point3 localPt = line.pts[i].p;
		outPts[i] = localPt * nodeTM;
	}
}

float SplineGradeMod::ClosestPointOnPolylineXY(const Point3& p, const Tab<Point3>& polyline, bool closed, float& outTargetZ)
{
	float bestDistSq = 1e30f;
	float bestZ = p.z;
	int n = polyline.Count();

	if (n < 2)
	{
		outTargetZ = p.z;
		return 1e30f;
	}

	int nSeg = closed ? n : (n - 1);
	for (int i = 0; i < nSeg; i++)
	{
		const Point3& A = polyline[i];
		const Point3& B = polyline[(i + 1) % n];

		float dx = B.x - A.x;
		float dy = B.y - A.y;
		float segLenSq = (dx * dx) + (dy * dy);

		float t = 0.0f;
		if (segLenSq > 0.00001f)
		{
			t = (((p.x - A.x) * dx) + ((p.y - A.y) * dy)) / segLenSq;
			t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
		}

		float cx = A.x + (t * dx);
		float cy = A.y + (t * dy);
		float distSq = ((p.x - cx) * (p.x - cx)) + ((p.y - cy) * (p.y - cy));

		if (distSq < bestDistSq)
		{
			bestDistSq = distSq;
			bestZ = A.z + (t * (B.z - A.z));
		}
	}

	outTargetZ = bestZ;
	return sqrtf(bestDistSq);
}

bool SplineGradeMod::PointInPolygonXY(const Point3& p, const Tab<Point3>& polyline)
{
	int n = polyline.Count();
	if (n < 3) return false;

	bool inside = false;
	int j = n - 1;
	for (int i = 0; i < n; i++)
	{
		float yi = polyline[i].y;
		float yj = polyline[j].y;
		if ((yi > p.y) != (yj > p.y))
		{
			float xi = polyline[i].x;
			float xj = polyline[j].x;
			float denom = yj - yi;
			if (denom != 0.0f && (p.x < (xj - xi) * (p.y - yi) / denom + xi))
				inside = !inside;
		}
		j = i;
	}
	return inside;
}

bool SplineGradeMod::FitPlaneXY(const Tab<Point3>& pts, float& outA, float& outB, float& outC)
{
	int n = pts.Count();
	if (n < 3) return false;

	double sxx = 0.0, syy = 0.0, sxy = 0.0, sx = 0.0, sy = 0.0, sz = 0.0, sxz = 0.0, syz = 0.0;
	for (int i = 0; i < n; i++)
	{
		double x = pts[i].x;
		double y = pts[i].y;
		double z = pts[i].z;
		sxx += x * x;
		syy += y * y;
		sxy += x * y;
		sx += x;
		sy += y;
		sz += z;
		sxz += x * z;
		syz += y * z;
	}

	double nn = (double)n;
	double m00 = sxx, m01 = sxy, m02 = sx;
	double m10 = sxy, m11 = syy, m12 = sy;
	double m20 = sx, m21 = sy, m22 = nn;
	double r0 = sxz, r1 = syz, r2 = sz;

	double det = m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);
	if (fabs(det) < 1e-10) return false;

	double invDet = 1.0 / det;
	outA = (float)((r0 * (m11 * m22 - m12 * m21) - m01 * (r1 * m22 - m12 * r2) + m02 * (r1 * m21 - m11 * r2)) * invDet);
	outB = (float)((m00 * (r1 * m22 - m12 * r2) - r0 * (m10 * m22 - m12 * m20) + m02 * (m10 * r2 - r1 * m20)) * invDet);
	outC = (float)((m00 * (m11 * r2 - r1 * m21) - m01 * (m10 * r2 - r1 * m20) + r0 * (m10 * m21 - m11 * m20)) * invDet);
	return true;
}

float SplineGradeMod::FalloffWeight(float lateralDist, float halfWidth, float falloffDist)
{
	if (lateralDist <= halfWidth) return 1.0f;
	if (falloffDist <= 0.0f) return 0.0f;

	float outerEdge = halfWidth + falloffDist;
	if (lateralDist >= outerEdge) return 0.0f;

	float u = (lateralDist - halfWidth) / falloffDist;
	float s = 1.0f - u;
	return s * s * (3.0f - (2.0f * s));
}

void SplineGradeMod::DeformWorldPositions(Tab<Point3>& working, const BitArray& vertActive, TimeValue t)
{
	int numVerts = working.Count();
	int numEntries = pblock->Count(pb_splineNodes);
	if (numEntries == 0) return;

	int samples = 100;
	pblock->GetValue(pb_samples, t, samples, FOREVER);

	for (int e = 0; e < numEntries; e++)
	{
		INode* splineNode = nullptr;
		pblock->GetValue(pb_splineNodes, t, splineNode, FOREVER, e);
		if (!splineNode) continue;

		int enabled = 1;
		pblock->GetValue(pb_enabled, t, enabled, FOREVER, e);
		if (!enabled) continue;

		float width = 120.0f, falloff = 60.0f, strength = 1.0f;
		pblock->GetValue(pb_widths, t, width, FOREVER, e);
		pblock->GetValue(pb_falloffs, t, falloff, FOREVER, e);
		pblock->GetValue(pb_strengths, t, strength, FOREVER, e);

		Tab<Point3> polyline;
		bool closed = false;
		SampleSplineToPolyline(splineNode, t, samples, polyline, closed);
		if (polyline.Count() < 2) continue;

		float halfWidth = width / 2.0f;

		float planeA = 0.0f, planeB = 0.0f, planeC = 0.0f;
		bool hasPadPlane = closed && FitPlaneXY(polyline, planeA, planeB, planeC);

		for (int i = 0; i < numVerts; i++)
		{
			if (i >= vertActive.GetSize() || !vertActive[i]) continue;

			Point3 baseP = working[i];
			float targetZ = baseP.z;
			float lateralDist = ClosestPointOnPolylineXY(baseP, polyline, closed, targetZ);

			float w;
			if (closed && PointInPolygonXY(baseP, polyline))
			{
				w = strength;
				if (hasPadPlane)
					targetZ = (planeA * baseP.x) + (planeB * baseP.y) + planeC;
			}
			else
			{
				w = FalloffWeight(lateralDist, halfWidth, falloff) * strength;
			}
			if (w <= 0.0f) continue;

			float newZ = baseP.z + (w * (targetZ - baseP.z));
			working[i] = Point3(baseP.x, baseP.y, newZ);
		}
	}
}

// =======================================================================
// ModifyObject -- poly, tri, and convertible primitives (Plane, etc.)
// =======================================================================
void SplineGradeMod::ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node)
{
	if (!os->obj) return;
	if (os->obj->SuperClassID() != GEOMOBJECT_CLASS_ID) return;

	PolyObject* polyObj = nullptr;
	TriObject* triObj = nullptr;

	if (os->obj->IsSubClassOf(polyObjectClassID))
	{
		polyObj = static_cast<PolyObject*>(os->obj);
	}
	else if (os->obj->IsSubClassOf(triObjectClassID))
	{
		triObj = static_cast<TriObject*>(os->obj);
	}
	else if (os->obj->CanConvertToType(triObjectClassID))
	{
		triObj = static_cast<TriObject*>(os->obj->ConvertToType(t, triObjectClassID));
		if (!triObj) return;
		if (triObj != os->obj)
		{
			os->obj = triObj;
			os->obj->UnlockObject();
		}
	}
	else if (os->obj->CanConvertToType(polyObjectClassID))
	{
		polyObj = static_cast<PolyObject*>(os->obj->ConvertToType(t, polyObjectClassID));
		if (!polyObj) return;
		if (polyObj != os->obj)
		{
			os->obj = polyObj;
			os->obj->UnlockObject();
		}
	}
	else
	{
		return;
	}

	if (pblock->Count(pb_splineNodes) == 0) return;

	Matrix3 meshTM = node ? node->GetNodeTM(t) : (mc.tm ? *mc.tm : Matrix3());
	Matrix3 invMeshTM = Inverse(meshTM);

	Interval valid = FOREVER;
	if (pblock)
		pblock->GetValidity(t, valid);

	if (polyObj)
	{
		MNMesh& mesh = polyObj->GetMesh();
		int numVerts = mesh.VNum();

		BitArray vertActive;
		vertActive.SetSize(numVerts);
		vertActive.SetAll();

		Tab<Point3> working;
		working.SetCount(numVerts);
		for (int i = 0; i < numVerts; i++)
		{
			if (mesh.v[i].GetFlag(MN_DEAD) || !mesh.V(i))
			{
				vertActive.Clear(i);
				working[i] = Point3(0.0f, 0.0f, 0.0f);
				continue;
			}
			working[i] = mesh.V(i)->p * meshTM;
		}

		DeformWorldPositions(working, vertActive, t);

		for (int i = 0; i < numVerts; i++)
		{
			if (!vertActive[i]) continue;
			mesh.V(i)->p = working[i] * invMeshTM;
		}

		mesh.InvalidateGeomCache();
	}
	else
	{
		Mesh& mesh = triObj->GetMesh();
		int numVerts = mesh.getNumVerts();
		if (numVerts <= 0) return;

		BitArray vertActive;
		vertActive.SetSize(numVerts);
		vertActive.SetAll();

		Tab<Point3> working;
		working.SetCount(numVerts);
		for (int i = 0; i < numVerts; i++)
			working[i] = mesh.verts[i] * meshTM;

		DeformWorldPositions(working, vertActive, t);

		for (int i = 0; i < numVerts; i++)
			mesh.verts[i] = working[i] * invMeshTM;

		mesh.InvalidateGeomCache();
		triObj->PointsWereChanged();
	}

	os->obj->UpdateValidity(GEOM_CHAN_NUM, valid);
}
