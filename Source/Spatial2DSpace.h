#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "ClickButton.h"
#include "Transport.h"

class SpatialObject;

class Spatial2DSpace : public IAudioProcessor, public IDrawableModule, public IFloatSliderListener, public IDropdownListener, public IButtonListener, public IAudioPoller
{
public:
   Spatial2DSpace();
   virtual ~Spatial2DSpace();
   static IDrawableModule* Create() { return new Spatial2DSpace(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;

   void Init() override;
   void OnTransportAdvanced(float amount) override;
   void Process(double time) override;

   int GetNumSpeakers() const { return (int)mSpeakerPositions.size(); }
   const std::vector<ofVec2f>& GetSpeakerPositions() const { return mSpeakerPositions; }
   ofVec2f GetUserPosition() const { return ofVec2f(mUserX, mUserY); }
   float GetRoomWidth() const { return mRoomWidth; }
   float GetRoomDepth() const { return mRoomDepth; }
   float GetRoomHeight() const { return mRoomHeight; }
   int GetOutputMode() const { return mOutputMode; }
   bool GetShowVirtualSpeakers() const { return mShowVirtualSpeakers; }
   float GetVirtualSpeakerSPL() const { return mVirtualSpeakerSPL; }

   void RegisterObject(SpatialObject* obj) { mObjects.push_back(obj); }
   void UnregisterObject(SpatialObject* obj);
   const std::vector<SpatialObject*>& GetObjects() const { return mObjects; }

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void ButtonClicked(ClickButton* button, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   bool IsEnabled() const override { return mEnabled; }
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override;
   void OnClicked(float x, float y, bool right) override;
   bool MouseMoved(float x, float y) override;
   void MouseReleased() override;

   void RebuildSpeakers();
   ofVec2f PosToCanvas(float x, float y) const;
   ofVec2f CanvasToPos(float cx, float cy) const;

   enum DragTarget
   {
      kDrag_None = -1,
      kDrag_User = -2,
      kDrag_SpeakerStart = 0
   };

   int mNumSpeakers{ 2 };
   std::vector<ofVec2f> mSpeakerPositions;
   float mRoomWidth{ 600.0f };
   float mRoomDepth{ 500.0f };
   float mRoomHeight{ 300.0f };
   float mUserX{ 0.0f };
   float mUserY{ -100.0f };
   bool mEnabled{ true };

   int mOutputMode{ 0 };

   bool mShowVirtualSpeakers{ false };
   float mVirtualSpeakerSPL{ 85.0f };

   bool mRoomEffectEnabled{ false };
   float mReverbMix{ 0.3f };
   static const int kReverbBufSize = 16384;
   float mCombL1[kReverbBufSize]{};
   float mCombL2[kReverbBufSize]{};
   float mApL[kReverbBufSize]{};
   float mCombR1[kReverbBufSize]{};
   float mCombR2[kReverbBufSize]{};
   float mApR[kReverbBufSize]{};
   int mReverbIdx{ 0 };

   std::vector<SpatialObject*> mObjects;

   FloatSlider* mRoomWidthSlider{ nullptr };
   FloatSlider* mRoomDepthSlider{ nullptr };
   FloatSlider* mRoomHeightSlider{ nullptr };
   DropdownList* mSpeakerCountSelector{ nullptr };
   DropdownList* mOutputModeSelector{ nullptr };
   FloatSlider* mUserXSlider{ nullptr };
   FloatSlider* mUserYSlider{ nullptr };
   Checkbox* mRoomEffectCheckbox{ nullptr };
   FloatSlider* mReverbMixSlider{ nullptr };
   Checkbox* mShowVirtualSpeakersCheckbox{ nullptr };
   FloatSlider* mVirtualSpeakerSPLSlider{ nullptr };

   int mDragTarget{ kDrag_None };
   int mHoverTarget{ kDrag_None };
   bool mDragging{ false };
   ofVec2f mDragOffset;

   float mModuleWidth{ 520 };
   float mModuleHeight{ 520 };
};
