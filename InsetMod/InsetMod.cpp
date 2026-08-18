//**************************************************************************/
// Copyright (c) 1998-2020 Autodesk, Inc.
// All rights reserved.
// 
// Use of this software is subject to the terms of the Autodesk license 
// agreement provided at the time of installation or download, or which 
// otherwise accompanies this software in either electronic or hard copy form.
//**************************************************************************/
// DESCRIPTION: Plugin Wizard generated plugin
// AUTHOR: 
//***************************************************************************/

#include "InsetMod.h"

#define InsetMod_CLASS_ID Class_ID(0x8f2ccc97, 0x630252b2)


#define PBLOCK_REF 0


class InsetMod : public Modifier
{
public:
	// Constructor/Destructor
	InsetMod();
	virtual ~InsetMod();

	void DeleteThis() override { delete this; }

	// From Animatable
	const TCHAR* GetObjectName(bool localized) const override { return localized ? GetString(IDS_CLASS_NAME) : _T("InsetMod"); }

	ChannelMask ChannelsUsed() override { return GEOM_CHANNEL | TOPO_CHANNEL; }
#pragma message(TODO("Add the channels that the modifier actually modifies"))
	ChannelMask ChannelsChanged() override { return GEOM_CHANNEL; }

#pragma message(TODO("Return the ClassID of the object that the modifier can modify"))
	Class_ID InputType() override { return defObjectClassID; }

	void ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node) override;
	void NotifyInputChanged(const Interval& changeInt, PartID partID, RefMessage message, ModContext* mc) override;

	void NotifyPreCollapse(INode* node, IDerivedObject* derObj, int index) override;
	void NotifyPostCollapse(INode* node, Object* obj, IDerivedObject* derObj, int index) override;

	Interval LocalValidity(TimeValue t) override;

// From BaseObject
#pragma message(TODO("Return true if the modifier changes topology"))
	BOOL ChangeTopology() override { return FALSE; }

	CreateMouseCallBack* GetCreateMouseCallBack() override { return NULL; }

	BOOL HasUVW() override;
	void SetGenUVW(BOOL sw) override;

	void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
	void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;

	virtual Interval GetValidity(TimeValue t);

	// Automatic texture support

	// Loading/Saving
	IOResult Load(ILoad* iload) override;
	IOResult Save(ISave* isave) override;

	// From Animatable
	Class_ID ClassID() override { return InsetMod_CLASS_ID; }
	SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
	void GetClassName(TSTR& s, bool localized) const override { s = localized ? GetString(IDS_CLASS_NAME) : _T("InsetMod"); }

	RefTargetHandle Clone(RemapDir& remap) override;
	RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID, RefMessage message, BOOL propagate) override;

	int NumSubs() override { return 1; }
	TSTR SubAnimName(int /*i*/, bool localized) override { return localized ? GetString(IDS_PARAMS) : _T("Parameters"); }
	Animatable* SubAnim(int /*i*/) override { return pblock; }

	// TODO: Maintain the number or references here
	int NumRefs() override { return 1; }
	RefTargetHandle GetReference(int i) override;

	int NumParamBlocks() override { return 1; } // Return number of ParamBlocks in this instance
	IParamBlock2* GetParamBlock(int /*i*/) override { return pblock; } // Return i'th ParamBlock
	IParamBlock2* GetParamBlockByID(BlockID id) override { return (pblock->ID() == id) ? pblock : NULL; } // Return id'd ParamBlock

protected:
	void SetReference(int, RefTargetHandle rtarg) override;

private:
	// Parameter block
	IParamBlock2* pblock; // Ref 0
};

void InsetMod::SetReference(int i, RefTargetHandle rtarg)
{
	if (i == PBLOCK_REF)
	{
		pblock = (IParamBlock2*)rtarg;
	}
}

RefTargetHandle InsetMod::GetReference(int i)
{
	if (i == PBLOCK_REF)
	{
		return pblock;
	}

	return nullptr;
}


class InsetModClassDesc : public ClassDesc2 
{
public:
	int           IsPublic() override                               { return TRUE; }
	void*         Create(BOOL /*loading = FALSE*/) override         { return new InsetMod(); }
	const TCHAR*  ClassName() override                              { return GetString(IDS_CLASS_NAME); }
	const TCHAR*  NonLocalizedClassName() override                  { return _T("InsetMod"); }
	SClass_ID     SuperClassID() override                           { return OSM_CLASS_ID; }
	Class_ID      ClassID() override                                { return InsetMod_CLASS_ID; }
	const TCHAR*  Category() override                               { return GetString(IDS_CATEGORY); }

	const TCHAR*  InternalName() override                           { return _T("InsetMod"); } // Returns fixed parsable name (scripter-visible name)
	HINSTANCE     HInstance() override                              { return hInstance; } // Returns owning module handle


};

ClassDesc2* GetInsetModDesc()
{
	static InsetModClassDesc InsetModDesc;
	return &InsetModDesc; 
}






static ParamBlockDesc2 insetmod_param_blk ( insetmod_params, _T("params"),  0, GetInsetModDesc(), 
	P_AUTO_CONSTRUCT + P_AUTO_UI, PBLOCK_REF,
	// rollout
	IDD_PANEL, IDS_PARAMS, 0, 0, NULL,
	// params
	pb_spin,            _T("spin"),         TYPE_FLOAT,     P_ANIMATABLE,   IDS_SPIN,
		p_default,      0.1f,
		p_range,        0.0f, 1000.0f,
		p_ui,           TYPE_SPINNER,       EDITTYPE_FLOAT, IDC_EDIT,       IDC_SPIN,   0.01f,
		p_end,
	p_end
);



