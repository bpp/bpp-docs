// bpp-docs — a terminal reference and search tool for the BPP manual.
//
// Standalone utility (like bpp-seqs/bpp-tree/bpp-lint): quick, authoritative,
// offline answers about BPP control-file variables and concepts, pulled
// verbatim from the real manual. Also feeds bpp-agent via --json.
//
//   bpp-docs <keyword>        reference for a control variable (phase, thetaprior…)
//   bpp-docs -l | --list      list every control variable + one-line summary
//   bpp-docs -s | --search Q  ranked full-text search of the manual
//   bpp-docs --syntax <kw>    just the syntax line + default (scriptable)
//   bpp-docs --json …         machine-readable output of any of the above
//   bpp-docs --update         fetch the latest manual (built-in libcurl)
//   bpp-docs --version        tool + embedded/cached manual versions
//
// The manual is embedded at build time (self-contained, offline). --update
// refreshes a local cache (~/.cache/bpp-docs/bpp-4-manual.md) that later runs
// prefer. No network needed except for --update.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <curl/curl.h>
#include "manual_embed.h"   // BPP_MANUAL_MD, MANUAL_FETCH_DATE

#define BPP_DOCS_VERSION "0.1.0"
#define MANUAL_URL "https://raw.githubusercontent.com/bpp/bpp-manual/main/bpp-4-manual.md"

// ─── manual sourcing: updated cache first, else the embedded copy ────────────
static std::string cache_path()
{
    const char *home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.cache/bpp-docs/bpp-4-manual.md";
}
static bool read_file(const std::string &p, std::string &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
static std::string load_manual(std::string &source, std::string &date)
{
    std::string cached;
    if (read_file(cache_path(), cached) && cached.size() > 1000) {
        source = "updated cache";
        struct stat st{}; if (stat(cache_path().c_str(), &st) == 0) {
            char buf[32]; strftime(buf, sizeof buf, "%Y-%m-%d", localtime(&st.st_mtime)); date = buf;
        }
        return cached;
    }
    source = "embedded"; date = MANUAL_FETCH_DATE;
    return std::string(BPP_MANUAL_MD);
}

// ─── section model ───────────────────────────────────────────────────────────
struct Section {
    int level = 0;
    std::string title;      // heading text without the #'s
    std::string keyword;    // control-variable name, if the title is "N keyword"
    std::string body;       // text until the next heading
};

static std::string lower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// a control-variable heading is "N keyword" (e.g. "15 phase", "10 species&tree")
static std::string keyword_of(const std::string &title)
{
    std::istringstream is(title);
    std::string first, second;
    is >> first;
    bool num = !first.empty() && std::all_of(first.begin(), first.end(), [](char c){ return isdigit((unsigned char)c); });
    if (num && (is >> second)) return second;
    return "";
}

static std::vector<Section> parse(const std::string &md)
{
    std::vector<Section> secs;
    std::istringstream is(md);
    std::string line;
    Section cur; bool have = false;
    auto flush = [&]() { if (have) { cur.body = trim(cur.body); secs.push_back(cur); } };
    while (std::getline(is, line)) {
        int level = 0;
        while (level < (int)line.size() && line[level] == '#') level++;
        if (level > 0 && level < (int)line.size() && line[level] == ' ') {
            flush();
            cur = Section{};
            cur.level = level;
            cur.title = trim(line.substr(level + 1));
            cur.keyword = keyword_of(cur.title);
            cur.body.clear();
            have = true;
        } else if (have) {
            cur.body += line; cur.body += "\n";
        }
    }
    flush();
    return secs;
}

// ─── light markdown → terminal rendering ─────────────────────────────────────
static bool g_tty = false;
static std::string render(std::string s)
{
    // drop code fences, keep contents; bold **LABELS**; strip inline backticks
    std::string out; bool bold = false;
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 3, "```") == 0) { size_t e = s.find('\n', i); i = (e == std::string::npos) ? s.size() : e + 1; continue; }
        if (s.compare(i, 2, "**") == 0) { if (g_tty) out += bold ? "\033[0m" : "\033[1m"; bold = !bold; i += 2; continue; }
        if (s[i] == '`') { i++; continue; }
        out += s[i++];
    }
    if (bold && g_tty) out += "\033[0m";
    return out;
}

// ─── control-variable field extraction (**DEFAULT** etc.) ────────────────────
static std::string field(const std::string &body, const std::string &label)
{
    std::string key = "**" + label + "**";
    size_t p = body.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    size_t e = body.find("**", p);                 // until the next **LABEL**
    return trim(body.substr(p, e == std::string::npos ? std::string::npos : e - p));
}
static std::string syntax_of(const std::string &body)   // first fenced code block
{
    size_t a = body.find("```");
    if (a == std::string::npos) return "";
    a = body.find('\n', a); if (a == std::string::npos) return "";
    size_t b = body.find("```", a);
    return trim(body.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1));
}

