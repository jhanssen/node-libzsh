#ifndef NODE_LIBZSH_ZLE_SESSION_H
#define NODE_LIBZSH_ZLE_SESSION_H

#include <napi.h>
#include <string>
#include <memory>

namespace node_libzsh {

// Forward declaration
class ZLESessionImpl;

// ZLE Session options
struct ZLESessionOptions {
    std::string keymap = "emacs";  // Default keymap
};

// Widget execution result
struct WidgetResult {
    bool success;
    std::string error;
    int returnValue;
};

// Key feed result
struct KeyFeedResult {
    bool success;
    std::string error;
    bool complete;      // True if a complete command was entered
    std::string line;   // Current line content
};

// ZLE Session wrapper class
class ZLESession : public Napi::ObjectWrap<ZLESession> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    ZLESession(const Napi::CallbackInfo& info);
    ~ZLESession();

private:
    // Line buffer operations
    Napi::Value GetLine(const Napi::CallbackInfo& info);
    Napi::Value SetLine(const Napi::CallbackInfo& info);
    Napi::Value GetCursor(const Napi::CallbackInfo& info);
    Napi::Value SetCursor(const Napi::CallbackInfo& info);

    // Text manipulation
    Napi::Value Insert(const Napi::CallbackInfo& info);
    Napi::Value DeleteForward(const Napi::CallbackInfo& info);
    Napi::Value DeleteBackward(const Napi::CallbackInfo& info);

    // Keymap operations
    Napi::Value GetKeymap(const Napi::CallbackInfo& info);
    Napi::Value SetKeymap(const Napi::CallbackInfo& info);

    // Widget operations
    Napi::Value ExecuteWidget(const Napi::CallbackInfo& info);
    Napi::Value BindKey(const Napi::CallbackInfo& info);

    // Key input
    Napi::Value FeedKeys(const Napi::CallbackInfo& info);

    // State
    Napi::Value GetState(const Napi::CallbackInfo& info);
    Napi::Value Destroy(const Napi::CallbackInfo& info);

    // Implementation
    std::unique_ptr<ZLESessionImpl> impl_;
    bool destroyed_ = false;
};

// Factory function
Napi::Value CreateZLESession(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_ZLE_SESSION_H
