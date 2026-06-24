#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "DropdownList.h"
#include "Checkbox.h"

#define TRIGGERWAVE_BUFFER_SIZE 4096
#define TRIGGERWAVE_ENV_HISTORY 256

class VisualFBO;

class TriggerWaveEffect : public IAudioProcessor, public IDrawableModule, public IVisualSource,
                          public IFloatSliderListener, public IDropdownListener
{
public:
   TriggerWaveEffect();
   virtual ~TriggerWaveEffect();
   static IDrawableModule* Create() { return new TriggerWaveEffect(); }
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

   //IVisualSource
   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   void CheckBeat(float sample);
   void DrawBaseWaveform(float w, float h);
   void DrawGlitch(float w, float h);
   void DrawScanlines(float w, float h);
   void DrawFlash(float w, float h);
   void DrawPulse(float w, float h);

   float mWidth{ 500 };
   float mHeight{ 500 };
   float mSensitivity{ 1.5f };
   float mIntensity{ 1.0f };
   int mEffectMode{ 3 }; // 0=pulse, 1=glitch, 2=scanlines, 3=all
   int mColorSelect{ 0 }; // 0=cyan,1=magenta,2=green,3=white,4=rainbow

   FloatSlider* mSensitivitySlider{ nullptr };
   FloatSlider* mIntensitySlider{ nullptr };
   DropdownList* mEffectModeDropdown{ nullptr };
   DropdownList* mColorDropdown{ nullptr };

   // waveform buffer
   float mBuffer[TRIGGERWAVE_BUFFER_SIZE];
   int mWritePos{ 0 };
   int mNumStored{ 0 };

   // beat detection
   float mEnvHistory[TRIGGERWAVE_ENV_HISTORY];
   int mEnvPos{ 0 };
   float mEnvSum{ 0 };
   float mLocalEnergy{ 0 };
   float mAvgEnergy{ 0 };
   float mBeatFlash{ 0 };
   int mBeatHold{ 0 };
   int mBeatCount{ 0 };
   float mGlitchSeed{ 0 };

   VisualFBO* mFBO{ nullptr };
};