// ─── JSON escaping ───────────────────────────────────────────────────────────
static std::string jesc(const std::string &s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\t': o += "\\t"; break;
            case '\r': break;
            default: o += c;
        }
    }
    return o;
}

// ─── commands ────────────────────────────────────────────────────────────────
static const Section *find_keyword(const std::vector<Section> &secs, const std::string &kw)
{
    std::string k = lower(kw);
    for (const auto &s : secs) if (lower(s.keyword) == k) return &s;   // exact
    for (const auto &s : secs) if (!s.keyword.empty() && lower(s.keyword).find(k) != std::string::npos) return &s;  // prefix/substr
    return nullptr;
}

static int cmd_keyword(const std::vector<Section> &secs, const std::string &kw, bool json)
{
    const Section *s = find_keyword(secs, kw);
    if (!s) {
        if (json) printf("{\"keyword\":\"%s\",\"found\":false}\n", jesc(kw).c_str());
        else fprintf(stderr, "bpp-docs: no control variable '%s' in the manual (try: bpp-docs -l)\n", kw.c_str());
        return 1;
    }
    if (json) {
        printf("{\"keyword\":\"%s\",\"found\":true,\"heading\":\"%s\","
               "\"syntax\":\"%s\",\"description\":\"%s\",\"values\":\"%s\","
               "\"default\":\"%s\",\"dependencies\":\"%s\",\"comments\":\"%s\"}\n",
               jesc(s->keyword).c_str(), jesc(s->title).c_str(),
               jesc(syntax_of(s->body)).c_str(), jesc(field(s->body, "DESCRIPTION")).c_str(),
               jesc(field(s->body, "VALUES")).c_str(), jesc(field(s->body, "DEFAULT")).c_str(),
               jesc(field(s->body, "DEPENDENCIES")).c_str(), jesc(field(s->body, "COMMENTS")).c_str());
    } else {
        if (g_tty) printf("\033[1;36m%s\033[0m\n", s->title.c_str());
        else printf("%s\n", s->title.c_str());
        printf("%s\n", render(s->body).c_str());
    }
    return 0;
}

static int cmd_list(const std::vector<Section> &secs, bool json)
{
    if (json) printf("[");
    bool first = true;
    for (const auto &s : secs) {
        if (s.keyword.empty()) continue;
        std::string desc = field(s.body, "DESCRIPTION");
        if (desc.empty()) desc = trim(s.body).substr(0, 70);
        std::string one = desc.substr(0, desc.find('\n'));
        if (json) { printf("%s{\"keyword\":\"%s\",\"summary\":\"%s\"}", first ? "" : ",",
                           jesc(s.keyword).c_str(), jesc(one).c_str()); first = false; }
        else printf("  %-22s %s\n", s.keyword.c_str(), render(one).c_str());
    }
    if (json) printf("]\n");
    return 0;
}

static int cmd_search(const std::vector<Section> &secs, const std::string &query, bool json)
{
    std::vector<std::string> terms;
    { std::istringstream is(lower(query)); std::string t; while (is >> t) terms.push_back(t); }
    std::vector<std::pair<int, const Section *>> scored;
    for (const auto &s : secs) {
        std::string ht = lower(s.title), bt = lower(s.body);
        int score = 0;
        for (const auto &t : terms) {
            if (ht.find(t) != std::string::npos) score += 6;
            size_t pos = 0; while ((pos = bt.find(t, pos)) != std::string::npos) { score += 1; pos += t.size(); }
        }
        if (score > 0) scored.push_back({score, &s});
    }
    std::sort(scored.begin(), scored.end(), [](auto &a, auto &b){ return a.first > b.first; });
    if (scored.size() > 6) scored.resize(6);
    if (json) {
        printf("[");
        for (size_t i = 0; i < scored.size(); ++i) {
            const Section *s = scored[i].second;
            std::string snip = trim(s->body).substr(0, 240);
            printf("%s{\"heading\":\"%s\",\"score\":%d,\"snippet\":\"%s\"}",
                   i ? "," : "", jesc(s->title).c_str(), scored[i].first, jesc(snip).c_str());
        }
        printf("]\n");
    } else {
        if (scored.empty()) { fprintf(stderr, "bpp-docs: no manual sections match '%s'\n", query.c_str()); return 1; }
        for (auto &pr : scored) {
            const Section *s = pr.second;
            if (g_tty) printf("\033[1;36m%s\033[0m\n", s->title.c_str());
            else printf("== %s ==\n", s->title.c_str());
            std::string snip = trim(s->body).substr(0, 300);
            printf("%s…\n\n", render(snip).c_str());
        }
    }
    return 0;
}

