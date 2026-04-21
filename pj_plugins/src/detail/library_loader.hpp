#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"

namespace PJ::detail {

inline Expected<void*> loadLibraryHandle(std::string_view path) {
#if defined(_WIN32)
  HMODULE module = LoadLibraryA(std::string(path).c_str());
  if (module == nullptr) {
    return unexpected(std::string("LoadLibraryA failed"));
  }
  return reinterpret_cast<void*>(module);
#else
  // RTLD_DEEPBIND prevents symbol conflicts (e.g. Conan OpenSSL vs system libcrypto)
  // but is a glibc extension — not available on macOS or musl.
  // TODO: consider a Platform abstraction class (like pj_marketplace/PlatformUtils)
  //       to centralize OS-specific behavior.
  int flags = RTLD_NOW | RTLD_LOCAL;
  // RTLD_DEEPBIND is incompatible with AddressSanitizer runtime.
#if defined(__linux__) && defined(RTLD_DEEPBIND) && !defined(PJ_ASAN_ACTIVE)
  flags |= RTLD_DEEPBIND;
#endif
  void* handle = dlopen(std::string(path).c_str(), flags);
  if (handle == nullptr) {
    return unexpected(std::string(dlerror()));
  }
  return handle;
#endif
}

/// Resolve a named symbol from a loaded library handle.
inline Expected<void*> resolveSymbol(void* handle, const char* symbol_name) {
  if (handle == nullptr) {
    return unexpected(std::string("library not loaded"));
  }
#if defined(_WIN32)
  auto symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name);
  if (symbol == nullptr) {
    return unexpected(std::string(symbol_name) + " not found");
  }
  return reinterpret_cast<void*>(symbol);
#else
  dlerror();
  void* symbol = dlsym(handle, symbol_name);
  const char* err = dlerror();
  if (err != nullptr) {
    return unexpected(std::string(err));
  }
  return symbol;
#endif
}

/// Verify the plugin exports `pj_plugin_abi_version` and its value equals
/// PJ_ABI_VERSION. Must be called BEFORE the family vtable is fetched — the
/// vtable layout is only meaningful once the boot-level ABI matches.
inline Expected<void> checkPluginAbiVersion(void* handle) {
  auto sym = resolveSymbol(handle, "pj_plugin_abi_version");
  if (!sym) {
    return unexpected(std::string("plugin missing pj_plugin_abi_version symbol"));
  }
  const auto* plugin_abi = static_cast<const uint32_t*>(*sym);
  if (plugin_abi == nullptr || *plugin_abi != PJ_ABI_VERSION) {
    return unexpected(std::string("plugin pj_plugin_abi_version mismatch (expected 3)"));
  }
  return {};
}

inline void closeLibraryHandle(void* handle) {
  if (handle == nullptr) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

}  // namespace PJ::detail
