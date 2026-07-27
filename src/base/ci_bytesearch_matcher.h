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


#ifndef __charr_base_ci_bytesearch_matcher_h
#define __charr_base_ci_bytesearch_matcher_h


#include "ci_stringi.h"

#include <memory>

#ifndef USEARCH_DONE
#define USEARCH_DONE -1
#endif


namespace charr { namespace base {

/**
 * Performs actual pattern matching on behalf of StriContainerByteSearch
 *
 * @version 0.5-1 (Marek Gagolewski, 2015-02-14)
 *    code taken from StriContainerByteSearch
 */
class StriByteSearchMatcher {

private:

    StriByteSearchMatcher(const StriByteSearchMatcher&); /* no copy-able */
    StriByteSearchMatcher& operator=(const StriByteSearchMatcher&);

protected:

    bool m_optOverlap;

    R_len_t m_searchPos; // -1 after reset, searchLen on no further matches
    R_len_t m_searchEnd;
    const char* m_searchStr; // owned by caller
    R_len_t m_searchLen; // in bytes

    R_len_t m_patternLen;
    const char* m_patternStr;

    virtual R_len_t findFromPos(R_len_t pos) = 0;

public:

    StriByteSearchMatcher(const char* patternStr, R_len_t patternLen, bool optOverlap) {
        this->m_optOverlap = optOverlap;
        this->m_patternStr = patternStr;
        this->m_patternLen = patternLen;
        this->m_searchStr = NULL;
    }

    const char* getPatternStr() const {
        return m_patternStr;
    }

    virtual ~StriByteSearchMatcher() { }

    virtual void reset(const char* searchStr, R_len_t searchLen) {
        this->m_searchStr = searchStr;
        this->m_searchLen = searchLen;
        this->m_searchPos = -1;
        this->m_searchEnd = -1;
    }

    virtual R_len_t findFirst() = 0;
    virtual R_len_t findLast() = 0;

    R_len_t findNext() {
        if (m_searchPos < 0) return findFirst();

        if (m_optOverlap) {
            int pos = m_searchPos;
            U8_FWD_1(m_searchStr, pos, m_searchLen);
            return findFromPos(pos);
        }
        else
            return findFromPos(m_searchEnd);
    }


    /** get start index of pattern match from the last search
     *
     * @return byte index in searchStr
     */
    inline R_len_t getMatchedStart()
    {
        if (!this->m_searchStr || !this->m_patternStr)
            throw StriException("StriByteSearchMatcher: reset() hasn't been called yet");
        if (m_searchPos < 0 || m_searchEnd <= m_searchPos ||
                m_searchPos >= m_searchLen || m_searchEnd > m_searchLen)
            throw StriException("StriByteSearchMatcher: no match at current position! This is a BUG.");

        return m_searchPos;
    }


    /** get length of pattern match from the last search
     *
     * @return byte index in searchStr
     */
    inline R_len_t getMatchedLength()
    {
        if (!this->m_searchStr || !this->m_patternStr)
            throw StriException("StriByteSearchMatcher: reset() hasn't been called yet");
        if (m_searchPos < 0 || m_searchEnd <= m_searchPos ||
                m_searchPos >= m_searchLen || m_searchEnd > m_searchLen)
            throw StriException("StriByteSearchMatcher: no match at current position! This is a BUG.");

        return m_searchEnd-m_searchPos;
    }
};

class StriByteSearchMatcherKMP : public StriByteSearchMatcher {

private:

    StriByteSearchMatcherKMP(const StriByteSearchMatcherKMP&); /* no copy-able */
    StriByteSearchMatcherKMP& operator=(const StriByteSearchMatcherKMP&);

protected:

    int* m_kmpNext;
    int m_patternPos;


    virtual R_len_t findFromPos(R_len_t startPos) {
#ifndef NDEBUG
        if (!m_searchStr) throw StriException("!m_searchStr");
#endif

        int j = startPos;
        m_patternPos = 0;

        while (j < m_searchLen) {
            while (m_patternPos >= 0 && m_patternStr[m_patternPos] != m_searchStr[j])
                m_patternPos = m_kmpNext[m_patternPos];
            m_patternPos++;
            j++;
            if (m_patternPos == m_patternLen) {
                m_searchEnd = j;
                m_searchPos = j-m_patternLen;
                return m_searchPos;
            }
        }

        // else not found
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }

public:

