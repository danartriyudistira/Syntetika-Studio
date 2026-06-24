#include "ShaderModule.h"
#include "GlShaderUtil.h"
#include "ModuleFactory.h"
#include "ModularSynth.h"
#include "VisualFBO.h"
#include "nanovg/nanovg.h"
#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

#include <string>
#include <random>
#include <json/json.h>

extern NVGcontext* gNanoVG;

namespace
{
   constexpr float kCodeY = 32;
   float ComputeCodeH(float mH)
   {
      return ofClamp(mH * 0.35f, 60.0f, mH - 180.0f);
   }

   bool HasMainImage(const std::string& code)
   {
      return code.find("mainImage(") != std::string::npos;
   }

   bool HasISFMetadata(const std::string& code)
   {
      return code.find("/*{") != std::string::npos;
   }

   bool HasGLSLES(const std::string& code)
   {
      return code.find("precision ") != std::string::npos ||
             code.find("varying ") != std::string::npos;
   }

   std::string StripISFMetadata(const std::string& code)
   {
      size_t start = code.find("/*{");
      if (start == std::string::npos)
         return code;
      size_t end = code.find("*/", start);
      if (end == std::string::npos)
         return code;

      std::string jsonStr = code.substr(start + 3, end - start - 3);
      std::string result = code;
      result.erase(start, end - start + 2);

      Json::Value root;
      Json::Reader reader;
      if (!reader.parse(jsonStr, root))
         return result;

      std::string decls;
      decls += "#define TIME u_time\n#define RENDERSIZE u_resolution\n#define DATE vec4(2024.0, 6.0, 23.0, 0.0)\n";

      const Json::Value& inputs = root["INPUTS"];
      if (inputs.isArray())
      {
         for (const auto& input : inputs)
         {
            std::string name = input.get("NAME", "").asString();
            std::string type = input.get("TYPE", "").asString();
            if (name.empty()) continue;
            if (type == "float")
               decls += "uniform float " + name + ";\n";
            else if (type == "color" || type == "vec4")
               decls += "uniform vec4 " + name + ";\n";
            else if (type == "point2d" || type == "vec2")
               decls += "uniform vec2 " + name + ";\n";
         }
      }

      return decls + result;
   }

   std::string ConvertShadertoy(const std::string& code)
   {
      std::string r;
      r += "#define iTime u_time\n";
      r += "#define iResolution u_resolution\n";
      r += "#define iMouse vec4(0.0)\n";
      r += "#define iDate vec4(2024.0, 6.0, 23.0, 0.0)\n";
      r += "#define iChannel0 texture(u_texture, gl_FragCoord.xy / u_resolution.xy)\n";
      r += "#define iChannel1 vec4(0.0)\n";
      r += "#define iChannel2 vec4(0.0)\n";
      r += "#define iChannel3 vec4(0.0)\n";
      r += code;
      r += "\nvoid main() { mainImage(fragColor, gl_FragCoord.xy); }\n";
      return r;
   }

   std::string ConvertGLSLES(const std::string& code)
   {
      std::string r = code;

      // Remove precision statements
      size_t pos = 0;
      while ((pos = r.find("precision ", pos)) != std::string::npos)
      {
         size_t semi = r.find(';', pos);
         if (semi != std::string::npos)
         {
            r.erase(pos, semi - pos + 1);
         }
         else { pos += 10; }
      }

       // Remove varying declarations and track the varying name + type
       std::string varyingName;
       std::string varyingType = "vec2";
       pos = 0;
       while ((pos = r.find("varying ", pos)) != std::string::npos)
       {
          size_t semi = r.find(';', pos);
          if (semi != std::string::npos)
          {
             std::string decl = r.substr(pos + 8, semi - pos - 8);
             size_t first = decl.find_first_not_of(' ');
             if (first != std::string::npos) decl = decl.substr(first);
             size_t sp = decl.rfind(' ');
             if (sp != std::string::npos)
             {
                std::string name = decl.substr(sp + 1);
                size_t end = name.find_last_not_of(' ');
                if (end != std::string::npos) name = name.substr(0, end + 1);
                varyingType = "vec2";
                if (decl.substr(0, sp).find("vec4") != std::string::npos) varyingType = "vec4";
                else if (decl.substr(0, sp).find("vec3") != std::string::npos) varyingType = "vec3";
                varyingName = name;
             }
             r.erase(pos, semi - pos + 1);
          }
          else { pos += 8; }
       }

      // texture2D -> texture
      pos = 0;
      while ((pos = r.find("texture2D", pos)) != std::string::npos)
         r.replace(pos, 9, "texture");

       if (!varyingName.empty())
       {
          size_t mainPos = r.find("void main");
          if (mainPos != std::string::npos)
          {
             size_t brace = r.find('{', mainPos);
             if (brace != std::string::npos)
             {
                 std::string insertStr = "\n   " + varyingType + " " + varyingName + " = gl_FragCoord.xy / u_resolution.xy;\n";
                r.insert(brace + 1, insertStr);
             }
          }
       }

      return r;
   }
}

// ===== ShaderEditPopup =====

