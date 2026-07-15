with_altrep <- function(on, code) {
  old <- charr_altrep(on)
  on.exit(charr_altrep(old), add = TRUE)
  force(code)
}

with_threads <- function(n, code) {
  old <- charr_threads(n)
  on.exit(charr_threads(old), add = TRUE)
  force(code)
}

altrep_backend_calls <- function(expr) {
  before <- charr:::charr_altrep_count()
  force(expr)
  charr:::charr_altrep_count() - before
}
