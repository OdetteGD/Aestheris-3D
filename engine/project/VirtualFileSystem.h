#pragma once
#include <filesystem>
#include <string_view>
namespace aetheris {
class VirtualFileSystem final {
 std::filesystem::path root_;
 bool Ensure(const std::filesystem::path&) const;
public:
 explicit VirtualFileSystem(std::filesystem::path root):root_(std::move(root)){}
 bool MountNewProject() const;
 std::filesystem::path Resolve(std::string_view virtualPath) const;
 const auto& Root() const noexcept{return root_;}
};
}