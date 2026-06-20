#include "GLSLShaderModule.h"
#include "ModuleFactory.h"
#include "ModularSynth.h"
#include "VisualFBO.h"
#include "nanovg/nanovg.h"
#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

#include <string>
#include <random>



extern NVGcontext* gNanoVG;

namespace
{
   constexpr float kCodeY = 32;
   float ComputeCodeH(float mH)
   {
      return ofClamp(mH * 0.35f, 60.0f, mH - 180.0f);
   }
}

GLSLShaderModule::GLSLShaderModule()
{
}

GLSLShaderModule::~GLSLShaderModule()
{
   CleanupShader();
   if (mFBO)
      delete mFBO;
}

void GLSLShaderModule::CleanupShader()
{
   if (mProgramId) { glDeleteProgram(mProgramId); mProgramId = 0; }
   if (mVSId) { glDeleteShader(mVSId); mVSId = 0; }
   if (mFSId) { glDeleteShader(mFSId); mFSId = 0; }
   if (mVBO) { glDeleteBuffers(1, &mVBO); mVBO = 0; }
}

void GLSLShaderModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth - 15, mHeight / 2);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   float codeH = ComputeCodeH(mHeight);
   mCodeEntry = new CodeEntry(this, "code", 3, kCodeY, mWidth - 6, codeH);
   mCodeEntry->SetText(GetDefaultShader());

   float buttonY = kCodeY + codeH + 4;
   mCompileButton = new ClickButton(this, "compile", 3, buttonY);
   mRandomButton = new ClickButton(this, "random", 103, buttonY);

   float colW = (mWidth - 9) / 2;
   float sliderAY = buttonY + 20;
   float sliderBY = sliderAY + 20;
   mSliderA = new FloatSlider(this, "slider_a", 3, sliderAY, colW, 18, &mSliderAValue, 0, 1);
   mSliderB = new FloatSlider(this, "slider_b", 3, sliderBY, colW, 18, &mSliderBValue, 0, 1);
   mSliderC = new FloatSlider(this, "slider_c", mWidth / 2 + 3, sliderAY, colW, 18, &mSliderCValue, 0, 1);
   mSliderD = new FloatSlider(this, "slider_d", mWidth / 2 + 3, sliderBY, colW, 18, &mSliderDValue, 0, 1);

   float resY = sliderBY + 20;
   mResolutionSlider = new FloatSlider(this, "res", 3, resY, mWidth - 6, 15, &mResolutionScale, 0.25f, 2.0f);

   mCodeEntry->Publish();
   mShaderDirty = true;
   mShaderStartTime = gTime;
}

static const char* sDefaultShader = R"(
gl_FragColor = vec4(uv, 0.5 + 0.5*sin(u_time), 1.0);
)";

std::string GLSLShaderModule::GetDefaultShader()
{
   return sDefaultShader;
}

const char* sShaderBodies[] = {
   sDefaultShader,
   R"(
float d = length(uv - 0.5);
gl_FragColor = vec4(0.5 + 0.5*sin(d*10.0 - u_time), 0.0, 0.0, 1.0);
)",
   R"(
gl_FragColor = vec4(
   0.5 + 0.5*sin(uv.x*20.0 + u_time),
   0.5 + 0.5*sin(uv.y*20.0 + u_time*1.3),
   0.5 + 0.5*sin((uv.x+uv.y)*10.0 + u_time*0.7),
   1.0
);
)",
   R"(
vec2 p = uv - 0.5;
float a = atan(p.y, p.x);
float r = length(p);
gl_FragColor = vec4(
   0.5 + 0.5*sin(r*20.0 - u_time + a),
   0.5 + 0.5*sin(r*15.0 + u_time*1.7 + a*2.0),
   0.5 + 0.5*sin(r*10.0 + u_time*0.5 - a*3.0),
   1.0
);
)",
   R"(
