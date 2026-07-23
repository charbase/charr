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


#include "ci_stringi.h"
#include "ci_builder.h"
#include "ci_ucnv.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace {

static int ci__management_icu_c_string_length(const char* value)
{
    // Deviation from stringi: ICU's encoding-name APIs expose only
    // terminated strings. Copy that boundary once and retain explicit lengths.
    const size_t length = std::strlen(value);
    if (length > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("ICU encoding name exceeds R's string limit");
    return static_cast<int>(length);
}


static void ci__stage_management_icu_c_string(
    charport::charvec::Builder& output, R_len_t i, const char* value
)
{
    if (!value) {
        output.set_na(i);
        return;
    }
    ci::builder_set(
        output, i, value, ci__management_icu_c_string_length(value),
        cetype_ext_t::CE_ASCII
    );
}


struct CiEncodingInfoValue {
    enum Type {
        UNSET,
        CHARACTER,
        LOGICAL,
        INTEGER
    };

    Type type;
    charport::charvec::Store character;
    int scalar;

    CiEncodingInfoValue()
        : type(UNSET), character(0, 0), scalar(0)
    {
    }

    CiEncodingInfoValue(CiEncodingInfoValue&&) noexcept = default;
    CiEncodingInfoValue& operator=(CiEncodingInfoValue&&) noexcept = default;

    void set_character(const char* value)
    {
        if (!value) {
            character = charport::charvec::Store::scalar(
                NULL, 0, cetype_ext_t::CE_NA
            );
        }
        else {
            character = ci::scalar_store(
                value, ci__management_icu_c_string_length(value),
                cetype_ext_t::CE_ASCII
            );
        }
        type = CHARACTER;
    }

    void set_na_character()
    {
        character = charport::charvec::Store::scalar(
            NULL, 0, cetype_ext_t::CE_NA
        );
        type = CHARACTER;
    }

    void set_logical(int value)
    {
        scalar = value;
        type = LOGICAL;
    }

    void set_integer(int value)
    {
        scalar = value;
        type = INTEGER;
    }

private:
    CiEncodingInfoValue(const CiEncodingInfoValue&);
    CiEncodingInfoValue& operator=(const CiEncodingInfoValue&);
};

} // namespace


/**
 * Sets current (default) ICU charset
 *
 * If given charset is unavailable, an error is raised
 *
 * @param enc new charset (single string)
 * @return nothing (\code{R_NilValue})
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski)
 *          use StriUcnv; make StriException-friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    #112: str_prepare_arg* retvals were not PROTECTed from gc
 *
 * @version 1.3.1 (Marek Gagolewski, 2019-02-06)
 *    #335: if system ICU uses U_CHARSET_IS_UTF8=1, the function has no effect
 */
SEXP ci_enc_set(SEXP enc)
{
    // here, the default encoding may not be requested:
    const char* selected_enc
        = ci__prepare_arg_enc(enc, "enc", false/*no default*/); /* this is R_alloc'ed */

#ifdef U_CHARSET_IS_UTF8
#if U_CHARSET_IS_UTF8
    // #335: if system ICU uses U_CHARSET_IS_UTF8=1, the function has no effect
    Rf_warning(MSG__U_CHARSET_IS_UTF8);
    return R_NilValue;
#endif
#endif

    STRI__ERROR_HANDLER_BEGIN(0)
    {
        StriUcnv uconv_obj(selected_enc);
        // this will generate an error if selected_enc is not supported:
        UConverter* uconv = uconv_obj.getConverter();

        UErrorCode status = U_ZERO_ERROR;
        // get "official" encoding name:
        const char* name = ucnv_getName(uconv, &status);
        STRI__CHECKICUSTATUS_THROW(status, {/* do nothing special on err */})

        /*
         DO NOT call this function when ANY ICU function is being used
         from more than one thread! This function sets the current default
         converter name. If this function needs to be called, it should be
         called during application initialization.
         Do not use unless you know what you are doing.
         */
        ucnv_setDefaultName(name); // set as default
    }

    STRI__DEFERRED_WARNINGS.emit();
    return R_NilValue;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}