ShaderEditPopup::ShaderEditPopup()
{
}

void ShaderEditPopup::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mCodeEntry = new CodeEntry(this, "code", 10, 25, 500, 310);
   mCodeEntry->SetText(mParent ? mParent->GetShaderCode() : "");

   mCompileButton = new ClickButton(this, "compile", 10, 350);
   mCancelButton = new ClickButton(this, "cancel", 150, 350);
}

void ShaderEditPopup::DrawModule()
{
   float w, h;
   GetModuleDimensions(w, h);

   ofSetColor(50, 50, 60);
   ofFill();
   ofRect(0, 0, w, h);

   ofSetColor(100, 100, 130);
   ofFill();
   ofRect(0, 0, w, 20);

   ofSetColor(220, 220, 240);
   nvgFontSize(gNanoVG, 13);
   nvgTextAlign(gNanoVG, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
   nvgText(gNanoVG, 10, 4, "Shader Editor", nullptr);

   if (mCodeEntry) mCodeEntry->Draw();
   if (mCompileButton) mCompileButton->Draw();
   if (mCancelButton) mCancelButton->Draw();
}

void ShaderEditPopup::KeyPressed(int key, bool isRepeat)
{
   if (key == 27) // Escape
      Hide();
}

void ShaderEditPopup::ButtonClicked(ClickButton* button, double time)
{
   if (button == mCompileButton)
   {
      mCodeEntry->Publish();
      if (mParent)
      {
         mParent->SetShaderCode(mCodeEntry->GetText(true));
      }
      Hide();
   }
   else if (button == mCancelButton)
   {
      Hide();
   }
}

void ShaderEditPopup::ExecuteCode()
{
   if (mParent)
   {
      mParent->SetShaderCode(mCodeEntry->GetText(true));
   }
   Hide();
}

void ShaderEditPopup::Show()
{
   SetOwningContainer(mParent ? mParent->GetOwningContainer() : nullptr);
   SetPosition(mParent ? mParent->GetPosition().x + 20 : 100,
               mParent ? mParent->GetPosition().y + 20 : 100);
   TheSynth->PushModalFocusItem(this);
}

void ShaderEditPopup::Hide()
{
   TheSynth->PopModalFocusItem();
}

// ===== ShaderModule =====

ShaderModule::ShaderModule()
   : mShaderCode(GetDefaultShader())
{
}

ShaderModule::~ShaderModule()
{
   if (TheSynth && TheSynth->IsModalFocusItem(&mEditPopup))
      mEditPopup.Hide();
   CleanupShader();
   if (mFBO)
      delete mFBO;
   if (mDefaultTexture)
      glDeleteTextures(1, &mDefaultTexture);
}

void ShaderModule::CleanupShader()
{
   GlShaderUtil::DeleteProgram(mProgramId); mProgramId = 0;
   mVSId = 0; // already deleted as part of program cleanup
   mFSId = 0;
   if (mVBO) { glDeleteBuffers(1, &mVBO); mVBO = 0; }
   for (auto& p : mShaderParams)
   {
      if (p.slider) { RemoveUIControl(p.slider); p.slider->Delete(); p.slider = nullptr; }
      if (p.s1) { RemoveUIControl(p.s1); p.s1->Delete(); p.s1 = nullptr; }
      if (p.s2) { RemoveUIControl(p.s2); p.s2->Delete(); p.s2 = nullptr; }
      if (p.s3) { RemoveUIControl(p.s3); p.s3->Delete(); p.s3 = nullptr; }
   }
   mShaderParams.clear();
}

void ShaderModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   mInputCable = new PatchCableSource(this, kConnectionType_Special);
   mInputCable->SetManualPosition(5, mHeight / 2);
   mInputCable->SetManualSide(PatchCableSource::Side::kLeft);
   AddPatchCableSource(mInputCable);

   mOutputCable = new PatchCableSource(this, kConnectionType_Special);
   mOutputCable->SetManualPosition(mWidth - 15, mHeight / 2);
   mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   AddPatchCableSource(mOutputCable);

   mSpeedSlider = new FloatSlider(this, "speed", -1, -1, 100, 15, &mSpeed, 0.0f, 2.0f);
   mResolutionSlider = new FloatSlider(this, "res", -1, -1, 100, 15, &mResolutionScale, 0.25f, 2.0f);
   mRandomButton = new ClickButton(this, "random", -1, -1);

   mEditPopup.SetParent(this);
   mEditPopup.CreateUIControls();

   mShaderDirty = true;
   mShaderStartTime = gTime;
}

static const char* sDefaultShader = R"(
gl_FragColor = vec4(uv, 0.5 + 0.5*sin(u_time), 1.0);
)";

std::string ShaderModule::GetDefaultShader()
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
    // Tunnel
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
float tunnel = sin(r*30.0 - u_time*4.0 + a*4.0) * 0.5 + 0.5;
float shade = 1.0 / (r + 0.3);
gl_FragColor = vec4(
   tunnel * shade * (0.5 + 0.5*sin(a + u_time)),
   tunnel * shade * (0.5 + 0.5*sin(a + u_time + 2.0)),
   tunnel * shade * (0.3 + 0.3*sin(a + u_time + 4.0)),
   1.0
);
)",
    // Julia fractal
    R"(
