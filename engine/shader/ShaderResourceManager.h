#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
namespace aetheris{
class ShaderResourceManager final{
public:
 bool LoadGLSL(const std::filesystem::path&,std::string&)const;
 bool LoadSPIRV(const std::filesystem::path&,std::vector<uint32_t>&)const;
 static bool ValidateSPIRV(std::span<const uint32_t>)noexcept;
 // Shipping builds return false here: shaderc belongs in an editor/import worker, never the render thread.
 bool CompileGLSLToSPIRV(std::string_view,std::string_view,std::vector<uint32_t>&)const;
};
}