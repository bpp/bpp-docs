# bpp-docs — BPP manual reference & search CLI.
#
# Build embeds the LATEST manual (fetched from the bpp/bpp-manual repo) so the
# binary is self-contained and offline; falls back to the vendored copy when
# there is no network. `bpp-docs --update` refreshes a local cache at run time.
CXX      ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
MANUAL_URL := https://raw.githubusercontent.com/bpp/bpp-manual/main/bpp-4-manual.md

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

BIN := bpp-docs

$(BIN): src/bpp-docs.cpp build/manual_embed.h
	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) $(CURL_CFLAGS) -Ibuild -o $@ src/bpp-docs.cpp $(CURL_LIBS) $(EXTRA_LDFLAGS)

# Fetch the latest manual (fall back to the vendored copy offline), then embed.
build/manual_embed.h: vendor/bpp-4-manual.md scripts/embed.sh
	@mkdir -p build
	@echo "bpp-docs: fetching latest manual…"
	@curl -fsSL "$(MANUAL_URL)" -o build/bpp-4-manual.md \
	  || { echo "bpp-docs: offline — embedding vendored manual"; cp vendor/bpp-4-manual.md build/bpp-4-manual.md; }
	@sh scripts/embed.sh build/bpp-4-manual.md build/manual_embed.h

# Refresh the vendored fallback from the live manual (maintainers).
.PHONY: vendor-refresh clean
vendor-refresh:
	curl -fsSL "$(MANUAL_URL)" -o vendor/bpp-4-manual.md

clean:
	rm -rf build $(BIN)
