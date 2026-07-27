# charr-owned file: tools/import-upstream.R must not rename here.
#
# Custom shared-object installation hook. When src/install.libs.R exists,
# R CMD INSTALL sources it INSTEAD of the default libs installation, so this
# file must copy the shared objects itself before its real job: decompressing
# the trimmed ICU data archive (built by tools/trim-icudt.R, shipped as
# src/icu78/data/icudt78l.dat.xz) into the installed package, where .onLoad
# reads it via system.file("icu", "icudt78l.dat").

libs <- Sys.glob(paste0("*", SHLIB_EXT))
libs_dir <- file.path(R_PACKAGE_DIR, paste0("libs", R_ARCH))
if (!dir.exists(libs_dir) &&
    !dir.create(libs_dir, recursive = TRUE, showWarnings = FALSE)) {
  stop("failed to create shared-library installation directory")
}
if (!length(libs) || !all(file.copy(libs, libs_dir, overwrite = TRUE))) {
  stop("failed to install charr shared library")
}
if (file.exists("symbols.rds")) {
  if (!file.copy("symbols.rds", libs_dir, overwrite = TRUE)) {
    stop("failed to install native registration metadata")
  }
}

# configure writes src/icu_mode ("system" or "bundle"); Windows has no
# configure and always bundles. Under the system ICU there is no data
# archive to install -- the system library carries its own.
icu_mode <- if (file.exists("icu_mode")) {
  readLines("icu_mode", n = 1L, warn = FALSE)
} else {
  "bundle"
}

if (!identical(icu_mode, "system")) {
  if (.Platform$endian != "little") {
    stop("charr bundles a little-endian ICU data archive; on a big-endian ",
         "platform install against the system ICU4C instead ",
         "(certified versions: ICU4C 78.2 or 78.3)")
  }

  dat_name <- "icudt78l.dat"
  xz_path <- file.path("icu78", "data", paste0(dat_name, ".xz"))
  md5_path <- file.path("icu78", "data", paste0(dat_name, ".md5sum"))
  icu_dir <- file.path(R_PACKAGE_DIR, "icu")
  dat_path <- file.path(icu_dir, dat_name)
  md5_expected <- scan(md5_path, what = character(), n = 1L, quiet = TRUE)

  # Biarch installs run this file once per architecture; the data is
  # architecture-independent, so a verified existing copy is kept.
  if (!(file.exists(dat_path) &&
        identical(unname(tools::md5sum(dat_path)), md5_expected))) {
    if (!dir.exists(icu_dir) &&
        !dir.create(icu_dir, recursive = TRUE, showWarnings = FALSE)) {
      stop("failed to create ICU data installation directory")
    }
    fin <- xzfile(xz_path, "rb")
    fout <- file(dat_path, "wb")
    repeat {
      chunk <- readBin(fin, raw(), 1048576L)
      if (!length(chunk)) break
      writeBin(chunk, fout)
    }
    close(fin)
    close(fout)

    md5_got <- unname(tools::md5sum(dat_path))
    if (!identical(md5_got, md5_expected)) {
      unlink(dat_path)
      stop("checksum mismatch decompressing ", dat_name, " (got ", md5_got,
           ", expected ", md5_expected, "); corrupted source package?")
    }
  }
}
