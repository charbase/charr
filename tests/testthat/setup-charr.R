# The CHARR_ALTREP environment variable routes the whole suite through charr's
# ALTREP backend. Running once with it unset and once with it set to "true"
# checks both R-dispatched paths. .onLoad already reads the
# variable; this repeats it so the flag holds even if the package was
# loaded before the variable was set.
if (tolower(Sys.getenv("CHARR_ALTREP")) %in% c("1", "true", "yes")) {
  charr_altrep(TRUE)
}
