# charr-owned file: tools/import-upstream.R must not rename here.
#
# tools/trim-icudt.R — build charr's trimmed, xz-compressed ICU data archive.
#
# Usage:
#   Rscript tools/trim-icudt.R [path/to/full/icudt78l.dat]
#
# Reads a COMPLETE little-endian ICU common data archive, drops the categories
# unreachable through charr's public API (rationale below), and writes
#   src/icu78/data/icudt78l.dat.xz       (shipped in the source package)
#   src/icu78/data/icudt78l.dat.md5sum   (of the decompressed trimmed .dat)
# src/install.libs.R decompresses and verifies the archive at install time.
#
# Where to get the full archive when it is not in the tree:
#   - `tools/icu78/extract-full-data.sh` verifies the official ICU4C 78.3
#     source archive and extracts its `icu/source/data/in/icudt78l.dat` to
#     `local/icu78/icudt78l.dat`.
#
# ── Why trimming is sound: the reader relation ────────────────────────────
#
# The archive is a lazily-loaded, name-keyed database. An item's bytes are
# read only when some ICU service executes udata_open/ures_open with that
# item's name; nothing is "linked". Each service reads only its own subtree,
# so droppability reduces to: can charr's public API construct that service?
# Decide by WHO READS A TABLE, never by whether the concept can occur in
# processed text (a "€" in a string is the string's bytes; curr/ is only the
# locale money-formatting vocabulary, which no exported verb consults).
#
#   kept    coll/          Collator: str_sort/order/rank/equal/unique, coll()
#   kept    brkitr/        BreakIterator: boundary(), str_wrap, str_to_title,
#                          regex (?w); includes the word dictionaries
#   kept    *.cnv,cnvalias Converters: str_conv(from=), non-UTF-8 input
#   kept    unames.icu     regex \N{CHARACTER NAME}
#   kept    uemoji/ulayout regex \p{Emoji}, \p{InSC=...}, \p{InPC=...}
#   kept    nfkc.nrm       NOT exposed directly, but the CJK dictionary break
#                          engine NFKC-normalizes before cjdict lookup; without
#                          it CJK word segmentation silently returns unsplit
#                          text (no error!). Found empirically 2026-07-14.
#   kept    root machinery keyTypeData/supplementalData/metadata/res_index/...
#                          locale keyword + fallback plumbing for the above
#   drop    main locales   date/number patterns, display names: formatters only
#   drop    zone/ curr/ lang/ unit/ region/  datetime, money, display names
#   drop    rbnf/ translit/                  spellout, transliteration
#   drop    *.spp uts46.nrm confusables.cfu  stringprep/IDNA, spoof checking
#   drop    nfkc_cf/nfkc_scf.nrm             casefold NFKC: uts46/uspoof users
#
# Notes that save future-you a day:
#   - Character properties (\p{L}, \p{Sc}, scripts, ucase, ubidi) and NFC are
#     compiled into libicuuc as C arrays; there is deliberately no nfc.nrm in
#     the archive, and no property class can ever constrain the trim.
#   - ICU degrades gracefully: a missing bundle falls back (often silently, or
#     with U_USING_DEFAULT_WARNING), a missing dictionary just disables the
#     engine. "It runs" proves nothing — sufficiency means value-AND-warning
#     identity vs installed stringi. Warning parity has a baseline: e.g.
#     str_to_title(locale = "nl") warns on BOTH routes even with full data
#     (brkitr has no nl bundle; stringi surfaces the root-fallback warning).
#   - tests/testthat/test-icu-data.R holds the canary probes. The ICU 78.3
#     import was checked against both the full and trimmed 78.3 archives. The
#     stringr suite alone stayed green while CJK segmentation was broken — do
#     not treat it as sufficiency evidence.
#   - Sufficiency evidence so far: dual suite + those canaries. Entry-by-entry
#     NECESSITY proofs (drop one kept item -> its canary must fail) are not
#     complete, so this script's keep set is validated-sufficient, not
#     proven-minimal.
#   - Only little-endian is handled ("l" archives; every CRAN platform except
#     s390x). For big-endian, ship icudt<NN>b and flip endian= in the readBin
#     calls or byte-swap at build time like stringi does.
#
# ── ICU upgrade protocol ──────────────────────────────────────────────────
#   1. Vendor the new ICU sources; obtain the matching full icudt<NN>l.dat.
#   2. Update `icudt_version` below; run this script against the full archive.
#      The guards abort on renamed/moved/vanished critical items and on drop
#      patterns that no longer match anything — investigate, don't silence.
#   3. Reinstall; run the complete base and ALTREP backend suites.
#      If a test-icu-data.R literal legitimately changed with Unicode/CLDR,
#      re-derive it from a same-version ICU oracle.
#   4. Re-check compression choice if tempted. For ICU 78.3 the 13,478,992-byte
#      trimmed archive compresses to 3,846,908 bytes with xz -9e. xzfile is in
#      base R, and R's compression = -9 is byte-identical to CLI `xz -9e`.