vec2 p = (uv - 0.5) * 3.0;
p.x *= u_resolution.x / u_resolution.y;
vec2 c = vec2(0.7885*cos(u_time*0.3), 0.7885*sin(u_time*0.3));
vec2 z = p;
float iter = 0.0;
for(int i = 0; i < 64; i++) {
   z = vec2(z.x*z.x - z.y*z.y, 2.0*z.x*z.y) + c;
   if(length(z) > 2.0) break;
   iter += 1.0;
}
float color = iter / 64.0;
gl_FragColor = vec4(
   0.5 + 0.5*sin(color*6.0 + 0.0),
   0.5 + 0.5*sin(color*6.0 + 2.0),
   0.5 + 0.5*sin(color*6.0 + 4.0),
   1.0
);
)",
    // Starfield
    R"(
vec2 p = (uv - 0.5) * 10.0;
p.x *= u_resolution.x / u_resolution.y;
vec2 i = floor(p);
vec2 f = fract(p) - 0.5;
float star = 0.0;
for(int y = -1; y <= 1; y++) {
   for(int x = -1; x <= 1; x++) {
      vec2 neighbor = vec2(float(x), float(y));
      vec2 offset = vec2(sin(dot(i + neighbor, vec2(127.1, 311.7))),
                         cos(dot(i + neighbor, vec2(269.5, 183.3)))) * 0.5;
      float d = length(f - neighbor - offset);
      float brightness = 0.5 + 0.5*sin(dot(i + neighbor, vec2(u_time*0.5)));
      star += 0.005 / (d*d + 0.005) * (0.3 + 0.7*brightness);
   }
}
gl_FragColor = vec4(star*0.8, star*0.6, star, 1.0);
)",
    // Fire
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float d = length(p);
float flicker = sin(p.y*20.0 - u_time*5.0) * 0.5 + 0.5;
float flame = sin(p.x*10.0 + u_time*3.0 + p.y*15.0) * 0.5 + 0.5;
flame += sin(p.x*8.0 - u_time*4.0 + p.y*12.0) * 0.3;
flame += sin(p.y*25.0 - u_time*6.0) * 0.2;
flame = flame / 1.5;
float glow = 1.0 - smoothstep(0.0, 0.8, d);
gl_FragColor = vec4(
   flame * glow * 1.5,
   flame * flame * glow * 0.8,
   flame * flame * flame * glow * 0.3,
   1.0
);
)",
    // Rotating polygons
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x) + u_time*0.5;
float r = length(p);
float sides = 5.0 + 3.0*sin(u_time*0.2);
a = mod(a, 6.2832/sides);
a = abs(a - 3.1416/sides);
float shape = 1.0 - smoothstep(0.1, 0.3, cos(a)*r);
float pulse = 0.5 + 0.5*sin(r*10.0 - u_time*2.0);
gl_FragColor = vec4(
   shape * (0.5 + 0.5*sin(r*8.0 + u_time)),
   shape * (0.3 + 0.3*sin(r*8.0 + u_time + 2.0)),
   shape * pulse,
   1.0
);
)",
    // Pulsing rings
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float r = length(p);
float rings = 0.0;
for(float i = 0.0; i < 5.0; i++) {
   float phase = u_time*(1.0 + i*0.3) + i*1.5;
   float radius = 0.15 + i*0.12 + 0.1*sin(phase);
   rings += smoothstep(0.02, 0.0, abs(r - radius));
}
float glow = rings * (1.0 - smoothstep(0.0, 0.7, r));
gl_FragColor = vec4(
   glow * (0.5 + 0.5*sin(u_time)),
   glow * (0.5 + 0.5*sin(u_time + 1.5)),
   glow,
   1.0
);
)",
    // Spiral
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
float spiral = sin(a*5.0 - r*20.0 + u_time*3.0) * 0.5 + 0.5;
spiral *= 1.0 - smoothstep(0.0, 0.8, r);
float twist = sin(a*3.0 + u_time) * 0.2;
p += twist;
float inner = 1.0 - smoothstep(0.0, 0.15, r);
gl_FragColor = vec4(
   spiral * (0.5 + 0.5*sin(a + u_time)),
   spiral * (0.3 + 0.3*cos(a*2.0 + u_time)),
   spiral * inner * 0.5 + inner * 0.3,
   1.0
);
)",
    // Voronoi-like cells
    R"(
