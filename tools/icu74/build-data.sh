#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)

release=74_1
source_name=icu4c-${release}-src.tgz
data_name=icu4c-${release}-data.zip
base_url=https://github.com/unicode-org/icu/releases/download/release-74-1
source_sha512=32c28270aa5d94c58d2b1ef46d4ab73149b5eaa2e0621d4a4c11597b71d146812f5e66db95f044e8aaa11b94e99edd4a48ab1aa8efbe3d72a73870cd56b564c2
data_sha512=7e411e0f190428588a872a3c65477eed60063f6fef0c84d09822c3b6b7ebba3c956a484cd91da1e26f93360f4b3e08da6ba226b674f2d139c44f86fdb2ac90a3

output=${1:-$repo_root/inst/icu/icudt74l.dat}
cache_dir=${ICU_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/charr/icu74}
jobs=${JOBS:-2}
if [[ -z ${ICU_SOURCE_ARCHIVE:-} || -z ${ICU_DATA_ARCHIVE:-} ]]; then
  mkdir -p "$cache_dir"
fi

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

source_archive=${ICU_SOURCE_ARCHIVE:-$cache_dir/$source_name}
data_archive=${ICU_DATA_ARCHIVE:-$cache_dir/$data_name}
download "$base_url/$source_name" "$source_archive"
download "$base_url/$data_name" "$data_archive"
check_hash 512 "$source_sha512" "$source_archive"
check_hash 512 "$data_sha512" "$data_archive"

owned_work=0
if [[ -n ${ICU_WORK_DIR:-} ]]; then
  work_dir=$ICU_WORK_DIR
  mkdir -p "$work_dir"
else
  work_dir=$(mktemp -d "${TMPDIR:-/tmp}/charr-icu74.XXXXXX")
  owned_work=1
fi

cleanup() {
  if [[ $owned_work == 1 && ${ICU_KEEP_WORK:-0} != 1 ]]; then
    rm -rf "$work_dir"
  fi
}
trap cleanup EXIT

source_dir=$work_dir/source
build_dir=$work_dir/build
final_dir=$work_dir/final
if [[ -e $source_dir || -e $build_dir || -e $final_dir ]]; then
  printf 'work directory is not empty: %s\n' "$work_dir" >&2
  exit 1
fi
mkdir -p "$source_dir" "$build_dir" "$final_dir"

tar -xzf "$source_archive" -C "$source_dir"
unzip -qo "$data_archive" -d "$source_dir/icu/source"

# The source tarball includes a prebuilt archive. Remove it so the result is
# rebuilt from the raw source-data release instead of copied from a binary.
rm -f "$source_dir/icu/source/data/in/icudt74l.dat"

make_cmd=make
if command -v gmake >/dev/null 2>&1; then
  make_cmd=gmake
fi

(
  cd "$build_dir"
  configure=(
    "$source_dir/icu/source/runConfigureICU" Linux
    --disable-shared
    --enable-static
    --disable-tests
    --disable-samples
    --disable-extras
    --disable-icuio
    --with-data-packaging=archive
  )
  "${configure[@]}"
  "$make_cmd" -j"$jobs"
)

intermediate=$build_dir/data/out/icudt74l.dat
final=$final_dir/icudt74l.dat
test -f "$intermediate"

cp "$intermediate" "$final"
expected_file=$script_dir/expected-full-sha256.txt

expected_sha256=$(awk '{print $1}' "$expected_file")
check_hash 256 "$expected_sha256" "$final"

mkdir -p "$(dirname -- "$output")"
cp "$final" "$output"

printf 'wrote %s\n' "$output"
printf 'sha256 %s\n' "$expected_sha256"
wc -c "$output"

if [[ ${ICU_KEEP_WORK:-0} == 1 ]]; then
  printf 'kept work directory %s\n' "$work_dir"
fi
