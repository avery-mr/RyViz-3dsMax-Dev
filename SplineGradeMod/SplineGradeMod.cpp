/*
	SplineGradeMod.cpp

	Phase 2: multi-spline cascading list. See header for the architecture
	writeup (manual UI, no P_AUTO_UI) and coordinate-space rules carried
	over from Phase 1.
*/

#include "SplineGradeMod.h"
#include "iparamm2.h"
#include "polyobj.h"
#include "mnmesh.h"
#include "shape.h"
#include "cmdmode.h"
#include "custcont.h"
#include "hold.h"

IObjParam* SplineGradeMod::ip = nullptr;
static SplineGradeAddSplinePick thePickMode;

class SplineGradeModClassDesc : public ClassDesc2
{
public:
	int IsPublic() override { return TRUE; }
	void* Create(BOOL loading = FALSE) override { return new SplineGradeMod(); }
	const TCHAR* ClassName() override { return _T("RyViz Spline Grade"); }
	const TCHAR* NonLocalizedClassName() override { return _T("RyViz Spline Grade"); }
	SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
	Class_ID ClassID() override { return SPLINEGRADEMOD_CLASS_ID; }
	const TCHAR* Category() override { return _T("RyViz"); }
	const TCHAR* InternalName() override { return _T("SplineGradeMod"); }
	HINSTANCE HInstance() override { return hInstance; }
};

static SplineGradeModClassDesc splinegrademodDesc;
ClassDesc2* GetSplineGradeModDesc() { return &splinegrademodDesc; }

