SHELL   := /bin/bash
PACKAGE := $(shell perl -aF: -ne 'print, exit if s/^Package:\s+//' DESCRIPTION)
VERSION := $(shell perl -aF: -ne 'print, exit if s/^Version:\s+//' DESCRIPTION)
BUILD   := $(PACKAGE)_$(VERSION).tar.gz
RLIBS   := $(shell Rscript -e 'cat(paste(.libPaths(), collapse = ":"))')
CODE_MAP_DIR ?= scratch/code-map
CODE_MAP_BACKEND_METHODS ?= 70
LINT_EFFECT_ARGS := \
	--effects tools/charr-lint/effects.tsv \
	--effect-overrides tools/charr-lint/effect-overrides.tsv

.PHONY: doc build install install-dev check check-no-vignette test test-stringi \
	test-base test-altrep test-system test-bundle \
	test-san test-valgrind vignette figures pkgdown pkgdown-index \
	lint lint-tool lint-fixtures lint-db lint-frontier lint-converted lint-audit \
	lint-effects-update \
	code-map code-map-current code-map-validate \
	clean-pkgdown clean clean-altrep \
	clean-build-products

LINT_CONVERTED := \
	src/runtime/icu.cpp \
	src/runtime/registration.cpp \
	src/shared/boundary_iterator.cpp \
	src/shared/case_mapper.cpp \
	src/shared/character_class.cpp \
	src/shared/character_class_search.cpp \
	src/shared/collation_ordering.cpp \
	src/shared/collation_search.cpp \
	src/shared/collator.cpp \
	src/shared/encoding_info.cpp \
	src/shared/fixed_search.cpp \
	src/shared/join.cpp \
	src/shared/line_split.cpp \
	src/shared/native_to_utf8.cpp \
	src/shared/nfc_normalizer.cpp \
	src/shared/r_matrix.cpp \
	src/shared/read_lines.cpp \
	src/shared/regex_search.cpp \
	src/shared/repeat.cpp \
	src/shared/replacement.cpp \
	src/shared/substring.cpp \
	src/shared/title_case.cpp \
	src/shared/utf8.cpp \
	src/shared/wrap.cpp \
	src/base_backend/collator/options.cpp \
	src/altrep_backend/collator/options.cpp \
	src/base_backend/boundary/options_r.cpp \
	src/altrep_backend/boundary/options_r.cpp \
	src/base_backend/fixed/options.cpp \
	src/altrep_backend/fixed/options.cpp \
	src/base_backend/regex/options_r.cpp \
	src/altrep_backend/regex/options_r.cpp \
	src/base_backend/entrypoints.cpp \
	src/altrep_backend/entrypoints.cpp \
	src/base_backend/ci_exception.cpp \
	src/altrep_backend/ci_exception.cpp \
	src/base_backend/ci_prepare_arg.cpp \
	src/altrep_backend/ci_prepare_arg.cpp \
	src/base_backend/ci_search_common.cpp \
	src/altrep_backend/ci_search_common.cpp \
	src/base_backend/ci_length.cpp \
	src/altrep_backend/ci_length.cpp \
	src/base_backend/ci_reverse.cpp \
	src/altrep_backend/ci_reverse.cpp \
	src/base_backend/ci_split_lines.cpp \
	src/altrep_backend/ci_split_lines.cpp \
	src/base_backend/ci_search_other_split.cpp \
	src/altrep_backend/ci_search_other_split.cpp \
	src/base_backend/ci_escape.cpp \
	src/altrep_backend/ci_escape.cpp \
	src/base_backend/ci_join.cpp \
	src/altrep_backend/ci_join.cpp \
	src/base_backend/ci_sub.cpp \
	src/altrep_backend/ci_sub.cpp \
	src/base_backend/ci_pad.cpp \
	src/altrep_backend/ci_pad.cpp \
	src/base_backend/ci_compare.cpp \
	src/altrep_backend/ci_compare.cpp \
	src/base_backend/ci_dup.cpp \
	src/altrep_backend/ci_dup.cpp \
	src/base_backend/ci_duplicated.cpp \
	src/altrep_backend/ci_duplicated.cpp \
	src/base_backend/ci_order_rank.cpp \
	src/altrep_backend/ci_order_rank.cpp \
	src/base_backend/ci_search_coll_count.cpp \
	src/altrep_backend/ci_search_coll_count.cpp \
	src/base_backend/ci_search_coll_detect.cpp \
	src/altrep_backend/ci_search_coll_detect.cpp \
	src/base_backend/ci_search_coll_extract.cpp \
	src/altrep_backend/ci_search_coll_extract.cpp \
	src/base_backend/ci_search_coll_locate.cpp \
	src/altrep_backend/ci_search_coll_locate.cpp \
	src/base_backend/ci_search_coll_replace.cpp \
	src/altrep_backend/ci_search_coll_replace.cpp \
	src/base_backend/ci_search_coll_split.cpp \
	src/altrep_backend/ci_search_coll_split.cpp \
	src/base_backend/ci_search_coll_startsendswith.cpp \
	src/altrep_backend/ci_search_coll_startsendswith.cpp \
	src/base_backend/ci_search_fixed_count.cpp \
	src/altrep_backend/ci_search_fixed_count.cpp \
	src/base_backend/ci_search_fixed_detect.cpp \
	src/altrep_backend/ci_search_fixed_detect.cpp \
	src/base_backend/ci_search_fixed_extract_first.cpp \
	src/altrep_backend/ci_search_fixed_extract_first.cpp \
	src/base_backend/ci_search_fixed_extract.cpp \
	src/altrep_backend/ci_search_fixed_extract.cpp \
	src/base_backend/ci_search_fixed_locate.cpp \
	src/altrep_backend/ci_search_fixed_locate.cpp \
	src/base_backend/ci_search_fixed_replace.cpp \
	src/altrep_backend/ci_search_fixed_replace.cpp \
	src/base_backend/ci_search_fixed_split.cpp \
	src/altrep_backend/ci_search_fixed_split.cpp \
	src/base_backend/ci_search_fixed_startsendswith.cpp \
	src/altrep_backend/ci_search_fixed_startsendswith.cpp \
	src/base_backend/ci_search_regex_count.cpp \
	src/altrep_backend/ci_search_regex_count.cpp \
	src/base_backend/ci_search_regex_detect.cpp \
	src/altrep_backend/ci_search_regex_detect.cpp \
	src/base_backend/ci_search_regex_extract.cpp \
	src/altrep_backend/ci_search_regex_extract.cpp \
	src/base_backend/ci_search_regex_locate.cpp \
	src/altrep_backend/ci_search_regex_locate.cpp \
	src/base_backend/ci_search_regex_match.cpp \
	src/altrep_backend/ci_search_regex_match.cpp \
	src/base_backend/ci_search_regex_replace.cpp \
	src/altrep_backend/ci_search_regex_replace.cpp \
	src/base_backend/ci_search_regex_split.cpp \
	src/altrep_backend/ci_search_regex_split.cpp \
	src/base_backend/ci_search_boundaries_count.cpp \
	src/altrep_backend/ci_search_boundaries_count.cpp \
	src/base_backend/ci_search_boundaries_extract.cpp \
	src/altrep_backend/ci_search_boundaries_extract.cpp \
	src/base_backend/ci_search_boundaries_split.cpp \
	src/altrep_backend/ci_search_boundaries_split.cpp \
	src/base_backend/ci_search_boundaries_locate.cpp \
	src/altrep_backend/ci_search_boundaries_locate.cpp \
	src/base_backend/ci_search_class_replace.cpp \
	src/altrep_backend/ci_search_class_replace.cpp \
	src/base_backend/ci_search_class_trim.cpp \
	src/altrep_backend/ci_search_class_trim.cpp \
	src/base_backend/ci_replace_na.cpp \
	src/altrep_backend/ci_replace_na.cpp \
	src/base_backend/ci_trans_normalization.cpp \
	src/altrep_backend/ci_trans_normalization.cpp \
	src/base_backend/ci_trans_casemap.cpp \
	src/altrep_backend/ci_trans_casemap.cpp \
	src/base_backend/ci_trans_title.cpp \
	src/altrep_backend/ci_trans_title.cpp \
	src/base_backend/ci_ucnv.cpp \
	src/altrep_backend/ci_ucnv.cpp \
	src/base_backend/ci_encoding_conversion.cpp \
	src/altrep_backend/ci_encoding_conversion.cpp \
	src/base_backend/ci_encoding_management.cpp \
	src/altrep_backend/ci_encoding_management.cpp \
	src/base_backend/ci_wrap.cpp \
	src/altrep_backend/ci_wrap.cpp \
	src/altrep_backend/io/utf8_output.cpp

