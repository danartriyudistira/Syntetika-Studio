#include "VisualFBO.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"

#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

// Include nvglu declarations BEFORE defining NANOVG_GLES2_IMPLEMENTATION,
// so we get struct + declarations without compiling the implementations.
// Implementations come from Push2Control.obj at link time.
#include "nanovg/nanovg_gl_utils.h"

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg/nanovg_gl.h"

extern NVGcontext* gNanoVG;
extern bool gDrawingToFBO;

void VisualFBO::Create(int width, int height, int flags)
{
   Destroy();

   mWidth = width;
   mHeight = height;

   mNVG = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
   assert(mNVG);

   mFB = nvgluCreateFramebuffer(mNVG, width, height, flags);
   assert(mFB);
}

void VisualFBO::Destroy()
{
   if (mDisplayImage >= 0 && gNanoVG)
   {
      nvgDeleteImage(gNanoVG, mDisplayImage);
      mDisplayImage = -1;
   }
   if (mFB)
   {
      nvgluDeleteFramebuffer(mFB);
      mFB = nullptr;
   }
   if (mNVG)
   {
      nvgDeleteGLES2(mNVG);
      mNVG = nullptr;
   }
   mWidth = 0;
   mHeight = 0;
   mBound = false;
   mSavedNVG = nullptr;
}

void VisualFBO::Bind()
{
   assert(mFB);
   assert(!mBound);

   mSavedNVG = gNanoVG;
   gNanoVG = mNVG;
   gDrawingToFBO = true;

   nvgluBindFramebuffer(mFB);
   glViewport(0, 0, mWidth, mHeight);
   glClearColor(0, 0, 0, 0);
   glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   nvgBeginFrame(mNVG, mWidth, mHeight, 1.0f);

   mBound = true;
}

void VisualFBO::Unbind()
{
   assert(mBound);

   nvgEndFrame(mNVG);
   nvgluBindFramebuffer(nullptr);

   gDrawingToFBO = false;
   gNanoVG = mSavedNVG;
   mSavedNVG = nullptr;

   mBound = false;
}

std::vector<uint8_t> VisualFBO::ReadPixels() const
{
   if (!mFB)
      return {};

   int prevFB = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);

   nvgluBindFramebuffer(mFB);
   glViewport(0, 0, mWidth, mHeight);

   std::vector<uint8_t> pixels(mWidth * mHeight * 4);
   glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

   glBindFramebuffer(GL_FRAMEBUFFER, prevFB);

   return pixels;
}

void VisualFBO::Draw(float x, float y, float w, float h)
{
   if (!mFB)
      return;

   if (mDisplayImage < 0)
   {
      mDisplayImage = nvglCreateImageFromHandleGLES2(
         gNanoVG, mFB->texture, mWidth, mHeight, NVG_IMAGE_FLIPY);
   }

   NVGpaint imgPaint = nvgImagePattern(gNanoVG, x, y, w, h, 0.0f, mDisplayImage, 1.0f);
   nvgBeginPath(gNanoVG);
   nvgRect(gNanoVG, x, y, w, h);
   nvgFillPaint(gNanoVG, imgPaint);
   nvgFill(gNanoVG);
}
