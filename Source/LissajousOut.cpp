#include "LissajousOut.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "OpenFrameworksPort.h"
#include "VisualFBO.h"
#include "PatchCableSource.h"

LissajousOut::LissajousOut()
: IAudioProcessor(gBufferSize)
{
   for (int i = 0; i < LISSAJOUSOUT_BUFFER_SIZE; ++i)
      mBuffer[i] = { 0, 0 };
}

LissajousOut::~LissajousOut()
{
   delete mFBO;
}

void LissajousOut::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mVisualCable = new PatchCableSource(this, kConnectionType_Special);
   mVisualCable->SetColor(IDrawableModule::GetColor(kModuleCategory_Visual));
   mVisualCable->SetManualSide(PatchCableSource::Side::kRight);
   mVisualCable->SetManualPosition(mWidth - 8, mHeight / 2);
   AddPatchCableSource(mVisualCable);

   UIBLOCK0();
   FLOATSLIDER(mScaleSlider, "scale", &mScale, 0.5f, 4);
   FLOATSLIDER(mZoomSlider, "zoom", &mZoom, 0.2f, 5);
   FLOATSLIDER(mIntensitySlider, "intensity", &mIntensity, 0, 3);
   FLOATSLIDER(mLineWidthSlider, "line width", &mLineWidth, 0.5f, 8);
   FLOATSLIDER(mDecaySlider, "decay", &mDecay, 0.5f, 6);
   DROPDOWN(mColorDropdown, "color", &mColorSelect, 45);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mBackgroundCheckbox, "background", &mShowBackground);
   ENDUIBLOCK(mHeight);

   mColorDropdown->AddLabel("green", 0);
   mColorDropdown->AddLabel("amber", 1);
   mColorDropdown->AddLabel("blue", 2);
   mColorDropdown->AddLabel("white", 3);
   mColorDropdown->AddLabel("red", 4);
}

void LissajousOut::Process(double time)
{
   PROFILER(LissajousOut);

   SyncBuffers();

   int bufferSize = GetBuffer()->BufferSize();
   IAudioReceiver* target = GetTarget();
   if (target)
   {
      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         Add(target->GetBuffer()->GetChannel(ch), GetBuffer()->GetChannel(ch), bufferSize);
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize(), ch);
      }
   }

   if (mEnabled)
   {
      int secondChannel = (GetBuffer()->NumActiveChannels() == 1) ? 0 : 1;

      for (int i = 0; i < bufferSize; ++i)
      {
         mBuffer[mWritePos].x = GetBuffer()->GetChannel(0)[i];
         mBuffer[mWritePos].y = GetBuffer()->GetChannel(secondChannel)[i];
         mWritePos = (mWritePos + 1) % LISSAJOUSOUT_BUFFER_SIZE;
         if (mNumStored < LISSAJOUSOUT_BUFFER_SIZE)
            ++mNumStored;
      }
   }

   GetBuffer()->Reset();
}

void LissajousOut::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   if (mFBO && mFBO->IsValid())
      mFBO->Draw(0, 0, mWidth, mHeight);

   mScaleSlider->Draw();
   mZoomSlider->Draw();
   mIntensitySlider->Draw();
   mLineWidthSlider->Draw();
   mDecaySlider->Draw();
   mColorDropdown->Draw();
   mBackgroundCheckbox->Draw();
}

