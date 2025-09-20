// unsplash_wallpaper.cpp
// Requires: libcurl, SDL2, SDL2_image
// Build: g++ unsplash_wallpaper.cpp -o unsplash_wallpaper -lcurl -lSDL2 -lSDL2_image -std=c++17

#include <SDL.h>
#include <SDL_image.h>
#include <curl/curl.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

// Simple write callback for libcurl -> append bytes into vector<unsigned char>
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* buf = static_cast<std::vector<unsigned char>*>(userp);
    const unsigned char* data = static_cast<const unsigned char*>(contents);
    buf->insert(buf->end(), data, data + realsize);
    return realsize;
}

// Extract a JSON string value for a simple flat key like "raw":"https:\/\/..."
static std::string extract_json_string(const std::vector<unsigned char>& buf, const char* key) {
    std::string s(buf.begin(), buf.end());
    std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat);
    while (p != std::string::npos) {
        // find the ':' after the key
        size_t colon = s.find(':', p + pat.size());
        if (colon == std::string::npos) break;
        // skip whitespace
        size_t q = s.find_first_not_of(" \t\r\n", colon + 1);
        if (q == std::string::npos) break;
        if (s[q] == '"') {
            // quoted string value
            size_t i = q + 1;
            std::string out;
            while (i < s.size()) {
                char c = s[i++];
                if (c == '"') break;
                if (c == '\\' && i < s.size()) {
                    char e = s[i++];
                    switch (e) {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case '/': out.push_back('/'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        default: out.push_back(e); break;
                    }
                } else {
                    out.push_back(c);
                }
            }
            return out;
        } else {
            // non-quoted value (number, true, false, null) — return trimmed token
            size_t end = s.find_first_of(",}", q);
            if (end == std::string::npos) end = s.size();
            std::string tok = s.substr(q, end - q);
            // trim trailing whitespace
            size_t start_non = tok.find_first_not_of(" \t\r\n");
            size_t end_non = tok.find_last_not_of(" \t\r\n");
            if (start_non == std::string::npos) return std::string();
            return tok.substr(start_non, end_non - start_non + 1);
        }
        // try next occurrence
        p = s.find(pat, p + pat.size());
    }
    return std::string();
}

// Helper: download bytes from a URL into buffer. returns true on success.
static bool download_to_buffer(const std::string& url, std::vector<unsigned char>& out_buf, std::string* out_content_type = nullptr, long timeout_seconds = 20) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    struct curl_slist* headers = nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_buf);
    // curl_easy_setopt(curl, CURLOPT_USERAGENT, "ProjectMino/1.0 (+https://example.com)");
    // use a browser UA to avoid source.unsplash returning a landing HTML page
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // enable gzip/deflate
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    CURLcode res = curl_easy_perform(curl);

    char* eff_url_ptr = nullptr;
    char* content_type_ptr = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url_ptr);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type_ptr);
    std::string eff_url = eff_url_ptr ? std::string(eff_url_ptr) : std::string();
    std::string content_type = content_type_ptr ? std::string(content_type_ptr) : std::string();

    if (out_content_type) *out_content_type = content_type;

    // Diagnostic logging
    std::cerr << "download_to_buffer: url=" << url
              << " eff_url=" << eff_url
              << " result=" << res << " (" << curl_easy_strerror(res) << ")"
              << " content_type=" << content_type
              << " bytes=" << out_buf.size()
              << "\n";

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        return false;
    }
    return !out_buf.empty();
}

// forward-declare HTML extractor so fetchUnsplashWallpaper can call it
static std::string extract_image_url_from_html(const std::string& h);

