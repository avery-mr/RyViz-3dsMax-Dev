/*
	TiltUpPanel.cpp

	Phase 2+: MNMesh cut-grid. Reveal strips are unions of front-face
	cells. Rectangular openings punch through the same grid (opening
	wins over reveal). The base wall is a box of thickness `depth`.
	Reveal cells keep front quads (groove floor). Field cells share one
	outer grid at grooveDepth and boundary walls only — one welded element.
*/

#include "TiltUpPanel.h"
#include "./resource.h"
#include "polyobj.h"
#include "mnmesh.h"
#include "custcont.h"
#include "maxscript/maxscript.h"
#include <cmath>
#include <io.h>
#include <tchar.h>

#define PBLOCK_REF 0

IObjParam* TiltUpPanel::editIp = nullptr;

static const float kCutEps = 1.0e-4f;
static const float kDefaultGrooveW = 2.0f;
static const float kDefaultGrooveD = 0.5f;

static void NotifyUser(const TCHAR* msg)
{
	Interface* ip = GetCOREInterface();
	HWND hwnd = ip ? ip->GetMAXHWnd() : nullptr;
	MessageBox(hwnd, msg, _T("RyViz TiltUpPanel"), MB_OK | MB_ICONINFORMATION);
}

static bool FileExists(const MSTR& path)
{
	return path.Length() > 0 && _taccess(path, 0) == 0;
}

static bool ResolveEditorScriptPath(MSTR& outPath)
{
	TCHAR envVar[MAX_PATH];
	DWORD envLen = GetEnvironmentVariable(_T("RYVIZ_TILTUP_EDITOR"), envVar, MAX_PATH);
	if (envLen > 0 && envLen < MAX_PATH)
	{
		outPath = envVar;
		if (FileExists(outPath))
			return true;
	}

	// Dev default: source tree next to this repo (plugin .dlo lives in Max Plugins).
	static const TCHAR* kFixedCandidates[] = {
		_T("C:\\Users\\mavery\\source\\MaxDev\\RyViz-MaxDev\\TiltUpPanel\\python\\tilt_up_panel_editor.py"),
	};
	for (const TCHAR* fixed : kFixedCandidates)
	{
		outPath = fixed;
		if (FileExists(outPath))
			return true;
	}

	TCHAR userProfile[MAX_PATH];
	DWORD profileLen = GetEnvironmentVariable(_T("USERPROFILE"), userProfile, MAX_PATH);
	if (profileLen > 0 && profileLen < MAX_PATH)
	{
		outPath = MSTR(userProfile) +
			_T("\\Documents\\3ds Max 2027\\scripts\\RyViz\\tilt_up_panel_editor.py");
		if (FileExists(outPath))
			return true;
	}

	TCHAR modulePath[MAX_PATH];
	if (!GetModuleFileName(hInstance, modulePath, MAX_PATH))
		return false;

	MSTR dir(modulePath);
	int lastSlash = dir.last(_T('\\'));
	if (lastSlash >= 0)
		dir = dir.Substr(0, lastSlash + 1);

	static const TCHAR* kRelCandidates[] = {
		_T("TiltUpPanel\\python\\tilt_up_panel_editor.py"),
		_T("RyViz\\tilt_up_panel_editor.py"),
		_T("tilt_up_panel_editor.py"),
	};

	for (const TCHAR* suffix : kRelCandidates)
	{
		MSTR candidate = dir + suffix;
		if (FileExists(candidate))
		{
			outPath = candidate;
			return true;
		}
	}
	return false;
}

