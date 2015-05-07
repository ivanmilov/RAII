#include <cassert>
#include <memory>  // std::addressof
 
template<typename ResourceTag, typename ResourceType> class Resource {
public:
  Resource() noexcept = default;
  explicit Resource(ResourceType resource) noexcept : resource_{ resource } {}
 
  Resource(const Resource&) = delete;
  Resource& operator=(const Resource&) = delete;
 
  Resource(Resource&& other) noexcept : resource_{ other.resource_ } {
    other.resource_ = {};
  }
  
  Resource& operator=(Resource&& other) noexcept {
    assert(this != std::addressof(other));
    Cleanup();
    resource_ = other.resource_;
    other.resource_ = {};
    return *this;
  }
 
  ~Resource() { Cleanup(); }
    
  operator const ResourceType&() const noexcept { return resource_; }  
  
  ResourceType* operator&() noexcept {
    Cleanup();
    return &resource_;
  }
 
private:
  static constexpr bool False() noexcept { return false; }
    
  void Cleanup() noexcept {
    static_assert(False(), "This function must be explicitly specialized.");
  }
 
  ResourceType resource_{};
};


//--------------------------------------------------------------------------
// Example usage. 
//--------------------------------------------------------------------------

#include <iostream>

// Resources.
using Handle = void*;
using FileHandle = void*;    
void CloseHandle(Handle) { /*...*/ }
void CloseFileHandle(FileHandle) { /*...*/ }

// Specialization for Handle.
using ScopedHandle = Resource<struct HandleTag, Handle>;
template<> inline void ScopedHandle::Cleanup() noexcept {
  std::cout << "CloseHandle()" << std::endl;
  if (resource_)
    CloseHandle(resource_);
}
 
// Specialization for FileHandle.
using ScopedFileHandle = Resource<struct FileHandleTag, FileHandle>;
template<> inline void ScopedFileHandle::Cleanup() noexcept {
  std::cout << "CloseFileHandle()" << std::endl;
  if (resource_)
    CloseFileHandle(resource_);
}

int main() {
  ScopedHandle handle;
  ScopedFileHandle file_handle;
}
