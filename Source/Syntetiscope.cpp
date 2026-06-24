#include "Syntetiscope.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"
#include "OpenFrameworksPort.h"
#include "VisualFBO.h"

Syntetiscope::Syntetiscope()
: IAudioProcessor(gBufferSize)
{
   for (int i = 0; i < SYNTETISCOPE_BUFFER_SIZE; ++i)
      mBuffer[i] = { 0, 0 };
}

Syntetiscope::~Syntetiscope()
{
   delete mFBO;
}

void Syntetiscope::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   FLOATSLIDER(mScaleSlider, "scale", &mScale, 0.5f, 4);
   FLOATSLIDER(mZoomSlider, "zoom", &mZoom, 0.2f, 5);
   FLOATSLIDER(mIntensitySlider, "intensity", &mIntensity, 0, 3);
   FLOATSLIDER(mBeamSlider, "beam", &mBeamSize, 1, 20);
   FLOATSLIDER(mDecaySlider, "decay", &mDecay, 0.5f, 6);
   DROPDOWN(mColorDropdown, "color", &mColorSelect, 45);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mShowLissaCheckbox, "lissa", &mShowLissa);
   ENDUIBLOCK(mHeight);

   mColorDropdown->AddLabel("green", 0);
   mColorDropdown->AddLabel("amber", 1);
   mColorDropdown->AddLabel("blue", 2);
   mColorDropdown->AddLabel("white", 3);
   mColorDropdown->AddLabel("red", 4);
}

void Syntetiscope::Process(double time)
{
   PROFILER(Syntetiscope);

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
         mWritePos = (mWritePos + 1) % SYNTETISCOPE_BUFFER_SIZE;
         if (mNumStored < SYNTETISCOPE_BUFFER_SIZE)
            ++mNumStored;
      }
   }

   GetBuffer()->Reset();
}

void Syntetiscope::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   // Draw FBO (contains glow visualization from PostRender)
   if (mFBO && mFBO->IsValid())
      mFBO->Draw(0, 0, mWidth, mHeight);

   // Draw controls on top
   mScaleSlider->Draw();
   mZoomSlider->Draw();
   mIntensitySlider->Draw();
   mBeamSlider->Draw();
   mDecaySlider->Draw();
   mColorDropdown->Draw();
   mShowLissaCheckbox->Draw();
}

void Syntetiscope::PostRender()
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

   mFBO->Bind();

   float cr, cg, cb;
   switch (mColorSelect)
   {
   case 0: cr=0;   cg=1.0f; cb=0;    break;
   case 1: cr=1;   cg=0.8f; cb=0;    break;
   case 2: cr=0;   cg=0.5f; cb=1;    break;
   case 3: cr=1;   cg=1;    cb=1;    break;
   case 4: cr=1;   cg=0;    cb=0;    break;
   default: cr=0;  cg=1;    cb=0;
   }

   int numDraw = ofClamp(mNumStored, 2, SYNTETISCOPE_BUFFER_SIZE);
   int startIdx = (mWritePos - numDraw + SYNTETISCOPE_BUFFER_SIZE) % SYNTETISCOPE_BUFFER_SIZE;
   float halfW = mWidth / 2.0f;
   float halfH = mHeight / 2.0f;
   float effScale = mScale / mZoom;
   float scaleX = mWidth * effScale;
   float scaleY = mHeight * effScale;
   int r = (int)(cr * 255);
   int g = (int)(cg * 255);
   int b = (int)(cb * 255);

   float fadeLUT[SYNTETISCOPE_BUFFER_SIZE];
   for (int i = 0; i < numDraw; ++i)
   {
      float age = (float)(numDraw - 1 - i) / (numDraw - 1);
      fadeLUT[i] = powf(1.0f - age, mDecay);
   }

   struct GlowPass { float widthMul; float alphaMul; };
   GlowPass passes[] = {
      { 1.0f, 1.0f },
      { 3.0f, 0.35f },
      { 6.0f, 0.12f },
      { 12.0f, 0.04f },
   };
   int numPasses = sizeof(passes) / sizeof(passes[0]);

   for (int p = 0; p < numPasses; ++p)
   {
      float intensityAlpha = mIntensity * passes[p].alphaMul;
      if (intensityAlpha < 0.001f)
         continue;
      ofSetLineWidth(mBeamSize * passes[p].widthMul);
      ofBeginShape();
      ofSetColor(r, g, b, 0);
      for (int i = 0; i < numDraw; ++i)
      {
         int idx = (startIdx + i) % SYNTETISCOPE_BUFFER_SIZE;
         float alpha = fadeLUT[i] * intensityAlpha;
         ofSetColor(r, g, b, (int)(alpha * 255));
         ofVertex(halfW + mBuffer[idx].x * scaleX, halfH + mBuffer[idx].y * scaleY);
      }
      ofEndShape(false);
   }

   if (mShowLissa)
   {
      ofSetLineWidth(1);
      ofBeginShape();
      ofSetColor(r, g, b, 0);
      for (int i = 0; i < numDraw; ++i)
      {
         int idx = (startIdx + i) % SYNTETISCOPE_BUFFER_SIZE;
         float fade = sqrtf(fadeLUT[i]);
         float alpha = fade * mIntensity * 1.5f;
         ofSetColor(r, g, b, (int)(alpha * 255));
         ofVertex(halfW + mBuffer[idx].x * scaleX, halfH + mBuffer[idx].y * scaleY);
      }
      ofEndShape(false);
   }

   mFBO->Unbind();
}

VisualFBO* Syntetiscope::GetFBO()
{
   return mFBO;
}

void Syntetiscope::Resize(float w, float h)
{
   mWidth = w;
   mHeight = h;
}

void Syntetiscope::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("width", moduleInfo, 500);
   mModuleSaveData.LoadFloat("height", moduleInfo, 500);
   mModuleSaveData.LoadFloat("zoom", moduleInfo, 1.0f);

   SetUpFromSaveData();
}

void Syntetiscope::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["width"] = mWidth;
   moduleInfo["height"] = mHeight;
   moduleInfo["zoom"] = mZoom;
}

void Syntetiscope::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mWidth = mModuleSaveData.GetFloat("width");
   mHeight = mModuleSaveData.GetFloat("height");
   mZoom = mModuleSaveData.GetFloat("zoom");
}
