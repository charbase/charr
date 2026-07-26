#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)

release=78.3
source_name=icu4c-${release}-sources.tgz
base_url=https://github.com/unicode-org/icu/releases/download/release-${release}
source_sha512=04a49455e1489030c520a4bfd2664fa2171e7938d08f2acdbbcb1fda976639fd8b1f0704f2eec89ba59a7b6d118ceaab6ec5a096e40d9085a0895d91ce225245

output=${1:-$repo_root/local/icu78/icudt78l.dat}
cache_root=${ICU_CACHE_DIR:-${XDG_CACHE_HOME:-${TMPDIR:-/tmp}}/charr/icu78}

hash_file() {
  local bits=$1
  local file=$2
  if command -v "sha${bits}sum" >/dev/null 2>&1; then
    "sha${bits}sum" "$file" | awk '{print $1}'
  else
    shasum -a "$bits" "$file" | awk '{print $1}'
  fi
}

check_hash() {
  local bits=$1
  local expected=$2
  local file=$3
  local actual
  actual=$(hash_file "$bits" "$file")
  if [[ $actual != "$expected" ]]; then
    printf 'checksum mismatch for %s\nexpected: %s\nactual:   %s\n' \
      "$file" "$expected" "$actual" >&2
    exit 1
  fi
}

download() {
  local url=$1
  local dest=$2
  if [[ -f $dest ]]; then
    return
  fi
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$dest.part" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$dest.part" "$url"
  else
    printf 'need curl or wget to download %s\n' "$url" >&2
    exit 1
  fi
  mv "$dest.part" "$dest"
}

if [[ -n ${ICU_SOURCE_ARCHIVE:-} ]]; then
  source_archive=$ICU_SOURCE_ARCHIVE
else
  mkdir -p "$cache_root"
  source_archive=$cache_root/$source_name
  download "$base_url/$source_name" "$source_archive"
fi
check_hash 512 "$source_sha512" "$source_archive"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/charr-icu78-data.XXXXXX")
cleanup() {
  rm -rf "$work_dir"
}
trap cleanup EXIT

tar -xzf "$source_archive" -C "$work_dir" \
  icu/source/data/in/icudt78l.dat
full_data=$work_dir/icu/source/data/in/icudt78l.dat

expected_sha256=$(awk '{print $1}' "$script_dir/expected-full-sha256.txt")
check_hash 256 "$expected_sha256" "$full_data"

mkdir -p "$(dirname -- "$output")"
cp "$full_data" "$output"

printf 'wrote %s\n' "$output"
printf 'sha256 %s\n' "$expected_sha256"
wc -c "$output"
