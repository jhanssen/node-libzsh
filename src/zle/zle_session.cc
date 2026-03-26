#include "zle_session.h"
#include "../init/libzsh_init.h"
#include "../util/thread_safety.h"
#include "../util/string_convert.h"
#include "../zsh_wrapper.h"
#include <cstring>
#include <cstdlib>

extern "C" {
// Additional declarations not in standard headers
extern void sizeline(int sz);
extern void setline(char *s, int flags);
extern void spaceinline(int ct);
extern void foredel(int ct, int flags);
extern void backdel(int ct, int flags);
extern char *zlelineasstring(ZLE_STRING_T instr, int inll, int incs,
                             int *outllp, int *outcsp, int useheap);
extern ZLE_STRING_T stringaszleline(char *instr, int incs,
                                    int *outll, int *outsz, int *outcs);
extern int execzlefunc(Thingy func, char **args, int set_bindk, int set_lbindk);
}

// Flags for setline
#define ZSL_COPY 1

namespace node_libzsh {

// Implementation class to hold ZLE state
class ZLESessionImpl {
public:
    ZLESessionImpl() : keymap_(nullptr), savedLine_(nullptr), savedLineLen_(0), savedCursor_(0) {
        // Initialize with default line buffer
        sizeline(256);
        zlell = 0;
        zlecs = 0;
    }

    ~ZLESessionImpl() {
        if (savedLine_) {
            free(savedLine_);
        }
    }

    // Save current ZLE state
    void saveState() {
        if (zleline && zlell > 0) {
            savedLine_ = static_cast<ZLE_CHAR_T*>(realloc(savedLine_,
                (zlell + 1) * sizeof(ZLE_CHAR_T)));
            memcpy(savedLine_, zleline, zlell * sizeof(ZLE_CHAR_T));
            savedLineLen_ = zlell;
        } else {
            savedLineLen_ = 0;
        }
        savedCursor_ = zlecs;
    }

    // Restore ZLE state
    void restoreState() {
        if (savedLine_ && savedLineLen_ > 0) {
            sizeline(savedLineLen_ + 10);
            memcpy(zleline, savedLine_, savedLineLen_ * sizeof(ZLE_CHAR_T));
            zlell = savedLineLen_;
        } else {
            zlell = 0;
        }
        zlecs = savedCursor_;
    }

    Keymap keymap_;
    ZLE_CHAR_T* savedLine_;
    int savedLineLen_;
    int savedCursor_;
};

Napi::Object ZLESession::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "ZLESession", {
        InstanceMethod("getLine", &ZLESession::GetLine),
        InstanceMethod("setLine", &ZLESession::SetLine),
        InstanceMethod("getCursor", &ZLESession::GetCursor),
        InstanceMethod("setCursor", &ZLESession::SetCursor),
        InstanceMethod("insert", &ZLESession::Insert),
        InstanceMethod("deleteForward", &ZLESession::DeleteForward),
        InstanceMethod("deleteBackward", &ZLESession::DeleteBackward),
        InstanceMethod("getKeymap", &ZLESession::GetKeymap),
        InstanceMethod("setKeymap", &ZLESession::SetKeymap),
        InstanceMethod("executeWidget", &ZLESession::ExecuteWidget),
        InstanceMethod("bindKey", &ZLESession::BindKey),
        InstanceMethod("feedKeys", &ZLESession::FeedKeys),
        InstanceMethod("getState", &ZLESession::GetState),
        InstanceMethod("destroy", &ZLESession::Destroy),
    });

    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    env.SetInstanceData(constructor);

    exports.Set("ZLESession", func);
    return exports;
}

ZLESession::ZLESession(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ZLESession>(info), impl_(new ZLESessionImpl()) {

    Napi::Env env = info.Env();

    if (!isInitialized()) {
        Napi::Error::New(env, "libzsh not initialized. Call initialize() first.").ThrowAsJavaScriptException();
        return;
    }

    LIBZSH_LOCK();

    ZLESessionOptions options;

    // Parse options
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object opts = info[0].As<Napi::Object>();
        if (opts.Has("keymap")) {
            options.keymap = opts.Get("keymap").As<Napi::String>().Utf8Value();
        }
    }

    // Set up keymap (keymaps are owned by the hash table, no ref-counting needed)
    Keymap km = openkeymap(const_cast<char*>(options.keymap.c_str()));
    if (km) {
        impl_->keymap_ = km;
        selectkeymap(const_cast<char*>(options.keymap.c_str()), 0);
    } else {
        // Fall back to main keymap
        km = openkeymap(const_cast<char*>("main"));
        if (km) {
            impl_->keymap_ = km;
            selectkeymap(const_cast<char*>("main"), 0);
        }
    }
}

