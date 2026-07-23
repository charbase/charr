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
#include "ci_container_bytesearch.h"
#include <unicode/usearch.h>


/**
 * Default constructor
 *
 */
StriContainerByteSearch::StriContainerByteSearch()
    : StriContainerUTF8()
{
    this->matcher = NULL;
    this->flags = 0;
}


/**
 * Construct String Container from R character vector
 * @param rstr R character vector
 * @param _nrecycle extend length [vectorization]
 */
StriContainerByteSearch::StriContainerByteSearch(
    ci::ReaderContext& context, SEXP rstr,
    R_len_t _nrecycle, uint32_t _flags
) : StriContainerUTF8(context, rstr, _nrecycle, true)
{
    this->flags = _flags;
    this->matcher = NULL;

    R_len_t n = get_n();
    for (R_len_t i=0; i<n; ++i) {
        if (!isNA(i) && get(i).length() <= 0) {
            // Deviation from stringi: defer warnings until the operation has
            // destroyed every Reader-owning container. R handlers may touch
            // any input alias and invalidate a live borrow.
            context.warn(MSG__EMPTY_SEARCH_PATTERN_UNSUPPORTED);
        }
    }
}


/** Copying constructor
 *
 */
StriContainerByteSearch::StriContainerByteSearch(StriContainerByteSearch& container)
    :    StriContainerUTF8((StriContainerUTF8&)container)
{
    this->matcher = NULL;
    this->flags = container.flags;
}


/** Copy operator
 * @param container source
 * @return *this
 */
StriContainerByteSearch& StriContainerByteSearch::operator=(StriContainerByteSearch& container)
{
    if (this == &container)
        return *this;

    // Deviation from stringi: replace owned state without explicitly ending
    // and then reusing this object's lifetime; assignment also copies flags.
    delete matcher;
    matcher = NULL;
    (StriContainerUTF8&) (*this) = (StriContainerUTF8&)container;
    flags = container.flags;
    return *this;
}


/** Destructor
 *
 */
StriContainerByteSearch::~StriContainerByteSearch()
{
    if (matcher) {
        delete matcher;
        matcher = NULL;
    }
}


/**
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 */
StriByteSearchMatcher* StriContainerByteSearch::getMatcher(R_len_t i) {
    if (i >= n && matcher && matcher->getPatternStr() == get(i).data()) {
        // matcher reuse
    }
    else {
        if (matcher) {
            delete matcher;
            matcher = NULL;
        }

        if (isCaseInsensitive())
            matcher = new StriByteSearchMatcherKMPci(get(i).data(), get(i).length(), isOverlap());
        else if (get(i).length() == 1)
            matcher = new StriByteSearchMatcher1(get(i).data(), get(i).length(), isOverlap());
        else if (get(i).length() < 16)
            matcher = new StriByteSearchMatcherShort(get(i).data(), get(i).length(), isOverlap());
        else
            matcher = new StriByteSearchMatcherKMP(get(i).data(), get(i).length(), isOverlap());
    }

    return matcher;
}


/** find first match - case of short pattern
 *
 * @param startPos where to start
 * @return USEARCH_DONE on no match, otherwise start index
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-11)
 *          special procedure for patternLen <= 4
 *
 * @version 0.2-4 (Marek Gagolewski, 2014-05-15)
 *          BUGFIX: load of misaligned addresses
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-30)
 *          BUGFIX: ret USEARCH_DONE immediately if startPos is too far away
 */
