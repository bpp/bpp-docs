#!/bin/sh
# Embed the manual as a C++ raw string + stamp the fetch date.
set -eu
SRC="$1"; OUT="$2"
{
  printf 'static const char BPP_MANUAL_MD[] = R"BPPMAN('
  cat "$SRC"
  printf ')BPPMAN";\n'
  printf '#define MANUAL_FETCH_DATE "%s"\n' "$(date +%Y-%m-%d)"
} > "$OUT"
