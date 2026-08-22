/*
	TiltUpPanel.cpp

	Phase 2: MNMesh cut-grid. Reveal strips are unions of front-face
	cells. The base wall is a box of thickness `depth`. Reveal cells
	keep front quads (groove floor). Field cells share one outer grid
	at grooveDepth and boundary walls only — one welded element.
*/

#include "TiltUpPanel.h"
#include "polyobj.h"
#include "mnmesh.h"
#include "custcont.h"
#include <cmath>
#include <tchar.h>

#define PBLOCK_REF 0

static const float kCutEps = 1.0e-4f;
static const float kDefaultGrooveW = 2.0f;
static const float kDefaultGrooveD = 0.5f;

class TiltUpPanelClassDesc : public ClassDesc2
{
public:
	int IsPublic() override { return TRUE; }
	void* Create(BOOL /*loading*/) override { return new TiltUpPanel(); }
	const TCHAR* ClassName() override { return GetString(IDS_CLASS_NAME); }
	const TCHAR* NonLocalizedClassName() override { return _T("TiltUpPanel"); }
	SClass_ID SuperClassID() override { return GEOMOBJECT_CLASS_ID; }
	Class_ID ClassID() override { return TILTUPPANEL_CLASS_ID; }
	const TCHAR* Category() override { return GetString(IDS_CATEGORY); }
	const TCHAR* InternalName() override { return _T("RyViz_TiltUpPanel"); }
	HINSTANCE HInstance() override { return hInstance; }
};

static TiltUpPanelClassDesc tiltUpPanelDesc;
ClassDesc2* GetTiltUpPanelDesc() { return &tiltUpPanelDesc; }