//R_len_t StriContainerByteSearch::findFromPosFwd_short(R_len_t startPos)
//{
//   if (startPos > searchLen-patternLen) { // this check is OK, we do a case-sensitive search
//      searchPos = searchEnd = searchLen;
//      return USEARCH_DONE;
//   }
//
//   if (patternLen == 1) {
//         // else not found
//      unsigned char pat = (unsigned char)patternStr[0];  /* TO DO: why can't this be cached? */
//      for (searchPos = startPos; searchPos<searchLen-1+1; ++searchPos) {
//         if (pat == (unsigned char)searchStr[searchPos]) {
//            searchEnd = searchPos + 1;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 2) {
//// /* v1: 17.67ms; BUG: loads misaligned addresses... */
////      uint16_t pat = *((uint16_t*)patternStr);
////      for (searchPos = startPos; searchPos<searchLen-2+1; ++searchPos) {
////         if (pat == *((uint16_t*)(searchStr+searchPos))) {
////            return searchPos;
////         }
////      }
//
///* v2: 21.62 ms */
//      // be careful: little vs big endian!
//      uint16_t pat  = ((uint16_t)((unsigned char)patternStr[0]));  /* TO DO: why can't this be cached? */
//               pat <<= 8;
//               pat |= ((uint16_t)((unsigned char)patternStr[1]));
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint16_t cur  = ((uint16_t)(*curstr));
//      ++curstr;
//      for (searchPos = startPos; searchPos<searchLen-2+1; ++searchPos) {
//         cur <<= 8;
//         cur |= (uint16_t)(*curstr);
//         ++curstr;
//         if (pat == cur) {
//            searchEnd = searchPos + 2;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 3) {
//// /* v1: 25.52ms; BUG: loads misaligned addresses... */
////      uint8_t  pat1 = (uint8_t)patternStr[0];
////      uint16_t pat2 = *((uint16_t*)(patternStr+1));
////      for (searchPos = startPos; searchPos<searchLen-3+1; ++searchPos) {
////         if (pat1 == (uint8_t)searchStr[searchPos]
////             && pat2 == *((uint16_t*)(searchStr+searchPos+1))) {
////            return searchPos;
////         }
////      }
//
///* v2: 25.95 ms */
//      uint32_t pat  = ((uint32_t)((unsigned char)patternStr[0])); /* TO DO: why can't this be cached? */
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[1]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[2]));
//
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint32_t cur  = ((uint32_t)(*curstr));
//      ++curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      ++curstr;
//
//      uint32_t mask = ~(((unsigned char)0xff)<<24);  /* TO DO: why can't this be cached? */
//
//      for (searchPos = startPos; searchPos<searchLen-3+1; ++searchPos) {
//         cur <<= 8;
//         cur |= (uint32_t)(*curstr);
//         ++curstr;
//         if ((pat&mask) == (cur&mask)) {
//            searchEnd = searchPos + 3;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 4) {
//// /* v1: 17.71ms; BUG: loads misaligned addresses... */
////      uint32_t pat = *((uint32_t*)patternStr);
////      for (searchPos = startPos; searchPos<searchLen-4+1; ++searchPos) {
////         if (pat == *((uint32_t*)(searchStr+searchPos))) {
////            return searchPos;
////         }
////      }
//
///* v2: 21.68 ms */
//      uint32_t pat  = ((uint32_t)((unsigned char)patternStr[0])); /* TO DO: why can't this be cached? */
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[1]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[2]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[3]));
//
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint32_t cur  = ((uint32_t)(*curstr));
//      ++curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      ++curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      ++curstr;
//
//
//      for (searchPos = startPos; searchPos<searchLen-4+1; ++searchPos) {
//         cur <<= 8;
//         cur |= (uint32_t)(*curstr);
//         ++curstr;
//         if (pat == cur) {
//            searchEnd = searchPos + 4;
//            return searchPos;
//         }
//      }
//   }
//   // else not found
//   searchPos = searchEnd = searchLen;
//   return USEARCH_DONE;
//}


/** find last match - case of short pattern
 *
 * @param startPos where to start
 * @return USEARCH_DONE on no match, otherwise start index
 *
 * @version 0.2-3 (Marek Gagolewski, 2014-05-11)
 *
 * @version 0.2-4 (Marek Gagolewski, 2014-05-15)
 *          BUGFIX: load of misaligned addresses
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-11-30)
 *          BUGFIX: ret USEARCH_DONE immediately if startPos indicates no match
 *
 */
//R_len_t StriContainerByteSearch::findFromPosBack_short(R_len_t startPos)
//{
//   if (startPos+1 < patternLen) { // check OK, case-sensitive search
//      searchPos = searchEnd = searchLen;
//      return USEARCH_DONE;
//   }
//
//   if (patternLen == 1) {
//      unsigned char pat = (unsigned char)patternStr[0];
//      for (searchPos = startPos-0; searchPos>=0; --searchPos) {
//         if (pat == (unsigned char)searchStr[searchPos]) {
//            searchEnd = searchPos + 1;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 2) {
//      // be careful: little vs big endian!
//      uint16_t pat  = ((uint16_t)((unsigned char)patternStr[1]));
//               pat <<= 8;
//               pat |= ((uint16_t)((unsigned char)patternStr[0]));
//
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint16_t cur  = ((uint16_t)(*curstr));
//      --curstr;
//      for (searchPos = startPos-1; searchPos>=0; --searchPos) {
//         cur <<= 8;
//         cur |= (uint16_t)(*curstr);
//         --curstr;
//         if (pat == cur) {
//            searchEnd = searchPos + 2;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 3) {
//      uint32_t pat  = ((uint32_t)((unsigned char)patternStr[2]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[1]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[0]));
//
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint32_t cur  = ((uint32_t)(*curstr));
//      --curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      --curstr;
//
//      uint32_t mask = ~(((unsigned char)0xff)<<24);
//
//      for (searchPos = startPos-2; searchPos>=0; --searchPos) {
//         cur <<= 8;
//         cur |= (uint32_t)(*curstr);
//         --curstr;
//         if ((pat&mask) == (cur&mask)) {
//            searchEnd = searchPos + 3;
//            return searchPos;
//         }
//      }
//   }
//   else if (patternLen == 4) {
//      uint32_t pat  = ((uint32_t)((unsigned char)patternStr[3])); /* TO DO: can't this be cached? */
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[2]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[1]));
//               pat <<= 8;
//               pat |= ((uint32_t)((unsigned char)patternStr[0]));
//
//      unsigned char*  curstr = (unsigned char*)(searchStr+startPos);
//      uint32_t cur  = ((uint32_t)(*curstr));
//      --curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      --curstr;
//      cur <<= 8;
//      cur |= (uint32_t)(*curstr);
//      --curstr;
//
//
//      for (searchPos = startPos-3; searchPos>=0; --searchPos) {
//         cur <<= 8;
//         cur |= (uint32_t)(*curstr);
//         --curstr;
//         if (pat == cur) {
//            searchEnd = searchPos + 4;
//            return searchPos;
//         }
//      }
//   }
//   // else not found
//   searchPos = searchEnd = searchLen;
//   return USEARCH_DONE;
//}


