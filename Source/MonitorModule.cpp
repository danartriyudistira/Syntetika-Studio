#include "MonitorModule.h"
#include "VisualFBO.h"
#include "ModularSynth.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"

#include "juce_gui_basics/juce_gui_basics.h"

class MonitorModule::VisualOutputWindow : public juce::DocumentWindow
{
public:
   VisualOutputWindow(MonitorModule* owner, bool fullscreen = false)
      : juce::DocumentWindow("Visual Output",
                             juce::Colours::black,
                             fullscreen ? juce::DocumentWindow::TitleBarButtons(0)
                                        : (juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton))
      , mOwner(owner)
   {
      mImageComponent = new juce::ImageComponent("output");
      mImageComponent->setImage(juce::Image(), juce::RectanglePlacement::stretchToFit);
      setContentOwned(mImageComponent, true);

      if (fullscreen)
      {
         setUsingNativeTitleBar(false);
         setResizable(false, false);
      }
      else
      {
         setResizable(true, true);
         setUsingNativeTitleBar(true);
         setSize(640, 480);

         if (const auto* dpy = juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                 TheSynth->GetMainComponent()->getScreenBounds()))
         {
            const auto& mainMon = dpy->userArea;
            setTopLeftPosition(mainMon.getX() + mainMon.getWidth() / 4,
                                mainMon.getY() + mainMon.getHeight() / 8);
         }
      }

      setVisible(true);
      setWantsKeyboardFocus(true);
   }

   void closeButtonPressed() override
   {
      mOwner->CloseWindow();
   }

   bool keyPressed(const juce::KeyPress& key) override
   {
      if (key.getKeyCode() == juce::KeyPress::escapeKey)
      {
         mOwner->CloseWindow();
         return true;
      }
      return juce::DocumentWindow::keyPressed(key);
   }

   void SetImage(const juce::Image& img)
   {
      if (img.isValid())
         mImageComponent->setImage(img, juce::RectanglePlacement::stretchToFit);
   }

private:
   MonitorModule* mOwner;
   juce::ImageComponent* mImageComponent;
};

MonitorModule::MonitorModule()
{
}

MonitorModule::~MonitorModule()
{
   CloseOutputWindow();
}

void MonitorModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mInputCable = new PatchCableSource(this, kConnectionType_Special);
   AddPatchCableSource(mInputCable);

   mDisplayModeDropdown = new DropdownList(this, "mode", 3, 2, (int*)&mDisplayMode, 110);
   mDisplayModeDropdown->AddLabel("In Module", kDisplayMode_InModule);
   mDisplayModeDropdown->AddLabel("Popup", kDisplayMode_Popup);
   mDisplayModeDropdown->AddLabel("Fullscreen Main", kDisplayMode_FullscreenMain);
   mDisplayModeDropdown->AddLabel("Fullscreen Secondary", kDisplayMode_FullscreenSecondary);
   AddUIControl(mDisplayModeDropdown);
}

void MonitorModule::DrawModule()
{
   float headerH = 20;

   ofSetColor(20, 20, 30);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   ofPushStyle();
   ofSetColor(50, 50, 72);
   ofRect(0, 0, mWidth, headerH);
   ofPopStyle();

   mDisplayModeDropdown->Draw();

   ofPushStyle();
   ofSetColor(150, 150, 255);
   std::string modeText;
   switch (mDisplayMode)
   {
      case kDisplayMode_InModule: modeText = "In Module"; break;
      case kDisplayMode_Popup: modeText = "Popup"; break;
      case kDisplayMode_FullscreenMain: modeText = "Fullscreen Main"; break;
      case kDisplayMode_FullscreenSecondary: modeText = "Fullscreen Secondary"; break;
   }
   DrawTextNormal(modeText, mWidth - 80, (int)mHeight - 12, 11);
   ofPopStyle();

   // Method 1: check our input cable
   mSource = nullptr;
   if (mInputCable && !mInputCable->GetPatchCables().empty())
   {
      auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
      mSource = dynamic_cast<IVisualSource*>(target);
   }

   // Method 2: check for cables targeting THIS module (body drop)
   if (!mSource)
   {
      std::vector<IDrawableModule*> allModules;
      TheSynth->GetAllModules(allModules);
      IClickable* me = dynamic_cast<IClickable*>(this);
      for (auto* mod : allModules)
      {
         if (mod == this) continue;
         for (auto* source : mod->GetPatchCableSources())
         {
            for (auto* cable : source->GetPatchCables())
            {
               if (cable->GetTarget() && cable->GetTarget() == me)
               {
                  mSource = dynamic_cast<IVisualSource*>(mod);
                  if (mSource) break;
               }
            }
            if (mSource) break;
         }
         if (mSource) break;
      }
   }

   float contentTop = headerH + 3;
   float contentH = mHeight - contentTop - 3;

   if (mSource)
   {
      auto* fbo = mSource->GetFBO();
      if (fbo && fbo->IsValid())
      {
         fbo->ReleaseDisplayImage();
         fbo->Draw(3, contentTop, mWidth - 6, contentH);
      }
   }
   else
   {
      ofSetColor(60, 60, 60);
      ofFill();
      float cx = mWidth / 2;
      float cy = contentTop + contentH / 2;
      ofCircle(cx, cy, 20);
      ofSetColor(150, 150, 150);
      DrawTextNormal("No input", 3, (int)mHeight - 12, 12);
   }

}