vec2 p = uv * 10.0;
vec2 i = floor(p);
vec2 f = fract(p);
float v = sin(i.x + i.y + u_time);
gl_FragColor = vec4(
   step(0.5, v)*f.x,
   step(0.5, v)*f.y,
   step(0.5, v)*(1.0 - f.x - f.y),
   1.0
);
)",
   R"(
vec2 p = uv - 0.5;
float d = length(p);
float glow = 0.01 / (d + 0.01);
gl_FragColor = vec4(
   glow * (0.5 + 0.5*sin(u_time)),
   glow * (0.5 + 0.5*sin(u_time*1.3 + 2.0)),
   glow * (0.5 + 0.5*sin(u_time*0.7 + 4.0)),
   1.0
);
)",
   // Plasma
   R"(
vec2 p = uv - 0.5;
float c = sin(length(p*8.0) + u_time*2.0)
        + sin(p.x*12.0 - u_time*1.5 + p.y*8.0)
        + sin(p.y*10.0 + u_time*1.2 + p.x*6.0);
c = c * 0.25 + 0.5;
gl_FragColor = vec4(
   0.5 + 0.5*sin(c*6.0 + 0.0),
   0.5 + 0.5*sin(c*6.0 + 2.0),
   0.5 + 0.5*sin(c*6.0 + 4.0),
   1.0
);
)",
   // Water ripple
   R"(
vec2 p = (uv - 0.5) * 8.0;
float r = length(p);
float wave = sin(r*10.0 - u_time*3.0) * 0.5 + 0.5;
float ripple = 0.5 + 0.5*sin(r*20.0 - u_time*5.0);
gl_FragColor = vec4(
   wave * ripple,
   wave * (1.0 - ripple),
   0.3 + 0.3*sin(r*8.0 + u_time*2.0),
   1.0
);
)",
   // Kaleidoscope
   R"(
vec2 p = uv - 0.5;
float a = atan(p.y, p.x);
float r = length(p);
float segments = 6.0;
a = mod(a, 6.2832/segments);
a = abs(a - 3.1416/segments);
p = vec2(cos(a)*r, sin(a)*r) + 0.5;
vec3 col = vec3(
   0.5 + 0.5*sin(p.x*20.0 + u_time),
   0.5 + 0.5*sin(p.y*20.0 + u_time*1.3),
   0.5 + 0.5*sin((p.x+p.y)*10.0 - u_time*0.7)
);
col *= 0.8 + 0.2*sin(r*15.0 - u_time);
gl_FragColor = vec4(col, 1.0);
)",
   // Hex grid
   R"(
vec2 p = uv * 15.0;
vec2 i = floor(p);
vec2 f = fract(p) - 0.5;
float d = length(f - vec2(0.0, 0.0));
float glow = 0.02 / (d*d + 0.02);
float pulse = 0.5 + 0.5*sin(i.x*3.0 + i.y*7.0 + u_time*2.0);
gl_FragColor = vec4(
   glow * pulse,
   glow * (1.0 - pulse),
   glow * 0.5,
   1.0
);
)",
   // Color wave
   R"(
vec2 p = uv - 0.5;
float d = length(p);
float angle = atan(p.y, p.x);
vec3 col;
col.r = 0.5 + 0.5*sin(d*12.0 - u_time*2.0 + angle);
col.g = 0.5 + 0.5*sin(d*10.0 - u_time*1.7 + angle*2.0 + 2.0);
col.b = 0.5 + 0.5*sin(d*8.0 - u_time*1.3 + angle*3.0 + 4.0);
col *= 0.7 + 0.3*sin(d*5.0 - u_time);
gl_FragColor = vec4(col, 1.0);
)",
   // Morphing Mandala
   R"(
vec2 p = uv - 0.5;
float a = atan(p.y, p.x);
float r = length(p);
float pattern = sin(r*20.0 - u_time) + sin(a*8.0 + u_time*1.5) + cos((r*12.0 + a*4.0) + u_time*0.7);
pattern = pattern * 0.25 + 0.5;
float mask = 1.0 - smoothstep(0.4, 0.5, r);
gl_FragColor = vec4(
   pattern * mask,
   pattern * mask * 0.6,
   (1.0 - pattern) * mask * 0.8,
   1.0
);
)",
};

