#pragma once

#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "SpatialDataSource.h"
#include "Slider.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "PatchCableSource.h"

class VisualFBO;

class EclipSpatialRender : public IAudioProcessor,
                           public IDrawableModule,
                           public IVisualSource,
                           public IFloatSliderListener,
                           public IDropdownListener
{
public:
   enum SpeakerLayout
   {
      kSpeakerLayout_Stereo = 0,
      kSpeakerLayout_5_1,
      kSpeakerLayout_7_1,
      kSpeakerLayout_5_1_2,
      kSpeakerLayout_5_1_4,
      kSpeakerLayout_7_1_4,
      kSpeakerLayout_Ambisonics1,
      kSpeakerLayout_Ambisonics3,
   };

   enum AnimMode
   {
      kAnim_Static = 0,
      kAnim_Orbit,
      kAnim_LFO_X,
      kAnim_LFO_XY,
      kAnim_LFO_XYZ,
   };

   EclipSpatialRender();
   virtual ~EclipSpatialRender();
   static IDrawableModule* Create() { return new EclipSpatialRender(); }
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

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 3; }

   VisualFBO* GetFBO() override;
   void PostRender() override;

   bool IsEnabled() const override { return mEnabled; }

   struct AnimState
   {
      AnimMode mMode{ kAnim_Static };
      float    mRate{ 0.5f };
      float    mDepth{ 0.5f };
      float    mPhase{ 0 };
   };

   struct HRTFState
   {
      float delayLineL[256]{};
      float delayLineR[256]{};
      int   delayWritePos{ 0 };
   };

   struct ObjectData
   {
      float mX{ 0 };
      float mY{ 0 };
      float mZ{ 0 };
      float mVolume{ 1 };
      bool  mMuted{ false };
      bool  mActive{ false };
      int   mColorHue{ 0 };
      float mOcclusion{ 0 };
      AnimState mAnim;
      HRTFState mHRTF;
      std::vector<float> mAudioBuffer;
   };

   void SetObjectAudio(int index, const float* left, const float* right, int bufferSize,
                       float x, float y, float z, float volume);

   void SetObjectProperties(int index, int colorHue, float occlusion,
                            AnimMode animMode, float animRate, float animDepth);

   int GetNumActiveObjects() const;
   const ObjectData* GetObject(int index) const;

   int GetNumSpatialSources() const;
   bool GetSpatialSourceInfo(int index, SpatialSourceInfo& out) const;
   bool GetSpatialRoomInfo(SpatialRoomInfo& out) const;
   int GetNumSpeakers() const;
   bool GetSpeakerInfo(int index, SpatialSpeakerInfo& out) const;

private:
   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override { w = mWidth; h = mHeight; }

   int GetNumOutputChannels() const;
   void SpatializeObject(int index, float** outputs, int numChannels, int bufferSize);
   void ProcessHRTF(int index, float** outputs, int numChannels, int bufferSize);
   void ProcessReverb(float* outL, float* outR, int bufferSize);
   ofColor GetObjectColor(const ObjectData& obj) const;
   void UpdateAnimations(double time);

   static constexpr int kMaxObjects = 8;
   static constexpr int kReverbMaxDelay = 24000;

   std::vector<ObjectData> mObjects;

   float  mWidth{ 400 };
   float  mHeight{ 400 };
   float  mMasterVolume{ 1 };
   int    mSpeakerLayout{ kSpeakerLayout_Stereo };

   // Room acoustics
   float  mDistanceMin{ 0.1f };
   float  mDistanceMax{ 1.0f };
   float  mRolloff{ 1.0f };
   float  mReverbMix{ 0 };
   float  mReverbDecay{ 0.4f };
   float  mReverbSize{ 0.5f };
   float  mReverbDamping{ 0.3f };

   // Reverb state
   float  mReverbDelayL[kReverbMaxDelay]{};
   float  mReverbDelayR[kReverbMaxDelay]{};
   int    mReverbWritePos{ 0 };

   // HRTF
   bool         mHRTFEnabled{ false };
   float        mHeadRadius{ 8.75f };
   int          mHRTFQuality{ 2 };
   Checkbox*    mHRTFEnabledCheckbox{ nullptr };
   DropdownList* mHRTFQualityDropdown{ nullptr };
   FloatSlider* mHeadRadiusSlider{ nullptr };

   static constexpr int kMaxProcessBufSize = 4096;
   float mBinauralL[kMaxProcessBufSize]{};
   float mBinauralR[kMaxProcessBufSize]{};

   VisualFBO*   mFBO{ nullptr };
   FloatSlider* mMasterVolumeSlider{ nullptr };
   DropdownList* mSpeakerLayoutDropdown{ nullptr };

   // Room controls
   FloatSlider* mDistanceMinSlider{ nullptr };
   FloatSlider* mDistanceMaxSlider{ nullptr };
   FloatSlider* mRolloffSlider{ nullptr };
   FloatSlider* mReverbMixSlider{ nullptr };
   FloatSlider* mReverbDecaySlider{ nullptr };
   FloatSlider* mReverbSizeSlider{ nullptr };
   FloatSlider* mReverbDampingSlider{ nullptr };

   float mRoomControlWidth{ 250 };
   float mRoomControlHeight{ 80 };

   PatchCableSource* mVisualCable{ nullptr };
};
