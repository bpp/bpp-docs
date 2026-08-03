# Seed copy of the Homebrew formula. The canonical copy lives in
# bpp/homebrew-tap (Formula/bpp-docs.rb); add this there once, and the release
# workflow's bump-homebrew job keeps its url + sha256 current on each tag.
class BppDocs < Formula
  desc "Command-line reference and search for the BPP manual"
  homepage "https://github.com/bpp/bpp-docs"
  url "https://github.com/bpp/bpp-docs/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "AGPL-3.0-or-later"
  head "https://github.com/bpp/bpp-docs.git", branch: "main"

  depends_on "curl"
  depends_on "pkg-config" => :build

  def install
    system "make"
    bin.install "bpp-docs"
    doc.install "README.md"
  end

  test do
    assert_match "bpp-docs", shell_output("#{bin}/bpp-docs --version")
    assert_match "phase", shell_output("#{bin}/bpp-docs --syntax phase")
  end
end