static ParamBlockDesc2 tiltUpPanel_paramblock(
	tiltuppanel_params, _T("Parameters"), 0, &tiltUpPanelDesc,
	P_AUTO_CONSTRUCT + P_AUTO_UI, PBLOCK_REF,
	IDD_TILTUP_PANEL, IDS_PARAMS, 0, 0, NULL,

	pb_width, _T("width"), TYPE_WORLD, P_ANIMATABLE + P_RESET_DEFAULT, IDS_WIDTH,
		p_default, 0.0f,
		p_ms_default, 96.0f,
		p_range, 0.0f, 1.0e30f,
		p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_WIDTH_EDIT, IDC_WIDTH_SPIN, SPIN_AUTOSCALE,
		p_end,

	pb_height, _T("height"), TYPE_WORLD, P_ANIMATABLE + P_RESET_DEFAULT, IDS_HEIGHT,
		p_default, 0.0f,
		p_ms_default, 144.0f,
		p_range, 0.0f, 1.0e30f,
		p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_HEIGHT_EDIT, IDC_HEIGHT_SPIN, SPIN_AUTOSCALE,
		p_end,

	pb_depth, _T("depth"), TYPE_WORLD, P_ANIMATABLE + P_RESET_DEFAULT, IDS_DEPTH,
		p_default, 0.0f,
		p_ms_default, 8.0f,
		p_range, 0.0f, 1.0e30f,
		p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_DEPTH_EDIT, IDC_DEPTH_SPIN, SPIN_AUTOSCALE,
		p_end,

	pb_revealAxis, _T("revealAxis"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, IDS_REVEAL_AXIS,
		p_end,

	pb_revealPos, _T("revealPos"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE | P_ANIMATABLE, IDS_REVEAL_POS,
		p_end,

	pb_grooveWidth, _T("grooveWidth"), TYPE_WORLD, P_ANIMATABLE, IDS_GROOVE_WIDTH,
		p_default, kDefaultGrooveW,
		p_ms_default, kDefaultGrooveW,
		p_range, 0.0f, 1.0e30f,
		p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_GROOVE_W_EDIT, IDC_GROOVE_W_SPIN, SPIN_AUTOSCALE,
		p_end,

	pb_grooveDepth, _T("grooveDepth"), TYPE_WORLD, P_ANIMATABLE, IDS_GROOVE_DEPTH,
		p_default, kDefaultGrooveD,
		p_ms_default, kDefaultGrooveD,
		p_range, 0.0f, 1.0e30f,
		p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_GROOVE_D_EDIT, IDC_GROOVE_D_SPIN, SPIN_AUTOSCALE,
		p_end,

	pb_edgeSides, _T("edgeSides"), TYPE_BOOL, 0, IDS_EDGE_SIDES,
		p_default, FALSE,
		p_ui, TYPE_SINGLECHEKBOX, IDC_EDGE_SIDES,
		p_end,

	pb_edgeTopBot, _T("edgeTopBot"), TYPE_BOOL, 0, IDS_EDGE_TOPBOT,
		p_default, FALSE,
		p_ui, TYPE_SINGLECHEKBOX, IDC_EDGE_TOPBOT,
		p_end,

	p_end
);

// ---------------------------------------------------------------------
// Cut list
// ---------------------------------------------------------------------

static void FinalizeCuts(Tab<float>& cuts, float lo, float hi)
{
	if (hi < lo)
	{
		float tmp = lo;
		lo = hi;
		hi = tmp;
	}

	Tab<float> raw;
	raw.Append(1, &lo);
	raw.Append(1, &hi);
	for (int i = 0; i < cuts.Count(); ++i)
	{
		float v = cuts[i];
		if (v > lo + kCutEps && v < hi - kCutEps)
			raw.Append(1, &v);
	}

	for (int i = 1; i < raw.Count(); ++i)
	{
		float key = raw[i];
		int j = i - 1;
		while (j >= 0 && raw[j] > key)
		{
			raw[j + 1] = raw[j];
			--j;
		}
		raw[j + 1] = key;
	}

	cuts.ZeroCount();
	for (int i = 0; i < raw.Count(); ++i)
	{
		if (cuts.Count() == 0 || fabsf(raw[i] - cuts[cuts.Count() - 1]) > kCutEps)
		{
			float v = raw[i];
			cuts.Append(1, &v);
		}
	}
}

// ---------------------------------------------------------------------
// MNMesh panel
// ---------------------------------------------------------------------

struct PanelBuildInput
{
	float width;
	float height;
	float depth;
	float grooveW;
	float grooveD;
	BOOL edgeSides;
	BOOL edgeTopBot;
	Tab<int> axis;
	Tab<float> pos;
};

static int GridVert(int nx, int i, int k, int base)
{
	return base + k * nx + i;
}

static void AddMNQuad(MNMesh& mm, int a, int b, int c, int d, DWORD sm)
{
	int nf = mm.AppendNewFaces(1);
	int vv[4] = { a, b, c, d };
	mm.F(nf)->MakePoly(4, vv);
	mm.F(nf)->smGroup = sm;
}

static BOOL CellInReveal(float cx, float cz, const PanelBuildInput& in)
{
	float half = in.grooveW * 0.5f;
	if (half <= kCutEps)
		return FALSE;
	const int n = in.axis.Count();
	for (int r = 0; r < n; ++r)
	{
		if (in.axis[r] == 0)
		{
			if (cz >= in.pos[r] - half && cz <= in.pos[r] + half)
				return TRUE;
		}
		else if (cx >= in.pos[r] - half && cx <= in.pos[r] + half)
			return TRUE;
	}
	return FALSE;
}

static BOOL CellIsField(int i, int k, const Tab<float>& xs, const Tab<float>& zs, const PanelBuildInput& in)
{
	float cx = 0.5f * (xs[i] + xs[i + 1]);
	float cz = 0.5f * (zs[k] + zs[k + 1]);
	return !CellInReveal(cx, cz, in);
}

static void BuildPanelMNMesh(MNMesh& mm, const PanelBuildInput& in)
{
	mm.Clear();
	if (in.width <= kCutEps || in.height <= kCutEps)
		return;

	Tab<float> xs;
	Tab<float> zs;
	const int nRev = in.axis.Count();
	float half = in.grooveW * 0.5f;
	if (half > kCutEps)
	{
		for (int r = 0; r < nRev; ++r)
		{
			float a = in.pos[r] - half;
			float b = in.pos[r] + half;
			if (in.axis[r] == 0)
			{
				zs.Append(1, &a);
				zs.Append(1, &b);
			}
			else
			{
				xs.Append(1, &a);
				xs.Append(1, &b);
			}
		}
	}
	FinalizeCuts(xs, 0.0f, in.width);
	FinalizeCuts(zs, 0.0f, in.height);

	const int nx = xs.Count();
	const int nz = zs.Count();
	if (nx < 2 || nz < 2)
		return;

	const int ncx = nx - 1;
	const int ncz = nz - 1;

	const int frontBase = 0;
	const int backBase = nx * nz;

	for (int k = 0; k < nz; ++k)
	{
		for (int i = 0; i < nx; ++i)
			mm.NewVert(Point3(xs[i], in.depth, zs[k]));
	}
	for (int k = 0; k < nz; ++k)
	{
		for (int i = 0; i < nx; ++i)
			mm.NewVert(Point3(xs[i], 0.0f, zs[k]));
	}

	const DWORD smFront = (1 << 0);
	const DWORD smPanel = (1 << 1);
	const DWORD smWall = (1 << 2);
	const DWORD smBack = (1 << 3);
	const DWORD smBottom = (1 << 4);
	const DWORD smTop = (1 << 5);
	const DWORD smLeft = (1 << 6);
	const DWORD smRight = (1 << 7);

	BOOL doRelief = (nRev > 0 && in.grooveD > kCutEps && half > kCutEps);

	Tab<BOOL> isField;
	isField.SetCount(ncx * ncz);
	for (int k = 0; k < ncz; ++k)
	{
		for (int i = 0; i < ncx; ++i)
			isField[k * ncx + i] = doRelief ? CellIsField(i, k, xs, zs, in) : FALSE;
	}

	for (int k = 0; k < ncz; ++k)
	{
		for (int i = 0; i < ncx; ++i)
		{
			// Groove floor on reveal cells only when relieving; else full front.
			if (!doRelief || !isField[k * ncx + i])
			{
				AddMNQuad(mm,
					GridVert(nx, i, k, frontBase),
					GridVert(nx, i, k + 1, frontBase),
					GridVert(nx, i + 1, k + 1, frontBase),
					GridVert(nx, i + 1, k, frontBase),
					smFront);
			}
			AddMNQuad(mm,
				GridVert(nx, i, k, backBase),
				GridVert(nx, i + 1, k, backBase),
				GridVert(nx, i + 1, k + 1, backBase),
				GridVert(nx, i, k + 1, backBase),
				smBack);
		}
	}

	for (int k = 0; k < ncz; ++k)
	{
		AddMNQuad(mm,
			GridVert(nx, 0, k, backBase),
			GridVert(nx, 0, k + 1, backBase),
			GridVert(nx, 0, k + 1, frontBase),
			GridVert(nx, 0, k, frontBase),
			smLeft);
		AddMNQuad(mm,
			GridVert(nx, nx - 1, k, backBase),
			GridVert(nx, nx - 1, k, frontBase),
			GridVert(nx, nx - 1, k + 1, frontBase),
			GridVert(nx, nx - 1, k + 1, backBase),
			smRight);
	}

	for (int i = 0; i < ncx; ++i)
	{
		AddMNQuad(mm,
			GridVert(nx, i, 0, backBase),
			GridVert(nx, i, 0, frontBase),
			GridVert(nx, i + 1, 0, frontBase),
			GridVert(nx, i + 1, 0, backBase),
			smBottom);
		AddMNQuad(mm,
			GridVert(nx, i, nz - 1, backBase),
			GridVert(nx, i + 1, nz - 1, backBase),
			GridVert(nx, i + 1, nz - 1, frontBase),
			GridVert(nx, i, nz - 1, frontBase),
			smTop);
	}

	if (doRelief)
	{
		const float yOut = in.depth + in.grooveD;
		Tab<int> outerId;
		outerId.SetCount(nx * nz);
		for (int v = 0; v < nx * nz; ++v)
			outerId[v] = -1;

		auto OuterVert = [&](int i, int k) -> int
		{
			int idx = k * nx + i;
			if (outerId[idx] < 0)
				outerId[idx] = mm.NewVert(Point3(xs[i], yOut, zs[k]));
			return outerId[idx];
		};

		auto NeighborIsField = [&](int ni, int nk) -> BOOL
		{
			if (ni < 0 || nk < 0 || ni >= ncx || nk >= ncz)
				return FALSE;
			return isField[nk * ncx + ni];
		};

		for (int k = 0; k < ncz; ++k)
		{
			for (int i = 0; i < ncx; ++i)
			{
				if (!isField[k * ncx + i])
					continue;

				int f00 = GridVert(nx, i, k, frontBase);
				int f01 = GridVert(nx, i, k + 1, frontBase);
				int f11 = GridVert(nx, i + 1, k + 1, frontBase);
				int f10 = GridVert(nx, i + 1, k, frontBase);

				int e00 = OuterVert(i, k);
				int e01 = OuterVert(i, k + 1);
				int e11 = OuterVert(i + 1, k + 1);
				int e10 = OuterVert(i + 1, k);

				AddMNQuad(mm, e00, e01, e11, e10, smPanel);

				// Walls only on field/reveal or field/perimeter edges.
				if (!NeighborIsField(i, k - 1))
					AddMNQuad(mm, f00, e00, e10, f10, smWall);
				if (!NeighborIsField(i, k + 1))
					AddMNQuad(mm, f01, f11, e11, e01, smWall);
				if (!NeighborIsField(i - 1, k))
					AddMNQuad(mm, f00, f01, e01, e00, smWall);
				if (!NeighborIsField(i + 1, k))
					AddMNQuad(mm, f10, e10, e11, f11, smWall);
			}
		}
	}

	mm.InvalidateGeomCache();
	mm.InvalidateTopoCache();
	mm.FillInMesh();
}

static void AppendEdgeReveals(PanelBuildInput& in)
{
	float half = in.grooveW * 0.5f;
	if (half <= kCutEps)
		return;

	if (in.edgeSides)
	{
		int axisV = 1;
		float left = 0.0f;
		float right = in.width;
		in.axis.Append(1, &axisV);
		in.pos.Append(1, &left);
		in.axis.Append(1, &axisV);
		in.pos.Append(1, &right);
	}
	if (in.edgeTopBot)
	{
		int axisH = 0;
		float bottom = 0.0f;
		float top = in.height;
		in.axis.Append(1, &axisH);
		in.pos.Append(1, &bottom);
		in.axis.Append(1, &axisH);
		in.pos.Append(1, &top);
	}
}

static void ReadBuildInput(IParamBlock2* pb, TimeValue t, Interval& valid, PanelBuildInput& in)
{
	in.width = 0.0f;
	in.height = 0.0f;
	in.depth = 0.0f;
	in.grooveW = 0.0f;
	in.grooveD = 0.0f;
	in.edgeSides = FALSE;
	in.edgeTopBot = FALSE;
	in.axis.ZeroCount();
	in.pos.ZeroCount();
	if (!pb)
		return;

	pb->GetValue(pb_width, t, in.width, valid);
	pb->GetValue(pb_height, t, in.height, valid);
	pb->GetValue(pb_depth, t, in.depth, valid);
	pb->GetValue(pb_grooveWidth, t, in.grooveW, valid);
	pb->GetValue(pb_grooveDepth, t, in.grooveD, valid);
	int edgeSides = 0;
	int edgeTopBot = 0;
	pb->GetValue(pb_edgeSides, t, edgeSides, valid);
	pb->GetValue(pb_edgeTopBot, t, edgeTopBot, valid);
	in.edgeSides = edgeSides ? TRUE : FALSE;
	in.edgeTopBot = edgeTopBot ? TRUE : FALSE;

	int n = pb->Count(pb_revealAxis);
	int nPos = pb->Count(pb_revealPos);
	if (nPos > n) n = nPos;

	in.axis.SetCount(n);
	in.pos.SetCount(n);
	for (int i = 0; i < n; ++i)
	{
		int axis = 0;
		float pos = 0.0f;
		if (i < pb->Count(pb_revealAxis))
			pb->GetValue(pb_revealAxis, t, axis, valid, i);
		if (i < pb->Count(pb_revealPos))
			pb->GetValue(pb_revealPos, t, pos, valid, i);
		in.axis[i] = axis;
		in.pos[i] = pos;
	}

	AppendEdgeReveals(in);
}

// ---------------------------------------------------------------------
// Reveal UI
// ---------------------------------------------------------------------

class TiltUpPanelDlgProc : public ParamMap2UserDlgProc
{
public:
	TiltUpPanel* ob;
	TiltUpPanelDlgProc(TiltUpPanel* o) : ob(o) {}
	void DeleteThis() override { delete this; }
	void SetThing(ReferenceTarget* m) override
	{
		TiltUpPanel* next = (TiltUpPanel*)m;
		if (ob && next && ob != next)
		{
			next->hPanel = ob->hPanel;
			next->spinRevealPos = ob->spinRevealPos;
			ob->hPanel = nullptr;
			ob->spinRevealPos = nullptr;
		}
		ob = next;
		if (ob)
		{
			ob->RefreshRevealList();
			ob->LoadSelectedRevealToUI();
		}
	}
	INT_PTR DlgProc(TimeValue t, IParamMap2* /*map*/, HWND hWnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) override;
};

INT_PTR TiltUpPanelDlgProc::DlgProc(TimeValue t, IParamMap2* /*map*/, HWND hWnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/)
{
	if (!ob)
		return FALSE;

	switch (msg)
	{
	case WM_INITDIALOG:
		ob->InitRevealControls(hWnd);
		return FALSE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_ADD_HORIZ:
			if (HIWORD(wParam) == BN_CLICKED)
				ob->AddReveal(0);
			return TRUE;
		case IDC_ADD_VERT:
			if (HIWORD(wParam) == BN_CLICKED)
				ob->AddReveal(1);
			return TRUE;
		case IDC_REMOVE_REVEAL:
			if (HIWORD(wParam) == BN_CLICKED)
				ob->RemoveSelectedReveal();
			return TRUE;
		case IDC_REVEAL_LIST:
			if (HIWORD(wParam) == LBN_SELCHANGE)
			{
				ob->SyncSelectionFromList();
				ob->LoadSelectedRevealToUI();
			}
			return TRUE;
		}
		break;
	case CC_SPINNER_CHANGE:
	case CC_SPINNER_BUTTONUP:
	{
		int id = LOWORD(wParam);
		if (id == IDC_REVEAL_POS_SPIN)
		{
			ob->WriteRevealSpinnersToPblock(t);
			return TRUE;
		}
		break;
	}
	case WM_CUSTEDIT_ENTER:
	{
		int id = LOWORD(wParam);
		if (id == IDC_REVEAL_POS_EDIT)
		{
			ob->WriteRevealSpinnersToPblock(t);
			return TRUE;
		}
		break;
	}
	}
	return FALSE;
}

void TiltUpPanel::SyncRevealTabs()
{
	if (!pblock2)
		return;
	int n = pblock2->Count(pb_revealAxis);
	int nPos = pblock2->Count(pb_revealPos);
	if (nPos > n) n = nPos;
	pblock2->SetCount(pb_revealAxis, n);
	pblock2->SetCount(pb_revealPos, n);
	if (selectedIndex >= n)
		selectedIndex = n - 1;
}

void TiltUpPanel::AddReveal(int axis)
{
	if (!pblock2)
		return;
	SyncRevealTabs();
	Interval valid = FOREVER;
	float width = 0.0f;
	float height = 0.0f;
	pblock2->GetValue(pb_width, 0, width, valid);
	pblock2->GetValue(pb_height, 0, height, valid);

	int n = pblock2->Count(pb_revealAxis);
	float pos = (axis == 0) ? height * 0.5f : width * 0.5f;

	theHold.Begin();
	pblock2->SetCount(pb_revealAxis, n + 1);
	pblock2->SetCount(pb_revealPos, n + 1);
	pblock2->SetValue(pb_revealAxis, 0, axis, n);
	pblock2->SetValue(pb_revealPos, 0, pos, n);
	theHold.Accept(_M("Add Reveal"));

	selectedIndex = n;
	RefreshRevealList();
	LoadSelectedRevealToUI();
}

void TiltUpPanel::RemoveSelectedReveal()
{
	if (!pblock2)
		return;
	SyncRevealTabs();
	int n = pblock2->Count(pb_revealAxis);
	if (n <= 0 || selectedIndex < 0 || selectedIndex >= n)
		return;

	theHold.Begin();
	pblock2->Delete(pb_revealAxis, selectedIndex, 1);
	pblock2->Delete(pb_revealPos, selectedIndex, 1);
	theHold.Accept(_M("Remove Reveal"));

	n = pblock2->Count(pb_revealAxis);
	if (selectedIndex >= n)
		selectedIndex = n - 1;
	RefreshRevealList();
	LoadSelectedRevealToUI();
}

void TiltUpPanel::InitRevealControls(HWND hWnd)
{
	hPanel = hWnd;
	if (!spinRevealPos)
	{
		spinRevealPos = SetupUniverseSpinner(hWnd, IDC_REVEAL_POS_SPIN, IDC_REVEAL_POS_EDIT, 0.0f, 1.0e30f, 0.0f);
		spinRevealPos->SetAutoScale(TRUE);
	}
	SyncRevealTabs();
	if (selectedIndex < 0 && pblock2 && pblock2->Count(pb_revealAxis) > 0)
		selectedIndex = 0;
	RefreshRevealList();
	LoadSelectedRevealToUI();
}

void TiltUpPanel::RefreshRevealList()
{
	if (!hPanel || !pblock2)
		return;
	HWND hList = GetDlgItem(hPanel, IDC_REVEAL_LIST);
	if (!hList)
		return;

	SendMessage(hList, LB_RESETCONTENT, 0, 0);
	int n = pblock2->Count(pb_revealAxis);
	Interval valid = FOREVER;
	for (int i = 0; i < n; ++i)
	{
		int axis = 0;
		float pos = 0.0f;
		pblock2->GetValue(pb_revealAxis, 0, axis, valid, i);
		if (i < pblock2->Count(pb_revealPos))
			pblock2->GetValue(pb_revealPos, 0, pos, valid, i);
		TCHAR buf[64];
		_stprintf_s(buf, _T("%s  %.4g"), axis ? _T("V") : _T("H"), pos);
		SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
	}
	if (selectedIndex >= 0 && selectedIndex < n)
		SendMessage(hList, LB_SETCURSEL, selectedIndex, 0);
}

int TiltUpPanel::SyncSelectionFromList()
{
	if (!hPanel)
		return selectedIndex;
	HWND hList = GetDlgItem(hPanel, IDC_REVEAL_LIST);
	if (!hList)
		return selectedIndex;
	int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
	selectedIndex = (sel == LB_ERR) ? -1 : sel;
	return selectedIndex;
}

void TiltUpPanel::LoadSelectedRevealToUI()
{
	BOOL on = (pblock2 && selectedIndex >= 0 && selectedIndex < pblock2->Count(pb_revealAxis));
	if (spinRevealPos) spinRevealPos->Enable(on);
	if (!on)
		return;

	Interval valid = FOREVER;
	float pos = 0.0f;
	pblock2->GetValue(pb_revealPos, 0, pos, valid, selectedIndex);
	if (spinRevealPos) spinRevealPos->SetValue(pos, FALSE);
}

void TiltUpPanel::WriteRevealSpinnersToPblock(TimeValue t)
{
	if (!pblock2 || selectedIndex < 0)
		return;
	if (selectedIndex >= pblock2->Count(pb_revealAxis))
		return;
	if (spinRevealPos)
		pblock2->SetValue(pb_revealPos, t, spinRevealPos->GetFVal(), selectedIndex);
}

// ---------------------------------------------------------------------
// Object
// ---------------------------------------------------------------------

TiltUpPanel::TiltUpPanel()
	: selectedIndex(-1), hPanel(nullptr), spinRevealPos(nullptr)
{
	GetTiltUpPanelDesc()->MakeAutoParamBlocks(this);
}

TiltUpPanel::~TiltUpPanel()
{
}

void TiltUpPanel::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
	SimpleObject2::BeginEditParams(ip, flags, prev);
	GetTiltUpPanelDesc()->BeginEditParams(ip, this, flags, prev);
	tiltUpPanel_paramblock.SetUserDlgProc(new TiltUpPanelDlgProc(this));
	if (pblock2)
	{
		IParamMap2* map = pblock2->GetMap();
		if (map && map->GetHWnd())
			InitRevealControls(map->GetHWnd());
	}
}