ZLESession::~ZLESession() {
    // Keymaps are owned by the hash table, no cleanup needed
}

Napi::Value ZLESession::GetLine(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LIBZSH_LOCK();

    if (!zleline || zlell == 0) {
        return Napi::String::New(env, "");
    }

    int outll, outcs;
    char* str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    if (!str) {
        return Napi::String::New(env, "");
    }

    return Napi::String::New(env, str, outll);
}

Napi::Value ZLESession::SetLine(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string text = info[0].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    // setline modifies the string, so we need a copy
    char* copy = strdup(text.c_str());
    if (copy) {
        setline(copy, ZSL_COPY);
        free(copy);
    }

    return env.Undefined();
}

Napi::Value ZLESession::GetCursor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LIBZSH_LOCK();
    return Napi::Number::New(env, zlecs);
}

Napi::Value ZLESession::SetCursor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected number argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int pos = info[0].As<Napi::Number>().Int32Value();

    LIBZSH_LOCK();

    // Clamp to valid range
    if (pos < 0) pos = 0;
    if (pos > zlell) pos = zlell;

    zlecs = pos;

    return env.Undefined();
}

Napi::Value ZLESession::Insert(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string text = info[0].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    // Convert string to ZLE format
    char* copy = strdup(text.c_str());
    if (!copy) {
        return env.Undefined();
    }

    int newll, newsz, newcs;
    ZLE_STRING_T zleText = stringaszleline(copy, 0, &newll, &newsz, &newcs);
    free(copy);

    if (!zleText || newll == 0) {
        return env.Undefined();
    }

    // Make space in the line
    spaceinline(newll);

    // Copy the text at cursor position
    memcpy(zleline + zlecs, zleText, newll * sizeof(ZLE_CHAR_T));
    zlecs += newll;

    return env.Undefined();
}

Napi::Value ZLESession::DeleteForward(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int count = 1;
    if (info.Length() > 0 && info[0].IsNumber()) {
        count = info[0].As<Napi::Number>().Int32Value();
    }

    LIBZSH_LOCK();

    // Can't delete more than remaining characters
    if (zlecs + count > zlell) {
        count = zlell - zlecs;
    }

    if (count > 0) {
        foredel(count, 0);
    }

    return env.Undefined();
}

Napi::Value ZLESession::DeleteBackward(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int count = 1;
    if (info.Length() > 0 && info[0].IsNumber()) {
        count = info[0].As<Napi::Number>().Int32Value();
    }

    LIBZSH_LOCK();

    // Can't delete more than characters before cursor
    if (count > zlecs) {
        count = zlecs;
    }

    if (count > 0) {
        backdel(count, 0);
    }

    return env.Undefined();
}

Napi::Value ZLESession::GetKeymap(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LIBZSH_LOCK();

    // We don't have a direct way to get the keymap name
    // Return a generic identifier based on the current state
    if (curkeymap) {
        // Try to identify common keymaps
        Keymap emacs = openkeymap(const_cast<char*>("emacs"));
        Keymap viins = openkeymap(const_cast<char*>("viins"));
        Keymap vicmd = openkeymap(const_cast<char*>("vicmd"));

        if (curkeymap == emacs) return Napi::String::New(env, "emacs");
        if (curkeymap == viins) return Napi::String::New(env, "viins");
        if (curkeymap == vicmd) return Napi::String::New(env, "vicmd");

        return Napi::String::New(env, "custom");
    }

    return Napi::String::New(env, "unknown");
}

