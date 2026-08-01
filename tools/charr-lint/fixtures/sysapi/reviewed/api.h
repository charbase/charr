#ifndef CHARR_LINT_REVIEWED_API_H
#define CHARR_LINT_REVIEWED_API_H

/* Mirrors ICU's urename.h: every entry point is renamed by pasting the
   version suffix onto the base name, so the declared name is spelled in
   Clang's scratch buffer rather than in this header. */
#define REVIEWED_PASTE(name, suffix) name##suffix
#define REVIEWED_RENAME(name) REVIEWED_PASTE(name, _78)

#define reviewed_pasted_call REVIEWED_RENAME(reviewed_pasted_call)

extern "C" int reviewed_plain_call(int value);
extern "C" int reviewed_pasted_call(int value);

#endif
