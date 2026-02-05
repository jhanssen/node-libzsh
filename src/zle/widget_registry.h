#ifndef NODE_LIBZSH_WIDGET_REGISTRY_H
#define NODE_LIBZSH_WIDGET_REGISTRY_H

#include <napi.h>
#include <string>
#include <unordered_map>
#include <functional>

namespace node_libzsh {

// Widget function signature
using WidgetFunction = std::function<int()>;

// Widget registry for custom JS widgets
class WidgetRegistry {
public:
    static WidgetRegistry& instance();

    // Register a custom widget
    bool registerWidget(const std::string& name, Napi::FunctionReference&& func);

    // Unregister a widget
    bool unregisterWidget(const std::string& name);

    // Check if widget exists
    bool hasWidget(const std::string& name) const;

    // Execute a custom widget
    int executeWidget(const std::string& name);

    // Get list of custom widgets
    std::vector<std::string> getWidgetNames() const;

private:
    WidgetRegistry() = default;
    ~WidgetRegistry() = default;

    // Non-copyable
    WidgetRegistry(const WidgetRegistry&) = delete;
    WidgetRegistry& operator=(const WidgetRegistry&) = delete;

    std::unordered_map<std::string, Napi::FunctionReference> widgets_;
};

// N-API wrapper
Napi::Value RegisterWidget(const Napi::CallbackInfo& info);
Napi::Value UnregisterWidget(const Napi::CallbackInfo& info);
Napi::Value GetCustomWidgets(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_WIDGET_REGISTRY_H
