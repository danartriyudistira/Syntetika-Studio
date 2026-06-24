#include "GlShaderUtil.h"
#include "juce_opengl/juce_opengl.h"
using namespace juce::gl;

#include <cstdio>
#include <algorithm>

namespace
{
   // Per-program uniform location cache
   struct UniformCache
   {
      std::unordered_map<std::string, int> locations;
   };
   std::unordered_map<unsigned int, UniformCache> sUniformCaches;
}

unsigned int GlShaderUtil::CompileShader(const std::string& source, unsigned int type, std::string* errorMsg)
{
   unsigned int shader = glCreateShader(type);
   if (shader == 0)
   {
      if (errorMsg) *errorMsg = "glCreateShader failed (returned 0)";
      return 0;
   }

   const char* src = source.c_str();
   glShaderSource(shader, 1, &src, nullptr);
   glCompileShader(shader);

   GLint compiled;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (compiled)
      return shader;

   // Compilation failed — get log
   char log[4096];
   GLsizei logLen = 0;
   glGetShaderInfoLog(shader, sizeof(log), &logLen, log);
   if (errorMsg)
      *errorMsg = std::string(log, logLen > 0 ? (size_t)logLen : 0);

   glDeleteShader(shader);
   return 0;
}

unsigned int GlShaderUtil::LinkProgram(const std::vector<unsigned int>& shaders, std::string* errorMsg)
{
   unsigned int program = glCreateProgram();
   if (program == 0)
   {
      if (errorMsg) *errorMsg = "glCreateProgram failed (returned 0)";
      return 0;
   }

   for (auto s : shaders)
      glAttachShader(program, s);

   glLinkProgram(program);

   GLint linked;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (linked)
      return program;

   char log[4096];
   GLsizei logLen = 0;
   glGetProgramInfoLog(program, sizeof(log), &logLen, log);
   if (errorMsg)
      *errorMsg = std::string(log, logLen > 0 ? (size_t)logLen : 0);

   glDeleteProgram(program);
   return 0;
}

unsigned int GlShaderUtil::CompileAndLink(const std::string& vertexSource, const std::string& fragmentSource,
                                           std::string* errorMsg)
{
   std::string vsErr, fsErr;
   unsigned int vs = CompileShader(vertexSource, GL_VERTEX_SHADER, &vsErr);
   if (!vs)
   {
      if (errorMsg) *errorMsg = "Vertex shader error: " + vsErr;
      return 0;
   }

   unsigned int fs = CompileShader(fragmentSource, GL_FRAGMENT_SHADER, &fsErr);
   if (!fs)
   {
      if (errorMsg) *errorMsg = "Fragment shader error: " + fsErr;
      glDeleteShader(vs);
      return 0;
   }

   std::vector<unsigned int> shaders = { vs, fs };
   std::string linkErr;
   unsigned int program = LinkProgram(shaders, &linkErr);
   if (!program)
   {
      if (errorMsg) *errorMsg = "Link error: " + linkErr;
      glDeleteShader(vs);
      glDeleteShader(fs);
      return 0;
   }

   // Shaders are now linked into program; intermediate objects can be deleted
   glDeleteShader(vs);
   glDeleteShader(fs);

   return program;
}

void GlShaderUtil::DeleteShader(unsigned int shader)
{
   if (shader != 0 && glIsShader(shader))
      glDeleteShader(shader);
}

void GlShaderUtil::DeleteProgram(unsigned int program)
{
   if (program != 0 && glIsProgram(program))
   {
      ClearUniformCache(program);
      glDeleteProgram(program);
   }
}

int GlShaderUtil::GetUniformLocation(unsigned int program, const std::string& name)
{
   auto& cache = sUniformCaches[program];
   auto it = cache.locations.find(name);
   if (it != cache.locations.end())
      return it->second;

   int loc = glGetUniformLocation(program, name.c_str());
   cache.locations[name] = loc;
   return loc;
}

void GlShaderUtil::ClearUniformCache(unsigned int program)
{
   sUniformCaches.erase(program);
}

void GlShaderUtil::ClearAllUniformCaches()
{
   sUniformCaches.clear();
}
