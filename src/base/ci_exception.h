// Copied from stringi 19e9586ba39b3320df49355e32bd18d74ed6098f; stri_* renamed to ci_*. See inst/COPYRIGHTS.
/* This file is part of the 'stringi' project.
 * Copyright (c) 2013-2025, Marek Gagolewski <https://www.gagolewski.com/>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __charr_base_ci_exception_h
#define __charr_base_ci_exception_h

#include "ci_external.h"
#include "ci_messages.h"


#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace charr { namespace base {

using namespace std;


#define StriException_BUFSIZE 4096


namespace unwind_detail {

struct RUnwind {
    SEXP token;
};


struct JumpBuffer {
    std::jmp_buf value;
};


template<typename Fn>
struct CallState {
    Fn* fn;
    std::exception_ptr error;
};


template<typename Fn>
SEXP call_body(void* data) noexcept
{
    CallState<Fn>* state = static_cast<CallState<Fn>*>(data);
    try {
        return (*state->fn)();
    }
    catch (...) {
        // Return through R's C frames before rethrowing in C++.
        state->error = std::current_exception();
        return R_NilValue;
    }
}


inline void call_cleanup(void* data, Rboolean jump) noexcept
{
    if (jump == TRUE)
        longjmp(static_cast<JumpBuffer*>(data)->value, 1);
}

} // namespace unwind_detail


/** Run one R API call without letting an R long jump skip C++ destructors. */
template<typename Fn>
SEXP unwind_protect(Fn&& fn)
{
    typedef typename std::remove_reference<Fn>::type fun_type;
    unwind_detail::CallState<fun_type> state{&fn, std::exception_ptr()};
    unwind_detail::JumpBuffer jump;
    SEXP token = PROTECT(R_MakeUnwindCont());

    if (setjmp(jump.value) != 0) {
        R_PreserveObject(token);
        UNPROTECT(1);
        throw unwind_detail::RUnwind{token};
    }

    SEXP out = R_UnwindProtect(
        &unwind_detail::call_body<fun_type>, &state,
        &unwind_detail::call_cleanup, &jump, token
    );

    // R_UnwindProtect temporarily keeps its result in the continuation token.
    SETCAR(token, R_NilValue);
    UNPROTECT(1);
    if (state.error)
        std::rethrow_exception(state.error);
    return out;
}


[[noreturn]] inline void continue_r_unwind(SEXP token)
{
    R_ReleaseObject(token);
    R_ContinueUnwind(token);
    Rf_error("charr base: failed to continue R error");
}


/** Issue a warning through the C++ unwind bridge. */
inline void r_warning(const char* format, ...)
{
    char message[StriException_BUFSIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, StriException_BUFSIZE, format, args);
    va_end(args);

    unwind_protect([&]() -> SEXP {
        Rf_warning("%s", message);
        return R_NilValue;
    });
}


/** Queue warnings raised by ICU callbacks until their owners have closed. */
class DeferredWarnings {
private:
    std::vector<std::string> messages_;

public:
    void push(const char* message)
    {
        messages_.push_back(message);
    }

    void emit()
    {
        std::vector<std::string> pending;
        pending.swap(messages_);
        for (std::vector<std::string>::const_iterator it = pending.begin();
                it != pending.end(); ++it) {
            r_warning("%s", it->c_str());
        }
    }
};


