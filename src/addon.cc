#include <napi.h>

// Module components
#include "init/libzsh_init.h"
#include "parser/parser.h"
#include "parser/preprocess.h"
#include "zle/zle_session.h"
#include "zle/widget_registry.h"
#include "zle/completion.h"

namespace node_libzsh {

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // Lifecycle functions
    exports.Set("initialize", Napi::Function::New(env, Initialize));
    exports.Set("shutdown", Napi::Function::New(env, Shutdown));
    exports.Set("isInitialized", Napi::Function::New(env, IsInitialized));

    // Parser functions
    exports.Set("parse", Napi::Function::New(env, Parse));
    exports.Set("validate", Napi::Function::New(env, Validate));
    exports.Set("generate", Napi::Function::New(env, Generate));
    exports.Set("extractPipelines", Napi::Function::New(env, ExtractPipelines));

    // Preprocessing for hybrid shell
    exports.Set("preprocess", Napi::Function::New(env, Preprocess));
    exports.Set("restoreJs", Napi::Function::New(env, RestoreJs));

    // ZLE Session class
    ZLESession::Init(env, exports);
    exports.Set("createZLESession", Napi::Function::New(env, CreateZLESession));

    // Widget functions
    exports.Set("registerWidget", Napi::Function::New(env, RegisterWidget));
    exports.Set("unregisterWidget", Napi::Function::New(env, UnregisterWidget));
    exports.Set("getCustomWidgets", Napi::Function::New(env, GetCustomWidgets));

    // Completion functions
    exports.Set("registerCompleter", Napi::Function::New(env, RegisterCompleter));
    exports.Set("unregisterCompleter", Napi::Function::New(env, UnregisterCompleter));
    exports.Set("getCompletionContext", Napi::Function::New(env, GetCompletionContext));
    exports.Set("addCompletions", Napi::Function::New(env, AddCompletions));

    return exports;
}

NODE_API_MODULE(node_libzsh, Init)

} // namespace node_libzsh
