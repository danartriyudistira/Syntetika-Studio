#include "GLSLShaderModule.h"
#include "ModuleFactory.h"
#include "ModularSynth.h"
#include "VisualFBO.h"
#include "nanovg/nanovg.h"
#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

#include <string>



extern NVGcontext* gNanoVG;

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
   mOutputCable->SetManualPosition(170, 10);
   AddPatchCableSource(mOutputCable);

   mCodeEntry = new CodeEntry(this, "code", 3, 32, 394, 140);
   mCodeEntry->SetText(GetDefaultShader());

   mCompileButton = new ClickButton(this, "compile", 3, 178);
   mRandomButton = new ClickButton(this, "random", 103, 178);

   mSliderA = new FloatSlider(this, "a", 3, 204, 190, 18, &mSliderAValue, 0, 1);
   mSliderB = new FloatSlider(this, "b", 3, 224, 190, 18, &mSliderBValue, 0, 1);
   mSliderC = new FloatSlider(this, "c", 203, 204, 190, 18, &mSliderCValue, 0, 1);
   mSliderD = new FloatSlider(this, "d", 203, 224, 190, 18, &mSliderDValue, 0, 1);

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
};

void GLSLShaderModule::RandomizeShader()
{
   int idx = rand() % (sizeof(sShaderBodies)/sizeof(sShaderBodies[0]));
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

void GLSLShaderModule::ExecuteCode()
{
   mShaderDirty = true;
}

void GLSLShaderModule::CreateFBO()
{
   if (mFBO)
      delete mFBO;
   mFBO = new VisualFBO();
   mFBO->Create(400, 300);
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
    // Draw module background
    ofSetColor(40, 40, 40);
    ofFill();
    ofRect(0, 0, mWidth, mHeight);

    // Draw UI controls
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

    // Draw preview area (FBO or black rect)
   float previewY = 248;
   float previewH = mHeight - previewY - 2;
   if (mFBO && mFBO->IsValid() && mProgramId && mLastError.empty())
   {
      mFBO->Draw(1, previewY, mWidth - 2, previewH);
   }
   else
   {
      ofSetColor(0, 0, 0);
      ofFill();
      ofRect(1, previewY, mWidth - 2, previewH);
   }

   // Draw shader compile error text if any
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
   mWidth = w;
   mHeight = h;
   if (mOutputCable)
      mOutputCable->SetManualPosition(mWidth * 0.5f - 30, 10);
   if (mCodeEntry)
   {
      mCodeEntry->SetPosition(3, 32);
      mCodeEntry->SetDimensions(mWidth - 6, 140);
   }
   if (mCompileButton)
      mCompileButton->SetPosition(3, 178);
   if (mRandomButton)
      mRandomButton->SetPosition(103, 178);
   if (mSliderA)
      mSliderA->SetPosition(3, 204);
   if (mSliderB)
      mSliderB->SetPosition(3, 224);
   if (mSliderC)
      mSliderC->SetPosition(mWidth / 2 + 3, 204);
   if (mSliderD)
      mSliderD->SetPosition(mWidth / 2 + 3, 224);
}

void GLSLShaderModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo, "");
   mModuleSaveData.LoadString("code", moduleInfo, "");
}

void GLSLShaderModule::SaveLayout(ofxJSONElement& moduleInfo)
{
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
}