static INode* FindEditingNode(TiltUpPanel* obj, IObjParam* ip)
{
	if (!obj || !ip)
		return nullptr;

	for (int i = 0; i < ip->GetSelNodeCount(); ++i)
	{
		INode* n = ip->GetSelNode(i);
		if (!n)
			continue;
		Object* ref = n->GetObjectRef();
		if (!ref)
			continue;
		Object* base = ref->FindBaseObject();
		if (base == obj || ref == obj)
			return n;
	}
	return nullptr;
}

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

	pb_openingX, _T("openingX"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_OPENING_X,
		p_end,
	pb_openingZ, _T("openingZ"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_OPENING_Z,
		p_end,
	pb_openingW, _T("openingW"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_OPENING_W,
		p_end,
	pb_openingH, _T("openingH"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_OPENING_H,
		p_end,

	pb_panelColors, _T("panelColors"), TYPE_POINT3_TAB, 0, P_VARIABLE_SIZE, IDS_PANEL_COLORS,
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
	Tab<float> openX;
	Tab<float> openZ;
	Tab<float> openW;
	Tab<float> openH;
	Tab<Point3> panelColors;
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

static BOOL PointInOpening(float cx, float cz, const PanelBuildInput& in)
{
	const int n = in.openX.Count();
	for (int o = 0; o < n; ++o)
	{
		float x0 = in.openX[o];
		float z0 = in.openZ[o];
		float x1 = x0 + in.openW[o];
		float z1 = z0 + in.openH[o];
		if (in.openW[o] <= kCutEps || in.openH[o] <= kCutEps)
			continue;
		if (cx >= x0 - kCutEps && cx <= x1 + kCutEps &&
			cz >= z0 - kCutEps && cz <= z1 + kCutEps)
			return TRUE;
	}
	return FALSE;
}

static BOOL CellInReveal(float cx, float cz, const PanelBuildInput& in);
static void ApplyMNMeshVertexColors(MNMesh& mm, const PanelBuildInput& in);

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

static void CellCenter(int i, int k, const Tab<float>& xs, const Tab<float>& zs, float& cx, float& cz)
{
	cx = 0.5f * (xs[i] + xs[i + 1]);
	cz = 0.5f * (zs[k] + zs[k + 1]);
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

	const int nOpen = in.openX.Count();
	for (int o = 0; o < nOpen; ++o)
	{
		if (in.openW[o] <= kCutEps || in.openH[o] <= kCutEps)
			continue;
		float x0 = in.openX[o];
		float x1 = x0 + in.openW[o];
		float z0 = in.openZ[o];
		float z1 = z0 + in.openH[o];
		xs.Append(1, &x0);
		xs.Append(1, &x1);
		zs.Append(1, &z0);
		zs.Append(1, &z1);
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
	const DWORD smJamb = (1 << 8);

	BOOL doRelief = (nRev > 0 && in.grooveD > kCutEps && half > kCutEps);

	Tab<BOOL> isOpening;
	Tab<BOOL> isField;
	isOpening.SetCount(ncx * ncz);
	isField.SetCount(ncx * ncz);
	for (int k = 0; k < ncz; ++k)
	{
		for (int i = 0; i < ncx; ++i)
		{
			float cx, cz;
			CellCenter(i, k, xs, zs, cx, cz);
			BOOL open = PointInOpening(cx, cz, in);
			isOpening[k * ncx + i] = open;
			// Opening wins over reveal; fields only on non-opening non-reveal cells.
			isField[k * ncx + i] = (!open && doRelief && !CellInReveal(cx, cz, in)) ? TRUE : FALSE;
		}
	}

	auto IsOpen = [&](int i, int k) -> BOOL
	{
		if (i < 0 || k < 0 || i >= ncx || k >= ncz)
			return FALSE;
		return isOpening[k * ncx + i];
	};
	auto IsField = [&](int i, int k) -> BOOL
	{
		if (i < 0 || k < 0 || i >= ncx || k >= ncz)
			return FALSE;
		return isField[k * ncx + i];
	};

	for (int k = 0; k < ncz; ++k)
	{
		for (int i = 0; i < ncx; ++i)
		{
			if (IsOpen(i, k))
				continue;

			// Groove floor on reveal cells only when relieving; else full front.
			if (!doRelief || !IsField(i, k))
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

	// Perimeter walls — skip where an opening meets the panel edge (doors).
	for (int k = 0; k < ncz; ++k)
	{
		if (!IsOpen(0, k))
		{
			AddMNQuad(mm,
				GridVert(nx, 0, k, backBase),
				GridVert(nx, 0, k + 1, backBase),
				GridVert(nx, 0, k + 1, frontBase),
				GridVert(nx, 0, k, frontBase),
				smLeft);
		}
		if (!IsOpen(ncx - 1, k))
		{
			AddMNQuad(mm,
				GridVert(nx, nx - 1, k, backBase),
				GridVert(nx, nx - 1, k, frontBase),
				GridVert(nx, nx - 1, k + 1, frontBase),
				GridVert(nx, nx - 1, k + 1, backBase),
				smRight);
		}
	}

	for (int i = 0; i < ncx; ++i)
	{
		if (!IsOpen(i, 0))
		{
			AddMNQuad(mm,
				GridVert(nx, i, 0, backBase),
				GridVert(nx, i, 0, frontBase),
				GridVert(nx, i + 1, 0, frontBase),
				GridVert(nx, i + 1, 0, backBase),
				smBottom);
		}
		if (!IsOpen(i, ncz - 1))
		{
			AddMNQuad(mm,
				GridVert(nx, i, nz - 1, backBase),
				GridVert(nx, i + 1, nz - 1, backBase),
				GridVert(nx, i + 1, nz - 1, frontBase),
				GridVert(nx, i, nz - 1, frontBase),
				smTop);
		}
	}

	// Through-wall jambs on opening / solid shared edges.
	// No jamb on panel-perimeter sides (doors/windows open to the outside).
	auto SolidNeighbor = [&](int ni, int nk) -> BOOL
	{
		if (ni < 0 || nk < 0 || ni >= ncx || nk >= ncz)
			return FALSE;
		return !isOpening[nk * ncx + ni];
	};

	for (int k = 0; k < ncz; ++k)
	{
		for (int i = 0; i < ncx; ++i)
		{
			if (!IsOpen(i, k))
				continue;

			int f00 = GridVert(nx, i, k, frontBase);
			int f01 = GridVert(nx, i, k + 1, frontBase);
			int f11 = GridVert(nx, i + 1, k + 1, frontBase);
			int f10 = GridVert(nx, i + 1, k, frontBase);
			int b00 = GridVert(nx, i, k, backBase);
			int b01 = GridVert(nx, i, k + 1, backBase);
			int b11 = GridVert(nx, i + 1, k + 1, backBase);
			int b10 = GridVert(nx, i + 1, k, backBase);

			if (SolidNeighbor(i, k - 1))
				AddMNQuad(mm, b00, b10, f10, f00, smJamb);
			if (SolidNeighbor(i, k + 1))
				AddMNQuad(mm, b01, f01, f11, b11, smJamb);
			if (SolidNeighbor(i - 1, k))
				AddMNQuad(mm, b00, f00, f01, b01, smJamb);
			if (SolidNeighbor(i + 1, k))
				AddMNQuad(mm, b10, b11, f11, f10, smJamb);
		}
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

		for (int k = 0; k < ncz; ++k)
		{
			for (int i = 0; i < ncx; ++i)
			{
				if (!IsField(i, k))
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

				if (!IsField(i, k - 1))
					AddMNQuad(mm, f00, e00, e10, f10, smWall);
				if (!IsField(i, k + 1))
					AddMNQuad(mm, f01, f11, e11, e01, smWall);
				if (!IsField(i - 1, k))
					AddMNQuad(mm, f00, f01, e01, e00, smWall);
				if (!IsField(i + 1, k))
					AddMNQuad(mm, f10, e10, e11, f11, smWall);
			}
		}
	}

	mm.InvalidateGeomCache();
	mm.InvalidateTopoCache();
	mm.FillInMesh();
	ApplyMNMeshVertexColors(mm, in);
}

static void BuildRegionDividers(const PanelBuildInput& in, Tab<float>& vx, Tab<float>& hz)
{
	vx.ZeroCount();
	hz.ZeroCount();
	float zero = 0.0f;
	vx.Append(1, &zero);
	hz.Append(1, &zero);
	const int n = in.axis.Count();
	for (int r = 0; r < n; ++r)
	{
		float p = in.pos[r];
		if (in.axis[r] == 0)
			hz.Append(1, &p);
		else
			vx.Append(1, &p);
	}
	float w = in.width;
	float h = in.height;
	vx.Append(1, &w);
	hz.Append(1, &h);
	FinalizeCuts(vx, 0.0f, in.width);
	FinalizeCuts(hz, 0.0f, in.height);
}

static int RegionSpanIndex(const Tab<float>& cuts, float v)
{
	const int n = cuts.Count() - 1;
	if (n <= 0)
		return 0;
	for (int i = 0; i < n; ++i)
	{
		if (v < cuts[i + 1] - kCutEps || i == n - 1)
			return i;
	}
	return n - 1;
}

static int RegionIdAt(float cx, float cz, const Tab<float>& vx, const Tab<float>& hz)
{
	int ix = RegionSpanIndex(vx, cx);
	int iz = RegionSpanIndex(hz, cz);
	int nx = vx.Count() - 1;
	if (nx < 1)
		nx = 1;
	return iz * nx + ix;
}

static Point3 ColorForRegion(const PanelBuildInput& in, int rid)
{
	Point3 gray(0.55f, 0.55f, 0.55f);
	if (rid < 0 || rid >= in.panelColors.Count())
		return gray;
	return in.panelColors[rid];
}

static void ApplyMNMeshVertexColors(MNMesh& mm, const PanelBuildInput& in)
{
	if (mm.numv <= 0 || mm.numf <= 0)
		return;

	Tab<float> vx, hz;
	BuildRegionDividers(in, vx, hz);

	mm.SetMapNum(1);
	mm.InitMap(0);
	MNMap* map = mm.M(0);
	if (!map)
		return;

	map->setNumVerts(mm.numv);
	Point3 gray(0.55f, 0.55f, 0.55f);
	for (int i = 0; i < mm.numv; ++i)
		map->v[i] = gray;

	const float yPaint = (in.grooveD > kCutEps) ? (in.depth + in.grooveD) : in.depth;
	for (int i = 0; i < mm.numv; ++i)
	{
		Point3 p = mm.v[i].p;
		if (fabsf(p.y - yPaint) > 0.01f * (1.0f + fabsf(yPaint)))
			continue;
		if (PointInOpening(p.x, p.z, in))
			continue;
		int rid = RegionIdAt(p.x, p.z, vx, hz);
		map->v[i] = ColorForRegion(in, rid);
	}

	for (int f = 0; f < mm.numf && f < map->numf; ++f)
	{
		MNFace* face = mm.F(f);
		MNMapFace* mf = map->F(f);
		if (!face || !mf)
			continue;
		if (mf->deg != face->deg)
			mf->SetSize(face->deg);
		for (int j = 0; j < face->deg; ++j)
			mf->tv[j] = face->vtx[j];
	}
}

static void EnableMeshVertexColorDisplay(Mesh& mesh)
{
	if (mesh.getNumVerts() <= 0)
		return;
	// Max 2027: map channel 0 holds vertex colors; setCVertMode/setVCDisplay are gone.
	if (!mesh.mapSupport(0))
		mesh.setMapSupport(0, TRUE);
	mesh.setVCDisplayData(0);
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
	in.openX.ZeroCount();
	in.openZ.ZeroCount();
	in.openW.ZeroCount();
	in.openH.ZeroCount();
	in.panelColors.ZeroCount();
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

	int no = pb->Count(pb_openingX);
	int nz = pb->Count(pb_openingZ);
	int nw = pb->Count(pb_openingW);
	int nh = pb->Count(pb_openingH);
	if (nz < no) no = nz;
	if (nw < no) no = nw;
	if (nh < no) no = nh;

	in.openX.SetCount(no);
	in.openZ.SetCount(no);
	in.openW.SetCount(no);
	in.openH.SetCount(no);
	for (int i = 0; i < no; ++i)
	{
		float ox = 0.0f, oz = 0.0f, ow = 0.0f, oh = 0.0f;
		pb->GetValue(pb_openingX, t, ox, valid, i);
		pb->GetValue(pb_openingZ, t, oz, valid, i);
		pb->GetValue(pb_openingW, t, ow, valid, i);
		pb->GetValue(pb_openingH, t, oh, valid, i);
		in.openX[i] = ox;
		in.openZ[i] = oz;
		in.openW[i] = ow;
		in.openH[i] = oh;
	}

	int nc = pb->Count(pb_panelColors);
	in.panelColors.SetCount(nc);
	for (int i = 0; i < nc; ++i)
	{
		Point3 col(0.55f, 0.55f, 0.55f);
		pb->GetValue(pb_panelColors, t, col, valid, i);
		in.panelColors[i] = col;
	}
}

// ---------------------------------------------------------------------
// Modify panel
// ---------------------------------------------------------------------

class TiltUpPanelDlgProc : public ParamMap2UserDlgProc
{
public:
	TiltUpPanel* ob;
	TiltUpPanelDlgProc(TiltUpPanel* o) : ob(o) {}
	void DeleteThis() override { delete this; }
	void SetThing(ReferenceTarget* m) override { ob = (TiltUpPanel*)m; }
	INT_PTR DlgProc(TimeValue /*t*/, IParamMap2* /*map*/, HWND /*hWnd*/, UINT msg, WPARAM wParam, LPARAM /*lParam*/) override;
};

INT_PTR TiltUpPanelDlgProc::DlgProc(TimeValue /*t*/, IParamMap2* /*map*/, HWND /*hWnd*/, UINT msg, WPARAM wParam, LPARAM /*lParam*/)
{
	if (!ob)
		return FALSE;
	if (msg == WM_COMMAND && LOWORD(wParam) == IDC_EDIT_LAYOUT && HIWORD(wParam) == BN_CLICKED)
	{
		ob->LaunchRevealLayoutEditor();
		return TRUE;
	}
	return FALSE;
}

void TiltUpPanel::LaunchRevealLayoutEditor()
{
	IObjParam* ip = editIp;
	if (!ip)
		ip = static_cast<IObjParam*>(GetCOREInterface());

	INode* node = FindEditingNode(this, ip);
	if (!node)
	{
		NotifyUser(_T("Select a RyViz Tilt-Up Panel node, then click Edit Reveal Layout."));
		return;
	}

	MSTR scriptPath;
	if (!ResolveEditorScriptPath(scriptPath))
	{
		NotifyUser(
			_T("Could not find tilt_up_panel_editor.py.\n\n")
			_T("Set Windows env var RYVIZ_TILTUP_EDITOR to the full path,\n")
			_T("or copy TiltUpPanel\\python to Documents\\3ds Max 2027\\scripts\\RyViz\\"));
		return;
	}

	const ULONG handle = node->GetHandle();
	MSTR setHandle;
	setHandle.printf(_T("global RyViz_TiltUpPanel_EditNodeHandle = %lu"), handle);

	FPValue fpv;
	if (!ExecuteMAXScriptScript(setHandle, MAXScript::ScriptSource::NonEmbedded, FALSE, &fpv))
	{
		MSTR err(_T("Failed to set editor node handle."));
		if (fpv.type == TYPE_TSTR && fpv.tstr)
		{
			err += _T("\n");
			err += *fpv.tstr;
		}
		NotifyUser(err);
		return;
	}

	// Prefer filein_script_ex — Max's supported entry for .py from C++.
	MSTR pyErr;
	if (!filein_script_ex(scriptPath, MAXScript::ScriptSource::NonEmbedded, &pyErr))
	{
		MSTR err;
		err.printf(_T("Failed to launch editor script:\n%s"), scriptPath.data());
		if (pyErr.Length() > 0)
		{
			err += _T("\n\n");
			err += pyErr;
		}
		NotifyUser(err);
	}
}

// ---------------------------------------------------------------------
// Object
// ---------------------------------------------------------------------

TiltUpPanel::TiltUpPanel()
{
	GetTiltUpPanelDesc()->MakeAutoParamBlocks(this);
}

TiltUpPanel::~TiltUpPanel()
{
}

void TiltUpPanel::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
	editIp = ip;
	SimpleObject2::BeginEditParams(ip, flags, prev);
	GetTiltUpPanelDesc()->BeginEditParams(ip, this, flags, prev);
	tiltUpPanel_paramblock.SetUserDlgProc(new TiltUpPanelDlgProc(this));
}

void TiltUpPanel::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
	editIp = nullptr;
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
	EnableMeshVertexColorDisplay(mesh);
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