void GLSLShaderModule::RandomizeShader()
{
   static std::mt19937 rng(std::random_device{}());
   int numShaders = sizeof(sShaderBodies) / sizeof(sShaderBodies[0]);
   std::uniform_int_distribution<int> dist(0, numShaders - 1);
   int idx = dist(rng);
   mCodeEntry->SetText(std::string(sShaderBodies[idx]) + "\n");
   mCodeEntry->Publish();
   mShaderDirty = true;
}

void GLSLShaderModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mCompileButton)
   {
      mCodeEntry->Publish();
      mShaderDirty = true;
   }
   else if (button == mRandomButton)
   {
      RandomizeShader();
   }
}

void GLSLShaderModule::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (slider == mResolutionSlider)
   {
      if (mFBO)
      {
         delete mFBO;
         mFBO = nullptr;
      }
   }
}

void GLSLShaderModule::ExecuteCode()
{
   mShaderDirty = true;
}

void GLSLShaderModule::CreateFBO()
{
   if (mFBO)
      delete mFBO;
   mFBO = new VisualFBO();
   int fboW = std::max(64, (int)(mWidth * mResolutionScale));
   int fboH = std::max(64, (int)(mHeight * mResolutionScale));
   mFBO->Create(fboW, fboH);
}

void GLSLShaderModule::CompileShader()
{
   mLastError.clear();

   const std::string userCode = mCodeEntry->GetText(true);
   if (userCode.empty())
   {
      mLastError = "shader source is empty";
      CleanupShader();
      return;
   }

   // Wrap user code with uniforms and version
   std::string fsSource;
   if (userCode.find("#version") != std::string::npos)
   {
      fsSource = userCode;
   }
   else
   {
      if (userCode.find("void main") != std::string::npos)
      {
         fsSource += "#version 150\n";
         fsSource += "uniform float u_time;\n";
         fsSource += "uniform vec2 u_resolution;\n";
         fsSource += "uniform float u_slider_a;\n";
         fsSource += "uniform float u_slider_b;\n";
         fsSource += "uniform float u_slider_c;\n";
         fsSource += "uniform float u_slider_d;\n";
         fsSource += "uniform float u_millis;\n";
         fsSource += "out vec4 fragColor;\n";
         fsSource += "#define gl_FragColor fragColor\n";
         fsSource += userCode;
      }
      else
      {
         fsSource += "#version 150\n";
         fsSource += "uniform float u_time;\n";
         fsSource += "uniform vec2 u_resolution;\n";
         fsSource += "uniform float u_slider_a;\n";
         fsSource += "uniform float u_slider_b;\n";
         fsSource += "uniform float u_slider_c;\n";
         fsSource += "uniform float u_slider_d;\n";
         fsSource += "uniform float u_millis;\n";
         fsSource += "out vec4 fragColor;\n";
         fsSource += "#define gl_FragColor fragColor\n";
         fsSource += "void main() {\n";
         fsSource += "vec2 uv = gl_FragCoord.xy / u_resolution.xy;\n";
         fsSource += userCode;
         fsSource += "}\n";
      }
   }

   CleanupShader();

   // Vertex shader
   const char* vsSource = R"(
#version 150
in vec2 a_position;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

   GLint status;

   mVSId = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(mVSId, 1, &vsSource, nullptr);
   glCompileShader(mVSId);
   glGetShaderiv(mVSId, GL_COMPILE_STATUS, &status);
   if (!status)
   {
      char log[4096];
      glGetShaderInfoLog(mVSId, sizeof(log), nullptr, log);
      mLastError = "vertex shader error:\n";
      mLastError += log;
      CleanupShader();
      return;
   }

   mFSId = glCreateShader(GL_FRAGMENT_SHADER);
   const char* fsPtr = fsSource.c_str();
   glShaderSource(mFSId, 1, &fsPtr, nullptr);
   glCompileShader(mFSId);
   glGetShaderiv(mFSId, GL_COMPILE_STATUS, &status);
   if (!status)
   {
      char log[4096];
      glGetShaderInfoLog(mFSId, sizeof(log), nullptr, log);
      mLastError = "fragment shader error:\n";
      mLastError += log;
      CleanupShader();
      return;
   }

   mProgramId = glCreateProgram();
   glBindAttribLocation(mProgramId, 0, "a_position");
   glAttachShader(mProgramId, mVSId);
   glAttachShader(mProgramId, mFSId);
   glLinkProgram(mProgramId);
   glGetProgramiv(mProgramId, GL_LINK_STATUS, &status);
   if (!status)
   {
      char log[4096];
      glGetProgramInfoLog(mProgramId, sizeof(log), nullptr, log);
      mLastError = "link error:\n";
      mLastError += log;
      CleanupShader();
      return;
   }

   // Create VBO for fullscreen quad
   if (!mVBO)
   {
      float verts[] = {
         -1.0f, -1.0f,
          1.0f, -1.0f,
         -1.0f,  1.0f,
          1.0f,  1.0f
      };
      glGenBuffers(1, &mVBO);
      glBindBuffer(GL_ARRAY_BUFFER, mVBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
   }

   mShaderStartTime = gTime;
}

void GLSLShaderModule::DrawModule()
{
   ofSetColor(40, 40, 40);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   if (mCodeEntry)
      mCodeEntry->Draw();
   if (mCompileButton)
      mCompileButton->Draw();
   if (mRandomButton)
      mRandomButton->Draw();
   if (mSliderA)
      mSliderA->Draw();
   if (mSliderB)
      mSliderB->Draw();
   if (mSliderC)
      mSliderC->Draw();
   if (mSliderD)
      mSliderD->Draw();
   if (mResolutionSlider)
      mResolutionSlider->Draw();

   float codeH = ComputeCodeH(mHeight);
   float buttonY = kCodeY + codeH + 4;
   float sliderAY = buttonY + 20;
   float sliderBY = sliderAY + 20;
   float resY = sliderBY + 20;
   float previewY = resY + 18;
   float previewH = mHeight - previewY - 4;

   if (previewH > 0)
   {
      if (mFBO && mFBO->IsValid() && mProgramId && mLastError.empty())
      {
         mFBO->Draw(1, previewY, mWidth - 2, previewH);
      }
      else
      {
         ofSetColor(0, 0, 0);
         ofFill();
         ofRect(1, previewY, mWidth - 2, previewH);

         ofSetColor(80, 80, 80);
         nvgFontSize(gNanoVG, 12);
         nvgTextAlign(gNanoVG, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
         nvgText(gNanoVG, mWidth / 2, previewY + previewH / 2, "preview", nullptr);
         nvgTextAlign(gNanoVG, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
      }
   }

   if (!mLastError.empty())
   {
      nvgBeginPath(gNanoVG);
      nvgFillColor(gNanoVG, nvgRGBA(255, 0, 0, 255));
      float tx = 5;
      float ty = mHeight - 30;
      const float lineH = 14;
      std::string err = mLastError;
      size_t pos;
      while ((pos = err.find('\n')) != std::string::npos)
      {
         nvgText(gNanoVG, tx, ty, err.substr(0, pos).c_str(), nullptr);
         ty += lineH;
         err.erase(0, pos + 1);
      }
      if (!err.empty())
         nvgText(gNanoVG, tx, ty, err.c_str(), nullptr);
   }
}

void GLSLShaderModule::PostRender()
{
   // Compile shader outside NanoVG frame (safe for raw GL calls)
   if (mShaderDirty)
   {
      mShaderDirty = false;
      CompileShader();
   }

   if (!mFBO || !mFBO->IsValid())
      CreateFBO();

   if (!mFBO || !mFBO->IsValid())
      return;

   if (!mProgramId)
      return;

   // Render shader to FBO (PostRender runs AFTER nvgEndFrame, safe for raw GL)
   mFBO->Bind();

   float time = (float)(gTime - mShaderStartTime);
   int fboW = mFBO->GetWidth();
   int fboH = mFBO->GetHeight();

   glUseProgram(mProgramId);
   glUniform1f(glGetUniformLocation(mProgramId, "u_time"), time);
   glUniform2f(glGetUniformLocation(mProgramId, "u_resolution"), (float)fboW, (float)fboH);
   glUniform1f(glGetUniformLocation(mProgramId, "u_slider_a"), mSliderAValue);
   glUniform1f(glGetUniformLocation(mProgramId, "u_slider_b"), mSliderBValue);
   glUniform1f(glGetUniformLocation(mProgramId, "u_slider_c"), mSliderCValue);
   glUniform1f(glGetUniformLocation(mProgramId, "u_slider_d"), mSliderDValue);
   glUniform1f(glGetUniformLocation(mProgramId, "u_millis"), time * 1000.0f);

   glBindBuffer(GL_ARRAY_BUFFER, mVBO);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glDisableVertexAttribArray(0);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glUseProgram(0);

   mFBO->Unbind();
}

void GLSLShaderModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

void GLSLShaderModule::Resize(float w, float h)
{
   bool sizeChanged = (mWidth != w || mHeight != h);
   mWidth = w;
   mHeight = h;

   float colW = (mWidth - 9) / 2;
   float codeH = ComputeCodeH(mHeight);
   float buttonY = kCodeY + codeH + 4;
   float sliderAY = buttonY + 20;
   float sliderBY = sliderAY + 20;
   float resY = sliderBY + 20;

   if (mOutputCable)
   {
      mOutputCable->SetManualPosition(mWidth - 15, mHeight / 2);
      mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   }
   if (mCodeEntry)
   {
      mCodeEntry->SetPosition(3, kCodeY);
      mCodeEntry->SetDimensions(mWidth - 6, codeH);
   }
   if (mCompileButton)
      mCompileButton->SetPosition(3, buttonY);
   if (mRandomButton)
      mRandomButton->SetPosition(103, buttonY);
   if (mSliderA)
      mSliderA->SetPosition(3, sliderAY);
   if (mSliderB)
      mSliderB->SetPosition(3, sliderBY);
   if (mSliderC)
      mSliderC->SetPosition(mWidth / 2 + 3, sliderAY);
   if (mSliderD)
      mSliderD->SetPosition(mWidth / 2 + 3, sliderBY);
   if (mResolutionSlider)
   {
      mResolutionSlider->SetPosition(3, resY);
      mResolutionSlider->SetDimensions(mWidth - 6, 15);
   }

   // Only recreate FBO if size actually changed
   if (sizeChanged && mFBO)
   {
      delete mFBO;
      mFBO = nullptr;
   }
}

void GLSLShaderModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo, "");
   mModuleSaveData.LoadString("code", moduleInfo, "");
}

void GLSLShaderModule::SaveLayout(ofxJSONElement& moduleInfo)
{
   if (mCodeEntry)
      mModuleSaveData.SetString("code", mCodeEntry->GetText(true));
   mModuleSaveData.Save(moduleInfo);
}

void GLSLShaderModule::SetUpFromSaveData()
{
   if (mCodeEntry)
   {
      std::string savedCode = mModuleSaveData.GetString("code");
      if (!savedCode.empty())
      {
         mCodeEntry->SetText(savedCode);
         mCodeEntry->Publish();
         mShaderDirty = true;
      }
   }
}

void GLSLShaderModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   if (mCodeEntry) out << mCodeEntry->GetText(true);
   else out << std::string();
   out << mResolutionScale;
}

void GLSLShaderModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   std::string code;
   in >> code;
   if (mCodeEntry && !code.empty())
   {
      mCodeEntry->SetText(code);
      mCodeEntry->Publish();
      mShaderDirty = true;
   }
   if (rev >= 1)
   {
      in >> mResolutionScale;
      if (mFBO)
      {
         delete mFBO;
         mFBO = nullptr;
      }
   }
}
