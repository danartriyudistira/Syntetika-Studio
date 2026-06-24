#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"
#include "DropdownList.h"

class MonitorModule : public IDrawableModule, public IDropdownListener
{
public:
   MonitorModule();
   ~MonitorModule();

   static IDrawableModule* Create() { return new MonitorModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void DrawModule() override;
   void PostRender() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   void GetModuleDimensions(float& w, float& h) override;

   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   bool IsEnabled() const override { return true; }

   void CloseWindow();

private:
   enum DisplayMode
   {
      kDisplayMode_InModule,
      kDisplayMode_Popup,
      kDisplayMode_FullscreenMain,
      kDisplayMode_FullscreenSecondary
   };

   void OpenWindowForMode(int mode);
   void CloseOutputWindow();
   void CaptureAndSendFrame();

   IVisualSource* mSource{ nullptr };
   PatchCableSource* mInputCable{ nullptr };

   DropdownList* mDisplayModeDropdown{ nullptr };
   int mDisplayMode{ kDisplayMode_InModule };

   class VisualOutputWindow;
   VisualOutputWindow* mWindow{ nullptr };

   float mWidth{ 320 };
   float mHeight{ 300 };

   static constexpr float kMinWidth = 200;
   static constexpr float kMinHeight = 150;

};
