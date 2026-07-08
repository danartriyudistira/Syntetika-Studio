#pragma once

#include "IAudioSource.h"
#include "INoteReceiver.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "PatchCableSource.h"
#include "Transport.h"
#include "foleys_video_engine.h"

#include <vector>
#include <string>
#include <array>
#include <memory>

class VisualFBO;

class VideoDrumSampler : public IAudioSource,
                         public INoteReceiver,
                         public IDrawableModule,
                         public IVisualSource,
                         public IFloatSliderListener,
                         public IButtonListener,
                         public IDropdownListener,
                         public ITimeListener
{
public:
   VideoDrumSampler();
   ~VideoDrumSampler();

   static IDrawableModule* Create() { return new VideoDrumSampler(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;

   void Process(double time) override;
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void OnTimeEvent(double time) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 3; }

   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   static const int kNumPads = 16;
   static constexpr float kPadSize = 55;
   static constexpr float kPadGap = 3;
   static constexpr float kGridX = 5;
   static constexpr float kGridY = 38;

private:
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;

   struct Pad
   {
      std::string mVideoPath;
      float mVol{ 1.0f }, mSpeed{ 1.0f }, mPan{ 0.0f }, mFps{ 30.0f };
      bool mLooping{ false };
      int mLinkId{ -1 };
      int mTrimStart{ 0 }, mTrimEnd{ 0 };
      float mStartOffset{ 0 };

      std::shared_ptr<foleys::MovieClip> mClip;
      bool mLoaded{ false };
      bool mActive{ false };
      double mStartTime{ 0 };
      int mNvgHandle{ -1 };
      int mLastTimecode{ -1 };
      int mCachedW{ 0 }, mCachedH{ 0 };
   };

   void OnClicked(float x, float y, bool right) override;
   void SyncEditVars();
   void DrawEditPanel();
   void DrawWaveformTimeline();
   void RenderPadFrame(Pad& pad, double playheadSec, float fbw, float fbh);

   int PadFromClick(float x, float y) const;
   void TriggerPad(int index, double time);
   void LoadPadVideo(int index);
   void ClearPad(int index);
   void SaveKit();
   void LoadKit();

   std::array<Pad, kNumPads> mPads;
   NoteInputBuffer mNoteInputBuffer;
   VisualFBO* mFBO{ nullptr };
   foleys::VideoEngine mVideoEngine;
   std::vector<uint8_t> mConvertBuffer;
   ChannelBuffer mWriteBuffer{ gBufferSize };

   PatchCableSource* mOutputCable{ nullptr };
   PatchCableSource* mVisualCable{ nullptr };

   ClickButton* mSaveButton{ nullptr }, *mLoadButton{ nullptr };
   DropdownList* mQuantizeDropdown{ nullptr };
   DropdownList* mDisplayModeDropdown{ nullptr };
   Checkbox* mNoteRepeatCheckbox{ nullptr }, *mFullVelocityCheckbox{ nullptr };
   Checkbox* mSingleVoiceCheckbox{ nullptr }, *mEditCheckbox{ nullptr };
   Checkbox* mPianoModeCheckbox{ nullptr };
   ClickButton* mOctDownButton{ nullptr }, *mOctUpButton{ nullptr };

   bool mEditMode{ false };
   int mEditIndex{ -1 }, mLastClickedPad{ -1 };
   bool mPianoMode{ false };
   int mPianoRootNote{ 36 };

   FloatSlider* mEditVolSlider{ nullptr }, *mEditSpeedSlider{ nullptr }, *mEditPanSlider{ nullptr };
   FloatSlider* mEditFpsSlider{ nullptr }, *mEditTrimStartSlider{ nullptr }, *mEditTrimEndSlider{ nullptr };
   FloatSlider* mEditStartOffsetSlider{ nullptr };
   Checkbox* mEditLoopCheckbox{ nullptr };
   ClickButton* mLoadVideoButton{ nullptr }, *mClearPadButton{ nullptr };
   ClickButton* mPlayPadButton{ nullptr }, *mStopPadButton{ nullptr };

   float mEditVol{ 1 }, mEditSpeed{ 1 }, mEditPan{ 0 }, mEditFps{ 30 };
   float mEditTrimStart{ 0 }, mEditTrimEnd{ 1 }, mEditStartOffset{ 0 };
   bool mEditLoop{ false };

   NoteInterval mQuantizeInterval{ kInterval_None };
   bool mNoteRepeat{ false }, mFullVelocity{ false }, mSingleVoice{ false };
   float mButtonHeldVelocity[kNumPads]{};

   enum { kDisplay_Fit, kDisplay_Fill, kDisplay_16x9, kDisplay_4x3, kDisplay_1x1 };
   int mDisplayMode{ kDisplay_Fit };

   float mWidth{ 520 };
   float mHeight{ 340 };
};
