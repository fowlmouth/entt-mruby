#pragma once

#include <memory>
#include <cxxabi.h>
#include <string>

namespace MRuby
{
  std::string demangle_cxx_type_name(const char* mangled)
  {
      int status;
      std::unique_ptr<char[], void (*)(void*)> result(
          abi::__cxa_demangle(mangled, 0, 0, &status), std::free);
      return result.get() ? std::string(result.get()) : "error occurred";
  }

  // needs to be free'd with std::free
  template<typename T>
  const char* type_name_unsafe()
  {
    int status;
    return abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
  }

  template<typename T>
  std::string type_name()
  {
    const char* name = type_name_unsafe<T>();
    std::string str;
    if(name)
    {
      str = name;
      std::free((void*)name);
    }
    return str;
  }
}
