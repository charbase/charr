SHELL   := /bin/bash
PACKAGE := $(shell perl -aF: -ne 'print, exit if s/^Package:\s+//' DESCRIPTION)
VERSION := $(shell perl -aF: -ne 'print, exit if s/^Version:\s+//' DESCRIPTION)
BUILD   := $(PACKAGE)_$(VERSION).tar.gz
RLIBS   := $(shell Rscript -e 'cat(paste(.libPaths(), collapse = ":"))')

.PHONY: doc build install check check-no-vignette test test-stringi \
	test-base test-altrep test-system test-bundle \
	test-san test-valgrind vignette figures pkgdown pkgdown-index \
	clean-pkgdown clean clean-altrep clean-build-products

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

# Run the complete suite against all three public backends after one install.
test: install
	for backend in stringi base altrep; do \
		printf '== charr_backend=%s\n' "$$backend"; \
		(cd tests && NOT_CRAN=true CHARR_BACKEND=$$backend Rscript testthat.R) || exit 1; \
	done

test-stringi: install
	cd tests && NOT_CRAN=true CHARR_BACKEND=stringi Rscript testthat.R

test-base: install
	cd tests && NOT_CRAN=true CHARR_BACKEND=base Rscript testthat.R

test-altrep: install
	cd tests && NOT_CRAN=true CHARR_BACKEND=altrep Rscript testthat.R

# ICU-mode validation uses the same three-backend matrix with configure's
# choice made explicit. The system target fails instead of falling back when
# the installed ICU is not an allowlisted candidate.
test-system:
	CHARR_SYSTEM_ICU=yes $(MAKE) test

test-bundle:
	CHARR_SYSTEM_ICU=no $(MAKE) test

# ASan + UBSan over the full suite, all three backends. Only the package
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
	printf 'CXXFLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all\nCXX17FLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all\nSHLIB_CXXLDFLAGS = -fsanitize=address,undefined -shared\nSHLIB_CXX17LDFLAGS = -fsanitize=address,undefined -shared\n' > $$tmp_lib/Makevars.san; \
	CHARR_SYSTEM_ICU=yes ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=detect_leaks=0 \
	  R_MAKEVARS_USER=$$tmp_lib/Makevars.san \
	  R CMD INSTALL --preclean --no-test-load -l $$tmp_lib .; \
	for backend in stringi base altrep; do \
	  printf '== charr_backend=%s\n' "$$backend"; \
	  (cd tests && LD_PRELOAD=$$(gcc -print-file-name=libasan.so) \
	    ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=detect_leaks=0 \
	    UBSAN_OPTIONS=print_stacktrace=1 \
	    R_MAKEVARS_USER=$$tmp_lib/Makevars.san R_LIBS=$$tmp_lib:$(RLIBS) \
	    NOT_CRAN=true CHARR_BACKEND=$$backend Rscript testthat.R) || exit 1; \
	done; \
	rm -rf $$tmp_lib

# Full suite under valgrind memcheck, all three backends (no
# instrumentation rebuild; slow). Requires valgrind.
test-valgrind:
	tmp_lib=$$(mktemp -d /tmp/$(PACKAGE)-vg-XXXXXX); \
	R CMD INSTALL --preclean -l $$tmp_lib .; \
	for backend in stringi base altrep; do \
	  printf '== charr_backend=%s\n' "$$backend"; \
	  (cd tests && R_LIBS=$$tmp_lib:$(RLIBS) NOT_CRAN=true CHARR_BACKEND=$$backend \
	    R --vanilla -d "valgrind --tool=memcheck --leak-check=no --error-exitcode=1" -f testthat.R) || exit 1; \
	done; \
	rm -rf $$tmp_lib

# Documentation. The main vignette is also the README and the pkgdown home
# page, rendered from one source so the three cannot drift apart.
BENCH_LABEL := optimized-backends-record-20260724

figures:
	Rscript inst/extra/benchmark/make-vignette-figures.R $(BENCH_LABEL)

vignette:
	mkdir -p local/cache
	XDG_CACHE_HOME=$(CURDIR)/local/cache quarto render vignettes/charr.qmd --to html
	XDG_CACHE_HOME=$(CURDIR)/local/cache quarto render vignettes/charr.qmd --to gfm --output README.md --output-dir .
	XDG_CACHE_HOME=$(CURDIR)/local/cache quarto render vignettes/under-the-hood.qmd --to html

pkgdown: clean-pkgdown doc
	$(MAKE) pkgdown-index
	mkdir -p local/cache
	XDG_CACHE_HOME=$(CURDIR)/local/cache R_USER_CACHE_DIR=$(CURDIR)/local/cache/R \
	  IN_PKGDOWN=true Rscript -e 'pkgdown::build_site(new_process = FALSE, install = FALSE, quiet = FALSE, override = list(home = list(sidebar = FALSE)))'
	# pkgdown turns every root-level .md into a page and its file list is hard
	# coded, so CLAUDE.md becomes CLAUDE.html with no way to configure it out.
	# The CI workflow that publishes the site drops the same two files.
	rm -f docs/CLAUDE.html docs/CLAUDE.md
	$(MAKE) clean-altrep

pkgdown-index:
	mkdir -p pkgdown
	Rscript -e 'x <- readLines("README.md", warn = FALSE); keep <- !grepl("^<img src=\"man/figures/logo\\.svg\"", x); writeLines(x[keep], "pkgdown/index.md")'

clean-pkgdown:
	rm -rf docs
	rm -f pkgdown/index.md

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