vec2 p = (uv - 0.5) * 6.0;
p.x *= u_resolution.x / u_resolution.y;
vec2 i = floor(p);
vec2 f = fract(p) - 0.5;
float minDist = 1.0;
vec2 minOffset;
for(int y = -1; y <= 1; y++) {
   for(int x = -1; x <= 1; x++) {
      vec2 neighbor = vec2(float(x), float(y));
      vec2 offset = vec2(sin(dot(i + neighbor, vec2(127.1, 311.7)) + u_time),
                         cos(dot(i + neighbor, vec2(269.5, 183.3)) + u_time*0.7)) * 0.5;
      float d = length(f - neighbor - offset);
      if(d < minDist) { minDist = d; minOffset = neighbor + offset; }
   }
}
float cellVal = sin(dot(minOffset, vec2(12.9898 + u_time, 78.233))) * 0.5 + 0.5;
gl_FragColor = vec4(
   cellVal * 0.8,
   minDist * 2.0,
   (1.0 - cellVal) * 0.6,
   1.0
);
)",
    // Twisted wave
    R"(
vec2 p = (uv - 0.5) * 4.0;
p.x *= u_resolution.x / u_resolution.y;
float wave = 0.0;
for(float i = 0.0; i < 4.0; i++) {
   float speed = 1.0 + i*0.5;
   float freq = 2.0 + i*1.5;
   vec2 dir = vec2(cos(i*1.5 + u_time*0.2), sin(i*2.0 + u_time*0.3));
   wave += sin(dot(p, dir)*freq + u_time*speed) * (1.0/(1.0 + i));
}
wave = wave * 0.3 + 0.5;
float mask = 1.0 - smoothstep(0.0, 1.5, length(p));
gl_FragColor = vec4(
   wave * mask,
   wave * wave * mask * 0.6,
   (1.0 - wave) * mask * 0.5,
   1.0
);
)",
    // Digital glitch
    R"(
vec2 p = uv;
float bars = step(0.5, fract(p.y * 30.0 + u_time*5.0));
float noiseVal = sin(dot(floor(p*vec2(50.0, 20.0)), vec2(12.9898, 78.233)) + u_time*10.0);
noiseVal = noiseVal * 0.5 + 0.5;
float glitch = step(0.95, noiseVal);
float offset = (noiseVal - 0.5) * glitch * 0.1;
p.x += offset;
vec3 col;
col.r = 0.5 + 0.5*sin(p.x*30.0 + u_time*2.0);
col.g = 0.3 + 0.3*sin(p.y*20.0 + u_time*1.5);
col.b = 0.5 + 0.5*sin((p.x+p.y)*15.0 + u_time);
col *= 1.0 + glitch * vec3(0.5, 0.0, 0.5);
col += bars * 0.1;
gl_FragColor = vec4(col, 1.0);
)",
    // Neon glow
    R"(
vec2 p = (uv - 0.5) * 3.0;
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
float shape = 0.0;
for(int i = 0; i < 6; i++) {
   float phase = float(i) * 1.047 + u_time * 0.5;
   vec2 pos = vec2(cos(phase), sin(phase)) * 0.6;
   float d = length(p - pos);
   shape += 0.002 / (d*d + 0.002);
}
float ring = 0.002 / (abs(r - 0.8) + 0.002);
ring += 0.002 / (abs(r - 0.4) + 0.002);
gl_FragColor = vec4(
   shape * (0.5 + 0.5*sin(u_time)) + ring,
   shape * (0.3 + 0.3*sin(u_time*1.3)) + ring*0.5,
   shape * (0.5 + 0.5*sin(u_time*0.7)) + ring*0.8,
   1.0
);
)",
    // Vortex
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
a += r * 5.0 - u_time * 2.0;
vec2 warp = vec2(cos(a), sin(a)) * r;
float pattern = sin(warp.x*10.0 + warp.y*8.0 + u_time) * 0.5 + 0.5;
pattern *= 1.0 - smoothstep(0.0, 0.9, r);
float core = 1.0 - smoothstep(0.0, 0.1, r);
gl_FragColor = vec4(
   pattern + core,
   pattern * (1.0 - smoothstep(0.3, 0.9, r)),
   pattern * pattern + core*0.5,
   1.0
);
)",
    // 3D wave grid
    R"(
vec2 p = (uv - 0.5) * 8.0;
p.x *= u_resolution.x / u_resolution.y;
float wave = sin(p.x*2.0 + u_time*2.0) * 0.3 + sin(p.y*2.0 + u_time*1.5) * 0.3;
float gridX = 1.0 - smoothstep(0.0, 0.05, abs(fract(p.x) - 0.5));
float gridY = 1.0 - smoothstep(0.0, 0.05, abs(fract(p.y) - 0.5));
float grid = max(gridX, gridY);
float height = wave * 0.5 + 0.5;
float light = height * (0.5 + 0.5*sin(p.x*3.0 + p.y*2.0));
gl_FragColor = vec4(
   light * (0.5 + 0.5*grid),
   light * (0.3 + 0.3*grid),
   light * (1.0 - grid) * 0.6,
   1.0
);
)",
    // Morphing checker
    R"(