icudt_version <- "78l"

args <- commandArgs(trailingOnly = TRUE)
dat_name <- sprintf("icudt%s.dat", icudt_version)
out_dir <- file.path("src", "icu78", "data")

find_input <- function() {
  if (length(args) >= 1L) return(args[[1L]])
  cands <- c(
    file.path("local", "icu78", dat_name),         # extraction helper output
    file.path("inst", "icu", dat_name),            # older local workflow
    file.path(dirname(out_dir), "data", dat_name)  # manually decompressed
  )
  for (p in cands) if (file.exists(p)) return(p)
  stop(
    "full ", dat_name, " not found; pass its path as the first argument.\n",
    "Run tools/icu78/extract-full-data.sh to verify and extract it from ",
    "the official ICU4C 78.3 source archive.",
    call. = FALSE
  )
}

u32 <- function(x, off) {  # off is 0-based
  v <- readBin(x[(off + 1L):(off + 4L)], "integer", size = 4L,
               endian = "little")
  stopifnot(v >= 0L)
  v
}

parse_dat <- function(x) {
  hdr <- readBin(x[1:2], "integer", size = 2L, endian = "little",
                 signed = FALSE)
  if (!(x[3] == as.raw(0xda) && x[4] == as.raw(0x27))) {
    stop("not an ICU data archive (bad magic)")
  }
  n <- u32(x, hdr)
  toc <- readBin(x[(hdr + 5L):(hdr + 4L + 8L * n)], "integer", n = 2L * n,
                 size = 4L, endian = "little")
  name_off <- toc[seq(1L, 2L * n, by = 2L)]
  data_off <- toc[seq(2L, 2L * n, by = 2L)]
  stopifnot(all(name_off >= 0L), all(data_off >= 0L))
  names <- vapply(name_off, function(o) {
    s <- hdr + o + 1L
    end <- s
    while (x[end] != as.raw(0L)) end <- end + 1L
    rawToChar(x[s:(end - 1L)])
  }, character(1))
  data_off <- data_off + hdr  # TOC offsets are relative to the header end
  # item size = gap to the next item in file order (last runs to EOF)
  ord <- order(data_off)
  size <- integer(n)
  size[ord] <- diff(c(data_off[ord], length(x)))
  list(hdr = hdr, names = names, data_off = data_off, size = size)
}

# Drop policy: validated variant E of the 2026-07-14 feasibility study.
# Patterns are PCRE, matched against item names with the "icudt78l/" prefix
# removed.
drop_patterns <- c(
  "^zone/", "^curr/", "^lang/", "^unit/", "^region/",  # locale vocabulary
  "^translit/", "^rbnf/",                              # utrans, spellout
  "^confusables\\.cfu$", "\\.spp$", "^uts46\\.nrm$",   # uspoof, stringprep
  "^nfkc_[^/]*\\.nrm$",                                # casefold NFKC only
  "^zoneinfo64\\.res$", "^windowsZones\\.res$",        # tz database
  "^timezoneTypes\\.res$", "^metaZones\\.res$",
  "^root\\.res$",                                      # main-tree locales:
  # 2-3 lowercase letters + optional subtags. The lookahead protects
  # res_index.res, which this shape would otherwise swallow (found the hard
  # way). Machinery bundles (keyTypeData, supplementalData, pool, ...) do
  # not match the shape and are kept.
  "^(?!res_index)[a-z]{2,3}(_[A-Za-z0-9]+)*\\.res$"
)

# Items whose absence would be a silent semantic break; abort if a future
# archive renames or loses them rather than shipping without.
must_keep <- c(
  "nfkc.nrm", "unames.icu", "cnvalias.icu", "uemoji.icu", "ulayout.icu",
  "coll/ucadata.icu", "brkitr/cjdict.dict", "brkitr/thaidict.dict",
  "keyTypeData.res", "res_index.res"
)
# Sanity: the policy must actually drop these (guards against pattern rot).
must_drop <- c("nfkc_cf.nrm", "root.res", "en.res", "zoneinfo64.res",
               "curr/pool.res")

