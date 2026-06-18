#pragma once

#include "IDrawableModule.h"
#include "OpenFrameworksPort.h"
#include "CodeEntry.h"
#include "ClickButton.h"
#include "Slider.h"
#include "PatchCableSource.h"
#include "IVisualSource.h"

class VisualFBO;

class GLSLShaderModule : public IDrawableModule, public IButtonListener, public ICodeEntryListener, public IFloatSliderListener, public IVisualSource
{
public:
   GLSLShaderModule();
   virtual ~GLSLShaderModule();
   static IDrawableModule* Create() { return new GLSLShaderModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}

   //ICodeEntryListener
   void ExecuteCode() override;
   std::pair<int, int> ExecuteBlock(int lineStart, int lineEnd) override { return {0, 0}; }
   void OnCodeUpdated() override {}

   //IVisualSource
   VisualFBO* GetFBO() override { return mFBO; }

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   bool IsEnabled() const override { return true; }

private:
   //IDrawableModule
   void DrawModule() override;
   void PostRender() override;

   void CompileShader();
   void CleanupShader();
   void RandomizeShader();
   std::string GetDefaultShader();
   void CreateFBO();

   CodeEntry* mCodeEntry{ nullptr };
   ClickButton* mRandomButton{ nullptr };
   ClickButton* mCompileButton{ nullptr };
   FloatSlider* mSliderA{ nullptr };
   FloatSlider* mSliderB{ nullptr };
   FloatSlider* mSliderC{ nullptr };
   FloatSlider* mSliderD{ nullptr };

   // Shader
   unsigned int mProgramId{ 0 };
   unsigned int mVSId{ 0 };
   unsigned int mFSId{ 0 };
   bool mShaderDirty{ true };
   std::string mLastError;
   double mShaderStartTime{ 0 };

   unsigned int mVBO{ 0 };

   VisualFBO* mFBO{ nullptr };
   PatchCableSource* mOutputCable{ nullptr };

   float mWidth{ 400 };
   float mHeight{ 350 };
   float mSliderAValue{ 0 };
   float mSliderBValue{ 0 };
   float mSliderCValue{ 0 };
   float mSliderDValue{ 0 };
};