// ---------------------------------------------------------------------
// ParamBlock2 descriptor -- Tabs, P_AUTO_CONSTRUCT only, NO P_AUTO_UI.
// The 4th arg MUST be &splinegrademodDesc, not nullptr (Phase 1 lesson).
// ---------------------------------------------------------------------
static ParamBlockDesc2 splinegrademod_paramblock(
	splinegrademod_params, _T("Parameters"), 0, &splinegrademodDesc,
	P_AUTO_CONSTRUCT, 0,

	pb_splineNodes, _T("splineNodes"), TYPE_INODE_TAB, 0, P_VARIABLE_SIZE, 0,
		p_end,

	pb_widths, _T("widths"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, 0,
		p_end,

	pb_falloffs, _T("falloffs"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, 0,
		p_end,

	pb_strengths, _T("strengths"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, 0,
		p_end,

	pb_enabled, _T("enabled"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, 0,
		p_end,

	pb_samples, _T("samples"), TYPE_INT, 0, 0,
		p_default, 100,
		p_range, 4, 2000,
		p_end,

	p_end
);

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------
SplineGradeMod::SplineGradeMod() : pblock(nullptr), selectedIndex(-1), hPanel(nullptr)
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
	return FOREVER;	// see Phase 1 note -- tighten later once stable
}

RefResult SplineGradeMod::NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID, RefMessage message, BOOL propagate)
{
	return REF_SUCCEED;
}

// =======================================================================
// Manual dialog proc
// =======================================================================
static void RefreshEntrySpinnersFromPblock(SplineGradeMod* mod, HWND hWnd);

static INT_PTR CALLBACK SplineGradeDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	SplineGradeMod* mod = (SplineGradeMod*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (msg)
	{
	case WM_INITDIALOG:
	{
		mod = (SplineGradeMod*)lParam;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)mod);
		mod->hPanel = hWnd;

		// Global samples spinner
		ISpinnerControl* spnSamples = GetISpinner(GetDlgItem(hWnd, IDC_SAMPLES_SPIN));
		spnSamples->LinkToEdit(GetDlgItem(hWnd, IDC_SAMPLES_EDIT), EDITTYPE_INT);
		spnSamples->SetLimits(4, 2000, TRUE);
		int samplesVal = 100;
		mod->pblock->GetValue(pb_samples, 0, samplesVal, FOREVER);
		spnSamples->SetValue(samplesVal, FALSE);
		ReleaseISpinner(spnSamples);

		// Per-entry spinners -- limits only here; values loaded on selection
		ISpinnerControl* spnW = GetISpinner(GetDlgItem(hWnd, IDC_ENTRY_WIDTH_SPIN));
		spnW->LinkToEdit(GetDlgItem(hWnd, IDC_ENTRY_WIDTH_EDIT), EDITTYPE_FLOAT);
		spnW->SetLimits(0.0f, 100000.0f, TRUE);
		ReleaseISpinner(spnW);

		ISpinnerControl* spnF = GetISpinner(GetDlgItem(hWnd, IDC_ENTRY_FALLOFF_SPIN));
		spnF->LinkToEdit(GetDlgItem(hWnd, IDC_ENTRY_FALLOFF_EDIT), EDITTYPE_FLOAT);
		spnF->SetLimits(0.0f, 100000.0f, TRUE);
		ReleaseISpinner(spnF);

		ISpinnerControl* spnS = GetISpinner(GetDlgItem(hWnd, IDC_ENTRY_STRENGTH_SPIN));
		spnS->LinkToEdit(GetDlgItem(hWnd, IDC_ENTRY_STRENGTH_EDIT), EDITTYPE_FLOAT);
		spnS->SetLimits(0.0f, 1.0f, TRUE);
		ReleaseISpinner(spnS);

		ICustButton* iAdd = GetICustButton(GetDlgItem(hWnd, IDC_ADD_SPLINE));
		if (iAdd)
		{
			iAdd->SetType(CBT_CHECK);
			iAdd->SetCheckHighlight(TRUE);
			ReleaseICustButton(iAdd);
		}
		ICustButton* iRemove = GetICustButton(GetDlgItem(hWnd, IDC_REMOVE_SPLINE));
		if (iRemove) ReleaseICustButton(iRemove);
		ICustButton* iUp = GetICustButton(GetDlgItem(hWnd, IDC_MOVE_UP));
		if (iUp) ReleaseICustButton(iUp);
		ICustButton* iDown = GetICustButton(GetDlgItem(hWnd, IDC_MOVE_DOWN));
		if (iDown) ReleaseICustButton(iDown);

		mod->RefreshListbox();
		return TRUE;
	}

	case WM_DESTROY:
		mod->hPanel = nullptr;
		return FALSE;

	case CC_SPINNER_CHANGE:
	{
		if (!mod) return FALSE;
		int ctrlID = LOWORD(wParam);

		if (ctrlID == IDC_SAMPLES_SPIN)
		{
			ISpinnerControl* spn = (ISpinnerControl*)lParam;
			mod->pblock->SetValue(pb_samples, 0, spn->GetIVal());
			return TRUE;
		}

		if (mod->selectedIndex < 0) return TRUE;	// no entry selected, ignore

		if (ctrlID == IDC_ENTRY_WIDTH_SPIN)
		{
			ISpinnerControl* spn = (ISpinnerControl*)lParam;
			mod->pblock->SetValue(pb_widths, 0, spn->GetFVal(), mod->selectedIndex);
			mod->RefreshListbox();
		}
		else if (ctrlID == IDC_ENTRY_FALLOFF_SPIN)
		{
			ISpinnerControl* spn = (ISpinnerControl*)lParam;
			mod->pblock->SetValue(pb_falloffs, 0, spn->GetFVal(), mod->selectedIndex);
		}
		else if (ctrlID == IDC_ENTRY_STRENGTH_SPIN)
		{
			ISpinnerControl* spn = (ISpinnerControl*)lParam;
			mod->pblock->SetValue(pb_strengths, 0, spn->GetFVal(), mod->selectedIndex);
		}
		return TRUE;
	}

	case WM_COMMAND:
	{
		if (!mod) return FALSE;
		int ctrlID = LOWORD(wParam);
		int notifyCode = HIWORD(wParam);

		if (ctrlID == IDC_ADD_SPLINE && notifyCode == BN_CLICKED)
		{
			if (!SplineGradeMod::ip) return FALSE;
			if (SplineGradeMod::ip->GetCommandMode() &&
				SplineGradeMod::ip->GetCommandMode()->ID() == CID_STDPICK)
			{
				SplineGradeMod::ip->ClearPickMode();
			}
			else
			{
				thePickMode.mod = mod;
				thePickMode.hitNode = nullptr;
				SplineGradeMod::ip->SetPickMode(&thePickMode);
			}
			return TRUE;
		}
		else if (ctrlID == IDC_REMOVE_SPLINE && notifyCode == BN_CLICKED)
		{
			mod->RemoveSelectedEntry();
			return TRUE;
		}
		else if (ctrlID == IDC_MOVE_UP && notifyCode == BN_CLICKED)
		{
			mod->MoveSelectedEntry(-1);
			return TRUE;
		}
		else if (ctrlID == IDC_MOVE_DOWN && notifyCode == BN_CLICKED)
		{
			mod->MoveSelectedEntry(1);
			return TRUE;
		}
		else if (ctrlID == IDC_SPLINE_LIST && notifyCode == LBN_SELCHANGE)
		{
			int sel = (int)SendDlgItemMessage(hWnd, IDC_SPLINE_LIST, LB_GETCURSEL, 0, 0);
			mod->selectedIndex = (sel == LB_ERR) ? -1 : sel;
			mod->LoadSelectedEntryToUI();
			return TRUE;
		}
		else if (ctrlID == IDC_ENTRY_ENABLED && notifyCode == BN_CLICKED)
		{
			if (mod->selectedIndex >= 0)
			{
				BOOL checked = IsDlgButtonChecked(hWnd, IDC_ENTRY_ENABLED);
				mod->pblock->SetValue(pb_enabled, 0, checked ? 1 : 0, mod->selectedIndex);
				mod->RefreshListbox();
			}
			return TRUE;
		}
		return FALSE;
	}
	}
	return FALSE;
}