void LissajousOut::PostRender()
{
   if (!mEnabled || mNumStored < 2 || mWidth < 10 || mHeight < 10)
      return;

   if (!mFBO || !mFBO->IsValid() ||
       mFBO->GetWidth() != (int)mWidth ||
       mFBO->GetHeight() != (int)mHeight)
   {
      delete mFBO;
      mFBO = new VisualFBO();
       mFBO->Create(std::max(64, (int)mWidth), std::max(64, (int)mHeight));
   }

   if (!mFBO || !mFBO->IsValid())
      return;

   float cr, cg, cb;
   switch (mColorSelect)
   {
   case 0: cr = 0; cg = 1;    cb = 0;    break;
   case 1: cr = 1; cg = 0.8f; cb = 0;    break;
   case 2: cr = 0; cg = 0.5f; cb = 1;    break;
   case 3: cr = 1; cg = 1;    cb = 1;    break;
   case 4: cr = 1; cg = 0;    cb = 0;    break;
   default: cr = 0; cg = 1;    cb = 0;
   }

   mFBO->Bind();

    if (mShowBackground)
    {
       ofPushStyle();
       ofSetColor(0, 0, 0, 255);
       ofFill();
       ofRect(0, 0, mWidth, mHeight);

       ofSetColor(20, 20, 30);
       ofSetLineWidth(0.5f);

       float step = mWidth / 16.0f;
       for (float x = 0; x <= mWidth; x += step)
          ofLine(x, 0, x, mHeight);
       for (float y = 0; y <= mHeight; y += step)
          ofLine(0, y, mWidth, y);

       ofSetColor(40, 40, 60);
       ofSetLineWidth(1);
       ofLine(mWidth * 0.5f, 0, mWidth * 0.5f, mHeight);
       ofLine(0, mHeight * 0.5f, mWidth, mHeight * 0.5f);

       ofPopStyle();
    }
    else
    {
       ofSetColor(0, 0, 0, 255);
       ofFill();
       ofRect(0, 0, mWidth, mHeight);
    }

   int numDraw = ofClamp(mNumStored, 2, LISSAJOUSOUT_BUFFER_SIZE);
   int startIdx = (mWritePos - numDraw + LISSAJOUSOUT_BUFFER_SIZE) % LISSAJOUSOUT_BUFFER_SIZE;
   float halfW = mWidth / 2.0f;
   float halfH = mHeight / 2.0f;
   float effScale = mScale / mZoom;
   float scaleX = mWidth * effScale;
   float scaleY = mHeight * effScale;
   int r = (int)(cr * 255);
   int g = (int)(cg * 255);
   int b = (int)(cb * 255);

   float fadeLUT[LISSAJOUSOUT_BUFFER_SIZE];
   for (int i = 0; i < numDraw; ++i)
   {
      float age = (float)(numDraw - 1 - i) / (float)(numDraw - 1);
      fadeLUT[i] = powf(1.0f - age, mDecay);
   }

   ofSetLineWidth(mLineWidth);
   ofBeginShape();
   for (int i = 0; i < numDraw; ++i)
   {
      int idx = (startIdx + i) % LISSAJOUSOUT_BUFFER_SIZE;
      float alpha = fadeLUT[i] * mIntensity;
      ofSetColor(r, g, b, (int)(ofClamp(alpha, 0.0f, 1.0f) * 255));
      ofVertex(halfW + mBuffer[idx].x * scaleX, halfH + mBuffer[idx].y * scaleY);
   }
   ofEndShape(false);

   mFBO->Unbind();
}

VisualFBO* LissajousOut::GetFBO()
{
   return mFBO;
}

void LissajousOut::Resize(float w, float h)
{
   mWidth = w;
   mHeight = h;
   if (mVisualCable)
      mVisualCable->SetManualPosition(mWidth - 8, mHeight / 2);
}

void LissajousOut::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("width", moduleInfo, 500);
   mModuleSaveData.LoadFloat("height", moduleInfo, 500);
   mModuleSaveData.LoadFloat("zoom", moduleInfo, 1.0f);

   SetUpFromSaveData();
}

void LissajousOut::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
   moduleInfo["zoom"] = mZoom;
}

void LissajousOut::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mWidth = mModuleSaveData.GetFloat("width");
   mHeight = mModuleSaveData.GetFloat("height");
   mZoom = mModuleSaveData.GetFloat("zoom");
}

void LissajousOut::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mScale;
   out << mZoom;
   out << mIntensity;
   out << mLineWidth;
   out << mDecay;
   out << mColorSelect;
   out << mShowBackground;
}

void LissajousOut::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev < 1) return;
   in >> mScale;
   in >> mZoom;
   in >> mIntensity;
   in >> mLineWidth;
   in >> mDecay;
   in >> mColorSelect;
   in >> mShowBackground;
}