/**
 * Get all available ICU charsets and their aliases (elems 2,3,...)
 *
 * @return R list object; element name == ICU charset canonical name;
 * elements are character vectors (aliases)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski)
 *          use StriUcnv; make StriException-friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_list()
{
    R_len_t c = (R_len_t)ucnv_countAvailable();

    STRI__ERROR_HANDLER_BEGIN(0)
    SEXP ret;
    SEXP names;
    STRI__PROTECT(ret = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(VECSXP, c);
    }));
    STRI__PROTECT(names = charport::unwind_protect([&]() -> SEXP {
        return Rf_allocVector(STRSXP, c);
    }));

    for (R_len_t i=0; i<c; ++i) {
        const char* canonical_name = ucnv_getAvailableName(i);
        if (!canonical_name) {
            charport::unwind_protect([&]() -> SEXP {
                SET_STRING_ELT(names, i, NA_STRING);
                return R_NilValue;
            });
            continue;
        }

        const int canonical_length =
            ci__management_icu_c_string_length(canonical_name);
        charport::unwind_protect([&]() -> SEXP {
            SET_STRING_ELT(
                names, i,
                Rf_mkCharLenCE(
                    canonical_name, canonical_length, CE_UTF8
                )
            );
            return R_NilValue;
        });

        UErrorCode status = U_ZERO_ERROR;
        R_len_t alias_count = (R_len_t)ucnv_countAliases(
            canonical_name, &status
        );
        charport::charvec::Builder aliases(
            U_FAILURE(status) || alias_count <= 0 ? 1 : alias_count
        );
        if (U_FAILURE(status) || alias_count <= 0) {
            aliases.set_na(0);
        }
        else {
            for (R_len_t j=0; j<alias_count; ++j) {
                status = U_ZERO_ERROR;
                const char* alias = ucnv_getAlias(
                    canonical_name, j, &status
                );
                if (U_FAILURE(status) || !alias)
                    aliases.set_na(j);
                else
                    ci__stage_management_icu_c_string(aliases, j, alias);
            }
        }

        SEXP aliases_r;
        STRI__PROTECT(aliases_r = aliases.to_sexp());
        SET_VECTOR_ELT(ret, i, aliases_r);
        STRI__UNPROTECT(1);
    }

    charport::unwind_protect([&]() -> SEXP {
        Rf_setAttrib(ret, R_NamesSymbol, names);
        return R_NilValue;
    });

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}


/** Fetch information on an encoding
 *
 * @param enc either NULL or "" for default encoding,
 *        or one string with encoding name
 * @return R list object with many components (see R doc for details)
 *
 * @version 0.1-?? (Marek Gagolewski)
 *
 * @version 0.2-1 (Marek Gagolewski)
 *          use StriUcnv; make StriException-friendly
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_info(SEXP enc)
{
    const char* selected_enc = ci__prepare_arg_enc(enc, "enc", true/*default ok*/); /* this is R_alloc'ed */

    STRI__ERROR_HANDLER_BEGIN(0)
    SEXP vals;
    {
        std::vector<const char*> standards;
        std::vector<std::string> names;
        std::vector<unsigned char> has_name;
        std::vector<CiEncodingInfoValue> values;
        R_len_t standards_n = 0;
        int nval = 0;

        {
            StriUcnv uconv_obj(selected_enc);
            // Inspection uses ICU's ordinary substitution callbacks.
            UConverter* uconv = uconv_obj.getConverter();
            UErrorCode status = U_ZERO_ERROR;

            standards = StriUcnv::getStandards(STRI__DEFERRED_WARNINGS);
            standards_n = (R_len_t)standards.size();
            nval = standards_n+2+5;
            names.resize(static_cast<size_t>(nval));
            has_name.resize(static_cast<size_t>(nval), 0);
            values.resize(static_cast<size_t>(nval));
            names[0] = "Name.friendly";
            names[1] = "Name.ICU";
            has_name[0] = has_name[1] = 1;
            for (R_len_t i=0; i<standards_n; ++i) {
                if (!standards[static_cast<size_t>(i)])
                    continue;
                std::string& current_name = names[static_cast<size_t>(i+2)];
                current_name = "Name.";
                const char* standard = standards[static_cast<size_t>(i)];
                current_name.append(
                    standard,
                    static_cast<size_t>(
                        ci__management_icu_c_string_length(standard)
                    )
                );
                has_name[static_cast<size_t>(i+2)] = 1;
            }
            names[static_cast<size_t>(nval-5)] = "ASCII.subset";
            names[static_cast<size_t>(nval-4)] = "Unicode.1to1";
            names[static_cast<size_t>(nval-3)] = "CharSize.8bit";
            names[static_cast<size_t>(nval-2)] = "CharSize.min";
            names[static_cast<size_t>(nval-1)] = "CharSize.max";
            for (int i=nval-5; i<nval; ++i)
                has_name[static_cast<size_t>(i)] = 1;

            // Deviation from stringi: stage all converter diagnostics and
            // character values, then assemble the R list after closing it.
            status = U_ZERO_ERROR;
            const char* canname = ucnv_getName(uconv, &status);
            if (U_FAILURE(status) || !canname) {
                values[1].set_na_character();
                STRI__DEFERRED_WARNINGS.push(MSG__ENC_ERROR_GETNAME);
            }
            else {
                values[1].set_character(canname);

                const char* frname = StriUcnv::getFriendlyName(canname);
                if (frname)
                    values[0].set_character(frname);
                else
                    values[0].set_na_character();

                values[static_cast<size_t>(nval-5)].set_logical(
                    (int)uconv_obj.hasASCIIsubset(STRI__DEFERRED_WARNINGS)
                );

                int mincharsize = (int)ucnv_getMinCharSize(uconv);
                int maxcharsize = (int)ucnv_getMaxCharSize(uconv);
                int is8bit = (mincharsize==1 && maxcharsize == 1);
                values[static_cast<size_t>(nval-3)].set_logical(is8bit);
                values[static_cast<size_t>(nval-2)].set_integer(mincharsize);
                values[static_cast<size_t>(nval-1)].set_integer(maxcharsize);

                if (!is8bit) {
                    values[static_cast<size_t>(nval-4)].set_logical(
                        NA_LOGICAL
                    );
                }
                else {
                    values[static_cast<size_t>(nval-4)].set_logical(
                        (int)uconv_obj.is1to1Unicode(
                            STRI__DEFERRED_WARNINGS
                        )
                    );
                }

                for (R_len_t i=0; i<standards_n; ++i) {
                    const char* standard = standards[static_cast<size_t>(i)];
                    if (!standard)
                        continue;

                    status = U_ZERO_ERROR;
                    const char* stdname = ucnv_getStandardName(
                        canname, standard, &status
                    );
                    if (U_FAILURE(status) || !stdname)
                        values[static_cast<size_t>(i+2)].set_na_character();
                    else
                        values[static_cast<size_t>(i+2)].set_character(
                            stdname
                        );
                }
            }
        }

        STRI__PROTECT(vals = charport::unwind_protect([&]() -> SEXP {
            int protected_count = 0;
            try {
                SEXP value = PROTECT(Rf_allocVector(VECSXP, nval));
                ++protected_count;
                SEXP value_names = PROTECT(Rf_allocVector(STRSXP, nval));
                ++protected_count;

                for (int i=0; i<nval; ++i) {
                    if (has_name[static_cast<size_t>(i)]) {
                        const std::string& name = names[static_cast<size_t>(i)];
                        SET_STRING_ELT(
                            value_names, i,
                            Rf_mkCharLenCE(
                                name.data(), static_cast<int>(name.size()),
                                CE_UTF8
                            )
                        );
                    }

                    CiEncodingInfoValue& current =
                        values[static_cast<size_t>(i)];
                    if (current.type == CiEncodingInfoValue::UNSET)
                        continue;

                    SEXP child = R_NilValue;
                    if (current.type == CiEncodingInfoValue::CHARACTER) {
                        child = PROTECT(charport::charvec::wrap(
                            std::move(current.character)
                        ));
                    }
                    else if (current.type == CiEncodingInfoValue::LOGICAL) {
                        child = PROTECT(Rf_ScalarLogical(current.scalar));
                    }
                    else {
                        child = PROTECT(Rf_ScalarInteger(current.scalar));
                    }
                    ++protected_count;
                    SET_VECTOR_ELT(value, i, child);
                    UNPROTECT(1);
                    --protected_count;
                }

                Rf_setAttrib(value, R_NamesSymbol, value_names);
                UNPROTECT(2);
                protected_count -= 2;
                return value;
            }
            catch (...) {
                // Deviation from stringi: charvec wrapping can surface an R
                // unwind as a C++ exception inside this raw-PROTECT assembly
                // block, so balance the local stack before propagating it.
                UNPROTECT(protected_count);
                throw;
            }
        }));
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return vals;

    STRI__ERROR_HANDLER_END({/* no special action on error */})
}


