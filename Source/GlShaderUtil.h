#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace GlShaderUtil
{
   // Compile a single shader from source. Returns OpenGL handle (0 on error).
   // errorMsg is populated on failure.
   unsigned int CompileShader(const std::string& source, unsigned int type, std::string* errorMsg = nullptr);

   // Link a shader program from a list of compiled shader handles.
   // Returns program handle (0 on error). errorMsg is populated on failure.
   unsigned int LinkProgram(const std::vector<unsigned int>& shaders, std::string* errorMsg = nullptr);

   // Compile vertex + fragment shaders and link into a program. One-shot convenience.
   // Returns program handle (0 on error). errorMsg is populated on failure.
   unsigned int CompileAndLink(const std::string& vertexSource, const std::string& fragmentSource,
                               std::string* errorMsg = nullptr);

   // Delete a shader object safely (checks for 0 and glIsShader).
   void DeleteShader(unsigned int shader);

   // Delete a program object safely (checks for 0 and glIsProgram).
   void DeleteProgram(unsigned int program);

   // Get cached uniform location (queries GL once per program+name, caches result).
   int GetUniformLocation(unsigned int program, const std::string& name);

   // Clear the uniform location cache for a specific program (call when program is recompiled).
   void ClearUniformCache(unsigned int program);

   // Clear all cached uniform locations.
   void ClearAllUniformCaches();
}