void SplineGradeMod::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
	this->ip = ip;
	ip->AddRollupPage(hInstance, MAKEINTRESOURCE(IDD_SPLINEGRADE_PANEL), SplineGradeDlgProc, _T("RyViz Spline Grade"), (LPARAM)this);
}

void SplineGradeMod::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
	ip->ClearPickMode();
	thePickMode.mod = nullptr;
	thePickMode.hitNode = nullptr;
	ip->DeleteRollupPage(hPanel);
	hPanel = nullptr;
	this->ip = nullptr;
}

// =======================================================================
// UI helper methods
// =======================================================================
void SplineGradeMod::RefreshListbox()
{
	if (!hPanel) return;
	HWND hList = GetDlgItem(hPanel, IDC_SPLINE_LIST);
	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	int count = pblock->Count(pb_splineNodes);
	for (int i = 0; i < count; i++)
	{
		INode* n = nullptr;
		pblock->GetValue(pb_splineNodes, 0, n, FOREVER, i);
		float w = 0.0f;
		pblock->GetValue(pb_widths, 0, w, FOREVER, i);
		int en = 1;
		pblock->GetValue(pb_enabled, 0, en, FOREVER, i);

		TCHAR line[256];
		const TCHAR* name = n ? n->GetName() : _M("<none>");
		wsprintf(line, _M("%s  [w:%.0f]%s"), name, w, en ? _M("") : _M("  (disabled)"));
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)line);
	}

	if (selectedIndex >= 0 && selectedIndex < count)
		SendMessage(hList, LB_SETCURSEL, selectedIndex, 0);
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

	ISpinnerControl* spnW = GetISpinner(GetDlgItem(hPanel, IDC_ENTRY_WIDTH_SPIN));
	spnW->SetValue(w, FALSE);
	ReleaseISpinner(spnW);

	ISpinnerControl* spnF = GetISpinner(GetDlgItem(hPanel, IDC_ENTRY_FALLOFF_SPIN));
	spnF->SetValue(f, FALSE);
	ReleaseISpinner(spnF);

	ISpinnerControl* spnS = GetISpinner(GetDlgItem(hPanel, IDC_ENTRY_STRENGTH_SPIN));
	spnS->SetValue(s, FALSE);
	ReleaseISpinner(spnS);

	CheckDlgButton(hPanel, IDC_ENTRY_ENABLED, en ? BST_CHECKED : BST_UNCHECKED);
}

