#ifndef NODE_LIBZSH_THREAD_SAFETY_H
#define NODE_LIBZSH_THREAD_SAFETY_H

#include <mutex>

namespace node_libzsh {

// Global mutex for all libzsh operations since zsh uses global state.
// Recursive because some public methods call other public methods
// (e.g. registerCompleter calls unregisterCompleter).
class LibzshMutex {
public:
    static std::recursive_mutex& get() {
        static std::recursive_mutex mutex;
        return mutex;
    }

    // RAII lock guard
    class Guard {
    public:
        Guard() : lock_(LibzshMutex::get()) {}
        ~Guard() = default;

        // Non-copyable
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        std::lock_guard<std::recursive_mutex> lock_;
    };
};

// Convenience macro
#define LIBZSH_LOCK() node_libzsh::LibzshMutex::Guard _libzsh_lock_guard

} // namespace node_libzsh

#endif // NODE_LIBZSH_THREAD_SAFETY_H