    virtual ~StriByteSearchMatcherKMP() {
        delete [] m_kmpNext;
    }

#ifndef NDEBUG

#endif

    StriByteSearchMatcherKMP(const char* patternStr, R_len_t patternLen, bool optOverlap)
        : StriByteSearchMatcher(patternStr, patternLen, optOverlap)
    {
        const size_t kmpMaxSize = static_cast<size_t>(patternLen)+1;
        this->m_kmpNext = new int[kmpMaxSize];
        if (!this->m_kmpNext) throw StriException(MSG__MEM_ALLOC_ERROR);
        this->m_kmpNext[0] = -100; // magic constant for an uninitialized KMP table
    }

    virtual void reset(const char* searchStr, R_len_t searchLen) {
        StriByteSearchMatcher::reset(searchStr, searchLen);
        m_patternPos = -1;
    }

    virtual R_len_t findFirst() {
        if (this->m_kmpNext[0] <= -100) {
            // Setup KMP table for FWD search
            m_kmpNext[0] = -1;
            for (R_len_t i=0; i<m_patternLen; ++i) {
                m_kmpNext[i+1] = m_kmpNext[i]+1;
                while (m_kmpNext[i+1] > 0 &&
                        m_patternStr[i] != m_patternStr[m_kmpNext[i+1]-1])
                    m_kmpNext[i+1] = m_kmpNext[m_kmpNext[i+1]-1]+1;
            }
        }

        return findFromPos(0);
    }

    virtual R_len_t findLast()  {
        if (this->m_kmpNext[0] <= -100) {
            // Setup KMP table for BACK search
            m_kmpNext[0] = -1;
            for (R_len_t i=0; i<m_patternLen; ++i) {
                m_kmpNext[i+1] = m_kmpNext[i]+1;
                while (m_kmpNext[i+1] > 0 &&
                        m_patternStr[m_patternLen-i-1] != m_patternStr[m_patternLen-(m_kmpNext[i+1]-1)-1])
                    m_kmpNext[i+1] = m_kmpNext[m_kmpNext[i+1]-1]+1;
            }
        }

        int j = m_searchLen;
        m_patternPos = 0;
        while (j > 0) {
            j--;
            while (m_patternPos >= 0 && m_patternStr[m_patternLen-1-m_patternPos] != m_searchStr[j])
                m_patternPos = m_kmpNext[m_patternPos];
            m_patternPos++;
            if (m_patternPos == m_patternLen) {
                m_searchEnd = j+m_patternLen;
                m_searchPos = j;
                return m_searchPos;
            }
        }
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }
};

class StriByteSearchMatcherKMPci : public StriByteSearchMatcher {

private:

    StriByteSearchMatcherKMPci(const StriByteSearchMatcherKMPci&); /* no copy-able */
    StriByteSearchMatcherKMPci& operator=(const StriByteSearchMatcherKMPci&);

protected:

    int* m_kmpNext;
    int m_patternPos;
    R_len_t m_patternLenCaseInsensitive;
    UChar32* m_patternStrCaseInsensitive;

    virtual R_len_t findFromPos(R_len_t startPos) {
        int j = startPos;
        m_patternPos = 0;

        UChar32 c = 0;
        while (j < m_searchLen) {
            U8_NEXT(m_searchStr, j, m_searchLen, c);
            c = u_toupper(c);
            while (m_patternPos >= 0 && m_patternStrCaseInsensitive[m_patternPos] != c)
                m_patternPos = m_kmpNext[m_patternPos];
            m_patternPos++;
            if (m_patternPos == m_patternLenCaseInsensitive) {
                m_searchEnd = j;

                // we need to go back by patternLenCaseInsensitive code points
                R_len_t k = m_patternLenCaseInsensitive;
                m_searchPos = j;
                while (k > 0) {
                    U8_BACK_1((const uint8_t*)m_searchStr, 0, m_searchPos);
                    k--;
                }
                return m_searchPos;
            }
        }

        // else not found
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }


public:


    virtual ~StriByteSearchMatcherKMPci() {
        delete [] m_kmpNext;
        delete [] m_patternStrCaseInsensitive;
    }