void SplineGradeMod::AddSplineEntry(INode* splineNode)
{
	if (!splineNode) return;

	int newCount = pblock->Count(pb_splineNodes) + 1;
	pblock->SetCount(pb_splineNodes, newCount);
	pblock->SetCount(pb_widths, newCount);
	pblock->SetCount(pb_falloffs, newCount);
	pblock->SetCount(pb_strengths, newCount);
	pblock->SetCount(pb_enabled, newCount);

	int idx = newCount - 1;
	pblock->SetValue(pb_splineNodes, 0, splineNode, idx);
	pblock->SetValue(pb_widths, 0, 120.0f, idx);
	pblock->SetValue(pb_falloffs, 0, 60.0f, idx);
	pblock->SetValue(pb_strengths, 0, 1.0f, idx);
	pblock->SetValue(pb_enabled, 0, 1, idx);

	selectedIndex = idx;
	RefreshListbox();
	LoadSelectedEntryToUI();
	NotifyDependents(FOREVER, PART_GEOM, REFMSG_CHANGE);
}

void SplineGradeMod::RemoveSelectedEntry()
{
	if (selectedIndex < 0) return;
	int count = pblock->Count(pb_splineNodes);
	if (selectedIndex >= count) return;

	pblock->Delete(pb_splineNodes, selectedIndex, 1);
	pblock->Delete(pb_widths, selectedIndex, 1);
	pblock->Delete(pb_falloffs, selectedIndex, 1);
	pblock->Delete(pb_strengths, selectedIndex, 1);
	pblock->Delete(pb_enabled, selectedIndex, 1);

	count--;
	if (selectedIndex >= count) selectedIndex = count - 1;
	RefreshListbox();
	if (selectedIndex >= 0) LoadSelectedEntryToUI();
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

	pblock->SetValue(pb_splineNodes, 0, nB, a);
	pblock->SetValue(pb_splineNodes, 0, nA, b);
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
}

// =======================================================================
// Pick callback -- matches SDK samples (Linked XForm / Skin Wrap)
// =======================================================================
BOOL SplineGradeAddSplinePick::Filter(INode* node)
{
	if (!node || !mod) return FALSE;

	node->BeginDependencyTest();
	mod->NotifyDependents(FOREVER, 0, REFMSG_TEST_DEPENDENCY);
	if (node->EndDependencyTest()) return FALSE;

	Object* obj = node->GetObjectRef();
	if (!obj) return FALSE;
	obj = obj->FindBaseObject();
	return (obj && obj->SuperClassID() == SHAPE_CLASS_ID);
}

BOOL SplineGradeAddSplinePick::HitTest(IObjParam* ip, HWND hWnd, ViewExp* /*vpt*/, IPoint2 m, int /*flags*/)
{
	hitNode = ip->PickNode(hWnd, m, this);
	return (hitNode != nullptr);
}

BOOL SplineGradeAddSplinePick::Pick(IObjParam* ip, ViewExp* vpt)
{
	if (!vpt || !vpt->IsAlive())
		return FALSE;

	INode* node = vpt->GetClosestHit();
	if (!node)
		node = hitNode;
	if (!node || !mod)
		return FALSE;

	theHold.Begin();
	mod->AddSplineEntry(node);
	theHold.Accept(_M("Add Spline"));
	hitNode = nullptr;
	if (ip)
		ip->RedrawViews(ip->GetTime());
	return TRUE;
}

void SplineGradeAddSplinePick::EnterMode(IObjParam* /*ip*/)
{
	if (!mod || !mod->hPanel) return;
	ICustButton* iBut = GetICustButton(GetDlgItem(mod->hPanel, IDC_ADD_SPLINE));
	if (iBut)
	{
		iBut->SetCheck(TRUE);
		ReleaseICustButton(iBut);
	}
}

