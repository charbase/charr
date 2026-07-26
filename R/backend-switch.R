charr_icu_ok <- function() {
  .Call(C_charr_icu_ok)
}

charr_icu_bundled <- function() {
  .Call(C_charr_icu_bundled)
}

charr_icu_info <- function() {
  .Call(C_charr_icu_info)
}

.onLoad <- function(libname, pkgname) {
  if (!isTRUE(.Call(C_charr_abi_ok))) {
    stop(
      "charr was compiled against a different charport ABI; ",
      "reinstall charport, then reinstall charr",
      call. = FALSE
    )
  }

  if (charr_icu_bundled()) {
    dat <- system.file("icu", "icudt78l.dat", package = pkgname)
    if (!nzchar(dat) || !isTRUE(.Call(C_charr_icu_init, dat))) {
      stop(
        "charr's bundled ICU data could not be initialized; reinstall charr",
        call. = FALSE
      )
    }
  } else if (!isTRUE(.Call(C_charr_icu_init, NULL))) {
    stop(
      "charr could not initialize the system ICU library; reinstall charr",
      call. = FALSE
    )
  }

  .charr_initialize_base_native_aliases()

  invisible()
}
