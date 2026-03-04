#include <curl/curl.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

// --- HTML-to-text renderer ---

static std::string str_tolower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

static std::string decode_entities(const std::string& s) {
    static const std::unordered_map<std::string, std::string> entities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", " "},
        {"&ndash;", "-"}, {"&mdash;", "--"}, {"&copy;", "(c)"},
        {"&reg;", "(R)"}, {"&trade;", "(TM)"},
    };

    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            // Try numeric character references: &#123; or &#x1F;
            if (i + 2 < s.size() && s[i + 1] == '#') {
                size_t semi = s.find(';', i + 2);
                if (semi != std::string::npos && semi - i <= 10) {
                    std::string ref = s.substr(i + 2, semi - i - 2);
                    int codepoint = 0;
                    try {
                        if (!ref.empty() && (ref[0] == 'x' || ref[0] == 'X'))
                            codepoint = std::stoi(ref.substr(1), nullptr, 16);
                        else
                            codepoint = std::stoi(ref);
                    } catch (...) { codepoint = 0; }
                    if (codepoint > 0 && codepoint < 128) {
                        out += static_cast<char>(codepoint);
                        i = semi + 1;
                        continue;
                    }
                }
            }
            // Try named entities
            bool matched = false;
            for (auto& [ent, rep] : entities) {
                if (s.compare(i, ent.size(), ent) == 0) {
                    out += rep;
                    i += ent.size();
                    matched = true;
                    break;
                }
            }
            if (!matched) out += s[i++];
        } else {
            out += s[i++];
        }
    }
    return out;
}

// Extract an attribute value from inside a tag string, e.g. get "href" from <a href="...">
static std::string get_attr(const std::string& tag, const std::string& attr) {
    std::string lower_tag = str_tolower(tag);
    std::string search = str_tolower(attr) + "=";
    size_t pos = lower_tag.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    if (pos >= tag.size()) return "";
    char quote = tag[pos];
    if (quote == '"' || quote == '\'') {
        size_t end = tag.find(quote, pos + 1);
        if (end == std::string::npos) return "";
        return tag.substr(pos + 1, end - pos - 1);
    }
    // Unquoted value: run until whitespace or >
    size_t end = tag.find_first_of(" \t\n>", pos);
    if (end == std::string::npos) end = tag.size();
    return tag.substr(pos, end - pos);
}

static std::string collapse_whitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!in_space) { out += ' '; in_space = true; }
        } else {
            out += c;
            in_space = false;
        }
    }
    return out;
}

