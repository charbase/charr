#include "charr_base.h"
#include "ci_exception.h"
#include "ci_exports.h"

#define CHARR_BASE_ARGS_1 a1
#define CHARR_BASE_ARGS_2 a1, a2
#define CHARR_BASE_ARGS_3 a1, a2, a3
#define CHARR_BASE_ARGS_4 a1, a2, a3, a4
#define CHARR_BASE_ARGS_5 a1, a2, a3, a4, a5
#define CHARR_BASE_ARGS_6 a1, a2, a3, a4, a5, a6
#define CHARR_BASE_ARGS_7 a1, a2, a3, a4, a5, a6, a7
#define CHARR_BASE_ARGS_8 a1, a2, a3, a4, a5, a6, a7, a8
#define CHARR_BASE_ARGS_9 a1, a2, a3, a4, a5, a6, a7, a8, a9
#define CHARR_BASE_ARGS_10 a1, a2, a3, a4, a5, a6, a7, a8, a9, a10
#define CHARR_BASE_ARGS_11 a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11
#define CHARR_BASE_ARGS_12 \
    a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12

#define CHARR_BASE_DEFINE(name, arity) \
    extern "C" SEXP C_charr_base_##name(CHARR_BASE_FORMALS_##arity) \
    { \
        return charr::base::r_boundary([&]() -> SEXP { \
            return charr::base::name(CHARR_BASE_ARGS_##arity); \
        }); \
    }
CHARR_BASE_NATIVE_METHODS(CHARR_BASE_DEFINE)
#undef CHARR_BASE_DEFINE
