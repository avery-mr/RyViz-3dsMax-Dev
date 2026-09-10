/*
	TiltUpPanel.h

	Parametric tilt-up precast wall panel (GeomObject / SimpleObject2).
	MNMesh cut-grid. Reveals are axis+position Tabs; groove width/depth
	are global. Optional edgeSides / edgeTopBot inject half-width reveals
	at the panel bounds. Rectangular openings punch through the grid.
	Base wall thickness is `depth`. Field faces extrude on a shared outer
	grid (`grooveDepth`). Optional panelColors (Point3 tab) tint field
	subpanels via vertex color.

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
// Force project-local resource.h (MaxSdkInc can otherwise shadow "resource.h").
#include "./resource.h"

#define TILTUPPANEL_CLASS_ID Class_ID(0x7b3e1a90, 0x2c8d4f61)

extern HINSTANCE hInstance;
extern TCHAR* GetString(int id);
extern ClassDesc2* GetTiltUpPanelDesc();

enum { tiltuppanel_params };

enum
{
	pb_width,
	pb_height,
	pb_depth,
	pb_revealAxis,
	pb_revealPos,
	pb_grooveWidth,
	pb_grooveDepth,
	pb_edgeSides,
	pb_edgeTopBot,
	pb_openingX,
	pb_openingZ,
	pb_openingW,
	pb_openingH,
	pb_panelColors			// TYPE_POINT3_TAB, one RGB per subpanel region
};

class TiltUpPanel : public SimpleObject2
{
public:
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

	void LaunchRevealLayoutEditor();

	static IObjParam* editIp;

	friend class TiltUpPanelCreateCallBack;
};
