#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "Checkbox.h"

#define SYNTETISCOPE_BUFFER_SIZE 4096

class VisualFBO;

class Syntetiscope : public IAudioProcessor, public IDrawableModule, public IVisualSource, public IFloatSliderListener, public IIntSliderListener, public IDropdownListener
{
public:
   Syntetiscope();
   virtual ~Syntetiscope();
   static IDrawableModule* Create() { return new Syntetiscope(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   //IVisualSource
   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override
   {
      w = mWidth;
      h = mHeight;
   }

   float mWidth{ 500 };
   float mHeight{ 500 };
   float mScale{ 1 };
   float mZoom{ 1 };
   float mIntensity{ 1 };
   float mBeamSize{ 4 };
   float mDecay{ 2 };
   int mColorSelect{ 0 };
   bool mShowLissa{ true };

   FloatSlider* mScaleSlider{ nullptr };
   FloatSlider* mZoomSlider{ nullptr };
   FloatSlider* mIntensitySlider{ nullptr };
   FloatSlider* mBeamSlider{ nullptr };
   FloatSlider* mDecaySlider{ nullptr };
   DropdownList* mColorDropdown{ nullptr };
   Checkbox* mShowLissaCheckbox{ nullptr };

   struct Point { float x, y; };
   VisualFBO* mFBO{ nullptr };

   Point mBuffer[SYNTETISCOPE_BUFFER_SIZE];
   int mWritePos{ 0 };
   int mNumStored{ 0 };
};