vec2 p = uv * 8.0;
p.x *= u_resolution.x / u_resolution.y;
float morph = sin(u_time*0.5) * 0.5 + 0.5;
p += vec2(sin(p.y*2.0 + u_time)*0.3*morph, cos(p.x*2.0 - u_time)*0.3*(1.0-morph));
vec2 i = floor(p);
float checker = step(0.5, fract(dot(i, vec2(1.0, 1.0))*0.5));
float r = length(fract(p) - 0.5);
float glow = 0.01 / (r + 0.01);
gl_FragColor = vec4(
   checker * (0.5 + 0.5*sin(u_time)),
   (1.0-checker) * (0.3 + 0.3*sin(u_time*1.3)),
   glow * 0.5,
   1.0
);
)",
    // Rainbow spectrum
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
float bands = sin(r*20.0 - u_time*3.0 + a*3.0) * 0.5 + 0.5;
float hue = a / 6.2832 + u_time*0.1;
hue = fract(hue);
vec3 col = 0.5 + 0.5*vec3(
   sin(hue*6.2832 + 0.0),
   sin(hue*6.2832 + 2.094),
   sin(hue*6.2832 + 4.188)
);
col *= bands * (1.0 - smoothstep(0.0, 0.8, r));
col += (1.0 - bands) * vec3(0.02, 0.01, 0.05);
gl_FragColor = vec4(col, 1.0);
)",
    // Pulsing orbs
    R"(
vec2 p = (uv - 0.5) * 2.5;
p.x *= u_resolution.x / u_resolution.y;
float glow = 0.0;
for(int i = 0; i < 8; i++) {
   float phase = float(i) * 0.785 + u_time;
   vec2 pos = vec2(cos(phase*0.7)*0.8, sin(phase*0.5)*0.8);
   float pulse = 0.5 + 0.5*sin(u_time*(0.5 + float(i)*0.1) + float(i)*1.3);
   float d = length(p - pos);
   glow += 0.003 / (d*d + 0.003) * pulse;
}
float core = 0.01 / (length(p) + 0.01);
gl_FragColor = vec4(
   glow * (0.5 + 0.5*sin(u_time)),
   glow * 0.6,
   glow * (0.5 + 0.5*cos(u_time*0.7)),
   1.0
) + vec4(core*0.5, core, core*0.3, 0.0);
)",
    // Lissajous curve
    R"(
vec2 p = (uv - 0.5) * 2.0;
p.x *= u_resolution.x / u_resolution.y;
float trace = 0.0;
for(float t = 0.0; t < 1.0; t += 0.001) {
   float pt = t + u_time * 0.1;
   float x = sin(pt * 3.0 + u_time*0.5);
   float y = cos(pt * 4.0 + u_time*0.3);
   float d = length(p - vec2(x, y) * 0.8);
   trace += 0.002 / (d*d + 0.002);
}
float r = length(p);
float vignette = 1.0 - smoothstep(0.0, 1.5, r);
gl_FragColor = vec4(
   trace * vignette * (0.5 + 0.5*sin(u_time)),
   trace * vignette * (0.5 + 0.5*sin(u_time*1.3)),
   trace * vignette,
   1.0
);
)",
    // Geometric bloom
    R"(
vec2 p = (uv - 0.5);
p.x *= u_resolution.x / u_resolution.y;
float a = atan(p.y, p.x);
float r = length(p);
float bloom = 0.0;
for(int i = 0; i < 5; i++) {
   float fi = float(i);
   float sides = 3.0 + fi;
   float angle = a + u_time*(0.2 + fi*0.1);
   float segAngle = 6.2832 / sides;
   float normAngle = mod(angle + 3.1416/sides, segAngle) - 3.1416/sides;
   float shapeDist = abs(cos(normAngle)) * r;
   float glow = 0.002 / (shapeDist*shapeDist + 0.002);
   bloom += glow * (1.0 / (1.0 + fi*0.5));
}
bloom *= 1.0 - smoothstep(0.0, 0.9, r);
float center = 0.01 / (r + 0.01);
gl_FragColor = vec4(
   bloom + center,
   bloom * 0.5 + center,
   bloom * 0.2 + center,
   1.0
);
)",
};

void ShaderModule::RandomizeShader()
{
   static std::mt19937 rng(std::random_device{}());
   int numShaders = sizeof(sShaderBodies) / sizeof(sShaderBodies[0]);
   std::uniform_int_distribution<int> dist(0, numShaders - 1);
   int idx = dist(rng);
   mShaderCode = std::string(sShaderBodies[idx]) + "\n";
   mShaderDirty = true;
}

void ShaderModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mRandomButton)
   {
      RandomizeShader();
   }
}

void ShaderModule::PostRepatch(PatchCableSource* source, bool fromUserClick)
{
   if (source == mInputCable)
   {
      if (!mInputCable->GetPatchCables().empty())
      {
         auto* target = mInputCable->GetPatchCables()[0]->GetTarget();
         mInputSource = dynamic_cast<IVisualSource*>(target);
      }
      else
      {
         mInputSource = nullptr;
      }
   }
}

void ShaderModule::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
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

void ShaderModule::CreateFBO()
{
   if (mFBO)
      delete mFBO;
   mFBO = new VisualFBO();
   int fboW = std::max(64, (int)(mWidth * mResolutionScale));
   int fboH = std::max(64, (int)(mHeight * mResolutionScale));
   mFBO->Create(fboW, fboH);
}

