#pragma once

#include "IDrawableModule.h"
#include "OpenFrameworksPort.h"
#include "CodeEntry.h"
#include "ClickButton.h"
#include "Slider.h"
#include "PatchCableSource.h"
#include "IVisualSource.h"

class VisualFBO;

class ShaderEditPopup : public IDrawableModule, public IButtonListener, public ICodeEntryListener
{
public:
   ShaderEditPopup();
   void SetParent(class ShaderModule* parent) { mParent = parent; }
   void Show();
   void Hide();

   bool HasTitleBar() const override { return false; }
   void CreateUIControls() override;
   void GetModuleDimensions(float& w, float& h) override { w = 520; h = 400; }
   bool IsSaveable() override { return false; }

   // IButtonListener
   void ButtonClicked(ClickButton* button, double time) override;

   // ICodeEntryListener
   void ExecuteCode() override;
   std::pair<int, int> ExecuteBlock(int lineStart, int lineEnd) override { return {0, 0}; }
   void OnCodeUpdated() override {}

   CodeEntry* GetCodeEntry() const { return mCodeEntry; }

private:
   void DrawModule() override;
   void KeyPressed(int key, bool isRepeat) override;
   bool IsEnabled() const override { return true; }

   ShaderModule* mParent{ nullptr };
   CodeEntry* mCodeEntry{ nullptr };
   ClickButton* mCompileButton{ nullptr };
   ClickButton* mCancelButton{ nullptr };
};

class ShaderModule : public IDrawableModule, public IButtonListener, public IFloatSliderListener, public IVisualSource
{
public:
   ShaderModule();
   virtual ~ShaderModule();
   static IDrawableModule* Create() { return new ShaderModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void PostRepatch(PatchCableSource* source, bool fromUserClick) override;

   //IVisualSource
   VisualFBO* GetFBO() override { return mFBO; }

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 2; }

   bool IsEnabled() const override { return true; }

   // called by popup
   void SetShaderCode(const std::string& code);
   std::string GetShaderCode() const;
   void OpenEditPopup();
   void CloseEditPopup();

private:
   //IDrawableModule
   void DrawModule() override;
   void PostRender() override;
   void OnClicked(float x, float y, bool right) override;

   void CompileShader();
   void CleanupShader();
   void RandomizeShader();
   std::string GetDefaultShader();
   void CreateFBO();
   std::string GetCodePreview() const;

   std::string mShaderCode;
   ClickButton* mRandomButton{ nullptr };
   FloatSlider* mResolutionSlider{ nullptr };
   FloatSlider* mSpeedSlider{ nullptr };
   float mSpeed{ 1.0f };

    // Dynamic sliders from shader uniform parsing
    struct ShaderParam
    {
       std::string name;
       std::string type;
       int numComponents{ 1 };
       float value{ 0 };
       float c1{ 0 }, c2{ 0 }, c3{ 0 };
       FloatSlider* slider{ nullptr };
       FloatSlider* s1{ nullptr }, * s2{ nullptr }, * s3{ nullptr };
    };
    std::vector<ShaderParam> mShaderParams;

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
   PatchCableSource* mInputCable{ nullptr };
   IVisualSource* mInputSource{ nullptr };
   unsigned int mDefaultTexture{ 0 };

   float mWidth{ 400 };
   float mHeight{ 350 };
   float mResolutionScale{ 1.0f };

   ShaderEditPopup mEditPopup;
};
