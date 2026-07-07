#pragma once

#include "IAudioSource.h"
#include "INoteReceiver.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Sample.h"
#include "Slider.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "PatchCableSource.h"
#include "Transport.h"

#include <vector>
#include <string>
#include <array>

class VisualFBO;

class VideoDrumSampler : public IAudioSource,
                         public INoteReceiver,
                         public IDrawableModule,
                         public IVisualSource,
                         public IFloatSliderListener,
                         public IButtonListener,
                         public IDropdownListener
{
public:
   VideoDrumSampler();
   ~VideoDrumSampler();

   static IDrawableModule* Create() { return new VideoDrumSampler(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Process(double time) override;
   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void ButtonClicked(ClickButton* button, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 1; }

   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }

   static const int kNumPads = 16;
   static constexpr float kPadSize = 65;
   static constexpr float kPadGap = 4;
   static constexpr float kGridX = 5;
   static constexpr float kGridY = 25;

private:
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;

   struct Pad
   {
      std::string mVideoPath;
      std::string mTempDir;
      std::string mAudioPath;
      float mVol{ 1.0f };
      float mSpeed{ 1.0f };
      float mPan{ 0.0f };
      float mFps{ 30.0f };
      bool mLooping{ false };
      int mLinkId{ -1 };

      std::vector<int> mImageHandles;
      int mNumFrames{ 0 };
      int mImageW{ 0 };
      int mImageH{ 0 };
      bool mLoaded{ false };

      Sample mAudioSample;

      bool mActive{ false };
      double mLastAdvanceTime{ 0 };
      int mCurrentFrame{ 0 };
   };

   void OnClicked(float x, float y, bool right) override;
   void SyncEditVars();

   int PadFromClick(float x, float y) const;
   void TriggerPad(int index, double time);
   void LoadPadVideo(int index);
   void ClearPad(int index);

   static std::string FindFFmpeg();
   static std::string CreateTempDir();
   static bool ExtractFramesFFmpeg(const std::string& videoPath, const std::string& tempDir);
   static void RemoveTempDir(const std::string& tempDir);

   std::array<Pad, kNumPads> mPads;
   NoteInputBuffer mNoteInputBuffer;
   VisualFBO* mFBO{ nullptr };

   ChannelBuffer mWriteBuffer{ gBufferSize };

   PatchCableSource* mOutputCable{ nullptr };
   PatchCableSource* mVisualCable{ nullptr };

   ClickButton* mBrowseButton{ nullptr };
   DropdownList* mModeDropdown{ nullptr };
   FloatSlider* mFpsSlider{ nullptr };
   FloatSlider* mFramesPerBeatSlider{ nullptr };
   Checkbox* mEditCheckbox{ nullptr };
   bool mEditMode{ false };

   int mEditIndex{ -1 };
   ClickButton* mLoadAudioButton{ nullptr };
   ClickButton* mLoadVideoButton{ nullptr };
   ClickButton* mClearPadButton{ nullptr };
   FloatSlider* mEditVolSlider{ nullptr };
   FloatSlider* mEditSpeedSlider{ nullptr };
   FloatSlider* mEditPanSlider{ nullptr };
   FloatSlider* mEditFpsSlider{ nullptr };
   Checkbox* mEditLoopCheckbox{ nullptr };
   float mEditVol{ 1.0f };
   float mEditSpeed{ 1.0f };
   float mEditPan{ 0.0f };
   float mEditFps{ 30.0f };
   bool mEditLoop{ false };

   enum { kMode_FPS, kMode_FPB };
   int mMode{ kMode_FPS };
   float mFramesPerBeat{ 2.0f };
   float mGlobalFps{ 30.0f };

   float mWidth{ 500 };
   float mHeight{ 340 };
   static constexpr float kMinWidth = 300;
   static constexpr float kMinHeight = 200;
   static constexpr float kEditPanelWidth = 200;
};
