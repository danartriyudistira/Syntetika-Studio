#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "Transport.h"
#include "PatchCableSource.h"
#include <mutex>

class SpatialSource;

class SpatialRender : public IAudioProcessor, public IDrawableModule, public IFloatSliderListener, public IDropdownListener, public IAudioPoller
{
public:
   SpatialRender();
   virtual ~SpatialRender();
   static IDrawableModule* Create() { return new SpatialRender(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void OnTransportAdvanced(float amount) override;
   void Process(double time) override;

   int GetNumSpeakers() const { return (int)mSpeakerPositions.size(); }
   int GetNumTargets() override { return 4 + (int)mSpeakerCables.size(); }

   void RegisterSource(SpatialSource* src);
   void UnregisterSource(SpatialSource* src);
   void NotifySourceMoved(SpatialSource* src);
   void AcceptSourceAudio(SpatialSource* src, float* buffer, int bufferSize);

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   bool IsEnabled() const override { return mEnabled; }
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

private:
   enum
   {
      kRow1Y = 2, kRow2Y = 20, kRow3Y = 38,
      kHeaderH = 55, kRowH = 18
   };

   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override;
   void OnClicked(float x, float y, bool right) override;
   bool MouseMoved(float x, float y) override;
   void MouseReleased() override;

   void RebuildSpeakers();
   void RebuildSpeakerCables();
   void RebuildDropdowns();
   void UpdateDropdownPositions();
   void UpdateCablePositions();
   void UpdateCableLabels();
   float GetCanvasHeight() const;
   ofVec2f PosToCanvas(float x, float y) const;
   ofVec2f CanvasToPos(float cx, float cy) const;

   struct RegisteredSource
   {
      SpatialSource* src{ nullptr };
      bool isInternal{ false };
      int internalChannel{ 0 };
      float audioBuffer[4096];
      int bufferSize{ 0 };
      float x{ 0.0f }, y{ 0.0f }, z{ 100.0f };
      bool hasAudio{ false };
   };

   int mNumSpeakers{ 2 };
   std::vector<ofVec2f> mSpeakerPositions;
   int mSpeakerChannels[16];
   std::vector<float> mSpeakerSPL;
   int mSpeakerSources[16];
   int mDirectSource{ -1 };
   int mBinauralSource{ -1 };
   float mRoomWidth{ 600.0f }, mRoomDepth{ 500.0f }, mRoomHeight{ 300.0f };
   float mUserX{ 0.0f }, mUserY{ -100.0f };
   bool mEnabled{ true };
   float mSPL{ 85.0f };

   bool mRoomEffectEnabled{ false };
   float mReverbMix{ 0.3f };
   static const int kReverbBufSize = 16384;
   float mCombL1[kReverbBufSize]{}, mCombL2[kReverbBufSize]{}, mApL[kReverbBufSize]{};
   float mCombR1[kReverbBufSize]{}, mCombR2[kReverbBufSize]{}, mApR[kReverbBufSize]{};
   int mReverbIdx{ 0 };
   static const int kMaxProcessBufSize = 4096;
   float mDirectL[kMaxProcessBufSize]{}, mDirectR[kMaxProcessBufSize]{};
   float mBinauralL[kMaxProcessBufSize]{}, mBinauralR[kMaxProcessBufSize]{};
   float mSpeakerSignal[16][kMaxProcessBufSize]{};

   std::vector<RegisteredSource> mSources;
   mutable std::recursive_mutex mSourceMutex;

   FloatSlider* mRoomWidthSlider{ nullptr };
   FloatSlider* mRoomDepthSlider{ nullptr };
   FloatSlider* mRoomHeightSlider{ nullptr };
   DropdownList* mSpeakerCountSelector{ nullptr };
   FloatSlider* mUserXSlider{ nullptr };
   FloatSlider* mUserYSlider{ nullptr };
   Checkbox* mRoomEffectCheckbox{ nullptr };
   FloatSlider* mReverbMixSlider{ nullptr };
   FloatSlider* mSPLSlider{ nullptr };

   DropdownList* mDirectSourceSelector{ nullptr };
   DropdownList* mBinauralSourceSelector{ nullptr };
   std::vector<DropdownList*> mSpeakerSourceSelectors;
   std::vector<DropdownList*> mSpeakerChannelSelectors;

   PatchCableSource* mDirectRCable{ nullptr };
   PatchCableSource* mBinauralLCable{ nullptr };
   PatchCableSource* mBinauralRCable{ nullptr };
   std::vector<PatchCableSource*> mSpeakerCables;

   int mDragTarget{ -1 };
   int mHoverTarget{ -1 };
   bool mDragging{ false };
   ofVec2f mDragOffset;

   float mModuleWidth{ 520 };
   float mModuleHeight{ 520 };
};