// The message buffer is a local array rather than an R_alloc'ed block so that
// Rf_error() is reached only after every caught exception object has been
// destroyed, and so that no R allocation happens on the error path itself.
#define STRI__ERROR_HANDLER_BEGIN(nprotect)                   \
   int __ci_protected_sexp_num = nprotect;                  \
   char __ci_error_msg[StriException_BUFSIZE] = {0};        \
   SEXP __ci_unwind_token = R_NilValue;                     \
   SEXP __ci_warning_unwind_token = R_NilValue;             \
   bool __ci_warning_error = false;                         \
   {                                                         \
   DeferredWarnings __ci_deferred_warnings;                 \
   try {

#define STRI__DEFERRED_WARNINGS __ci_deferred_warnings

// Every entry point is reached through an extern "C" shim (charr_base.cpp), so
// an escaping exception would be std::terminate rather than an R error. The
// std::exception and catch-all arms are load-bearing: the base foundation
// throws std::bad_alloc (stable_slice_arena), std::length_error and
// std::runtime_error (native_to_utf8), and std::out_of_range /
// std::invalid_argument / std::logic_error (string_output).
#define STRI__ERROR_HANDLER_END(cleanup)                      \
   }                                                          \
   catch (const StriException& e) {                           \
      cleanup;                                                \
      snprintf(__ci_error_msg, StriException_BUFSIZE, "%s", e.getMessage()); \
   }                                                          \
   catch (const unwind_detail::RUnwind& e) {                  \
      cleanup;                                                \
      __ci_unwind_token = e.token;                            \
   }                                                          \
   catch (const std::exception& e) {                          \
      cleanup;                                                \
      snprintf(__ci_error_msg, StriException_BUFSIZE, "%s", e.what()); \
   }                                                          \
   catch (...) {                                              \
      cleanup;                                                \
      snprintf(__ci_error_msg, StriException_BUFSIZE,         \
         "unknown C++ exception");                           \
   }                                                          \
   /* ICU callback owners have unwound; warnings can now reach R. */ \
   try {                                                      \
      __ci_deferred_warnings.emit();                          \
   }                                                          \
   catch (const unwind_detail::RUnwind& e) {                  \
      __ci_warning_unwind_token = e.token;                    \
   }                                                          \
   catch (const std::exception& e) {                          \
      __ci_warning_error = true;                              \
      snprintf(__ci_error_msg, StriException_BUFSIZE, "%s", e.what()); \
   }                                                          \
   catch (...) {                                              \
      __ci_warning_error = true;                              \
      snprintf(__ci_error_msg, StriException_BUFSIZE,         \
         "unknown C++ exception while emitting a warning"); \
   }                                                          \
   } /* destroy the warning queue before any R long jump */   \
   STRI__UNPROTECT_ALL                                        \
   if (__ci_warning_unwind_token != R_NilValue) {             \
      if (__ci_unwind_token != R_NilValue)                    \
         R_ReleaseObject(__ci_unwind_token);                  \
      continue_r_unwind(__ci_warning_unwind_token);           \
   }                                                          \
   if (__ci_warning_error && __ci_unwind_token != R_NilValue) { \
      R_ReleaseObject(__ci_unwind_token);                     \
      __ci_unwind_token = R_NilValue;                         \
   }                                                          \
   if (__ci_unwind_token != R_NilValue)                       \
      continue_r_unwind(__ci_unwind_token);                   \
   if (__ci_error_msg[0] == '\0')                            \
      snprintf(__ci_error_msg, StriException_BUFSIZE,         \
         "unknown C++ exception");                           \
   Rf_error("%s", __ci_error_msg); /* msg may feature %s */ \
   /* to avoid compiler warning: */                           \
   return R_NilValue;


#define STRI__PROTECT(s) {                                    \
   PROTECT(s);                                                \
   ++__ci_protected_sexp_num; }

#ifndef NDEBUG
#define STRI__UNPROTECT(n) {                                  \
   if (n > __ci_protected_sexp_num)                         \
      throw StriException("STRI__UNPROTECT: stack imbalance!"); \
   UNPROTECT(n);                                              \
   __ci_protected_sexp_num -= n; }
#else
#define STRI__UNPROTECT(n) {                                  \
   UNPROTECT(n);                                              \
   __ci_protected_sexp_num -= n; }
#endif

#define STRI__UNPROTECT_ALL {                                 \
   UNPROTECT(__ci_protected_sexp_num);                      \
   __ci_protected_sexp_num = 0; }


#define STRI__CHECKICUSTATUS_THROW(status, onerror) {         \
   if (U_FAILURE(status)) {                                   \
      onerror;                                                \
      throw StriException(status);                            \
   }                                                          \
}


#define STRI__CHECKICUSTATUS_RFERROR(status, onerror) {       \
   if (U_FAILURE(status)) {                                   \
      onerror;                                                \
      Rf_error(MSG__ICU_ERROR,                                \
         ICUError::getICUerrorName(status),                   \
         u_errorName(status));                                \
   }                                                          \
}



/** Translates ICU error code to an informative message.
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-21)
 * make independent from StriException
 */
class ICUError {
public:
    static const char* getICUerrorName(UErrorCode status);
};




#ifndef NDEBUG
/* *************** !NDEBUG *************************************************** */

#ifndef STRI_ASSERT
#define __STRI_ASSERT_STR(x) #x
#define STRI_ASSERT_STR(x) __STRI_ASSERT_STR(x)

