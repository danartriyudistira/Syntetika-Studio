#pragma once

#include "IAudioSource.h"
#include "PolyphonyMgr.h"
#include "ModelDVoice.h"
#include "INoteReceiver.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "DropdownList.h"
#include "ADSRDisplay.h"
#include "Checkbox.h"
#include "Oscillator.h"
#include "RadioButton.h"

class ModelDSynth : public IAudioSource, public INoteReceiver, public IDrawableModule, public IDropdownListener, public IFloatSliderListener, public IIntSliderListener, public IRadioButtonListener
{
public:
   ModelDSynth();
   ~ModelDSynth();
   static IDrawableModule* Create() { return new ModelDSynth(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override;

   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}

   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void RadioButtonUpdated(RadioButton* list, int oldVal, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   bool IsEnabled() const override { return mEnabled; }

private:
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override
   {
      width = mWidth;
      height = mHeight;
   }

   PolyphonyMgr mPolyMgr;
   NoteInputBuffer mNoteInputBuffer;
   ModelDParams mVoiceParams;
   ChannelBuffer mWriteBuffer;

   float mWidth{ 370 };
   float mHeight{ 360 };

   DropdownList* mOsc1Type{ nullptr };
   IntSlider* mOsc1Octave{ nullptr };
   IntSlider* mOsc1Semitone{ nullptr };
   FloatSlider* mOsc1Fine{ nullptr };
   FloatSlider* mOsc1PW{ nullptr };
   FloatSlider* mOsc1Vol{ nullptr };
   Checkbox* mOsc1Enabled{ nullptr };

   DropdownList* mOsc2Type{ nullptr };
   IntSlider* mOsc2Octave{ nullptr };
   IntSlider* mOsc2Semitone{ nullptr };
   FloatSlider* mOsc2Fine{ nullptr };
   FloatSlider* mOsc2PW{ nullptr };
   FloatSlider* mOsc2Vol{ nullptr };
   Checkbox* mOsc2Enabled{ nullptr };

   DropdownList* mOsc3Type{ nullptr };
   IntSlider* mOsc3Octave{ nullptr };
   IntSlider* mOsc3Semitone{ nullptr };
   FloatSlider* mOsc3Fine{ nullptr };
   FloatSlider* mOsc3PW{ nullptr };
   FloatSlider* mOsc3Vol{ nullptr };
   Checkbox* mOsc3Enabled{ nullptr };

   FloatSlider* mNoiseAmount{ nullptr };

   FloatSlider* mFilterCutoff{ nullptr };
   FloatSlider* mFilterResonance{ nullptr };
   FloatSlider* mFilterDrive{ nullptr };
   FloatSlider* mFilterKbdTrack{ nullptr };
   FloatSlider* mFilterEnvAmount{ nullptr };

   FloatSlider* mLFORate{ nullptr };
   FloatSlider* mLFODepth{ nullptr };
   DropdownList* mLFOType{ nullptr };
   DropdownList* mLFOTarget{ nullptr };

   FloatSlider* mVolSlider{ nullptr };
   ADSRDisplay* mADSRDisplay{ nullptr };
   ADSRDisplay* mFilterADSRDisplay{ nullptr };
};