/** Read settings flags from a list
 *
 * Direct mode may call R error helpers.
 *
 * @param opts_fixed list
 * @param allow_overlap
 * @param warnings optional operation-level deferred warning queue
 * @return flags
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-07)
 *
 * @version 0.4-1 (Marek Gagolewski, 2014-12-08)
 *    add `overlap` option
 *
 * @version 1.1.6 (Marek Gagolewski, 2017-11-10)
 *    PROTECT STRING_ELT(names, i)
 */
uint32_t StriContainerByteSearch::getByteSearchFlags(
    SEXP opts_fixed, bool allow_overlap, ci::DeferredWarnings* warnings
)
{
    uint32_t flags = 0;
    if (!Rf_isNull(opts_fixed) && !Rf_isVectorList(opts_fixed)) {
        if (warnings)
            throw StriException(MSG__ARG_EXPECTED_LIST, "opts_fixed");
        Rf_error(MSG__ARG_EXPECTED_LIST, "opts_fixed");
    }

    R_len_t narg = Rf_isNull(opts_fixed)?0:LENGTH(opts_fixed);

    if (narg > 0) {

        SEXP names = PROTECT(Rf_getAttrib(opts_fixed, R_NamesSymbol));
        if (names == R_NilValue || LENGTH(names) != narg) {
            if (warnings) {
                UNPROTECT(1);
                throw StriException(MSG__FIXED_CONFIG_FAILED);
            }
            Rf_error(MSG__FIXED_CONFIG_FAILED);
        }

        for (R_len_t i=0; i<narg; ++i) {
            if (STRING_ELT(names, i) == NA_STRING) {
                if (warnings) {
                    UNPROTECT(1);
                    throw StriException(MSG__FIXED_CONFIG_FAILED);
                }
                Rf_error(MSG__FIXED_CONFIG_FAILED);
            }

            SEXP tmp_arg;
            PROTECT(tmp_arg = STRING_ELT(names, i));
            const char* curname = ci__copy_string_Ralloc(tmp_arg, "curname");  /* this is R_alloc'ed */
            UNPROTECT(1);

            PROTECT(tmp_arg = VECTOR_ELT(opts_fixed, i));
            try {
                if  (!strcmp(curname, "case_insensitive")) {
                    bool val = ci__prepare_arg_logical_1_notNA(
                        tmp_arg, "case_insensitive", warnings
                    );
                    if (val) flags |= BYTESEARCH_CASE_INSENSITIVE;
                }
                else if (!strcmp(curname, "overlap") && allow_overlap) {
                    bool val = ci__prepare_arg_logical_1_notNA(
                        tmp_arg, "overlap", warnings
                    );
                    if (val) flags |= BYTESEARCH_OVERLAP;
                }
                else if (warnings) {
                    // Deviation from stringi: retain this diagnostic until the
                    // caller has released its operation state.
                    std::string warning("incorrect opts_fixed setting: '");
                    warning += curname;
                    warning += "'; ignoring";
                    warnings->push(warning.c_str());
                }
                else {
                    Rf_warning(MSG__INCORRECT_FIXED_OPTION, curname);
                }
            }
            catch (...) {
                // Deferred validation throws through C++, so balance the
                // current option and names protections before propagating it.
                UNPROTECT(2);
                throw;
            }
            UNPROTECT(1);
        }
        UNPROTECT(1); /* names */
    }

    return flags;
}
