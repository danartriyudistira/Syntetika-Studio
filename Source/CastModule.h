#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "ClickButton.h"
#include "TextEntry.h"
#include "DropdownList.h"
#include "Slider.h"
#include "SSDPDiscoverer.h"

class CastChannel;
class CastHTTPServer;

class CastModule : public IDrawableModule,
                   public IVisualSource,
                   public IButtonListener,
                   public ITextEntryListener,
                   public IDropdownListener,
                   public IFloatSliderListener
{
public:
   CastModule();
   virtual ~CastModule();
   static IDrawableModule* Create() { return new CastModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;

   void PostRender() override;
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}

   VisualFBO* GetFBO() override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   bool IsResizable() const override { return false; }
   void Resize(float w, float h) override;
   bool IsEnabled() const override { return true; }

private:
   enum OutputRes
   {
      kOutputRes_Source,
      kOutputRes_1080p,
      kOutputRes_720p,
      kOutputRes_480p,
      kOutputRes_360p
   };

   enum CastState
   {
      kCastState_Idle,
      kCastState_Connecting,
      kCastState_Streaming,
      kCastState_Error
   };

   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override;
   IVisualSource* FindVisualSource();
   void StartStream();
   void StopStream();
   void GetOutputDimensions(int res, int& w, int& h);
   void DiscoverDevices();
   void PopulateDeviceDropdown();

   PatchCableSource* mInputCable{ nullptr };

   IVisualSource* mSource{ nullptr };

   TextEntry* mTargetIPEntry{ nullptr };
   TextEntry* mTargetPortEntry{ nullptr };
   DropdownList* mDeviceDropdown{ nullptr };
   ClickButton* mDiscoverButton{ nullptr };
   ClickButton* mStartButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   DropdownList* mOutputResDropdown{ nullptr };
   FloatSlider* mFramerateSlider{ nullptr };

   std::string mTargetIP{ "192.168.1.100" };
    std::string mTargetPort{ "8009" };
   int mOutputRes{ kOutputRes_720p };
   float mFramerate{ 30 };
   bool mStreaming{ false };
   double mLastFrameTime{ 0 };

   FILE* mFFmpegPipe{ nullptr };

   CastChannel* mCastChannel{ nullptr };
   CastHTTPServer* mHttpServer{ nullptr };
   CastState mCastState{ kCastState_Idle };
   std::string mStreamDir;
   std::string mCastError;

   std::vector<SSDPDevice> mDiscoveredDevices;
   int mSelectedDeviceIndex{ -1 };

   float mWidth{ 320 };
   float mHeight{ 300 };
};