    StriByteSearchMatcherKMPci(const char* patternStr, R_len_t patternLen, bool optOverlap)
        : StriByteSearchMatcher(patternStr, patternLen, optOverlap),
          m_kmpNext(NULL), m_patternPos(0),
          m_patternLenCaseInsensitive(0), m_patternStrCaseInsensitive(NULL)
    {
        const size_t kmpMaxSize = static_cast<size_t>(patternLen)+1;
        std::unique_ptr<int[]> new_kmp_next(new int[kmpMaxSize]);
        std::unique_ptr<UChar32[]> new_pattern(new UChar32[kmpMaxSize]);
        new_kmp_next[0] = -100; // magic constant for an uninitialized KMP table

        UChar32 c = 0;
        R_len_t j = 0;
        while (j < patternLen) {
            U8_NEXT(patternStr, j, patternLen, c);
#ifndef NDEBUG
            if (m_patternLenCaseInsensitive >= kmpMaxSize)
                throw StriException("!NDEBUG: StriByteSearchMatcherKMPci::StriByteSearchMatcherKMPci()");
#endif
            new_pattern[m_patternLenCaseInsensitive++] = u_toupper(c);
        }

        this->m_kmpNext = new_kmp_next.release();
        this->m_patternStrCaseInsensitive = new_pattern.release();
    }

    virtual void reset(const char* searchStr, R_len_t searchLen) {
        StriByteSearchMatcher::reset(searchStr, searchLen);
        m_patternPos = -1;
    }

    virtual R_len_t findFirst() {
        if (this->m_kmpNext[0] <= -100) {
            // Setup KMP table for FWD search
            m_kmpNext[0] = -1;
            for (R_len_t i=0; i<m_patternLenCaseInsensitive; ++i) {
                m_kmpNext[i+1] = m_kmpNext[i]+1;
                while (m_kmpNext[i+1] > 0 &&
                        m_patternStrCaseInsensitive[i] != m_patternStrCaseInsensitive[m_kmpNext[i+1]-1])
                    m_kmpNext[i+1] = m_kmpNext[m_kmpNext[i+1]-1]+1;
            }
        }

        return findFromPos(0);
    }

    virtual R_len_t findLast()  {
        if (this->m_kmpNext[0] <= -100) {
            // Setup KMP table for BACK search
            m_kmpNext[0] = -1;
            for (R_len_t i=0; i<m_patternLenCaseInsensitive; ++i) {
                m_kmpNext[i+1] = m_kmpNext[i]+1;
                while (m_kmpNext[i+1] > 0 &&
                        m_patternStrCaseInsensitive[m_patternLenCaseInsensitive-i-1] !=
                        m_patternStrCaseInsensitive[m_patternLenCaseInsensitive-(m_kmpNext[i+1]-1)-1])
                    m_kmpNext[i+1] = m_kmpNext[m_kmpNext[i+1]-1]+1;
            }
        }

        int j = m_searchLen;
        m_patternPos = 0;
        while (j > 0) {
            UChar32 c;
            U8_PREV(m_searchStr, 0, j, c);
            c = u_toupper(c);
            while (m_patternPos >= 0 &&
                    m_patternStrCaseInsensitive[m_patternLenCaseInsensitive-1-m_patternPos] != c)
                m_patternPos = m_kmpNext[m_patternPos];
            m_patternPos++;
            if (m_patternPos == m_patternLenCaseInsensitive) {
                m_searchPos = j;

                // we need to go forward by patternLenCaseInsensitive code points
                R_len_t k = m_patternLenCaseInsensitive;
                m_searchEnd = j;
                while (k > 0) {
                    U8_FWD_1((const uint8_t*)m_searchStr, m_searchEnd, m_searchLen);
                    k--;
                }

                return m_searchPos;
            }
        }
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }
};


class StriByteSearchMatcher1 : public StriByteSearchMatcher {

private:

    StriByteSearchMatcher1(const StriByteSearchMatcher1&); /* no copy-able */
    StriByteSearchMatcher1& operator=(const StriByteSearchMatcher1&);

protected:

    virtual R_len_t findFromPos(R_len_t startPos) {
#ifndef NDEBUG
        if (!m_searchStr) throw StriException("!m_searchStr");
#endif

        if (startPos > m_searchLen-m_patternLen) { // this check is OK, we do a case-sensitive search
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        const char* res = static_cast<const char*>(memchr(
            m_searchStr+startPos,
            static_cast<unsigned char>(m_patternStr[0]),
            static_cast<size_t>(m_searchLen-startPos)
        ));
        if (res) {
            m_searchPos = (int)(res-m_searchStr);
            m_searchEnd = m_searchPos+1;
            return m_searchPos;
        }
        else {
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        /*unsigned char pat = (unsigned char)m_patternStr[0];
        for (m_searchPos = startPos; m_searchPos<m_searchLen-1+1; ++m_searchPos) {
           if (pat == (unsigned char)m_searchStr[m_searchPos]) {
              m_searchEnd = m_searchPos + 1;
              return m_searchPos;
           }
        }

        // else not found
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
        */
    }


public:

    StriByteSearchMatcher1(const char* patternStr, R_len_t patternLen, bool optOverlap)
        : StriByteSearchMatcher(patternStr, patternLen, optOverlap)
    {
#ifndef NDEBUG
        if (patternLen != 1) throw StriException("StriByteSearchMatcher1");
#endif
    }

    virtual R_len_t findFirst() {
        return findFromPos(0);
    }

    virtual R_len_t findLast()  {
        if (m_searchLen < m_patternLen) { // check OK, case-sensitive search
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        unsigned char pat = (unsigned char)m_patternStr[0];
        for (m_searchPos = m_searchLen-1; m_searchPos>=0; --m_searchPos) {
            if (pat == (unsigned char)m_searchStr[m_searchPos]) {
                m_searchEnd = m_searchPos + 1;
                return m_searchPos;
            }
        }

        // else not found
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }
};


class StriByteSearchMatcherShort : public StriByteSearchMatcher {

private:

    StriByteSearchMatcherShort(const StriByteSearchMatcherShort&); /* no copy-able */
    StriByteSearchMatcherShort& operator=(const StriByteSearchMatcherShort&);

protected:

    virtual R_len_t findFromPos(R_len_t startPos) {
#ifndef NDEBUG
        if (!m_searchStr) throw StriException("!m_searchStr");
#endif

        if (m_patternLen <= 0) {
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        if (startPos > m_searchLen-m_patternLen) { // this check is OK, we do a case-sensitive search
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        const R_len_t lastPos = m_searchLen-m_patternLen;
        const char* current = m_searchStr+startPos;
        const char* const last = m_searchStr+lastPos;
        while (current <= last) {
            const char* candidate = static_cast<const char*>(memchr(
                current,
                static_cast<unsigned char>(m_patternStr[0]),
                static_cast<size_t>(last-current+1)
            ));
            if (!candidate)
                break;

            if (0 == memcmp(
                    candidate+1,
                    m_patternStr+1,
                    static_cast<size_t>(m_patternLen-1))) {
                m_searchPos = static_cast<R_len_t>(candidate-m_searchStr);
                m_searchEnd = m_searchPos+m_patternLen;
                return m_searchPos;
            }

            current = candidate+1;
        }

        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }


public:

    StriByteSearchMatcherShort(const char* patternStr, R_len_t patternLen, bool optOverlap)
        : StriByteSearchMatcher(patternStr, patternLen, optOverlap)
    {

    }

    virtual R_len_t findFirst() {
        return findFromPos(0);
    }

    virtual R_len_t findLast()  {
        if (m_patternLen <= 0) {
            m_searchPos = m_searchEnd = m_searchLen;
            return USEARCH_DONE;
        }

        R_len_t startPos = m_searchLen;

        for (m_searchPos = startPos-m_patternLen; m_searchPos>=0; --m_searchPos) {
            if (0 == memcmp(
                    m_searchStr+m_searchPos,
                    m_patternStr,
                    static_cast<size_t>(m_patternLen))) {
                m_searchEnd = m_searchPos + m_patternLen;
                return m_searchPos;
            }
        }

        // else not found
        m_searchPos = m_searchEnd = m_searchLen;
        return USEARCH_DONE;
    }
};



} } // namespace charr::base

#endif