repack <- function(x, p, keep_idx, dst) {
  hdr <- p$hdr
  names <- p$names[keep_idx]           # original TOC order is name-sorted;
  off <- p$data_off[keep_idx]          # filtering preserves it (udata does a
  size <- p$size[keep_idx]             # bsearch over strcmp order)
  n <- length(names)

  toc_bytes <- 4L + 8L * n
  name_raw <- lapply(names, function(s) c(charToRaw(s), as.raw(0L)))
  name_len <- vapply(name_raw, length, integer(1))
  name_off_new <- toc_bytes + c(0L, cumsum(name_len))[seq_len(n)]

  data_start <- toc_bytes + sum(name_len)
  data_start <- bitwAnd(data_start + 15L, bitwNot(15L))

  data_off_new <- integer(n)
  pad <- integer(n)
  pos <- data_start
  for (i in seq_len(n)) {
    pad[i] <- bitwAnd(-pos, 15L)
    data_off_new[i] <- pos + pad[i]
    pos <- data_off_new[i] + size[i]
  }

  con <- file(dst, "wb")
  on.exit(close(con))
  writeBin(x[1:hdr], con)
  writeBin(c(n, as.vector(rbind(name_off_new, data_off_new))), con,
           size = 4L, endian = "little")
  for (nr in name_raw) writeBin(nr, con)
  writeBin(raw(data_start - toc_bytes - sum(name_len)), con)
  for (i in seq_len(n)) {
    if (pad[i] > 0L) writeBin(rep(as.raw(0xaa), pad[i]), con)
    writeBin(x[(off[i] + 1L):(off[i] + size[i])], con)
  }
  invisible(NULL)
}

main <- function() {
  src <- find_input()
  message("reading ", src)
  x <- readBin(src, raw(), file.size(src))
  p <- parse_dat(x)
  message(length(p$names), " items, ", round(length(x) / 1e6, 1), " MB")
  if (length(p$names) < 3500L) {
    stop("only ", length(p$names), " items: this looks like an already ",
         "trimmed archive; the input must be the COMPLETE icudt")
  }

  prefix <- sub("/.*$", "", p$names[[1L]])
  short <- sub("^[^/]+/", "", p$names)
  hit <- lapply(drop_patterns, function(pt) grepl(pt, short, perl = TRUE))
  for (i in seq_along(drop_patterns)) {
    if (!any(hit[[i]])) {
      stop("drop pattern matched nothing (stale on this ICU?): ",
           drop_patterns[[i]])
    }
  }
  drop <- Reduce(`|`, hit)

  missing_keep <- setdiff(must_keep, short[!drop])
  if (length(missing_keep)) {
    stop("critical items not in keep set (renamed in this ICU?): ",
         paste(missing_keep, collapse = ", "))
  }
  wrongly_kept <- setdiff(must_drop, short[drop])
  if (length(wrongly_kept)) {
    stop("expected these to be dropped, but they were kept: ",
         paste(wrongly_kept, collapse = ", "))
  }

  cat(sprintf("keeping %d items (%.2f MB), dropping %d (%.2f MB)\n",
              sum(!drop), sum(p$size[!drop]) / 1e6,
              sum(drop), sum(p$size[drop]) / 1e6))
  grp <- ifelse(grepl("/", short), sub("/.*$", "/", short), short)
  tab <- sort(tapply(p$size[drop], grp[drop], sum), decreasing = TRUE)
  cat("dropped by category (MB):\n")
  print(round(tab[tab > 5e4] / 1e6, 2))

  dir.create(out_dir, recursive = TRUE, showWarnings = FALSE)
  tmp_dat <- file.path(out_dir, dat_name)
  repack(x, p, which(!drop), tmp_dat)

  md5 <- unname(tools::md5sum(tmp_dat))
  writeLines(paste0(md5, "  ", dat_name),
             file.path(out_dir, paste0(dat_name, ".md5sum")))

  xz_path <- paste0(tmp_dat, ".xz")
  fin <- file(tmp_dat, "rb")
  fout <- xzfile(xz_path, "wb", compression = -9L)  # -9 == `xz -9e`
  repeat {
    chunk <- readBin(fin, raw(), 1048576L)
    if (!length(chunk)) break
    writeBin(chunk, fout)
  }
  close(fin); close(fout)

  cat(sprintf("wrote %s (%.2f MB), md5 %s\n", tmp_dat,
              file.size(tmp_dat) / 1e6, md5))
  cat(sprintf("wrote %s (%.2f MB)\n", xz_path, file.size(xz_path) / 1e6))
  unlink(tmp_dat)  # only the .xz and .md5sum are committed/shipped
  cat("next: make install, then the base/ALTREP suites + ICU canaries\n")
}

main()
