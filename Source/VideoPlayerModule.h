#pragma once

#include "IAudioSource.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "INoteReceiver.h"
#include "Slider.h"
#include "ClickButton.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "PatchCableSource.h"
#include "foleys_video_engine.h"

#include <vector>
#include <string>
#include <memory>
#include <atomic>

class VisualFBO;

class VideoPlayerModule : public IAudioSource,
                          public IDrawableModule,
                          public IVisualSource,
                          public IFloatSliderListener,
                          public IButtonListener,
                          public IDropdownListener,
                          public INoteReceiver
{
public:
   VideoPlayerModule();
   ~VideoPlayerModule();

   static IDrawableModule* Create() { return new VideoPlayerModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Process(double time) override;
   void PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void ButtonClicked(ClickButton* button, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 2; }

   VisualFBO* GetFBO() override;
   void PostRender() override;

    bool IsEnabled() const override { return mEnabled; }
    bool IsResizable() const override { return true; }
    void Resize(float width, float height) override;
    bool ShouldSuppressAutomaticOutputCable() override { return true; } 

private:
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;
   void OnClicked(float x, float y, bool right) override;
   bool MouseMoved(float x, float y) override;
   bool MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll) override;
   void MouseReleased() override;

   void LoadFile();
   void LoadFromPath(const std::string& path);
   void UnloadVideo();
   double SecondsPerPixel() const;
   double PixelToSeconds(float x) const;
   float SecondsToPixel(double seconds) const;
   void SetPosition(double seconds);

   void ComputeWaveform();

   foleys::VideoEngine mVideoEngine;
   std::shared_ptr<foleys::MovieClip> mClip;

   std::string mVideoPath;
   double mDuration{ 0 };
   double mFps{ 30.0 };
   int mVideoW{ 0 };
   int mVideoH{ 0 };
   bool mHasVideo{ false };
   int mCurrentFrameHandle{ -1 };
   juce::int64 mLastFrameTimecode{ -1 };

   bool mPlaying{ false };
   double mPlayhead{ 0 };
   float mPlayheadFloat{ 0 };
   double mPlayStartTime{ 0 };
   bool mLoop{ false };

   float mSpeed{ 1.0f };
   double mCuePoint{ 0 };
   bool mHasCue{ false };

   double mLoopIn{ -1 };
   double mLoopOut{ -1 };
   bool mLoopSectionActive{ false };

   enum { kSpeedRange_1x, kSpeedRange_2x, kSpeedRange_4x, kSpeedRange_8x };
   int mSpeedRange{ kSpeedRange_1x };
   float mSpeedRangeValues[4]{ 1.0f, 2.0f, 4.0f, 8.0f };

   enum { kCueMode_Jump, kCueMode_Set };
   int mCueMode{ kCueMode_Jump };

   double mScrollZoomSeconds{ 8.0 };
   double mTimelineViewStart{ 0 };
   std::atomic<bool> mSeekPending{ false };
   double mSeekTarget{ 0 };

   static constexpr int kNumHotcues = 8;
   double mHotcuePosition[kNumHotcues]{};
   ClickButton* mHotcueButton[kNumHotcues]{};

   bool mScrubbing{ false };
   float mLastMouseX{ 0 };
   float mLastMouseY{ 0 };
   double mLastClickTime{ 0 };
   float mLastClickX{ 0 };
   float mLastClickY{ 0 };

   VisualFBO* mFBO{ nullptr };
   ChannelBuffer mWriteBuffer{ gBufferSize };

   std::vector<float> mWaveformOverview;

   PatchCableSource* mOutputCable{ nullptr };
   PatchCableSource* mVisualCable{ nullptr };

   ClickButton* mOpenButton{ nullptr };
   ClickButton* mPlayButton{ nullptr };
   ClickButton* mPauseButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   ClickButton* mCueButton{ nullptr };
   ClickButton* mLoopInButton{ nullptr };
   ClickButton* mLoopOutButton{ nullptr };
   ClickButton* mLoopClearButton{ nullptr };
   Checkbox* mLoopCheckbox{ nullptr };
   FloatSlider* mSpeedSlider{ nullptr };
   FloatSlider* mPositionSlider{ nullptr };
   DropdownList* mSpeedRangeDropdown{ nullptr };
   DropdownList* mCueModeDropdown{ nullptr };

   float mWidth{ 460 };
   float mHeight{ 260 };
   static constexpr float kMargin = 5;
   static constexpr float kControlY = 3;
   static constexpr float kControlH = 15;
   static constexpr float kRow2Y = 22;
   static constexpr float kRow3Y = 41;
   static constexpr float kRow4Y = 60;
   static constexpr float kInfoY = 78;
   static constexpr float kInfoH = 16;
   static constexpr float kTimelineY = 98;
   static constexpr float kTimelineH = 72;
};
