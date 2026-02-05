#ifndef NODE_LIBZSH_ZSH_WRAPPER_H
#define NODE_LIBZSH_ZSH_WRAPPER_H

/**
 * Wrapper header for including zsh headers in C++ code.
 *
 * Zsh is a C codebase that uses C++ reserved keywords (like 'new')
 * as parameter names. This header works around those conflicts.
 */

#ifdef __cplusplus

// Temporarily rename C++ keywords used by zsh headers
#define new zsh_new
#define delete zsh_delete
#define class zsh_class
#define template zsh_template
#define this zsh_this
#define private zsh_private
#define public zsh_public
#define protected zsh_protected
#define virtual zsh_virtual
#define operator zsh_operator
#define namespace zsh_namespace
#define throw zsh_throw
#define try zsh_try
#define catch zsh_catch

extern "C" {
#endif

#include "zsh.mdh"
#include "zle.mdh"

#ifdef __cplusplus
}

// Restore C++ keywords
#undef new
#undef delete
#undef class
#undef template
#undef this
#undef private
#undef public
#undef protected
#undef virtual
#undef operator
#undef namespace
#undef throw
#undef try
#undef catch

// Undefine zsh macros that conflict with common C++ identifiers
#undef String
#undef empty

#endif // __cplusplus

#endif // NODE_LIBZSH_ZSH_WRAPPER_H