/** Get Declared Encodings of Each String
 *
 * @param str a character vector or an object coercible to
 * @return a character vector
 *
 * @version 0.2-1 (Marek Gagolewski, 2014-03-25)
 *
 * @version 0.3-1 (Marek Gagolewski, 2014-11-04)
 *    Issue #112: str_prepare_arg* retvals were not PROTECTed from gc
 */
SEXP ci_enc_mark(SEXP str) {
    PROTECT(str = ci__prepare_arg_string(str, "str"));    // prepare string argument

    STRI__ERROR_HANDLER_BEGIN(1)
    SEXP ret;
    {
        ci::ReaderContext context(STRI__DEFERRED_WARNINGS);
        R_len_t str_len = ci::checked_r_len(
            context.size(str), "character vectors"
        );
        charport::charvec::Builder output(str_len);
        {
            // Deviation from stringi: encoding marks are source metadata, so
            // inspect Reader views directly without materializing CHARSXPs.
            std::shared_ptr<ci::ReaderBorrow> borrow = context.acquire(str);
            const charport::StrViews& views = borrow->views();
            for (R_len_t i=0; i<str_len; ++i) {
                const charport::StrView current = views[i];
                if (current.is_na()) {
                    output.set_na(i);
                    continue;
                }

                const char* mark = NULL;
                int mark_length = 0;
                switch (current.enc) {
                case cetype_ext_t::CE_ASCII:
                    mark = "ASCII";
                    mark_length = 5;
                    break;
                case cetype_ext_t::CE_ASCII_OR_UTF8:
                    // Only the ambiguous mark needs payload inspection.
                    if (ci::is_ascii(current.ptr, current.len)) {
                        mark = "ASCII";
                        mark_length = 5;
                    }
                    else {
                        mark = "UTF-8";
                        mark_length = 5;
                    }
                    break;
                case cetype_ext_t::CE_UTF8:
                    mark = "UTF-8";
                    mark_length = 5;
                    break;
                case cetype_ext_t::CE_BYTES:
                    mark = "bytes";
                    mark_length = 5;
                    break;
                case cetype_ext_t::CE_LATIN1:
                    mark = "latin1";
                    mark_length = 6;
                    break;
                case cetype_ext_t::CE_NATIVE:
                    mark = "native";
                    mark_length = 6;
                    break;
                case cetype_ext_t::CE_NA:
                    output.set_na(i);
                    continue;
                default:
                    throw StriException("unknown charport string encoding");
                }

                ci::builder_set(
                    output, i, mark, mark_length, cetype_ext_t::CE_ASCII
                );
            }
        }

        STRI__PROTECT(ret = output.to_sexp());
    }

    STRI__DEFERRED_WARNINGS.emit();
    STRI__UNPROTECT_ALL
    return ret;
    STRI__ERROR_HANDLER_END(;/* nothing special to be done on error */)
}
