#include "VirtualFileSystem.h"
namespace aetheris {
bool VirtualFileSystem::Ensure(const std::filesystem::path&p)const{
 std::error_code ec; std::filesystem::create_directories(p,ec);
 return !ec && std::filesystem::is_directory(p,ec) && !ec;
}
bool VirtualFileSystem::MountNewProject()const{
 constexpr const char* dirs[]={
 "assets/models","assets/textures","assets/shaders/glsl","assets/shaders/spirv",
 "assets/materials","assets/scenes","cache/pipelines","cache/shader","cache/derived"};
 if(!Ensure(root_))return false;
 for(auto*d:dirs)if(!Ensure(root_/d))return false;
 return true;
}
std::filesystem::path VirtualFileSystem::Resolve(std::string_view v)const{
 std::filesystem::path p(v);
 if(p.is_absolute()||v.find("..")!=std::string_view::npos)return{};
 return root_/p;
}
}