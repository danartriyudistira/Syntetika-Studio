#include "SpoutModule.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"
#include "OpenFrameworksPort.h"
#include "UIControlMacros.h"
#include "VisualFBO.h"
#include "IVisualSource.h"
#include "PatchCableSource.h"

#include "SpoutLibrary.h"

#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif

SpoutModule::SpoutModule()
{
}

SpoutModule::~SpoutModule()
{
   delete mOutputFBO;
   UnloadSpoutLibrary();
}

void SpoutModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mInputCable = new PatchCableSource(this, kConnectionType_Special);
   AddPatchCableSource(mInputCable);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   AddPatchCableSource(mOutputCable);

   UIBLOCK0();
   CHECKBOX(mSpoutActiveCheckbox, "active", &mSpoutActive);
   UIBLOCK_SHIFTRIGHT();
   TEXTENTRY(mSenderNameEntry, "name", 16, &mSenderName);
   ENDUIBLOCK0();

   UIBLOCK(3, 35, 190);
   FLOATSLIDER(mPreviewScaleSlider, "preview scale", &mPreviewScale, 0.1f, 1.0f);
   ENDUIBLOCK0();

   UIBLOCK(3, 72, 200);
   DROPDOWN(mOutputResDropdown, "output res", &mOutputRes, 130);
   mOutputResDropdown->AddLabel("source", kOutputRes_Source);
   mOutputResDropdown->AddLabel("1920x1080", kOutputRes_1080p);
   mOutputResDropdown->AddLabel("1280x720", kOutputRes_720p);
   mOutputResDropdown->AddLabel("960x540", kOutputRes_540p);
   mOutputResDropdown->AddLabel("640x360", kOutputRes_360p);
   mOutputResDropdown->AddLabel("custom", kOutputRes_Custom);
   ENDUIBLOCK0();

   UIBLOCK(3, 100, 190);
   INTSLIDER(mCustomWSlider, "out W", &mCustomW, 64, 3840);
   UIBLOCK_NEWLINE();
   INTSLIDER(mCustomHSlider, "out H", &mCustomH, 64, 2160);
   ENDUIBLOCK0();
}

void SpoutModule::Init()
{
   IDrawableModule::Init();

   LoadSpoutLibrary();
}

void SpoutModule::Poll()
{
   IDrawableModule::Poll();
}

bool SpoutModule::LoadSpoutLibrary()
{
   if (mSpoutHandle)
      return true;

   mSpoutDll = LoadLibraryA("SpoutLibrary.dll");
   if (!mSpoutDll)
      return false;

   typedef SPOUTHANDLE(WINAPI * GetSpoutFunc)();
   auto getSpout = (GetSpoutFunc)GetProcAddress((HMODULE)mSpoutDll, "GetSpout");
   if (!getSpout)
   {
      FreeLibrary((HMODULE)mSpoutDll);
      mSpoutDll = nullptr;
      return false;
   }

   mSpoutHandle = getSpout();
   if (mSpoutHandle)
   {
      mSenderName = mSenderNameEntry->GetText();
      mSpoutHandle->SetSenderName(mSenderName.c_str());
      mSpoutAvailable = true;
      return true;
   }

   return false;
}

void SpoutModule::UnloadSpoutLibrary()
{
   if (mSpoutHandle)
   {
      mSpoutHandle->ReleaseSender();
      mSpoutHandle->Release();
      mSpoutHandle = nullptr;
   }
   if (mSpoutDll)
   {
      FreeLibrary((HMODULE)mSpoutDll);
      mSpoutDll = nullptr;
   }
   mSpoutAvailable = false;
}

IVisualSource* SpoutModule::FindVisualSource()
{
   if (mInputCable && !mInputCable->GetPatchCables().empty())
   {
      auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
      IVisualSource* src = dynamic_cast<IVisualSource*>(target);
      if (src)
         return src;
   }

   std::vector<IDrawableModule*> allModules;
   TheSynth->GetAllModules(allModules);
   IClickable* me = dynamic_cast<IClickable*>(this);
   for (auto* mod : allModules)
   {
      if (mod == this)
         continue;
      for (auto* source : mod->GetPatchCableSources())
      {
         for (auto* cable : source->GetPatchCables())
         {
            if (cable->GetTarget() && cable->GetTarget() == me)
            {
               IVisualSource* src = dynamic_cast<IVisualSource*>(mod);
               if (src)
                  return src;
            }
         }
      }
   }
   return nullptr;
}