Napi::Value ZLESession::SetKeymap(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    Keymap km = openkeymap(const_cast<char*>(name.c_str()));
    if (!km) {
        Napi::Error::New(env, "Unknown keymap: " + name).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    impl_->keymap_ = km;
    selectkeymap(const_cast<char*>(name.c_str()), 0);

    return env.Undefined();
}

Napi::Value ZLESession::ExecuteWidget(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected widget name string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string widgetName = info[0].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    // Look up the widget (thingy)
    Thingy t = rthingy(const_cast<char*>(widgetName.c_str()));
    if (!t) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", false);
        result.Set("error", "Widget not found: " + widgetName);
        return result;
    }

    // Execute the widget
    int ret = execzlefunc(t, nullptr, 1, 0);

    Napi::Object result = Napi::Object::New(env);
    result.Set("success", ret == 0);
    result.Set("returnValue", ret);

    return result;
}

Napi::Value ZLESession::BindKey(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 3) {
        Napi::TypeError::New(env, "Expected (keymap, sequence, widget) arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsString() || !info[1].IsString() || !info[2].IsString()) {
        Napi::TypeError::New(env, "All arguments must be strings").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string keymapName = info[0].As<Napi::String>().Utf8Value();
    std::string sequence = info[1].As<Napi::String>().Utf8Value();
    std::string widgetName = info[2].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    // Get keymap
    Keymap km = openkeymap(const_cast<char*>(keymapName.c_str()));
    if (!km) {
        Napi::Error::New(env, "Unknown keymap: " + keymapName).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Get widget
    Thingy t = rthingy(const_cast<char*>(widgetName.c_str()));
    if (!t) {
        Napi::Error::New(env, "Widget not found: " + widgetName).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Bind the key
    int ret = bindkey(km, sequence.c_str(), t, nullptr);

    return Napi::Boolean::New(env, ret == 0);
}

Napi::Value ZLESession::FeedKeys(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "Expected string or Buffer argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string keys;

    if (info[0].IsString()) {
        keys = info[0].As<Napi::String>().Utf8Value();
    } else if (info[0].IsBuffer()) {
        Napi::Buffer<char> buf = info[0].As<Napi::Buffer<char>>();
        keys = std::string(buf.Data(), buf.Length());
    } else {
        Napi::TypeError::New(env, "Expected string or Buffer argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LIBZSH_LOCK();

    // For now, we'll simulate key input by inserting characters
    // A full implementation would process key sequences through the keymap
    for (char c : keys) {
        if (c >= 32 && c < 127) {
            // Printable character - insert it
            char str[2] = { c, '\0' };
            char* copy = strdup(str);
            if (copy) {
                int newll, newsz, newcs;
                ZLE_STRING_T zleText = stringaszleline(copy, 0, &newll, &newsz, &newcs);
                free(copy);

                if (zleText && newll > 0) {
                    spaceinline(newll);
                    memcpy(zleline + zlecs, zleText, newll * sizeof(ZLE_CHAR_T));
                    zlecs += newll;
                }
            }
        }
        // Handle special keys in a more complete implementation
    }

    // Return result
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", true);
    result.Set("complete", false);

    int outll, outcs;
    char* lineStr = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    result.Set("line", lineStr ? Napi::String::New(env, lineStr, outll) : Napi::String::New(env, ""));

    return result;
}

Napi::Value ZLESession::GetState(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (destroyed_) {
        Napi::Error::New(env, "Session has been destroyed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    LIBZSH_LOCK();

    Napi::Object state = Napi::Object::New(env);

    // Line info
    int outll, outcs;
    char* lineStr = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    state.Set("line", lineStr ? Napi::String::New(env, lineStr, outll) : Napi::String::New(env, ""));
    state.Set("cursor", zlecs);
    state.Set("length", zlell);

    return state;
}

Napi::Value ZLESession::Destroy(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!destroyed_) {
        LIBZSH_LOCK();
        impl_->keymap_ = nullptr;
        destroyed_ = true;
    }

    return env.Undefined();
}

Napi::Value CreateZLESession(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    Napi::FunctionReference* constructor = env.GetInstanceData<Napi::FunctionReference>();
    if (!constructor) {
        Napi::Error::New(env, "ZLESession constructor not found").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() > 0) {
        return constructor->New({ info[0] });
    }
    return constructor->New({});
}

} // namespace node_libzsh
