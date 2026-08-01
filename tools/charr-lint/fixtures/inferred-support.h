#ifndef CHARR_LINT_INFERRED_SUPPORT_H
#define CHARR_LINT_INFERRED_SUPPORT_H

struct InferredOwner {
    ~InferredOwner() noexcept;
};

void inferred_may_throw();
void inferred_noexcept() noexcept;
extern "C" void inferred_c_call();
InferredOwner inferred_owner() noexcept;

#endif
