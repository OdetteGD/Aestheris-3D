#include "ShaderResourceManager.h"
#include <fstream>
namespace aetheris{
bool ShaderResourceManager::LoadGLSL(const std::filesystem::path&p,std::string&o)const{std::ifstream f(p,std::ios::binary);if(!f)return false;o.assign(std::istreambuf_iterator<char>(f),{});return !o.empty();}
bool ShaderResourceManager::LoadSPIRV(const std::filesystem::path&p,std::vector<uint32_t>&o)const{std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)return false;auto n=f.tellg();if(n<=0||n%4)return false;f.seekg(0);o.resize(static_cast<size_t>(n)/4);f.read(reinterpret_cast<char*>(o.data()),n);return f.good()||f.eof();}
bool ShaderResourceManager::ValidateSPIRV(std::span<const uint32_t>w)noexcept{return w.size()>=5&&w[0]==0x07230203u;}
bool ShaderResourceManager::CompileGLSLToSPIRV(std::string_view,std::string_view,std::vector<uint32_t>&)const{return false;}
}