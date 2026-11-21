// PaletteSearch.cpp
// How the command palette finds things by name: apart from the drawing, so that it can be tested.
#include "CommandPalette.hpp"

#include <algorithm>
#include <cctype>

namespace molten::editor {

int paletteScore(const std::string& query, const std::string& text) {
    auto lower = [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); };
    size_t q = 0;
    while (q < query.size() && query[q] == ' ') q++;
    if (q == query.size()) return 0;
    int score = 0;
    size_t last = std::string::npos;
    for (size_t t = 0; t < text.size(); t++) {
        while (q < query.size() && query[q] == ' ') q++;        // a space: any gap between words
        if (q == query.size()) break;
        if (lower(text[t]) != lower(query[q])) continue;
        score += 10;
        bool startsWord = t == 0 || !std::isalnum(static_cast<unsigned char>(text[t - 1])) ||
                          (std::isupper(static_cast<unsigned char>(text[t])) && std::islower(static_cast<unsigned char>(text[t - 1])));
        if (startsWord) score += 20;
        if (last != std::string::npos && t == last + 1) score += 20;
        else if (last != std::string::npos) score -= std::min<int>(int(t - last - 1), 10);
        if (t == 0) score += 25;
        last = t;
        q++;
    }
    while (q < query.size() && query[q] == ' ') q++;
    if (q < query.size()) return -1;
    // The whole of it, as written: better still.
    std::string a, b;
    for (char c : text) a += lower(c);
    for (char c : query) b += lower(c);
    size_t at = a.find(b);
    if (at != std::string::npos) score += at == 0 ? 60 : 30;
    if (a == b) score += 100;
    // Short names first among equals: "Car" before "Car Body Shadow".
    return std::max(1, score - int(text.size() / 8));
}

std::vector<size_t> rankPalette(const std::string& query, const std::vector<PaletteCommand>& commands, size_t limit) {
    std::vector<std::pair<int, size_t>> found;
    for (size_t i = 0; i < commands.size(); i++) {
        int score = paletteScore(query, commands[i].title);
        if (!commands[i].context.empty()) {
            int within = paletteScore(query, commands[i].context + " " + commands[i].title);
            if (within >= 0) score = std::max(score, within - 20);
        }
        if (score >= 0) found.push_back({score, i});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<size_t> out;
    for (size_t i = 0; i < found.size() && out.size() < limit; i++) out.push_back(found[i].second);
    return out;
}

} // namespace molten::editor
