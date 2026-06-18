#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "VisualFBO.h"

class PatchCableSource;

class VisualNodeBase : public IDrawableModule, public IVisualSource
{
public:
   VisualNodeBase();
   virtual ~VisualNodeBase() {}

   // IVisualSource
   VisualFBO* GetFBO() override { return &mFBO; }

   // IDrawableModule
   void CreateUIControls() override;
   void DrawModule() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   // Subclass overrides
   virtual void RenderToFBO() = 0;

protected:
   VisualFBO mFBO;
   int mFBOWidth{ 512 };
   int mFBOHeight{ 512 };
   float mDisplayWidth{ 256 };
   float mDisplayHeight{ 256 };
};