// Fetch a wallpaper image from Unsplash (tags: wilderness,wallpaper) sized to window w x h.
SDL_Texture* fetchUnsplashWallpaper(SDL_Renderer* renderer, int w, int h) {
    if (!renderer || w <= 0 || h <= 0) return nullptr;

    // Look for API key in environment variable UNSPLASH_ACCESS_KEY
    const char* key = std::getenv("UNSPLASH_ACCESS_KEY");
    if (!key || key[0] == '\0') {
        std::cerr << "UNSPLASH_ACCESS_KEY not set in process environment\n";
        return nullptr;
    }
    // mask the key for logging
    std::string km(key);
    if (km.size() > 8) km = km.substr(0,4) + "..." + km.substr(km.size()-4);
    std::cerr << "UNSPLASH_ACCESS_KEY present (len=" << strlen(key) << ") masked=" << km << "\n";

    // Official API flow (requires UNSPLASH_ACCESS_KEY)
    std::string api_url = "https://api.unsplash.com/photos/random?query=wilderness,wallpaper&orientation=landscape&w=" 
                        + std::to_string(w) + "&h=" + std::to_string(h);
    // Prepare libcurl call
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;

    std::vector<unsigned char> json_buf;
    struct curl_slist* headers = nullptr;
    std::string auth = std::string("Authorization: Client-ID ") + key;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept-Version: v1");
    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &json_buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);

    char* content_type_ptr = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type_ptr);
    std::string content_type = content_type_ptr ? std::string(content_type_ptr) : std::string();

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || json_buf.empty()) {
        std::cerr << "Unsplash API call failed: " << curl_easy_strerror(res) << "\n";
        if (!json_buf.empty()) {
            std::ofstream dbgjson("/tmp/unsplash_api.json", std::ios::binary);
            dbgjson.write(reinterpret_cast<const char*>(json_buf.data()), json_buf.size());
        }
        return nullptr;
    }

    // crude JSON extraction (prefer raw/full/regular)
    std::string img_url = extract_json_string(json_buf, "raw");
    if (img_url.empty()) img_url = extract_json_string(json_buf, "full");
    if (img_url.empty()) img_url = extract_json_string(json_buf, "regular");
    if (img_url.empty()) {
        std::ofstream dbgjson("/tmp/unsplash_api.json", std::ios::binary);
        dbgjson.write(reinterpret_cast<const char*>(json_buf.data()), json_buf.size());
        std::cerr << "Unable to extract image URL from Unsplash API response. See /tmp/unsplash_api.json\n";
        return nullptr;
    }

    // append parameters to request jpg and desired size
    std::string sep = (img_url.find('?') == std::string::npos) ? "?" : "&";
    img_url += sep + "fm=jpg&fit=crop&w=" + std::to_string(w) + "&h=" + std::to_string(h);

    // download the image bytes
    std::vector<unsigned char> img_buf;
    std::string ct2;
    if (!download_to_buffer(img_url, img_buf, &ct2, 30)) {
        std::cerr << "Failed to download image from Unsplash URL\n";
        if (!img_buf.empty()) {
            std::ofstream dbgfail("/tmp/unsplash_image_failed.bin", std::ios::binary);
            dbgfail.write(reinterpret_cast<const char*>(img_buf.data()), img_buf.size());
        }
        return nullptr;
    }

    SDL_RWops* rw = SDL_RWFromConstMem(img_buf.data(), static_cast<int>(img_buf.size()));
    if (!rw) {
        std::cerr << "SDL_RWFromConstMem failed\n";
        return nullptr;
    }
    SDL_Texture* tex = IMG_LoadTexture_RW(renderer, rw, 1);
    if (!tex) {
        std::ofstream dbgerr("/tmp/unsplash_download_failed.bin", std::ios::binary);
        if (dbgerr) dbgerr.write(reinterpret_cast<const char*>(img_buf.data()), img_buf.size());
        std::cerr << "IMG_LoadTexture_RW failed: " << IMG_GetError() << "\n";
        return nullptr;
    }

    return tex;
}

