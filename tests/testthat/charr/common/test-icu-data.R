# charr-owned file: tools/import-upstream.R must not rename here.
#
# ICU data canaries. Each block pins behavior that depends on a retained data
# category. The imported stringr suite alone stayed green with CJK
# segmentation broken, so these exist to make silent data degradation loud.
# They run under both optimized backends, which use the same charr ICU. The
# stringi route may use a different ICU and is not a valid oracle for this
# file.
#
# Two tiers, because configure may link the system ICU4C instead of a pinned
# bundle:
#   - unconditional: stable across every ICU charr accepts (>= 61); these
#     must hold on any build, bundled or system.
#   - `if (bundled)`: pinned to the bundled reference exactly (dictionary
#     segmentations, CLDR orders that drift, warning inventory). Re-derive
#     changed literals from a full-data build of the new reference version;
#     never infer trimming correctness from base/ALTREP agreement. Plain if(),
#     not skip(), keeps SKIP 0 everywhere.

bundled <- charr:::charr_icu_bundled()

test_that("the active ICU build reports one coherent runtime", {
  info <- charr:::charr_icu_info()

  expect_named(
    info,
    c(
      "mode",
      "headers_version",
      "runtime_version",
      "data_version",
      "unicode_version",
      "u_charset_is_utf8",
      "no_conversion",
      "no_normalization",
      "no_break_iteration",
      "no_collation",
      "no_regular_expressions"
    )
  )
  expect_identical(info$mode, if (bundled) "bundle" else "system")
  expect_identical(info$headers_version, info$runtime_version)
  expect_identical(
    strsplit(info$runtime_version, ".", fixed = TRUE)[[1]][1:2],
    strsplit(info$data_version, ".", fixed = TRUE)[[1]][1:2]
  )
  expect_false(any(unlist(info[7:11], use.names = FALSE)))
})

if (!identical(selected_test_backend, "stringi")) {

test_that("collation tailorings serve arbitrary locales (coll/)", {
  expect_identical(
    str_sort(c("Tofu", "Töne", "Tale"), locale = "de@collation=phonebook"),
    c("Tale", "Töne", "Tofu")
  )
  expect_identical(
    str_sort(c("chata", "hata", "čata"), locale = "cs"),
    c("čata", "hata", "chata")  # Czech digraph: ch sorts after h
  )
  expect_identical(str_sort(c("a10", "a2", "a1"), numeric = TRUE),
                   c("a1", "a2", "a10"))
  # strength-1 search collation ignores case and accents
  expect_true(str_detect("Hänsel", coll("hansel", strength = 1, locale = "de")))
  # canonical equivalence: precomposed vs combining acute
  expect_true(str_equal("é", "é"))
  if (bundled) {
    expect_identical(
      str_sort(c("中", "文", "拼"), locale = "zh@collation=pinyin"),
      c("拼", "文", "中")
    )
  }
})

test_that("word segmentation uses the break dictionaries (brkitr/, nfkc.nrm)", {
  expect_identical(str_count("One. Two! Three? Four.", boundary("sentence")), 4L)
  if (bundled) {
    # Thai: thaidict.dict
    expect_identical(str_count("สวัสดีครับ",
                               boundary("word")), 2L)
    # CJK: cjdict.dict + nfkc.nrm (the break engine NFKC-normalizes before
    # lookup; without nfkc.nrm this silently degrades to no split)
    expect_identical(
      str_split("日本語の文章です", boundary("word"))[[1]],
      c("日本語", "の", "文章", "です")
    )
  }
})

test_that("regex (?w) routes \\b through the word BreakIterator", {
  if (bundled) {
    expect_identical(str_replace_all("don't stop 3.14 now", regex("(?w)\\b"), "|"),
                     "|don't| |stop| |3.14| |now|")
    expect_identical(
      str_replace_all("สวัสดีครับ",
                      regex("(?w)\\b"), "|"),
      "|สวัสดี|ครับ|"
    )
    expect_identical(
      str_replace_all("日本語の文章です",
                      regex("(?w)\\b"), "|"),
      "|日本語|の|文章|です|"
    )
  } else {
    # weaker invariant off the pinned version: (?w) must still segment
    expect_true(str_detect("ab cd", regex("(?w)\\b")))
  }
})

test_that("regex satellite data loads (unames, uemoji, ulayout)", {
  expect_true(str_detect("café", regex("\\N{LATIN SMALL LETTER E WITH ACUTE}")))
  expect_true(str_detect("\U0001f642", regex("\\p{Emoji}")))
  # \p{Sc} is compiled-in UCD property data, NOT the dropped curr/ vocabulary
  expect_identical(
    str_extract_all("$1 = €0.92 ≈ ¥157, ₹83, ₿0.000015",
                    regex("\\p{Sc}"))[[1]],
    c("$", "€", "¥", "₹", "₿")
  )
  if (bundled) {
    expect_identical(str_count("कि", regex("\\p{InSC=Vowel_Dependent}")), 1L)
  }
})

test_that("charset converter tables load (*.cnv, cnvalias.icu)", {
  expect_identical(str_conv(rawToChar(as.raw(0xb1)), "ISO-8859-2"), "ą")
  expect_identical(str_conv(rawToChar(as.raw(c(0x93, 0xfa))), "Shift_JIS"), "日")
  expect_identical(str_conv(rawToChar(as.raw(c(0xc8, 0x85, 0x93, 0x93, 0x96))), "IBM-37"),
                   "Hello")
})

test_that("locale-sensitive case mapping (compiled-in) and its warning parity", {
  expect_identical(str_to_upper("i", locale = "tr"), "İ")
  expect_identical(str_to_lower("ΟΣ"), "ος")  # final sigma
  expect_identical(suppressWarnings(str_to_title("ijsland", locale = "nl")),
                   "IJsland")
  if (bundled) {
    # brkitr has no nl bundle even in the full 74.1 archive: stringi surfaces
    # ICU's root-fallback warning, and backend-altrep must reproduce it. A
    # future CLDR could add the bundle, so this is pinned-version-only.
    expect_warning(str_to_title("ijsland", locale = "nl"), "resource bundle")
  }
})

test_that("wrapping composes width and line breaking", {
  lorem <- str_dup("Lorem ipsum dolor sit amet, consectetur adipiscing elit. ", 4)
  if (bundled) {
    expect_identical(
      str_wrap(lorem, 25),
      paste0("Lorem ipsum dolor\nsit amet, consectetur\nadipiscing elit. Lorem\n",
             "ipsum dolor sit amet,\nconsectetur adipiscing\nelit. Lorem ipsum dolor\n",
             "sit amet, consectetur\nadipiscing elit. Lorem\nipsum dolor sit amet,\n",
             "consectetur adipiscing\nelit.")
    )
  } else {
    wrapped <- str_wrap(lorem, 25)
    expect_true(all(nchar(strsplit(wrapped, "\n")[[1]]) <= 25))
  }
})

}
