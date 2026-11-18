#!/bin/sh
# C preprocessor wrapper for universal (multi -arch) autotools builds.
#
# clang refuses to write preprocessed output for several -arch values at
# once ("cannot use 'cpp-output' output with multiple -arch options"), which
# breaks configure's CPP checks. Preprocess for the host architecture only:
# arm64 and x86_64 are both LP64 little-endian, so the checks agree.
skip=0
for arg in "$@"; do
  shift
  if [ "$skip" = 1 ]; then
    skip=0
    continue
  fi
  if [ "$arg" = "-arch" ]; then
    skip=1
    continue
  fi
  set -- "$@" "$arg"
done
exec "${CC_FOR_CPP:-cc}" -E "$@"