lint:
	$(MAKE) lint-fixtures
	$(MAKE) lint-db
	$(MAKE) lint-converted

lint-tool:
	$(MAKE) -C tools/charr-lint all

lint-fixtures:
	$(MAKE) -C tools/charr-lint fixtures

# Bundled ICU injects uconfig_local.h; it contains macros, not functions.
lint-frontier:
	@test -s compile_commands.json
	@diff -u --label lint-frontier --label production-cpp \
	  <(printf '%s\n' $(LINT_CONVERTED) | sort -u) \
	  <(find src -path src/icu78 -prune -o -name '*.cpp' -print | sort -u)
	@diff -u --label lint-frontier --label compiled-cpp \
	  <(printf '%s\n' $(LINT_CONVERTED) | sort -u) \
	  <(jq -r '.[].file' compile_commands.json | \
	    while IFS= read -r file; do \
	      realpath --relative-to='$(CURDIR)' "$$file"; \
	    done | grep -E '^src/.*\.cpp$$' | grep -v '^src/icu78/' | sort -u)
	@diff -u --label production-headers --label reachable-headers \
	  <(find src -path src/icu78 -prune -o -name '*.h' -print | \
	    grep -v '^src/uconfig_local\.h$$' | sort -u) \
	  <(clang-scan-deps -compilation-database=compile_commands.json \
	      -format=experimental-full | \
	    jq -r '.. | objects | .["file-deps"]? // empty | .[]' | \
	    grep -E '^$(CURDIR)/src/.*\.h$$' | grep -v '/src/icu78/' | \
	    while IFS= read -r file; do \
	      realpath --relative-to='$(CURDIR)' "$$file"; \
	    done | sort -u)

