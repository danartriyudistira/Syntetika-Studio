#pragma once

#include "nanovg/nanovg.h"
#include <vector>

struct NVGLUframebuffer;

class VisualFBO
{
public:
   VisualFBO() = default;
   VisualFBO(const VisualFBO&) = delete;
   VisualFBO& operator=(const VisualFBO&) = delete;
   VisualFBO(VisualFBO&&) = delete;
   VisualFBO& operator=(VisualFBO&&) = delete;
   ~VisualFBO() { Destroy(); }

   void Create(int width, int height, int flags = 0);
   void Destroy();

   void Bind();
   void Unbind();

   void Draw(float x, float y, float w, float h);

   std::vector<uint8_t> ReadPixels() const;

   int GetWidth() const { return mWidth; }
   int GetHeight() const { return mHeight; }
   bool IsValid() const { return mFB != nullptr; }
   NVGcontext* GetNVGContext() const { return mNVG; }
   unsigned int GetTexture() const;

private:
   NVGcontext* mNVG{ nullptr };
   NVGLUframebuffer* mFB{ nullptr };
   NVGcontext* mSavedNVG{ nullptr };
   int mSavedViewport[4]{};
   int mWidth{ 0 };
   int mHeight{ 0 };
   int mDisplayImage{ -1 };
   bool mBound{ false };
public:
   void ReleaseDisplayImage() { mDisplayImage = -1; }
};
