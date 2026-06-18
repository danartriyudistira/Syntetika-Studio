#include "CastParameter.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "PatchCable.h"

CastParameter::CastParameter()
{
}

CastParameter::~CastParameter()
{
}

void CastParameter::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mCable = new PatchCableSource(this, kConnectionType_UIControl);
   AddPatchCableSource(mCable);
}

void CastParameter::DrawModule()
{
   if (Minimized())
      return;

   ofSetColor(80, 80, 90, gModuleDrawAlpha);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   ofSetColor(200, 200, 200, gModuleDrawAlpha);
   DrawTextNormal(Name(), 3, 12, 10);

   if (mTarget)
      DrawTextNormal(std::string("-> ") + mTarget->Name(), 3, 25, 9);
   else
      DrawTextNormal("(unbound)", 3, 25, 9);
}

void CastParameter::GetModuleDimensions(float& width, float& height)
{
   width = mWidth;
   height = mHeight;
}

void CastParameter::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (TheSynth->IsLoadingState())
      return;

   auto cables = mCable->GetPatchCables();
   if (!cables.empty())
   {
      mTarget = dynamic_cast<IUIControl*>(cables[0]->GetTarget());
      if (mTarget)
         mTargetPath = mTarget->Path();
      else
         mTargetPath.clear();
   }
   else
   {
      mTarget = nullptr;
      mTargetPath.clear();
   }
}
