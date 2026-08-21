/*
	TiltUpPanel.h

	Parametric tilt-up precast wall panel (GeomObject / SimpleObject2).
	Phase 2: MNMesh cut-grid. Reveals are axis+position Tabs; groove
	width/depth are global. Base wall thickness is `depth`. Field faces
	get additional extrusion boxes on the front quads (`grooveDepth`);
	the original front grid is left intact.

	Local space: X = width, Y = thickness, Z = height.
	Origin is the lower-left-back corner. Base front is y = depth;
	panel caps sit at y = depth + grooveDepth.
	Horizontal reveal = constant Z. Vertical reveal = constant X.
*/

#pragma once

#include "3dsmaxsdk_preinclude.h"
#include "max.h"
#include "iparamm2.h"
#include "iparamb2.h"
#include "simpobj.h"
#include "resource.h"

#define TILTUPPANEL_CLASS_ID Class_ID(0x7b3e1a90, 0x2c8d4f61)

extern HINSTANCE hInstance;
extern TCHAR* GetString(int id);
extern ClassDesc2* GetTiltUpPanelDesc();

enum { tiltuppanel_params };

enum
{
	pb_width,
	pb_height,
	pb_depth,				// base wall thickness (minimum)
	pb_revealAxis,			// TYPE_INT_TAB    0 = horizontal (Z), 1 = vertical (X)
	pb_revealPos,			// TYPE_FLOAT_TAB
	pb_grooveWidth,			// global, all reveals
	pb_grooveDepth			// global; fields extrude out by this amount
};

class TiltUpPanel : public SimpleObject2
{
public:
	int selectedIndex;
	HWND hPanel;
	ISpinnerControl* spinRevealPos;

	TiltUpPanel();
	~TiltUpPanel();

	void DeleteThis() override { delete this; }
	Class_ID ClassID() override { return TILTUPPANEL_CLASS_ID; }
	SClass_ID SuperClassID() override { return GEOMOBJECT_CLASS_ID; }
	void GetClassName(MSTR& s, bool localized = true) const override
	{
		s = localized ? GetString(IDS_CLASS_NAME) : MSTR(_M("TiltUpPanel"));
	}
	const MCHAR* GetObjectName(bool localized) const override
	{
		return localized ? GetString(IDS_CLASS_NAME) : _M("TiltUpPanel");
	}

	void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
	void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;

	CreateMouseCallBack* GetCreateMouseCallBack() override;
	void BuildMesh(TimeValue t) override;
	void InvalidateUI() override;
	BOOL OKtoDisplay(TimeValue t) override;

	int CanConvertToType(Class_ID obtype) override;
	Object* ConvertToType(TimeValue t, Class_ID obtype) override;

	RefTargetHandle Clone(RemapDir& remap) override;

	void SyncRevealTabs();
	void AddReveal(int axis);
	void RemoveSelectedReveal();
	void InitRevealControls(HWND hWnd);
	void RefreshRevealList();
	void LoadSelectedRevealToUI();
	void WriteRevealSpinnersToPblock(TimeValue t);
	int SyncSelectionFromList();

	friend class TiltUpPanelCreateCallBack;
};
