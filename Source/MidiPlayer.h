#pragma once

#include "IDrawableModule.h"
#include "INoteSource.h"
#include "ClickButton.h"
#include "Slider.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "Transport.h"
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

class MidiPlayer : public IDrawableModule,
                   public INoteSource,
                   public IButtonListener,
                   public IFloatSliderListener,
                   public IDropdownListener,
                   public IAudioPoller
{
public:
   MidiPlayer();
   virtual ~MidiPlayer();
   static IDrawableModule* Create() { return new MidiPlayer(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void OnTransportAdvanced(float amount) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 5; }

   bool IsEnabled() const override { return mEnabled; }
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

private:
   struct MidiEvent
   {
      double mTime;
      int mChannel;
      int mTrack;
      juce::MidiMessage mMessage;
   };

   void DrawModule() override;
   void OnClicked(float x, float y, bool right) override;

   void LoadFile();
   void ClearMidiData();
   void Play();
   void Pause();
   void Stop();
   void Seek(double time);
   int CueSlotFromClick(float x, float y) const;
   static const int kNumCues = 8;
   static constexpr float kControlsTop = 2;
   static constexpr float kControlsHeight = 48;
   static constexpr float kBottomAreaHeight = 52;

   ClickButton* mLoadButton{ nullptr };
   ClickButton* mPlayButton{ nullptr };
   ClickButton* mPauseButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   ClickButton* mCueButtons[kNumCues]{};
   Checkbox* mLoopCheckbox{ nullptr };
   Checkbox* mSyncCheckbox{ nullptr };
   FloatSlider* mTempoSlider{ nullptr };
   FloatSlider* mVolumeSlider{ nullptr };
   ClickButton* mNudgeLeft{ nullptr };
   ClickButton* mNudgeRight{ nullptr };
   DropdownList* mChannelFilter{ nullptr };
   DropdownList* mTrackSelector{ nullptr };
   int mTrackSelect{ -1 };
   int mNumTracks{ 0 };

   bool mIsPlaying{ false };
   bool mLoop{ true };
   bool mSync{ false };
   float mTempo{ 120.0f };
   float mVolume{ 1.0f };
   float mNudge{ 0.0f };
   int mChannelFilterIndex{ 0 };
   float mWidth{ 520 };
   float mHeight{ 360 };
   bool mEnabled{ true };

   std::vector<MidiEvent> mEvents;
   int mNextEventIndex{ 0 };
   double mPlayPosition{ 0 };
   double mLength{ 0 };
   std::string mFileName;

   double mCuePositions[kNumCues]{};
   bool mCueSet[kNumCues]{};

   static constexpr int kWaveSlices = 256;
   std::vector<float> mWaveform;
};