void SpoutModule::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (cableSource == mInputCable)
      mInputSource = FindVisualSource();
}

void SpoutModule::PostRender()
{
   if (!mSpoutAvailable || !mSpoutHandle || !mSpoutActive)
      return;

   IVisualSource* src = FindVisualSource();
   if (!src)
      return;

   VisualFBO* srcFbo = src->GetFBO();
   if (!srcFbo || !srcFbo->IsValid())
      return;

   unsigned int srcW = (unsigned int)srcFbo->GetWidth();
   unsigned int srcH = (unsigned int)srcFbo->GetHeight();
   if (srcW < 2 || srcH < 2)
      return;

   unsigned int outW, outH;

   if (mOutputRes == kOutputRes_Custom)
   {
      outW = (unsigned int)mCustomW;
      outH = (unsigned int)mCustomH;
   }
   else if (mOutputRes == kOutputRes_Source)
   {
      outW = srcW;
      outH = srcH;
   }
   else
   {
      int resW, resH;
      GetOutputDimensions(mOutputRes, resW, resH);
      outW = (unsigned int)resW;
      outH = (unsigned int)resH;
   }

   EnsureOutputFBO(outW, outH);
   if (!mOutputFBO || !mOutputFBO->IsValid())
      return;

   srcFbo->ReleaseDisplayImage();
   mOutputFBO->Bind();
   ofPushStyle();
   ofSetColor(255, 255, 255);
   ofFill();
   srcFbo->Draw(0, 0, (float)outW, (float)outH);
   ofPopStyle();
   mOutputFBO->Unbind();
   srcFbo->ReleaseDisplayImage();

   UpdateSender(outW, outH);
   mSpoutHandle->SendTexture(mOutputFBO->GetTexture(), GL_TEXTURE_2D, outW, outH, false);
}

VisualFBO* SpoutModule::GetFBO()
{
   return mOutputFBO;
}

void SpoutModule::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   ofPushStyle();
   ofSetColor(20, 20, 30);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   ofSetColor(50, 50, 72);
   ofRect(0, 0, mWidth, 20);
   ofPopStyle();

   ofPushStyle();
   ofSetColor(200, 200, 255);
   DrawTextNormal("Spout Output", 5, 13, 11);
   ofPopStyle();

   mSpoutActiveCheckbox->Draw();
   mSenderNameEntry->Draw();
   mPreviewScaleSlider->Draw();
   mOutputResDropdown->Draw();
   mCustomWSlider->Draw();
   mCustomHSlider->Draw();

   IVisualSource* src = FindVisualSource();

   ofPushStyle();
   ofSetColor(40, 40, 60);
   ofSetLineWidth(1);
   ofLine(3, 132, mWidth - 3, 132);

   float previewX = 5;
   float previewY = 140;
   float previewW = mWidth - 10;
   float previewH = mHeight - previewY - 26;

   ofSetColor(10, 10, 18);
   ofFill();
   ofRect(previewX - 1, previewY - 1, previewW + 2, previewH + 2);

   if (src)
   {
      VisualFBO* fbo = src->GetFBO();
      if (fbo && fbo->IsValid())
         fbo->Draw(previewX, previewY, previewW, previewH);
   }
   else
   {
      ofSetColor(60, 60, 80);
      float cx = previewX + previewW * 0.5f;
      float cy = previewY + previewH * 0.5f;
      ofSetLineWidth(1);
      ofNoFill();
      float r = ofClamp(previewH * 0.12f, 10.0f, 28.0f);
      ofCircle(cx, cy, r);
      ofLine(cx - r * 0.6f, cy, cx + r * 0.6f, cy);
      ofLine(cx, cy - r * 0.6f, cx, cy + r * 0.6f);
      ofFill();
   }
   ofPopStyle();

   ofPushStyle();
   float infoY = mHeight - 20;

   if (!mSpoutAvailable)
   {
      ofSetColor(255, 100, 100);
      DrawTextNormal("DLL not found", 5, infoY, 10);
   }
   else if (!mSpoutActive)
   {
      ofSetColor(180, 180, 120);
      DrawTextNormal("OFF", 5, infoY, 10);
   }
   else if (src)
   {
      ofSetColor(120, 255, 120);
      DrawTextNormal("ON", 5, infoY, 10);

      char dims[64];
      if (mOutputRes == kOutputRes_Custom)
         snprintf(dims, sizeof(dims), "%dx%d cust", mCustomW, mCustomH);
      else if (mOutputRes == kOutputRes_Source)
         snprintf(dims, sizeof(dims), "%.0fx%.0f src", (float)src->GetFBO()->GetWidth(), (float)src->GetFBO()->GetHeight());
      else
      {
         int rw, rh;
         GetOutputDimensions(mOutputRes, rw, rh);
         snprintf(dims, sizeof(dims), "%dx%d", rw, rh);
      }
      ofSetColor(160, 160, 200);
      DrawTextNormal(dims, 30, infoY, 10);
   }
   else
   {
      ofSetColor(180, 180, 120);
      DrawTextNormal("No source", 5, infoY, 10);
   }
   ofPopStyle();
}

void SpoutModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

void SpoutModule::Resize(float w, float h)
{
   mWidth = (std::max)(w, 210.0f);
   mHeight = (std::max)(h, 250.0f);
}

void SpoutModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
#pragma push_macro("LoadString")
#undef LoadString
   mModuleSaveData.LoadString("sendername", moduleInfo, "Syntetika_Visual");
#pragma pop_macro("LoadString")
   mModuleSaveData.LoadBool("spoutactive", moduleInfo, true);
   mModuleSaveData.LoadFloat("previewscale", moduleInfo, 0.7f);
   mModuleSaveData.LoadInt("outputres", moduleInfo, kOutputRes_1080p, 0, 5);
   mModuleSaveData.LoadInt("customw", moduleInfo, 1920, 64, 3840);
   mModuleSaveData.LoadInt("customh", moduleInfo, 1080, 64, 2160);

   SetUpFromSaveData();
}

void SpoutModule::SaveLayout(ofxJSONElement& moduleInfo)
{
   mSenderName = mSenderNameEntry->GetText();
   moduleInfo["sendername"] = mSenderName;
   moduleInfo["spoutactive"] = mSpoutActive;
   moduleInfo["previewscale"] = mPreviewScale;
   moduleInfo["outputres"] = mOutputRes;
   moduleInfo["customw"] = mCustomW;
   moduleInfo["customh"] = mCustomH;
}

void SpoutModule::SetUpFromSaveData()
{
    mSenderName = mModuleSaveData.GetString("sendername");
    mSpoutActive = mModuleSaveData.GetBool("spoutactive");
    mPreviewScale = mModuleSaveData.GetFloat("previewscale");
    mOutputRes = mModuleSaveData.GetInt("outputres");
    mCustomW = mModuleSaveData.GetInt("customw");
    mCustomH = mModuleSaveData.GetInt("customh");
}

void SpoutModule::SaveState(FileStreamOut& out)
{
    IDrawableModule::SaveState(out);
    out << mSenderName;
    out << mSpoutActive;
    out << mPreviewScale;
    out << mOutputRes;
    out << mCustomW;
    out << mCustomH;
}

void SpoutModule::LoadState(FileStreamIn& in, int rev)
{
    IDrawableModule::LoadState(in, rev);
    if (rev < 1) return;
    in >> mSenderName;
    in >> mSpoutActive;
    in >> mPreviewScale;
    in >> mOutputRes;
    in >> mCustomW;
    in >> mCustomH;
}

void SpoutModule::UpdateSender(unsigned int w, unsigned int h)
{
   if (!mSpoutHandle)
      return;

   const char* currentName = mSenderNameEntry->GetText();
   bool nameChanged = (mSenderName != currentName);

   if (nameChanged)
   {
      mSenderName = currentName;
      mSpoutHandle->SetSenderName(mSenderName.c_str());
   }

   if (nameChanged || w != mSpoutWidth || h != mSpoutHeight)
   {
      mSpoutHandle->UpdateSender(mSenderName.c_str(), w, h);
      mSpoutWidth = w;
      mSpoutHeight = h;
   }
}

void SpoutModule::GetOutputDimensions(int res, int& w, int& h)
{
   switch (res)
   {
   case kOutputRes_1080p:   w = 1920; h = 1080; break;
   case kOutputRes_720p:    w = 1280; h = 720; break;
   case kOutputRes_540p:    w = 960; h = 540; break;
   case kOutputRes_360p:    w = 640; h = 360; break;
   default:                 w = 0; h = 0; break;
   }
}

void SpoutModule::EnsureOutputFBO(int w, int h)
{
   if (mOutputFBO && mOutputFBO->IsValid() &&
       mOutputFBOW == w && mOutputFBOH == h)
      return;

   delete mOutputFBO;
   mOutputFBO = new VisualFBO();
   mOutputFBO->Create(w, h);
   mOutputFBOW = w;
   mOutputFBOH = h;
}
