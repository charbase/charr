#ifndef CHARR_LINT_RESOURCE_SUPPORT_H
#define CHARR_LINT_RESOURCE_SUPPORT_H

void* raw_open() noexcept;
void raw_close(void*) noexcept;
void* raw_replace(void*, unsigned long) noexcept;

#endif