static int cmd_syntax(const std::vector<Section> &secs, const std::string &kw)
{
    const Section *s = find_keyword(secs, kw);
    if (!s) { fprintf(stderr, "bpp-docs: no control variable '%s'\n", kw.c_str()); return 1; }
    std::string syn = syntax_of(s->body), def = field(s->body, "DEFAULT");
    printf("%s", syn.empty() ? s->keyword.c_str() : syn.c_str());
    if (!def.empty()) printf("    (default: %s)", render(def).c_str());
    printf("\n");
    return 0;
}

// ─── --update via libcurl ────────────────────────────────────────────────────
static size_t write_cb(char *ptr, size_t sz, size_t n, void *ud)
{ ((std::string *)ud)->append(ptr, sz * n); return sz * n; }

static int cmd_update()
{
    std::string body, err(CURL_ERROR_SIZE, 0);
    CURL *c = curl_easy_init();
    if (!c) { fprintf(stderr, "bpp-docs: curl init failed\n"); return 1; }
    curl_easy_setopt(c, CURLOPT_URL, MANUAL_URL);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "bpp-docs/" BPP_DOCS_VERSION);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, &err[0]);
    CURLcode rc = curl_easy_perform(c);
    long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || http != 200 || body.size() < 1000) {
        fprintf(stderr, "bpp-docs: update failed (%s, HTTP %ld)\n",
                rc == CURLE_OK ? "bad response" : curl_easy_strerror(rc), http);
        return 1;
    }
    std::string prev; read_file(cache_path(), prev);
    std::string dir = cache_path().substr(0, cache_path().find_last_of('/'));
    std::string mk = "mkdir -p '" + dir + "'"; if (system(mk.c_str()) != 0) {}
    std::ofstream out(cache_path(), std::ios::binary);
    if (!out) { fprintf(stderr, "bpp-docs: cannot write %s\n", cache_path().c_str()); return 1; }
    out << body;
    printf("bpp-docs: manual updated (%zu bytes) -> %s\n%s", body.size(), cache_path().c_str(),
           prev == body ? "  (no change since last update)\n" : "");
    return 0;
}

static int usage()
{
    printf("bpp-docs %s — BPP manual reference & search\n\n"
           "  bpp-docs <keyword>        reference for a control variable\n"
           "  bpp-docs -l, --list       list all control variables\n"
           "  bpp-docs -s, --search Q   search the manual\n"
           "  bpp-docs --syntax <kw>    syntax line + default only\n"
           "  bpp-docs --json …         machine-readable output\n"
           "  bpp-docs --update         fetch the latest manual\n"
           "  bpp-docs --version        versions\n", BPP_DOCS_VERSION);
    return 0;
}

int main(int argc, char **argv)
{
    g_tty = isatty(fileno(stdout));
    std::vector<std::string> args(argv + 1, argv + argc);
    bool json = false; std::string mode, arg;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];
        if (a == "--json") json = true;
        else if (a == "-l" || a == "--list") mode = "list";
        else if (a == "-s" || a == "--search") { mode = "search"; if (i + 1 < args.size()) arg = args[++i]; }
        else if (a == "--syntax") { mode = "syntax"; if (i + 1 < args.size()) arg = args[++i]; }
        else if (a == "--update") mode = "update";
        else if (a == "--version") mode = "version";
        else if (a == "-h" || a == "--help") return usage();
        else if (a[0] != '-') { if (mode.empty()) mode = "keyword"; if (arg.empty()) arg = a; }
    }
    if (mode.empty()) return usage();
    if (mode == "update") return cmd_update();

    std::string source, date;
    std::string md = load_manual(source, date);
    if (mode == "version") {
        printf("bpp-docs %s\nmanual: %s (%s)\n", BPP_DOCS_VERSION, date.c_str(), source.c_str());
        std::string cached; if (read_file(cache_path(), cached)) printf("cache:  %s\n", cache_path().c_str());
        return 0;
    }
    std::vector<Section> secs = parse(md);
    if (mode == "list")    return cmd_list(secs, json);
    if (mode == "search")  return cmd_search(secs, arg, json);
    if (mode == "syntax")  return cmd_syntax(secs, arg);
    if (mode == "keyword") return cmd_keyword(secs, arg, json);
    return usage();
}