void TiltUpPanel::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
	if (spinRevealPos) { ReleaseISpinner(spinRevealPos); spinRevealPos = nullptr; }
	hPanel = nullptr;

	SimpleObject2::EndEditParams(ip, flags, next);
	GetTiltUpPanelDesc()->EndEditParams(ip, this, flags, next);
}

void TiltUpPanel::BuildMesh(TimeValue t)
{
	ivalid = FOREVER;
	PanelBuildInput in;
	ReadBuildInput(pblock2, t, ivalid, in);
	MNMesh mm;
	BuildPanelMNMesh(mm, in);
	mm.OutToTri(mesh);
}

void TiltUpPanel::InvalidateUI()
{
	if (pblock2)
		tiltUpPanel_paramblock.InvalidateUI(pblock2->LastNotifyParamID());
}

BOOL TiltUpPanel::OKtoDisplay(TimeValue /*t*/)
{
	return TRUE;
}

int TiltUpPanel::CanConvertToType(Class_ID obtype)
{
	if (obtype == polyObjectClassID)
		return 1;
	return SimpleObject2::CanConvertToType(obtype);
}

Object* TiltUpPanel::ConvertToType(TimeValue t, Class_ID obtype)
{
	if (obtype == polyObjectClassID)
	{
		Interval valid = FOREVER;
		PanelBuildInput in;
		ReadBuildInput(pblock2, t, valid, in);
		PolyObject* pobj = CreateEditablePolyObject();
		BuildPanelMNMesh(pobj->GetMesh(), in);
		pobj->SetChannelValidity(GEOM_CHAN_NUM, valid);
		pobj->SetChannelValidity(TOPO_CHAN_NUM, valid);
		pobj->UnlockObject();
		return pobj;
	}
	return SimpleObject2::ConvertToType(t, obtype);
}

