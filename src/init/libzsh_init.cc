#include "libzsh_init.h"
#include "../util/thread_safety.h"
#include "../zsh_wrapper.h"
#include <cstring>
#include <cstdlib>
#include <locale.h>

// Additional external functions from zsh
extern "C" {
// External functions from zsh
extern void init_jobs(char **argv, char **envp);
extern void createoptiontable(void);
extern void createaliastables(void);
extern void createreswdtable(void);
extern void initlextabs(void);
extern void init_parse(void);

// ZLE initialization functions
extern void init_thingies(void);
extern void init_keymaps(void);

// History
extern void hbegin(int);

// Global variables we need to initialize
extern unsigned char *cmdstack;
extern int cmdsp;
extern int strin;
extern int fdtable_size;
extern unsigned char *fdtable;

// Memory/heap
extern long zopenmax(void);

// Hash tables
extern HashTable thingytab;
extern HashTable keymapnamtab;
}

#define CMDSTACKSZ 256

namespace node_libzsh {

static bool g_initialized = false;
static bool g_zle_enabled = false;
static char* g_fake_argv[] = { const_cast<char*>("node-libzsh"), nullptr };
static char* g_fake_envp[] = {
    const_cast<char*>("PATH=/bin:/usr/bin"),
    const_cast<char*>("HOME=/tmp"),
    const_cast<char*>("TERM=xterm"),
    nullptr
};

bool initializeLibzsh(const InitOptions& options) {
    LIBZSH_LOCK();

    if (g_initialized) {
        return true; // Already initialized
    }

    // Set locale
    setlocale(LC_ALL, "");

    // Initialize job control structures
    init_jobs(g_fake_argv, g_fake_envp);

    // Set up metafication type table
    // Characters that are "meta" need special handling
    typtab['\0'] |= IMETA;
    typtab[STOUC(Meta)] |= IMETA;
    typtab[STOUC(Marker)] |= IMETA;
    for (int t0 = (int)STOUC(Pound); t0 <= (int)STOUC(Nularg); t0++) {
        typtab[t0] |= ITOK | IMETA;
    }

    // Set up file descriptor table
    fdtable_size = zopenmax();
    fdtable = static_cast<unsigned char*>(zshcalloc(fdtable_size * sizeof(*fdtable)));
    fdtable[0] = fdtable[1] = fdtable[2] = FDT_EXTERNAL;

    // Create option table
    createoptiontable();

    if (options.enableParser) {
        // Initialize lexer tables
        initlextabs();

        // Initialize hash tables for reserved words and aliases
        createreswdtable();
        createaliastables();

        // Initialize command stack
        cmdstack = static_cast<unsigned char*>(zalloc(CMDSTACKSZ));
        cmdsp = 0;

        // Initialize parser
        init_parse();

        // Initialize history (needed for some operations)
        strin = 1; // Reading from string, not interactive
        hbegin(0);
    }

    if (options.enableZLE) {
        // Initialize ZLE thingies (widget table)
        init_thingies();

        // Initialize ZLE keymaps
        init_keymaps();

        g_zle_enabled = true;
    }

    g_initialized = true;
    return true;
}

void shutdownLibzsh() {
    LIBZSH_LOCK();

    if (!g_initialized) {
        return;
    }

    // Note: zsh doesn't have a clean shutdown path for all subsystems
    // We do what we can to avoid memory leaks

    if (cmdstack) {
        zfree(cmdstack, CMDSTACKSZ);
        cmdstack = nullptr;
    }

    if (fdtable) {
        zfree(fdtable, fdtable_size * sizeof(*fdtable));
        fdtable = nullptr;
    }

    g_initialized = false;
    g_zle_enabled = false;
}

bool isInitialized() {
    return g_initialized;
}

Napi::Value Initialize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    InitOptions options;

    // Parse options object if provided
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object opts = info[0].As<Napi::Object>();

        if (opts.Has("enableZLE")) {
            options.enableZLE = opts.Get("enableZLE").ToBoolean().Value();
        }
        if (opts.Has("enableParser")) {
            options.enableParser = opts.Get("enableParser").ToBoolean().Value();
        }
    }

    bool success = initializeLibzsh(options);

    if (!success) {
        Napi::Error::New(env, "Failed to initialize libzsh").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return env.Undefined();
}

Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    shutdownLibzsh();
    return info.Env().Undefined();
}

Napi::Value IsInitialized(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), isInitialized());
}

} // namespace node_libzsh