lint-db: clean-altrep
	rm -f compile_commands.json
	lint_lib=$$(mktemp -d /tmp/$(PACKAGE)-lint-XXXXXX); \
	trap 'rm -rf "$$lint_lib"' EXIT; \
	bear --output compile_commands.json -- \
	  R CMD INSTALL --preclean --libs-only --no-test-load \
	    -l "$$lint_lib" .
	test -s compile_commands.json
	$(MAKE) clean-altrep

lint-converted: lint-tool
	@$(MAKE) lint-frontier
	local/charr-lint/charr-lint \
	  $(LINT_EFFECT_ARGS) -p . $(LINT_CONVERTED)

lint-audit: lint-tool lint-db
	@$(MAKE) lint-frontier
	files=$$(jq -r '.[].file' compile_commands.json | sort -u); \
	local/charr-lint/charr-lint --audit \
	  $(LINT_EFFECT_ARGS) -p . $$files

lint-effects-update: lint-tool lint-db
	@$(MAKE) lint-frontier
	files=$$(jq -r '.[].file' compile_commands.json | sort -u); \
	local/charr-lint/charr-lint --audit \
	  $(LINT_EFFECT_ARGS) \
	  --write-effects-manifest tools/charr-lint/effects.tsv \
	  -p . $$files

# `code-map` refreshes the compilation database. Use `code-map-current` while
# iterating on the viewer when the existing database still matches the source.
code-map: lint-db
	$(MAKE) code-map-current

code-map-current: lint-tool
	@$(MAKE) lint-frontier
	mkdir -p "$(CODE_MAP_DIR)"
	local/charr-lint/charr-lint \
	  $(LINT_EFFECT_ARGS) \
	  --code-map-dir "$(CODE_MAP_DIR)" \
	  -p . $(LINT_CONVERTED)
	cp tools/charr-map/index.html tools/charr-map/app.js "$(CODE_MAP_DIR)/"
	$(MAKE) code-map-validate

code-map-validate:
	Rscript tools/charr-map/validate.R \
	  "$(CODE_MAP_DIR)" "$(words $(LINT_CONVERTED))" \
	  "$(CODE_MAP_BACKEND_METHODS)"

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

install-dev: clean-altrep
	R CMD INSTALL --preclean .
	$(MAKE) clean-altrep

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