// Render the wallpaper stretched to the full window size with a 40% black tint on top.
void renderWallpaperWithTint(SDL_Renderer* renderer, SDL_Texture* wallpaper, int windowW, int windowH) {
    if (!renderer) return;

    if (wallpaper) {
        SDL_Rect dst = {0, 0, windowW, windowH};
        SDL_RenderCopy(renderer, wallpaper, nullptr, &dst);
    } else {
        SDL_SetRenderDrawColor(renderer, 16, 16, 16, 255);
        SDL_RenderClear(renderer);
    }

    // Apply stronger black tint (~60% opacity -> alpha 153)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 153);
    SDL_Rect full = {0, 0, windowW, windowH};
    SDL_RenderFillRect(renderer, &full);
}

// helper: extract an image URL from an HTML document (meta tags, rel=image_src, <img>, unsplash URL)
static std::string extract_image_url_from_html(const std::string& h) {
    auto find_meta_content = [&](const std::string& key)->std::string {
        size_t p = h.find(key);
        if (p == std::string::npos) return std::string();
        size_t tag_start = h.rfind('<', p);
        size_t tag_end = h.find('>', p);
        if (tag_start == std::string::npos || tag_end == std::string::npos) return std::string();
        size_t cpos = h.find("content=", tag_start);
        if (cpos != std::string::npos && cpos < tag_end) {
            size_t q = cpos + 8;
            if (q >= h.size()) return std::string();
            char quote = h[q];
            if (quote == '"' || quote == '\'') {
                size_t qend = h.find(quote, q + 1);
                if (qend != std::string::npos && qend < tag_end) return h.substr(q + 1, qend - (q + 1));
            } else {
                size_t qend = h.find_first_of(" \t\r\n>", q);
                if (qend != std::string::npos && qend < tag_end) return h.substr(q, qend - q);
            }
        }
        return std::string();
    };

    const char* meta_keys[] = {"og:image", "og:image:url", "og:image:secure_url", "twitter:image", "twitter:image:src"};
    for (auto key : meta_keys) {
        auto r = find_meta_content(key);
        if (!r.empty()) return r;
    }

    // rel="image_src"
    size_t relpos = h.find("rel=\"image_src\"");
    if (relpos != std::string::npos) {
        size_t href = h.find("href=", relpos);
        if (href != std::string::npos) {
            size_t q = href + 5;
            if (q < h.size()) {
                char quote = h[q];
                if (quote == '"' || quote == '\'') {
                    size_t qend = h.find(quote, q + 1);
                    if (qend != std::string::npos) return h.substr(q + 1, qend - (q + 1));
                } else {
                    size_t qend = h.find_first_of(" \t\r\n>", q);
                    if (qend != std::string::npos) return h.substr(q, qend - q);
                }
            }
        }
    }

    // first <img src= in document
    size_t imgpos = h.find("<img");
    while (imgpos != std::string::npos) {
        size_t tag_end = h.find('>', imgpos);
        if (tag_end == std::string::npos) break;
        size_t src = h.find("src=", imgpos);
        if (src != std::string::npos && src < tag_end) {
            size_t q = src + 4;
            if (q < h.size()) {
                char quote = h[q];
                if (quote == '"' || quote == '\'') {
                    size_t qend = h.find(quote, q + 1);
                    if (qend != std::string::npos && qend < tag_end) return h.substr(q + 1, qend - (q + 1));
                } else {
                    size_t qend = h.find_first_of(" \t\r\n>", q);
                    if (qend != std::string::npos && qend < tag_end) return h.substr(q, qend - q);
                }
            }
        }
        imgpos = h.find("<img", tag_end);
    }

    // fallback: direct images.unsplash URLs inside HTML
    size_t pos = h.find("https://images.unsplash.com");
    if (pos != std::string::npos) {
        size_t end = h.find_first_of("\"' \t\r\n<>", pos);
        if (end != std::string::npos) return h.substr(pos, end - pos);
    }
    pos = h.find("//images.unsplash.com");
    if (pos != std::string::npos) {
        size_t end = h.find_first_of("\"' \t\r\n<>", pos);
        if (end != std::string::npos) return std::string("https:") + h.substr(pos, end - pos);
    }

    return std::string();
}
