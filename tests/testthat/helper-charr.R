with_altrep <- function(on, code) {
  old <- charr_altrep(on)
  on.exit(charr_altrep(old), add = TRUE)
  force(code)
}

altrep_backend_calls <- function(expr) {
  before <- charr:::charr_altrep_count()
  force(expr)
  charr:::charr_altrep_count() - before
}
