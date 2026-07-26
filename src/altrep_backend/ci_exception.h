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


#ifndef __ci_exception_h
#define __ci_exception_h

#include <charport.h>

#include "ci_external.h"
#include "ci_messages.h"


#include <cstdarg>
#include <exception>
#include <string>
#include <vector>
using namespace std;


#define StriException_BUFSIZE 4096


namespace ci {

// Deviation from stringi: track PROTECT calls made inside a charport unwind
// callback without folding them into the operation-wide counter.
// A C++ exception runs this destructor and releases the callback's local
// protections. An R error skips C++ destructors but restores the protection
// stack to the R_UnwindProtect entry. Keeping this count separate from the
// operation-wide STRI__PROTECT count makes both exits balance correctly.
class UnwindCallbackProtector {
private:
    int count_;

public:
    UnwindCallbackProtector() : count_(0) {}
    ~UnwindCallbackProtector()
    {
        if (count_ > 0)
            UNPROTECT(count_);
    }

    UnwindCallbackProtector(const UnwindCallbackProtector&) = delete;
    UnwindCallbackProtector& operator=(
        const UnwindCallbackProtector&
    ) = delete;

    SEXP hold(SEXP value)
    {
        PROTECT(value);
        ++count_;
        return value;
    }

    /** Take responsibility for protections acquired by a helper. */
    void adopt(int count)
    {
        count_ += count;
    }

    void release(int count)
    {
        UNPROTECT(count);
        count_ -= count;
    }
};


/** Queue R warnings until Reader-backed inputs have been released. */
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
        // Move the whole queue out first. If warn=2 unwinds, neither the
        // current warning nor later queued warnings can be issued twice.
        std::vector<std::string> pending;
        pending.swap(messages_);
        for (std::vector<std::string>::const_iterator it = pending.begin();
                it != pending.end(); ++it) {
            charport::unwind_protect([&]() -> SEXP {
                // Rf_warning is an isolated, genuine C-string boundary; the
                // queued message is owned here until the call returns.
                Rf_warning("%s", it->c_str());
                return R_NilValue;
            });
        }
    }
};

} // namespace ci


#define STRI__ERROR_HANDLER_BEGIN(nprotect)                   \
   int __ci_protected_sexp_num = nprotect;                  \
   char __ci_error_msg[StriException_BUFSIZE] = {0};        \
   SEXP __ci_unwind_token = R_NilValue;                     \
   SEXP __ci_warning_unwind_token = R_NilValue;             \
   bool __ci_warning_error = false;                         \
   {                                                         \
   ci::DeferredWarnings __ci_deferred_warnings;             \
   try {

#define STRI__DEFERRED_WARNINGS __ci_deferred_warnings

#define STRI__ERROR_HANDLER_END(cleanup)                      \
   }                                                          \
   catch (const StriException& e) {                           \
      cleanup;                                                \
      snprintf(__ci_error_msg, StriException_BUFSIZE, "%s", e.getMessage()); \
   }                                                          \
   catch (const charport::r_unwind& e) {                      \
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
   /* Reader-owning locals have unwound; prepared inputs stay protected. */ \
   try {                                                      \
      __ci_deferred_warnings.emit();                          \
   }                                                          \
   catch (const charport::r_unwind& e) {                      \
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
   } /* destroy the warning queue before any R long-jump */   \
   STRI__UNPROTECT_ALL                                        \
   if (__ci_warning_unwind_token != R_NilValue) {             \
      if (__ci_unwind_token != R_NilValue)                    \
         R_ReleaseObject(__ci_unwind_token);                  \
      charport::continue_r_unwind(__ci_warning_unwind_token); \
   }                                                          \
   if (__ci_warning_error && __ci_unwind_token != R_NilValue) { \
      R_ReleaseObject(__ci_unwind_token);                     \
      __ci_unwind_token = R_NilValue;                         \
   }                                                          \
   /* Continue only after the caught unwind object is destroyed. */ \
   if (__ci_unwind_token != R_NilValue)                       \
      charport::continue_r_unwind(__ci_unwind_token);         \
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
// Deviation from stringi: report an internal protection imbalance through the
// C++ boundary so live staging owners unwind before the error reaches R.
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

#endif