void ShaderModule::CompileShader()
{
   mLastError.clear();

   for (auto& p : mShaderParams)
   {
      if (p.slider) { RemoveUIControl(p.slider); p.slider->Delete(); p.slider = nullptr; }
      if (p.s1) { RemoveUIControl(p.s1); p.s1->Delete(); p.s1 = nullptr; }
      if (p.s2) { RemoveUIControl(p.s2); p.s2->Delete(); p.s2 = nullptr; }
      if (p.s3) { RemoveUIControl(p.s3); p.s3->Delete(); p.s3 = nullptr; }
   }
   mShaderParams.clear();

   if (mShaderCode.empty())
   {
      mLastError = "shader source is empty";
      CleanupShader();
      return;
   }

   std::string code = mShaderCode;

   code = StripISFMetadata(code);

   {
      size_t incPos = 0;
      while ((incPos = code.find("#include", incPos)) != std::string::npos)
      {
         size_t eol = code.find('\n', incPos);
         if (eol == std::string::npos)
         {
            code.erase(incPos);
            break;
         }
         code.erase(incPos, eol - incPos + 1);
      }
   }

   bool hasVersion = (code.find("#version") != std::string::npos);
   bool hasMain = (code.find("void main") != std::string::npos);

   if (!hasVersion)
   {
      bool convertedToShadertoy = false;

      if (!hasMain && HasMainImage(code))
      {
         code = ConvertShadertoy(code);
         hasMain = true;
         convertedToShadertoy = true;
      }

      if (!convertedToShadertoy && HasGLSLES(code))
         code = ConvertGLSLES(code);
   }

   {
      std::vector<std::string> builtInNames = { "u_time", "u_resolution", "u_millis", "u_speed", "u_texture" };
      size_t upos = 0;
      while ((upos = code.find("uniform ", upos)) != std::string::npos)
      {
         size_t semi = code.find(';', upos);
         if (semi == std::string::npos) break;
         std::string fullDecl = code.substr(upos, semi - upos + 1);
         bool isBuiltIn = false;
         for (const auto& bn : builtInNames)
            if (fullDecl.find(bn) != std::string::npos) { isBuiltIn = true; break; }
         if (isBuiltIn)
            code.erase(upos, semi - upos + 1);
         else
            upos = semi + 1;
      }
   }

   std::string fsSource;
   if (hasVersion)
   {
      fsSource = code;
   }
   else
   {
      fsSource += "#version 150\n";
      fsSource += "uniform float u_time;\n";
      fsSource += "uniform vec2 u_resolution;\n";
      fsSource += "uniform float u_millis;\n";
      fsSource += "uniform float u_speed;\n";
      fsSource += "uniform sampler2D u_texture;\n";
      fsSource += "out vec4 fragColor;\n";
      fsSource += "#define gl_FragColor fragColor\n";

      if (hasMain)
      {
         fsSource += code;
      }
      else
      {
         fsSource += "void main() {\n";
         fsSource += "vec2 uv = gl_FragCoord.xy / u_resolution.xy;\n";
         fsSource += code;
         fsSource += "}\n";
      }
    }

    // Strip illegal characters from final source
    {
       size_t ipos = 0;
       while ((ipos = fsSource.find_first_of("`\x00\x01\x02\x03\x04\x05\x06\x07\x08\x0b\x0c\x0e\x0f", ipos)) != std::string::npos)
          fsSource.erase(ipos, 1);
    }

    CleanupShader();

   {
      std::vector<std::string> builtIns = {
         "u_time", "u_resolution", "u_millis", "u_speed",
         "fragColor", "gl_FragColor"
      };
      std::string::size_type upos = 0;
      while ((upos = fsSource.find("uniform ", upos)) != std::string::npos)
      {
         upos += 8;
         std::string::size_type semi = fsSource.find(';', upos);
         if (semi == std::string::npos) break;
         std::string decl = fsSource.substr(upos, semi - upos);
         while (!decl.empty() && decl[0] == ' ') decl.erase(0, 1);
         std::string::size_type space = decl.find(' ');
         if (space == std::string::npos) { upos = semi + 1; continue; }
         std::string type = decl.substr(0, space);
         std::string name = decl.substr(space + 1);
         std::string::size_type endName = name.find_first_of(" \t\r\n;/");
         if (endName != std::string::npos)
            name = name.substr(0, endName);
         if (name.empty()) { upos = semi + 1; continue; }

         int numComponents = 1;
         if (type == "float") numComponents = 1;
         else if (type == "vec2") numComponents = 2;
         else if (type == "vec3") numComponents = 3;
         else if (type == "vec4") numComponents = 4;
         else { upos = semi + 1; continue; }

         bool isBuiltIn = false;
         for (const auto& b : builtIns)
         {
            if (name == b) { isBuiltIn = true; break; }
         }
         if (isBuiltIn) { upos = semi + 1; continue; }

         bool already = false;
         for (const auto& p : mShaderParams)
         {
            if (p.name == name) { already = true; break; }
         }
         if (already) { upos = semi + 1; continue; }

         ShaderParam p;
         p.name = name;
         p.type = type;
         p.numComponents = numComponents;
         p.value = 0;
         p.slider = new FloatSlider(this, name.c_str(), -1, -1, 100, 15, &p.value, 0, 1);
         if (numComponents >= 2) p.s1 = new FloatSlider(this, (name + ".x").c_str(), -1, -1, 100, 15, &p.c1, 0, 1);
         if (numComponents >= 3) p.s2 = new FloatSlider(this, (name + ".y").c_str(), -1, -1, 100, 15, &p.c2, 0, 1);
         if (numComponents >= 4) p.s3 = new FloatSlider(this, (name + ".z").c_str(), -1, -1, 100, 15, &p.c3, 0, 1);
         mShaderParams.push_back(p);
         upos = semi + 1;
      }
   }

   // Vertex shader
   const char* vsSource = R"(
#version 150
in vec2 a_position;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

   std::string error;
   mProgramId = GlShaderUtil::CompileAndLink(vsSource, fsSource, &error);
   if (!mProgramId)
   {
      mLastError = error;
      CleanupShader();
      return;
   }
    mVSId = 0;
    mFSId = 0;

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

void ShaderModule::DrawModule()
{
   ofSetColor(40, 40, 40);
   ofFill();
   ofRect(0, 0, mWidth, mHeight);

   const float miniEditorH = 55.0f;
   const float sliderRowH = 18.0f;
   const float builtInRowY = 2; // speed + res + random row index

   int totalSlots = 0;
   for (const auto& p : mShaderParams) totalSlots += p.numComponents;
   int sliderRows = (totalSlots + 1) / 2;

   float builtInRowH = sliderRowH;
   float dynamicSlidersH = sliderRows * sliderRowH;
   float totalSlidersH = builtInRowH + dynamicSlidersH + 2;

   float previewH = mHeight - totalSlidersH - miniEditorH - 6;

   // === Preview area ===
   float previewY = 2;
   if (previewH > 20)
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

   float sliderY = previewY + previewH + 2;

   // === Speed + Resolution sliders ===
   if (mSpeedSlider)
   {
      mSpeedSlider->SetPosition(3, sliderY);
      mSpeedSlider->SetDimensions(mWidth / 3 - 4, 15);
      mSpeedSlider->Draw();
   }
   if (mResolutionSlider)
   {
      mResolutionSlider->SetPosition(mWidth / 3 + 4, sliderY);
      mResolutionSlider->SetDimensions(mWidth / 3 - 4, 15);
      mResolutionSlider->Draw();
   }
   if (mRandomButton)
   {
      mRandomButton->SetPosition(2 * mWidth / 3 + 4, sliderY);
      mRandomButton->Draw();
   }

   // === Dynamic shader sliders ===
   float paramY = sliderY + sliderRowH;
   int slot = 0;
   float colW = (mWidth - 9) / 2;
   for (auto& p : mShaderParams)
   {
      for (int c = 0; c < p.numComponents; c++)
      {
         FloatSlider* s = c == 0 ? p.slider : (c == 1 ? p.s1 : (c == 2 ? p.s2 : p.s3));
         int col = slot % 2;
         int row = slot / 2;
         float sx = col == 0 ? 3 : mWidth / 2 + 3;
         float sy = paramY + row * 18;
         if (s) { s->SetPosition(sx, sy); s->SetDimensions(colW, 15); s->Draw(); }
         slot++;
      }
   }

   // === Mini editor bar ===
   float editorY = mHeight - miniEditorH + 4;
   ofSetColor(30, 30, 40);
   ofFill();
   ofRect(2, editorY - 2, mWidth - 4, miniEditorH - 2);

   ofSetColor(100, 100, 120);
   nvgFontSize(gNanoVG, 11);
   nvgTextAlign(gNanoVG, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

   std::string preview = GetCodePreview();
   if (!preview.empty())
   {
      std::string line1, line2;
      size_t nl = preview.find('\n');
      if (nl != std::string::npos)
      {
         line1 = preview.substr(0, nl);
         line2 = preview.substr(nl + 1);
         if (line2.size() > 50) line2 = line2.substr(0, 50) + "...";
      }
      else
      {
         line1 = preview;
      }
      if (line1.size() > 50) line1 = line1.substr(0, 50) + "...";

      ofSetColor(180, 180, 200);
      nvgText(gNanoVG, 8, editorY, line1.c_str(), nullptr);
      ofSetColor(140, 140, 160);
      nvgText(gNanoVG, 8, editorY + 14, line2.c_str(), nullptr);
   }
   ofSetColor(80, 120, 200);
   nvgFontSize(gNanoVG, 11);
   nvgText(gNanoVG, 8, editorY + 30, "click to edit shader >>>", nullptr);

   // === Error text ===
   if (!mLastError.empty())
   {
      nvgBeginPath(gNanoVG);
      nvgFillColor(gNanoVG, nvgRGBA(255, 0, 0, 255));
      float tx = 5;
      float ty = mHeight - 80;
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

void ShaderModule::OnClicked(float x, float y, bool right)
{
   IDrawableModule::OnClicked(x, y, right);

   const float miniEditorH = 55.0f;
   float editorY = mHeight - miniEditorH + 4;

   if (y >= editorY - 2 && y <= editorY + miniEditorH)
   {
      OpenEditPopup();
   }
}

void ShaderModule::PostRender()
{
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

   mFBO->Bind();

   float time = (float)(gTime - mShaderStartTime) * mSpeed;
   int fboW = mFBO->GetWidth();
   int fboH = mFBO->GetHeight();

    glUseProgram(mProgramId);
    glUniform1f(GlShaderUtil::GetUniformLocation(mProgramId, "u_time"), time);
    glUniform2f(GlShaderUtil::GetUniformLocation(mProgramId, "u_resolution"), (float)fboW, (float)fboH);
    glUniform1f(GlShaderUtil::GetUniformLocation(mProgramId, "u_millis"), time * 1000.0f);
    glUniform1f(GlShaderUtil::GetUniformLocation(mProgramId, "u_speed"), mSpeed);

    // Bind input texture
    if (mInputSource)
    {
       VisualFBO* srcFBO = mInputSource->GetFBO();
       if (srcFBO && srcFBO->IsValid())
       {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, srcFBO->GetTexture());
       }
    }
    else
    {
       if (mDefaultTexture == 0)
       {
          glGenTextures(1, &mDefaultTexture);
          glBindTexture(GL_TEXTURE_2D, mDefaultTexture);
          unsigned char white[] = { 255, 255, 255, 255 };
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
       }
       glActiveTexture(GL_TEXTURE0);
       glBindTexture(GL_TEXTURE_2D, mDefaultTexture);
    }
    glUniform1i(GlShaderUtil::GetUniformLocation(mProgramId, "u_texture"), 0);

    for (const auto& p : mShaderParams)
    {
       GLint loc = GlShaderUtil::GetUniformLocation(mProgramId, p.name.c_str());
       if (loc < 0) continue;
       if (p.type == "float")
          glUniform1f(loc, p.value);
       else if (p.type == "vec2")
          glUniform2f(loc, p.value, p.c1);
       else if (p.type == "vec3")
          glUniform3f(loc, p.value, p.c1, p.c2);
       else if (p.type == "vec4")
          glUniform4f(loc, p.value, p.c1, p.c2, p.c3);
    }

   glBindBuffer(GL_ARRAY_BUFFER, mVBO);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glDisableVertexAttribArray(0);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glUseProgram(0);

   mFBO->Unbind();
}

void ShaderModule::GetModuleDimensions(float& w, float& h)
{
   w = mWidth;
   h = mHeight;
}

void ShaderModule::Resize(float w, float h)
{
   bool sizeChanged = (mWidth != w || mHeight != h);
   mWidth = w;
   mHeight = h;

   if (mOutputCable)
   {
      mOutputCable->SetManualPosition(mWidth - 15, mHeight / 2);
      mOutputCable->SetManualSide(PatchCableSource::Side::kRight);
   }

   if (sizeChanged && mFBO)
   {
      delete mFBO;
      mFBO = nullptr;
   }
}

void ShaderModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo, "");
   mModuleSaveData.LoadString("code", moduleInfo, "");
}

void ShaderModule::SaveLayout(ofxJSONElement& moduleInfo)
{
   mModuleSaveData.SetString("code", mShaderCode);
   mModuleSaveData.Save(moduleInfo);
}

void ShaderModule::SetUpFromSaveData()
{
   std::string savedCode = mModuleSaveData.GetString("code");
   if (!savedCode.empty())
   {
      mShaderCode = savedCode;
      mShaderDirty = true;
   }
}

void ShaderModule::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << mShaderCode;
   out << mResolutionScale;
   out << mSpeed;
}

void ShaderModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   std::string code;
   in >> code;
   if (!code.empty())
   {
      mShaderCode = code;
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
   if (rev >= 2)
   {
      in >> mSpeed;
   }
}

std::string ShaderModule::GetCodePreview() const
{
   if (mShaderCode.empty())
      return "";
   size_t start = 0;
   // skip leading whitespace
   while (start < mShaderCode.size() && (mShaderCode[start] == ' ' || mShaderCode[start] == '\t' || mShaderCode[start] == '\n'))
      start++;
   std::string trimmed = mShaderCode.substr(start);
   if (trimmed.empty()) return "";

   size_t n = 0;
   for (int i = 0; i < 3; i++)
   {
      size_t nl = trimmed.find('\n', n);
      if (nl == std::string::npos)
         return trimmed.substr(n);
      n = nl + 1;
   }
   return trimmed.substr(0, n - 1);
}

void ShaderModule::SetShaderCode(const std::string& code)
{
   mShaderCode = code;
   mShaderDirty = true;
}

std::string ShaderModule::GetShaderCode() const
{
   return mShaderCode;
}

void ShaderModule::OpenEditPopup()
{
   mEditPopup.GetCodeEntry()->SetText(mShaderCode);
   mEditPopup.Show();
}

void ShaderModule::CloseEditPopup()
{
   mEditPopup.Hide();
}
