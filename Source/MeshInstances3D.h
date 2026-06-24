#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include "TextEntry.h"

#include <vector>
#include <string>

class VisualFBO;

class MeshInstances3D : public IAudioProcessor, public IDrawableModule, public IVisualSource, public IFloatSliderListener, public IButtonListener, public IDropdownListener, public ITextEntryListener
{
public:
   MeshInstances3D();
   virtual ~MeshInstances3D();
   static IDrawableModule* Create() { return new MeshInstances3D(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void ButtonClicked(ClickButton* button, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void TextEntryComplete(TextEntry* entry) override {}

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   //IVisualSource
   VisualFBO* GetFBO() override;
   void PostRender() override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 8; }

   bool IsEnabled() const override { return mEnabled; }

private:
   enum BuiltInShape
   {
      kCube,
      kLowPolySphere,
      kPyramid,
      kCustomOBJ,
      kSVG
   };

   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override
   {
      w = mWidth;
      h = mHeight;
   }

   std::vector<float> mVertexPositions; // flat [x,y,z]
   int mNumVertices{ 0 };

   // flat edge list: [a0,b0, a1,b1, ...]
   std::vector<int> mEdges;
   int mNumEdges{ 0 };

   // ordered cyclic vertex path: each consecutive pair is a valid edge from mEdges, first == last
   std::vector<int> mVertexPath;
   int mPathLen{ 0 };

   void GenerateCube();
   void GenerateLowPolySphere();
   void GeneratePyramid();
   void SelectBuiltInShape(BuiltInShape shape);
   bool LoadModelOBJ(const std::string& path);
   bool LoadModelSVG(const std::string& path);

   // scan: internal phase + audio FM modulation
   float mScanFreq{ 55.0f };
   float mFmDepth{ 0.5f };
   float mAmplify{ 1.0f };
   float mPhase{ 0 };

   // input frequency detection (for freq follow)
   bool mFreqFollow{ false };
   float mDetectedFreq{ 55.0f };
   float mLastInputSample{ 1.0f };
   int mSampleCounter{ 0 };
   int mLastZeroCrossSample{ 0 };
   int mZeroCrossAccum{ 0 };
   int mZeroCrossCount{ 0 };

   VisualFBO* mFBO{ nullptr };
   bool mIsSVG{ false };

   // 3D rotation
   float mRotX{ 0 };  // degrees, around X axis
   float mRotY{ 0 };  // degrees, around Y axis
   float mRotZ{ 0 };  // degrees, around Z axis
   float mPerspective{ 0.5f };

   DropdownList* mShapeDropdown{ nullptr };
   TextEntry* mModelPathEntry{ nullptr };
   ClickButton* mLoadModelButton{ nullptr };
   FloatSlider* mScanFreqSlider{ nullptr };
   FloatSlider* mFmDepthSlider{ nullptr };
   FloatSlider* mAmplifySlider{ nullptr };

   FloatSlider* mRotXSlider{ nullptr };
   FloatSlider* mRotYSlider{ nullptr };
   FloatSlider* mRotZSlider{ nullptr };
   FloatSlider* mPerspectiveSlider{ nullptr };
   Checkbox* mFreqFollowCheckbox{ nullptr };

   int mShapeInt{ 0 };
    // Pre-allocated working buffers (avoid heap alloc per audio callback)
    std::vector<float> mAudioProjX;   // Process() audio thread
    std::vector<float> mAudioProjY;
    std::vector<float> mVizL;
    std::vector<float> mVizR;
    std::vector<float> mRenderProjX;  // PostRender() render thread
    std::vector<float> mRenderProjY;

    float mWidth{ 360 };
    float mHeight{ 280 };
    std::string mModelPath;
};