void MonitorModule::PostRender()
{
   if (mWindow && mSource)
      CaptureAndSendFrame();
}

void MonitorModule::CaptureAndSendFrame()
{
   auto* fbo = mSource->GetFBO();
   if (!fbo || !fbo->IsValid())
      return;

   auto pixels = fbo->ReadPixels();
   int w = fbo->GetWidth();
   int h = fbo->GetHeight();

   if (pixels.empty())
      return;

   juce::Image img(juce::Image::ARGB, w, h, true);
   juce::Image::BitmapData bitmapData(img, juce::Image::BitmapData::writeOnly);

   for (int y = 0; y < h; ++y)
   {
      int srcY = h - 1 - y;
      auto* src = pixels.data() + srcY * w * 4;
      auto* dst = bitmapData.getLinePointer(y);
      for (int x = 0; x < w; ++x)
      {
         dst[x * 4 + 0] = src[x * 4 + 2];
         dst[x * 4 + 1] = src[x * 4 + 1];
         dst[x * 4 + 2] = src[x * 4 + 0];
         dst[x * 4 + 3] = src[x * 4 + 3];
      }
   }

   mWindow->SetImage(img);
}

void MonitorModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

void MonitorModule::Resize(float w, float h)
{
   mWidth = std::max(kMinWidth, w);
   mHeight = std::max(kMinHeight, h);
}

void MonitorModule::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (cableSource == mInputCable)
   {
      if (!mInputCable->GetPatchCables().empty())
      {
         auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
         mSource = dynamic_cast<IVisualSource*>(target);
      }
      else
      {
         mSource = nullptr;
      }
   }
}

void MonitorModule::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mDisplayModeDropdown)
   {
      CloseOutputWindow();
      OpenWindowForMode(mDisplayMode);
   }
}

void MonitorModule::OpenWindowForMode(int mode)
{
   if (mode == kDisplayMode_Popup)
   {
      mWindow = new VisualOutputWindow(this, false);
   }
   else if (mode == kDisplayMode_FullscreenMain)
   {
      mWindow = new VisualOutputWindow(this, true);
      if (mWindow)
         mWindow->setFullScreen(true);
   }
   else if (mode == kDisplayMode_FullscreenSecondary)
   {
      mWindow = new VisualOutputWindow(this, true);
      if (mWindow)
      {
         auto& displays = juce::Desktop::getInstance().getDisplays();
         if (displays.displays.size() >= 2)
         {
            auto& display = displays.displays.getReference(1);
            mWindow->setBounds(display.totalArea);
         }
         mWindow->setFullScreen(true);
      }
   }
}

void MonitorModule::CloseWindow()
{
   mDisplayMode = kDisplayMode_InModule;
   CloseOutputWindow();
}

void MonitorModule::CloseOutputWindow()
{
   if (mWindow)
   {
      mWindow->setVisible(false);
      delete mWindow;
      mWindow = nullptr;
   }
}

void MonitorModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
}

void MonitorModule::SaveLayout(ofxJSONElement& moduleInfo)
{
}

void MonitorModule::SetUpFromSaveData()
{
}

void MonitorModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mDisplayMode;
   out << mWidth;
   out << mHeight;
}

void MonitorModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   in >> mDisplayMode;
   in >> mWidth;
   in >> mHeight;
}