//--- InsetMod -------------------------------------------------------
InsetMod::InsetMod()
	: pblock(nullptr)
{
	GetInsetModDesc()->MakeAutoParamBlocks(this);
}

InsetMod::~InsetMod()
{

}

/*===========================================================================*\
 |  The validity of the parameters. First a test for editing is performed
 |  then Start at FOREVER, and intersect with the validity of each item
\*===========================================================================*/
Interval InsetMod::LocalValidity(TimeValue /*t*/)
{
	// If being edited, return NEVER forces a cache to be built
	// after previous modifier.
	if (TestAFlag(A_MOD_BEING_EDITED))
	{
		return NEVER;
	}

#pragma message(TODO("Return the validity interval of the modifier"))
	return NEVER;
}

/*************************************************************************************************
*
	Between NotifyPreCollapse and NotifyPostCollapse, Modify is
	called by the system. NotifyPreCollapse can be used to save any plugin dependant data e.g.
	LocalModData
*
\*************************************************************************************************/

void InsetMod::NotifyPreCollapse(INode* /*node*/, IDerivedObject* /*derObj*/, int /*index*/)
{
#pragma message(TODO("Perform any Pre Stack Collapse methods here"))
}

/*************************************************************************************************
*
	NotifyPostCollapse can be used to apply the modifier back onto to the stack, copying over the
	stored data from the temporary storage. To reapply the modifier the following code can be
	used

	Object* bo = node->GetObjectRef();
	IDerivedObject* derob = NULL;
	if(bo->SuperClassID() != GEN_DERIVOB_CLASS_ID)
	{
		derob = CreateDerivedObject(obj);
		node->SetObjectRef(derob);
	}
	else
	{
		derob = (IDerivedObject*) bo;
	}

	// Add ourselves to the top of the stack
	derob->AddModifier(this,NULL,derob->NumModifiers());

*
\*************************************************************************************************/

void InsetMod::NotifyPostCollapse(INode* /*node*/, Object* /*obj*/, IDerivedObject* /*derObj*/, int /*index*/)
{
#pragma message(TODO("Perform any Post Stack collapse methods here."))
}

/*************************************************************************************************
*
	ModifyObject will do all the work in a full modifier
	This includes casting objects to their correct form, doing modifications
	changing their parameters, etc
*
\************************************************************************************************/

void InsetMod::ModifyObject(TimeValue /*t*/, ModContext& /*mc*/, ObjectState*  /*os*/, INode* /*node*/)
{
#pragma message(TODO("Add the code for actually modifying the object"))
}

void InsetMod::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev)
{
	TimeValue t = ip->GetTime();
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_BEGIN_EDIT);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_MOD_DISPLAY_ON);
	SetAFlag(A_MOD_BEING_EDITED);

	GetInsetModDesc()->BeginEditParams(ip, this, flags, prev);
}

void InsetMod::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next)
{
	GetInsetModDesc()->EndEditParams(ip, this, flags, next);

	TimeValue t = ip->GetTime();
	ClearAFlag(A_MOD_BEING_EDITED);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_END_EDIT);
	NotifyDependents(Interval(t, t), PART_ALL, REFMSG_MOD_DISPLAY_OFF);
}

Interval InsetMod::GetValidity(TimeValue /*t*/)
{
	Interval valid = FOREVER;
#pragma message(TODO("Return the validity interval of the modifier"))
	return valid;
}

RefTargetHandle InsetMod::Clone(RemapDir& remap)
{
	InsetMod* newmod = new InsetMod();
#pragma message(TODO("Add the cloning code here"))
	newmod->ReplaceReference(PBLOCK_REF, remap.CloneRef(pblock));
	BaseClone(this, newmod, remap);
	return (newmod);
}

// From ReferenceMaker
RefResult InsetMod::NotifyRefChanged(const Interval& /*changeInt*/, RefTargetHandle hTarget, PartID& /*partID*/, RefMessage message, BOOL /*propagate*/)
{
#pragma message(TODO("Add code to handle the various reference changed messages"))
	switch (message)
	{
	case REFMSG_TARGET_DELETED:
	{
		if (hTarget == pblock)
		{
			pblock = nullptr;
		}
	}
	break;
	}
	return REF_SUCCEED;
}

/****************************************************************************************
*
	NotifyInputChanged is called each time the input object is changed in some way
	We can find out how it was changed by checking partID and message
*
\****************************************************************************************/

void InsetMod::NotifyInputChanged(const Interval& /*changeInt*/, PartID /*partID*/, RefMessage /*message*/, ModContext* /*mc*/)
{

}

// From Object
BOOL InsetMod::HasUVW()
{
#pragma message(TODO("Return whether the object has UVW coordinates or not"))
	return TRUE;
}

void InsetMod::SetGenUVW(BOOL sw)
{
	if (sw == HasUVW())
	{
		return;
	}

#pragma message(TODO("Set the plugin internal value to sw"))
}

IOResult InsetMod::Load(ILoad* iload)
{
	Modifier::Load(iload);

#pragma message(TODO("Add code to allow plugin to load its data"))

	return IO_OK;
}

IOResult InsetMod::Save(ISave* isave)
{
	Modifier::Save(isave);

#pragma message(TODO("Add code to allow plugin to save its data"))

	return IO_OK;
}