void SplineGradeAddSplinePick::ExitMode(IObjParam* /*ip*/)
{
	if (!mod || !mod->hPanel) return;
	ICustButton* iBut = GetICustButton(GetDlgItem(mod->hPanel, IDC_ADD_SPLINE));
	if (iBut)
	{
		iBut->SetCheck(FALSE);
		ReleaseICustButton(iBut);
	}
}

// =======================================================================
// Sampling / math -- unchanged from Phase 1, direct carry-over
// =======================================================================
void SplineGradeMod::SampleSplineToPolyline(INode* splineNode, TimeValue t, int numSamples, Tab<Point3>& outPts)
{
	outPts.ZeroCount();
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

	outPts.SetCount(numPts);
	for (int i = 0; i < numPts; i++)
	{
		Point3 localPt = line.pts[i].p;
		outPts[i] = localPt * nodeTM;
	}
}

float SplineGradeMod::ClosestPointOnPolylineXY(const Point3& p, const Tab<Point3>& polyline, float& outTargetZ)
{
	float bestDistSq = 1e30f;
	float bestZ = p.z;
	int n = polyline.Count();

	if (n < 2)
	{
		outTargetZ = p.z;
		return 1e30f;
	}

	for (int i = 0; i < n - 1; i++)
	{
		const Point3& A = polyline[i];
		const Point3& B = polyline[i + 1];

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

// =======================================================================
// ModifyObject -- cascading multi-spline pass, ported from the
// prototype's applyAllSplines(). Each enabled entry deforms the RESULT
// of the previous entry, working on a temporary copy started from the
// mesh's current (pre-modifier) vertex positions.
// =======================================================================
void SplineGradeMod::ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node)
{
	if (!os->obj) return;
	if (os->obj->SuperClassID() != GEOMOBJECT_CLASS_ID) return;
	if (!os->obj->IsSubClassOf(polyObjectClassID)) return;

	PolyObject* polyObj = static_cast<PolyObject*>(os->obj);
	MNMesh& mesh = polyObj->GetMesh();

	int numEntries = pblock->Count(pb_splineNodes);
	if (numEntries == 0) return;

	int samples = 100;
	pblock->GetValue(pb_samples, t, samples, FOREVER);

	Matrix3 meshTM = node ? node->GetNodeTM(t) : (mc.tm ? *mc.tm : Matrix3());
	Matrix3 invMeshTM = Inverse(meshTM);

	int numVerts = mesh.VNum();

	Tab<Point3> working;
	working.SetCount(numVerts);
	for (int i = 0; i < numVerts; i++)
	{
		if (mesh.v[i].GetFlag(MN_DEAD) || !mesh.V(i))
		{
			working[i] = Point3(0.0f, 0.0f, 0.0f);
			continue;
		}
		working[i] = mesh.V(i)->p * meshTM;
	}

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
		SampleSplineToPolyline(splineNode, t, samples, polyline);
		if (polyline.Count() < 2) continue;

		float halfWidth = width / 2.0f;

		for (int i = 0; i < numVerts; i++)
		{
			if (mesh.v[i].GetFlag(MN_DEAD) || !mesh.V(i)) continue;

			Point3 baseP = working[i];
			float targetZ = baseP.z;
			float lateralDist = ClosestPointOnPolylineXY(baseP, polyline, targetZ);

			float w = FalloffWeight(lateralDist, halfWidth, falloff) * strength;
			if (w <= 0.0f) continue;

			float newZ = baseP.z + (w * (targetZ - baseP.z));
			working[i] = Point3(baseP.x, baseP.y, newZ);
		}
	}

	for (int i = 0; i < numVerts; i++)
	{
		if (mesh.v[i].GetFlag(MN_DEAD) || !mesh.V(i)) continue;
		mesh.V(i)->p = working[i] * invMeshTM;
	}

	mesh.InvalidateGeomCache();
	os->obj->UpdateValidity(GEOM_CHAN_NUM, FOREVER);
}
