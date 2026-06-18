#pragma once

#include "nanovg/nanovg.h"
#include <vector>

struct NVGLUframebuffer;

class VisualFBO
{
public:
   VisualFBO() = default;
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

private:
   NVGcontext* mNVG{ nullptr };
   NVGLUframebuffer* mFB{ nullptr };
   NVGcontext* mSavedNVG{ nullptr };
   int mWidth{ 0 };
   int mHeight{ 0 };
   int mDisplayImage{ -1 };
   bool mBound{ false };
};
