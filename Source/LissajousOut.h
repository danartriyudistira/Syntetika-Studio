#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "Checkbox.h"
#include "PatchCableSource.h"

#define LISSAJOUSOUT_BUFFER_SIZE 4096

class VisualFBO;

class LissajousOut : public IAudioProcessor,
                     public IDrawableModule,
                     public IVisualSource,
                     public IFloatSliderListener,
                     public IDropdownListener
{
public:
   LissajousOut();
   virtual ~LissajousOut();
   static IDrawableModule* Create() { return new LissajousOut(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}

    void LoadLayout(const ofxJSONElement& moduleInfo) override;
    void SaveLayout(ofxJSONElement& moduleInfo) override;
    void SetUpFromSaveData() override;

    void SaveState(FileStreamOut& out) override;
    void LoadState(FileStreamIn& in, int rev) override;
    int GetModuleSaveStateRev() const override { return 1; }

   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   struct Point { float x, y; };

   Point         mBuffer[LISSAJOUSOUT_BUFFER_SIZE];
   int           mWritePos{ 0 };
   int           mNumStored{ 0 };

   VisualFBO*    mFBO{ nullptr };

   float         mWidth{ 500 };
   float         mHeight{ 500 };
   float         mScale{ 1 };
   float         mZoom{ 1 };
   float         mIntensity{ 1 };
   float         mLineWidth{ 1.5f };
   float         mDecay{ 2 };
   int           mColorSelect{ 0 };
   bool          mShowBackground{ true };

   FloatSlider*  mScaleSlider{ nullptr };
   FloatSlider*  mZoomSlider{ nullptr };
   FloatSlider*  mIntensitySlider{ nullptr };
   FloatSlider*  mLineWidthSlider{ nullptr };
   FloatSlider*  mDecaySlider{ nullptr };
   DropdownList* mColorDropdown{ nullptr };
   Checkbox*     mBackgroundCheckbox{ nullptr };

   PatchCableSource* mVisualCable{ nullptr };
};
