SHELL   := /bin/bash
PACKAGE := $(shell perl -aF: -ne 'print, exit if s/^Package:\s+//' DESCRIPTION)
VERSION := $(shell perl -aF: -ne 'print, exit if s/^Version:\s+//' DESCRIPTION)
BUILD   := $(PACKAGE)_$(VERSION).tar.gz
RLIBS   := $(shell Rscript -e 'cat(paste(.libPaths(), collapse = ":"))')

.PHONY: doc build install check check-no-vignette test test-altrep \
	clean clean-altrep clean-build-products

check: $(BUILD)
	R CMD check --as-cran $<

check-no-vignette: $(BUILD)
	R CMD check --as-cran --no-build-vignettes $<

doc:
	Rscript -e 'roxygen2::roxygenise(roclets = "rd")'

$(BUILD): clean-altrep
	rm -f $(BUILD)
	R CMD build .
	$(MAKE) clean-altrep

build: $(BUILD)

install: $(BUILD)
	$(MAKE) clean-altrep
	R CMD INSTALL $(BUILD)
	$(MAKE) clean-altrep
	rm -f $(BUILD)

# The stringr suite, backend switch off: must be green verbatim.
test: install
	cd tests && NOT_CRAN=true Rscript testthat.R

# The same suite with backend-altrep enabled. The dual run is the
# equivalence proof; both must pass identically.
test-altrep: install
	cd tests && NOT_CRAN=true CHARR_ALTREP=true Rscript testthat.R

# ASan + UBSan over the full suite, both backend states. Only the package
# is instrumented -- R itself is not, so libasan must be preloaded and leak
# checking disabled (the R interpreter intentionally leaks at exit).
#
# --no-test-load is REQUIRED: R CMD INSTALL's post-install load test runs R
# without the libasan LD_PRELOAD, so loading the instrumented .so aborts and
# INSTALL removes the package, silently falling through to the
# uninstrumented user-library charr. The test runs below do the real
# (preloaded) load.
test-san:
	tmp_lib=$$(mktemp -d /tmp/$(PACKAGE)-san-XXXXXX); \
	printf 'CXXFLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all\nSHLIB_CXXLDFLAGS = -fsanitize=address,undefined -shared\n' > $$tmp_lib/Makevars.san; \
	R_MAKEVARS_USER=$$tmp_lib/Makevars.san R CMD INSTALL --preclean --no-test-load -l $$tmp_lib .; \
	for flag in "" true; do \
	  echo "== CHARR_ALTREP=$$flag"; \
	  (cd tests && LD_PRELOAD=$$(gcc -print-file-name=libasan.so) \
	    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
	    R_MAKEVARS_USER=$$tmp_lib/Makevars.san R_LIBS=$$tmp_lib:$(RLIBS) \
	    NOT_CRAN=true CHARR_ALTREP=$$flag Rscript testthat.R) || exit 1; \
	done; \
	rm -rf $$tmp_lib

# Full suite under valgrind memcheck, both backend states (no
# instrumentation rebuild; slow). Requires valgrind.
test-valgrind:
	tmp_lib=$$(mktemp -d /tmp/$(PACKAGE)-vg-XXXXXX); \
	R CMD INSTALL --preclean -l $$tmp_lib .; \
	for flag in "" true; do \
	  echo "== CHARR_ALTREP=$$flag"; \
	  (cd tests && R_LIBS=$$tmp_lib:$(RLIBS) NOT_CRAN=true CHARR_ALTREP=$$flag \
	    R --vanilla -d "valgrind --tool=memcheck --leak-check=no --error-exitcode=1" -f testthat.R) || exit 1; \
	done; \
	rm -rf $$tmp_lib

clean: clean-altrep clean-build-products

clean-altrep:
	find . -iname "*.a" -exec rm {} \;
	find . -iname "*.o" -exec rm {} \;
	find . -iname "*.so" -exec rm {} \;
	find . -iname "*.dll" -exec rm {} \;
	rm -f src/symbols.rds

clean-build-products:
	rm -f $(PACKAGE)_*.tar.gz
	rm -rf $(PACKAGE).Rcheck ..Rcheck
	rm -rf docs
