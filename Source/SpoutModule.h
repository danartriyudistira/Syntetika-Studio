#pragma once

#include "IDrawableModule.h"
#include "IVisualSource.h"
#include "Slider.h"
#include "Checkbox.h"
#include "TextEntry.h"
#include "DropdownList.h"

class PatchCableSource;
class VisualFBO;
struct SPOUTLIBRARY;

class SpoutModule : public IDrawableModule,
                    public IVisualSource,
                    public IFloatSliderListener,
                    public ITextEntryListener,
                    public IDropdownListener,
                    public IIntSliderListener
{
public:
   SpoutModule();
   virtual ~SpoutModule();
   static IDrawableModule* Create() { return new SpoutModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;

   void PostRender() override;
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}
   void TextEntryComplete(TextEntry* entry) override {}
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override {}
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override {}

    void LoadLayout(const ofxJSONElement& moduleInfo) override;
    void SaveLayout(ofxJSONElement& moduleInfo) override;
    void SetUpFromSaveData() override;

    void SaveState(FileStreamOut& out) override;
    void LoadState(FileStreamIn& in, int rev) override;
    int GetModuleSaveStateRev() const override { return 1; }

   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;
   bool IsEnabled() const override { return true; }

   VisualFBO* GetFBO() override;

private:
   enum {
      kOutputRes_Source = 0,
      kOutputRes_1080p = 1,
      kOutputRes_720p = 2,
      kOutputRes_540p = 3,
      kOutputRes_360p = 4,
      kOutputRes_Custom = 5
   };

   void DrawModule() override;
   void GetModuleDimensions(float& w, float& h) override;
   bool LoadSpoutLibrary();
   void UnloadSpoutLibrary();
   void UpdateSender(unsigned int w, unsigned int h);
   class IVisualSource* FindVisualSource();
   void GetOutputDimensions(int res, int& w, int& h);
   void EnsureOutputFBO(int w, int h);

   PatchCableSource* mInputCable{nullptr};
   PatchCableSource* mOutputCable{nullptr};

   class IVisualSource* mInputSource{nullptr};
   float mWidth{210};
   float mHeight{280};

   TextEntry*    mSenderNameEntry{nullptr};
   Checkbox*     mSpoutActiveCheckbox{nullptr};
   FloatSlider*  mPreviewScaleSlider{nullptr};
   DropdownList* mOutputResDropdown{nullptr};
   IntSlider*    mCustomWSlider{nullptr};
   IntSlider*    mCustomHSlider{nullptr};

   void*         mSpoutDll{nullptr};
   SPOUTLIBRARY* mSpoutHandle{nullptr};
   unsigned int  mSpoutWidth{0};
   unsigned int  mSpoutHeight{0};
   std::string   mSenderName{"Syntetika_Visual"};
   bool          mSpoutActive{true};
   float         mPreviewScale{0.7f};
   bool          mSpoutAvailable{false};
   int           mOutputRes{kOutputRes_1080p};
   int           mCustomW{1920};
   int           mCustomH{1080};
   VisualFBO*    mOutputFBO{nullptr};
   int           mOutputFBOW{0};
   int           mOutputFBOH{0};
};
