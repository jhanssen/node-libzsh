#ifndef NODE_LIBZSH_INIT_H
#define NODE_LIBZSH_INIT_H

#include <napi.h>

namespace node_libzsh {

// Options for initialization
struct InitOptions {
    bool enableZLE = true;
    bool enableParser = true;
};

// Initialize libzsh subsystems
// Must be called before any other libzsh operations
bool initializeLibzsh(const InitOptions& options = {});

// Shutdown libzsh subsystems
// Should be called on module cleanup
void shutdownLibzsh();

// Check if libzsh is initialized
bool isInitialized();

// N-API wrappers
Napi::Value Initialize(const Napi::CallbackInfo& info);
Napi::Value Shutdown(const Napi::CallbackInfo& info);
Napi::Value IsInitialized(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_INIT_H
