#pragma once

#include "IAudioProcessor.h"
#include "EnvOscillator.h"
#include "INoteReceiver.h"
#include "IDrawableModule.h"
#include "Checkbox.h"
#include "Slider.h"
#include "DropdownList.h"
#include "ClickButton.h"
#include "IPulseReceiver.h"
#include "SwitchAndRamp.h"

class Sample;

namespace KeyFinder { class KeyFinder; }

class DJPlayer : public IAudioProcessor, public IDrawableModule, public INoteReceiver, public IFloatSliderListener, public IDropdownListener, public IButtonListener, public IPulseReceiver
{
public:
   DJPlayer();
   ~DJPlayer();
   static IDrawableModule* Create() { return new DJPlayer(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return true; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;

   void PlayNote(double time, int pitch, int velocity, int voiceIdx = -1, ModulationParameters modulation = ModulationParameters()) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void OnPulse(double time, float velocity, int flags) override;

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }

   void FilesDropped(std::vector<std::string> files, int x, int y) override;
   void SampleDropped(int x, int y, Sample* sample) override;
   bool CanDropSample() const override { return true; }
   bool IsResizable() const override { return true; }
   void Resize(float width, float height) override
    {
       mWidth = ofClamp(width, 350, 9999);
       mHeight = ofClamp(height, 340, 9999);
    }

   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownClicked(DropdownList* list) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
    void ButtonClicked(ClickButton* button, double time) override;
    void KeyPressed(int key, bool isRepeat) override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
       int GetModuleSaveStateRev() const override { return 11; }

   bool IsEnabled() const override { return mEnabled; }

private:
     // ── CDJ display helpers ──
     enum class PitchRange { Six = 0, Ten, Sixteen, Wide };
     static float PitchRangeToFloat(PitchRange r);
     static const char* PitchRangeLabel(PitchRange r);

   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;
   void OnClicked(float x, float y, bool right) override;
   bool MouseMoved(float x, float y) override;
   bool MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll) override;
   void MouseReleased() override;

    void LoadFile();
    void UpdateSample(Sample* sample, bool ownsSample);
    float GetPlayPositionForMouse(float mouseX, float mouseY) const;
    bool IsInOverviewRegion(float mouseY) const;
    bool IsInZoomRegion(float mouseY) const;
    int GetZoomStartSample() const;
    int GetZoomEndSample() const;
    float GetZoomStartSeconds() const;
    float GetZoomEndSeconds() const;
    float GetCurrentBPM() const;
     float GetSamplesPerBeat() const;
     float SampleToBeat(int sample) const;
     int BeatToSample(float beat) const;
     void SetAutoLoop(int beats);
    void AnalyzeBPM();
    void AnalyzeKey();
    std::string GetKeyName() const;
     void DrawRGBWaveform(float x, float y, float w, float h, int startSample = 0, int numSamples = -1);
     std::string GetAnalysisFilePath() const;
    void LoadAnalysisFile();
      void SaveAnalysisFile();

     static constexpr float kPreRollSeconds{ 2.0f };  // pre-roll padding before sample 0

    float mWidth{ 420 };
   float mHeight{ 310 };

   Sample* mSample{ nullptr };
   bool mOwnsSample{ true };

   // ── transport ──
   float mVolume{ 1 };
   FloatSlider* mVolumeSlider{ nullptr };
   ClickButton* mPlayButton{ nullptr };
   ClickButton* mPauseButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   bool mPlay{ false };
   bool mLoop{ false };
   Checkbox* mLoopCheckbox{ nullptr };
   ClickButton* mLoadFileButton{ nullptr };

   // ── pitch / tempo fader (CDJ-style) ──
   float mPitchPercent{ 0 };         // -1.0 to +1.0 (center=0, up=slower, down=faster)
   FloatSlider* mPitchFader{ nullptr };
   PitchRange mPitchRange{ PitchRange::Ten };
   DropdownList* mPitchRangeDropdown{ nullptr };

   // ── master tempo (key lock) ──
   bool mMasterTempo{ false };
   Checkbox* mMasterTempoCheckbox{ nullptr };

   // ── BPM sync ──
   float mSampleBPM{ 120 };
   FloatSlider* mSampleBPMSlider{ nullptr };

   // ── nudge (phase-aware beat alignment) ──
   float mNudgeForwardHeld{ false };
   float mNudgeBackwardHeld{ false };
   ClickButton* mNudgeForwardButton{ nullptr };
   ClickButton* mNudgeBackwardButton{ nullptr };
   int mNudgeSamplesRemaining{ 0 };    // samples left to shift (positive=fwd, neg=bwd)

   // ── play speed (internal) ──
   float mPlaySpeed{ 1 };

    // ── waveform display ──
    ChannelBuffer mDrawBuffer{ 0 };
    bool mIsLoadingSample{ false };
      bool mScrubbingSample{ false };
      bool mOverviewDragged{ false };        // true if user dragged on overview beyond threshold
       int mOverviewClickSample{ 0 };         // sample position under cursor at click time
       float mScratchStartX{ 0 };
      float mScratchStartY{ 0 };
      int mScratchStartPlayPos{ 0 };
      float mLastMouseX{ 0 };               // last mouse X for scroll deltas
      float mLastMouseY{ 0 };               // last mouse Y for scroll deltas

   // ── cue points ──
   int mCuePoints[8]{ -1, -1, -1, -1, -1, -1, -1, -1 };  // sample positions (-1 = no cue)
   enum class CueMode { Jump = 0, Set, Edit, Delete };
   CueMode mCueMode{ CueMode::Jump };
   DropdownList* mCueModeDropdown{ nullptr };
   ClickButton* mHotCueButtons[8]{};
   ClickButton* mClearCuesButton{ nullptr };
    int mActiveHotCue{ 0 };
     bool mDraggingCue{ false };
     int mDragCueIndex{ -1 };

     // ── loop system ──
    int mLoopIn{ -1 };
    int mLoopOut{ -1 };
    bool mLoopActive{ false };
    int mLoopBeats{ 4 };
    ClickButton* mLoopInButton{ nullptr };
    ClickButton* mLoopOutButton{ nullptr };
    ClickButton* mLoopAuto1{ nullptr };
    ClickButton* mLoopAuto2{ nullptr };
    ClickButton* mLoopAuto4{ nullptr };
    ClickButton* mLoopAuto8{ nullptr };
    ClickButton* mLoopAuto16{ nullptr };
    ClickButton* mLoopClearButton{ nullptr };

   // ── auto BPM detection ──
   float mDetectedBPM{ 0 };
   float mBPMConfidence{ 0 };           // 0-1, how confident the detection is
   int mFirstBeatSample{ 0 };           // sample position of first detected beat

    // ── key detection (libkeyfinder) ──
    int mDetectedKey{ -1 };              // key_t enum value (-1 = unknown)
    std::string mKeyName{ "Unknown" };    // display string e.g. "C Major"

    // ── analysis file (.sjb) ──
    bool mAnalysisFileLoaded{ false };

    // ── key lock (time-stretch) ──
    // uses existing mMasterTempo checkbox + mPlaySpeed

    // ── zoom waveform ──
    float mScrollZoomBeats{ 8 };
    FloatSlider* mScrollZoomSlider{ nullptr };

    // ── internal ──
   SwitchAndRamp mSwitchAndRamp;
   NoteInputBuffer mNoteInputBuffer;
   ::ADSR mAdsr{ 10, 1, 1, 10 };
   std::string mErrorString;
};
