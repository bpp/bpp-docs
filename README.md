# bpp-docs

A fast, offline command-line reference and search tool for the
[BPP](https://github.com/bpp/bpp) manual. Get the exact syntax, default, and
description of any control-file variable — or search the whole manual — without
leaving your terminal or scrolling a 6,000-line document.

The manual is **embedded in the binary** (self-contained, works offline);
`bpp-docs --update` refreshes it from the live [bpp-manual](https://github.com/bpp/bpp-manual)
when you have a connection.

## Usage

```console
$ bpp-docs thetaprior          # full reference for a control variable
$ bpp-docs --syntax phase      # just the syntax line + default
phase = b*    (default: 0)
$ bpp-docs --list              # every control variable, one line each
$ bpp-docs --search "migration introgression"   # ranked full-text search
$ bpp-docs --update            # fetch the latest manual
$ bpp-docs --json thetaprior   # machine-readable (for scripts / editors / bpp-agent)
```

Every answer is lifted verbatim from the manual — nothing is paraphrased or
invented. If a query matches nothing, `bpp-docs` says so rather than guessing.

## Install

Homebrew (macOS / Linux):

```console
brew install bpp/tap/bpp-docs
```

From source (needs a C++17 compiler and libcurl):

```console
make            # fetches the latest manual, embeds it, links libcurl
./bpp-docs --version
```

`make` embeds the newest manual at build time; if there's no network it falls
back to the vendored copy in `vendor/`, so the build never breaks.

## How the manual is sourced

1. `bpp-docs` prefers an updated cache at `~/.cache/bpp-docs/bpp-4-manual.md`
   (written by `--update`).
2. Otherwise it uses the copy embedded at build time.

So it always works offline, is current as of your last build, and is
refreshable on demand without recompiling. `bpp-docs --version` shows which
manual it's using and its date.

## License

AGPL-3.0-or-later. The BPP manual is authored by the BPP project
([bpp/bpp-manual](https://github.com/bpp/bpp-manual)).