std::string html_to_text(const std::string& html) {
    std::string out;
    out.reserve(html.size());

    bool in_skip = false;       // inside <script> or <style>
    std::string skip_tag;       // which tag we're skipping
    int list_depth = 0;

    size_t i = 0;
    while (i < html.size()) {
        // --- HTML comment ---
        if (html.compare(i, 4, "<!--") == 0) {
            size_t end = html.find("-->", i + 4);
            i = (end == std::string::npos) ? html.size() : end + 3;
            continue;
        }

        // --- Tag ---
        if (html[i] == '<') {
            size_t close = html.find('>', i + 1);
            if (close == std::string::npos) { i++; continue; }

            std::string tag_raw = html.substr(i + 1, close - i - 1);
            std::string tag_lower = str_tolower(tag_raw);

            // Strip leading '/' for closing tags
            bool is_close = false;
            std::string tag_name;
            {
                size_t s = 0;
                while (s < tag_lower.size() && tag_lower[s] == '/') { is_close = true; s++; }
                size_t e = tag_lower.find_first_of(" \t\n/", s);
                tag_name = tag_lower.substr(s, (e == std::string::npos) ? std::string::npos : e - s);
            }

            // Handle skip regions (script, style)
            if (in_skip) {
                if (is_close && tag_name == skip_tag) in_skip = false;
                i = close + 1;
                continue;
            }
            if (!is_close && (tag_name == "script" || tag_name == "style")) {
                in_skip = true;
                skip_tag = tag_name;
                i = close + 1;
                continue;
            }

            // Block-level elements: emit newlines
            if (tag_name == "p" || tag_name == "div" || tag_name == "section" ||
                tag_name == "article" || tag_name == "main" || tag_name == "aside" ||
                tag_name == "header" || tag_name == "footer" || tag_name == "nav" ||
                tag_name == "blockquote" || tag_name == "table" || tag_name == "tr") {
                out += '\n';
            }

            // Headings
            if (tag_name.size() == 2 && tag_name[0] == 'h' &&
                tag_name[1] >= '1' && tag_name[1] <= '6') {
                if (is_close) {
                    out += '\n';
                } else {
                    out += "\n";
                    int level = tag_name[1] - '0';
                    for (int j = 0; j < (7 - level); j++) out += '#';
                    out += ' ';
                }
            }

            // Line breaks
            if (tag_name == "br") out += '\n';

            // Horizontal rule
            if (tag_name == "hr") out += "\n----------------------------------------\n";

            // Lists
            if (tag_name == "ul" || tag_name == "ol") {
                if (!is_close) { list_depth++; out += '\n'; }
                else { list_depth = std::max(0, list_depth - 1); out += '\n'; }
            }
            if (tag_name == "li" && !is_close) {
                out += '\n';
                for (int j = 0; j < list_depth; j++) out += "  ";
                out += "* ";
            }

            // Links: show href after link text
            if (tag_name == "a" && is_close) {
                // We can't easily get the opening tag's href here in a single pass,
                // so we handle it on the opening tag instead.
            }
            if (tag_name == "a" && !is_close) {
                std::string href = get_attr(tag_raw, "href");
                if (!href.empty() && href[0] != '#' && href.find("javascript:") != 0) {
                    // Store the href to print after the link text
                    // For simplicity, we emit it inline before the text
                    // We'll use a marker approach: emit nothing now,
                    // but we can't do deferred in a simple single-pass.
                    // Instead, just note it:
                    out += "[";
                    // After the closing </a>, we would want to add " (href)"
                    // For a simple approach, let's put the link after an opening bracket
                    // and close it at </a>. We'll handle </a> below.
                }
            }
            if (tag_name == "a" && is_close) {
                // Find if we had an opening bracket
                // Simple heuristic: check if last '[' exists without a closing ']'
                size_t last_bracket = out.rfind('[');
                if (last_bracket != std::string::npos &&
                    out.find(']', last_bracket) == std::string::npos) {
                    out += "]";
                }
            }

            // Table cells
            if (tag_name == "td" || tag_name == "th") {
                if (!is_close) out += "\t";
            }

            i = close + 1;
            continue;
        }

        // --- Text content ---
        if (!in_skip) {
            // Gather a run of text
            size_t start = i;
            while (i < html.size() && html[i] != '<') i++;
            std::string text = html.substr(start, i - start);
            text = decode_entities(text);
            // Collapse runs of whitespace (but preserve explicit newlines from tags)
            text = collapse_whitespace(text);
            out += text;
        } else {
            i++;
        }
    }

    // Clean up excessive blank lines (3+ newlines -> 2)
    std::string cleaned;
    cleaned.reserve(out.size());
    int newline_count = 0;
    for (char c : out) {
        if (c == '\n') {
            newline_count++;
            if (newline_count <= 2) cleaned += c;
        } else {
            newline_count = 0;
            cleaned += c;
        }
    }

    // Trim leading/trailing whitespace
    size_t first = cleaned.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = cleaned.find_last_not_of(" \t\n\r");
    return cleaned.substr(first, last - first + 1);
}

// --- cURL write callback ---

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* html = static_cast<std::string*>(userdata);
    html->append(ptr, size * nmemb);
    return size * nmemb;
}

// --- Main ---

int main() {
    std::string weblink;
    std::cout << "What website do you want? (Include https:// at the beginning): ";
    std::cin >> weblink;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();

    if (!curl) {
        std::cerr << "Error: Failed to initialize cURL.\n";
        curl_global_cleanup();
        return 1;
    }

    std::string html;
    curl_easy_setopt(curl, CURLOPT_URL, weblink.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TUI-Browser/0.1");

    // CA certificate bundle: check env var first, fall back to known path.
    const char* ca_env = std::getenv("CURL_CA_BUNDLE");
    if (ca_env) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_env);
    } else {
        curl_easy_setopt(curl, CURLOPT_CAINFO,
            "C:/CURL/curl-8.18.0_5-win64-mingw/curl-8.18.0_5-win64-mingw/bin/curl-ca-bundle.crt");
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        std::string text = html_to_text(html);
        std::cout << "\n" << text << "\n";
    } else {
        std::cerr << "Error: " << curl_easy_strerror(res) << "\n";
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
