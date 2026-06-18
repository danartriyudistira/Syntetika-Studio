#include "VisualNodeBase.h"
#include "PatchCableSource.h"

VisualNodeBase::VisualNodeBase()
{
}

void VisualNodeBase::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   auto* cable = new PatchCableSource(this, kConnectionType_Special);
   cable->SetManualPosition(170, 10);
   AddPatchCableSource(cable);
}

void VisualNodeBase::DrawModule()
{
   if (!mFBO.IsValid() || mFBO.GetWidth() != mFBOWidth || mFBO.GetHeight() != mFBOHeight)
      mFBO.Create(mFBOWidth, mFBOHeight);

   mFBO.Bind();
   RenderToFBO();
   mFBO.Unbind();

   mFBO.Draw(0, 0, mDisplayWidth, mDisplayHeight);
}

void VisualNodeBase::Resize(float w, float h)
{
   mDisplayWidth = w;
   mDisplayHeight = h;
}
