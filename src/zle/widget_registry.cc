#include "widget_registry.h"
#include "../util/thread_safety.h"

namespace node_libzsh {

WidgetRegistry& WidgetRegistry::instance() {
    static WidgetRegistry registry;
    return registry;
}

bool WidgetRegistry::registerWidget(const std::string& name, Napi::FunctionReference&& func) {
    LIBZSH_LOCK();

    // Check if already exists
    if (widgets_.find(name) != widgets_.end()) {
        return false;
    }

    widgets_.emplace(name, std::move(func));
    return true;
}

bool WidgetRegistry::unregisterWidget(const std::string& name) {
    LIBZSH_LOCK();

    auto it = widgets_.find(name);
    if (it == widgets_.end()) {
        return false;
    }

    widgets_.erase(it);
    return true;
}

bool WidgetRegistry::hasWidget(const std::string& name) const {
    return widgets_.find(name) != widgets_.end();
}

int WidgetRegistry::executeWidget(const std::string& name) {
    LIBZSH_LOCK();

    auto it = widgets_.find(name);
    if (it == widgets_.end()) {
        return -1;
    }

    try {
        Napi::Value result = it->second.Call({});
        if (result.IsNumber()) {
            return result.As<Napi::Number>().Int32Value();
        }
        return 0;
    } catch (...) {
        return -1;
    }
}

std::vector<std::string> WidgetRegistry::getWidgetNames() const {
    std::vector<std::string> names;
    for (const auto& pair : widgets_) {
        names.push_back(pair.first);
    }
    return names;
}

Napi::Value RegisterWidget(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected (name, function) arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Widget name must be a string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[1].IsFunction()) {
        Napi::TypeError::New(env, "Widget handler must be a function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();
    Napi::FunctionReference func = Napi::Persistent(info[1].As<Napi::Function>());

    bool success = WidgetRegistry::instance().registerWidget(name, std::move(func));

    if (!success) {
        Napi::Error::New(env, "Widget already registered: " + name).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return env.Undefined();
}

Napi::Value UnregisterWidget(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected widget name string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();
    bool success = WidgetRegistry::instance().unregisterWidget(name);

    return Napi::Boolean::New(env, success);
}

Napi::Value GetCustomWidgets(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    std::vector<std::string> names = WidgetRegistry::instance().getWidgetNames();

    Napi::Array result = Napi::Array::New(env, names.size());
    for (size_t i = 0; i < names.size(); i++) {
        result.Set(static_cast<uint32_t>(i), names[i]);
    }

    return result;
}

} // namespace node_libzsh
