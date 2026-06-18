#pragma once

#include "IDrawableModule.h"
#include "PatchCableSource.h"

class CastParameter : public IDrawableModule
{
public:
   CastParameter();
   ~CastParameter();
   static IDrawableModule* Create() { return new CastParameter(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   bool IsEnabled() const override { return true; }

   std::string GetTargetPath() const { return mTargetPath; }
   IUIControl* GetTarget() const { return mTarget; }

private:
   PatchCableSource* mCable{ nullptr };
   std::string mTargetPath;
   IUIControl* mTarget{ nullptr };
   float mWidth{ 120 };
   float mHeight{ 38 };
};