#define STRI_ASSERT(EXPR) { if (!(EXPR)) \
    REprintf("stringi: Assertion %s failed in %s:%d", #EXPR, __FILE__, __LINE__); }
#endif


/**
 * A class representing exceptions for !NDEBUG
 *
 * @version 1.4.7 (Marek Gagolewski, 2020-08-21)
 *          Improve !NDEBUG diagnostics
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-21) snprintf
 */
class __StriException
{

private:

    char msg[StriException_BUFSIZE]; ///< message to be passed to error()

public:

    __StriException(const char* file, int line, const char* format, ...)
    {
        snprintf(msg, StriException_BUFSIZE, "[!NDEBUG] Error in %s:%d: ", file, line);
        va_list args;
        va_start(args, format);
        R_len_t msg_size = strlen(msg);
        vsnprintf(msg+msg_size, StriException_BUFSIZE-msg_size, format, args);
        va_end(args);
    }

    __StriException(const char* file, int line, UErrorCode status, const char* context = NULL)
    {
        snprintf(msg, StriException_BUFSIZE, "[!NDEBUG: Error in %s:%d] ", file, line);
        R_len_t msg_size = strlen(msg);
        if (context) {
            snprintf(msg+msg_size, StriException_BUFSIZE-msg_size,
                MSG__ICU_ERROR_WITH_CONTEXT, ICUError::getICUerrorName(status),
                    u_errorName(status), context);
        }
        else {
            snprintf(msg+msg_size, StriException_BUFSIZE-msg_size,
                MSG__ICU_ERROR, ICUError::getICUerrorName(status), u_errorName(status));
        }
    }


    void throwRerror()
    {
        Rf_error("%s", msg);  // avoids treating %'s as special chars
    }

    const char* getMessage() const
    {
        return msg;
    }
};

#define StriException(...) __StriException(__FILE__, __LINE__, __VA_ARGS__)

typedef __StriException StriException;



/* *************** !NDEBUG *************************************************** */
#else
/* *************** NDEBUG *************************************************** */
#ifndef STRI_ASSERT
#define STRI_ASSERT(EXPR) { ; }
#endif

/**
 * A class representing exceptions
 *
 * @version 0.1-?? (Marek Gagolewski, 2013-06-16)
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-04-18)
 *          do not use R_alloc for msg
 *
 * @version 1.6.3 (Marek Gagolewski, 2021-05-21) snprintf
 */
class StriException
{

private:

    char msg[StriException_BUFSIZE]; ///< message to be passed to error()

public:

    StriException(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(msg, StriException_BUFSIZE, format, args);
        va_end(args);
    }

    StriException(UErrorCode status, const char* context = NULL)
    {
        if (context) {
            snprintf(msg, StriException_BUFSIZE, MSG__ICU_ERROR_WITH_CONTEXT,
                ICUError::getICUerrorName(status), u_errorName(status), context);
        }
        else {
            snprintf(msg, StriException_BUFSIZE, MSG__ICU_ERROR,
                ICUError::getICUerrorName(status), u_errorName(status));
        }
    }


    void throwRerror()
    {
        Rf_error("%s", msg);  // avoids treating %'s as special chars
    }

    const char* getMessage() const
    {
        return msg;
    }
};

/* *************** NDEBUG *************************************************** */
#endif


/** Keep C++ exceptions from escaping an extern "C" .Call entry point. */
template<typename Fn>
SEXP r_boundary(Fn&& fn)
{
    typedef typename std::decay<Fn>::type body_type;
    static_assert(
        std::is_trivially_destructible<body_type>::value,
        "r_boundary callback must be trivially destructible"
    );

    SEXP unwind_token = R_NilValue;
    char error_message[StriException_BUFSIZE] = {0};
    {
        try {
            return std::forward<Fn>(fn)();
        }
        catch (const unwind_detail::RUnwind& error) {
            unwind_token = error.token;
        }
        catch (const StriException& error) {
            snprintf(
                error_message, StriException_BUFSIZE,
                "%s", error.getMessage()
            );
        }
        catch (const std::exception& error) {
            snprintf(
                error_message, StriException_BUFSIZE, "%s", error.what()
            );
        }
        catch (...) {
            snprintf(
                error_message, StriException_BUFSIZE,
                "unknown C++ exception"
            );
        }
    }

    if (unwind_token != R_NilValue)
        continue_r_unwind(unwind_token);
    if (error_message[0] == '\0')
        snprintf(
            error_message, StriException_BUFSIZE, "unknown C++ exception"
        );
    Rf_error("%s", error_message);
    return R_NilValue;
}


} } // namespace charr::base

#endif