RefTargetHandle TiltUpPanel::Clone(RemapDir& remap)
{
	TiltUpPanel* newob = new TiltUpPanel();
	newob->ReplaceReference(0, remap.CloneRef(pblock2));
	newob->ivalid.SetEmpty();
	BaseClone(this, newob, remap);
	return newob;
}

// ---------------------------------------------------------------------
// Create: click-drag footprint (width on X, depth on Y), then click for
// height on Z -- same rhythm as Box. Node origin is the min-corner of
// the drag so the mesh lives in +X +Y +Z from the node.
// ---------------------------------------------------------------------

class TiltUpPanelCreateCallBack : public CreateMouseCallBack
{
	TiltUpPanel* ob;
	Point3 p0;
	IPoint2 sp0, sp1;
public:
	int proc(ViewExp* vpt, int msg, int point, int flags, IPoint2 m, Matrix3& mat) override;
	void SetObj(TiltUpPanel* obj) { ob = obj; }
};

int TiltUpPanelCreateCallBack::proc(ViewExp* vpt, int msg, int point, int flags, IPoint2 m, Matrix3& mat)
{
	if (!vpt || !vpt->IsAlive())
	{
		DbgAssert(!_T("Invalid viewport!"));
		return FALSE;
	}

	if (msg == MOUSE_FREEMOVE)
	{
		vpt->SnapPreview(m, m, nullptr, SNAP_IN_3D);
		return CREATE_CONTINUE;
	}

	if (msg == MOUSE_POINT || msg == MOUSE_MOVE)
	{
		switch (point)
		{
		case 0:
			sp0 = m;
			ob->suspendSnap = TRUE;
			p0 = vpt->SnapPoint(m, m, nullptr, SNAP_IN_3D);
			mat.SetTrans(p0);
			ob->pblock2->SetValue(pb_width, 0, 0.01f);
			ob->pblock2->SetValue(pb_height, 0, 0.01f);
			ob->pblock2->SetValue(pb_depth, 0, 0.01f);
			break;
		case 1:
		{
			sp1 = m;
			Point3 p1 = vpt->SnapPoint(m, m, nullptr, SNAP_IN_3D);
			p1.z = p0.z;
			Point3 d = p1 - p0;
			if (flags & MOUSE_CTRL)
			{
				float ax = fabsf(d.x);
				float ay = fabsf(d.y);
				float s = (ax > ay) ? ax : ay;
				d.x = (d.x >= 0.0f) ? s : -s;
				d.y = (d.y >= 0.0f) ? s : -s;
			}
			float x0 = (d.x >= 0.0f) ? p0.x : p0.x + d.x;
			float y0 = (d.y >= 0.0f) ? p0.y : p0.y + d.y;
			mat.SetTrans(Point3(x0, y0, p0.z));
			ob->pblock2->SetValue(pb_width, 0, fabsf(d.x));
			ob->pblock2->SetValue(pb_depth, 0, fabsf(d.y));
			ob->pblock2->SetValue(pb_height, 0, 0.01f);
			if (msg == MOUSE_POINT && Length(sp1 - sp0) < 3)
				return CREATE_ABORT;
			break;
		}
		case 2:
		{
			float zDisp = vpt->SnapLength(vpt->GetCPDisp(p0, Point3(0, 0, 1), sp1, m, TRUE));
			ob->pblock2->SetValue(pb_height, 0, fabsf(zDisp));
			if (msg == MOUSE_POINT)
			{
				ob->suspendSnap = FALSE;
				return CREATE_STOP;
			}
			break;
		}
		}
		return CREATE_CONTINUE;
	}

	if (msg == MOUSE_ABORT)
		return CREATE_ABORT;

	return CREATE_CONTINUE;
}

static TiltUpPanelCreateCallBack tiltUpPanelCreateCB;

CreateMouseCallBack* TiltUpPanel::GetCreateMouseCallBack()
{
	tiltUpPanelCreateCB.SetObj(this);
	return &tiltUpPanelCreateCB;
}
