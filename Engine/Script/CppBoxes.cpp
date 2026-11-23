#include "Script/CppBoxes.hpp"

#include "Script/Convert.hpp"
#include "Script/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>

namespace molten::script {

namespace {

// ================================================================ the words of the file, and where each is

enum class Kind { Name, Number, Text, Sign };

struct Word {
    Kind kind = Kind::Sign;
    size_t start = 0, end = 0;          // in the file's text
    int line = 0;
};

/// A file as its words. What is between them (spaces, line ends, comments, the lines of the
/// preprocessor) is not a word: it stays where it is in the text, and goes to whichever box
/// the text around it goes to.
struct Source {
    const std::string& text;
    std::vector<Word> words;
    bool isSound = true;                // false: a text or a comment is never closed

    std::string_view of(size_t i) const { return std::string_view(text).substr(words[i].start, words[i].end - words[i].start); }
    bool is(size_t i, std::string_view what) const { return i < words.size() && of(i) == what; }
    bool isSign(size_t i, std::string_view what) const { return i < words.size() && words[i].kind == Kind::Sign && of(i) == what; }
    bool isName(size_t i, std::string_view what) const { return i < words.size() && words[i].kind == Kind::Name && of(i) == what; }
};

bool startsName(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool continuesName(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

void readWords(Source& source) {
    const std::string& c = source.text;
    const size_t n = c.size();
    size_t i = 0;
    int line = 1;
    bool atLineStart = true;
    auto countLines = [&](size_t from, size_t to) {
        for (size_t k = from; k < to && k < n; k++) line += c[k] == '\n';
    };
    auto add = [&](Kind kind, size_t start, size_t end, int atLine) { source.words.push_back(Word{kind, start, end, atLine}); };
    while (i < n) {
        char ch = c[i];
        if (ch == '\n') {
            line++, i++;
            atLineStart = true;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v') {
            i++;
            continue;
        }
        if (ch == '\\' && i + 1 < n && (c[i + 1] == '\n' || c[i + 1] == '\r')) {       // a line carried on
            i++;
            continue;
        }
        if (ch == '/' && i + 1 < n && c[i + 1] == '/') {
            while (i < n && c[i] != '\n') {
                if (c[i] == '\\' && i + 1 < n && c[i + 1] == '\n') line++, i++;         // a comment carried on to the next line
                i++;
            }
            continue;
        }
        if (ch == '/' && i + 1 < n && c[i + 1] == '*') {
            size_t close = c.find("*/", i + 2);
            if (close == std::string::npos) {
                source.isSound = false;
                return;
            }
            countLines(i, close);
            i = close + 2;
            continue;
        }
        if (ch == '#' && atLineStart) {
            // A line for the preprocessor, with the lines it is carried on to.
            while (i < n && c[i] != '\n') {
                if (c[i] == '\\' && i + 1 < n && c[i + 1] == '\n') {
                    line++, i += 2;
                    continue;
                }
                if (c[i] == '\\' && i + 2 < n && c[i + 1] == '\r' && c[i + 2] == '\n') {
                    line++, i += 3;
                    continue;
                }
                if (c[i] == '/' && i + 1 < n && c[i + 1] == '*') {
                    size_t close = c.find("*/", i + 2);
                    if (close == std::string::npos) {
                        source.isSound = false;
                        return;
                    }
                    countLines(i, close);
                    i = close + 2;
                    continue;
                }
                if (c[i] == '/' && i + 1 < n && c[i + 1] == '/') {
                    while (i < n && c[i] != '\n') i++;
                    break;
                }
                i++;
            }
            continue;
        }
        atLineStart = false;
        const size_t start = i;
        const int startLine = line;
        if (startsName(ch)) {
            while (i < n && continuesName(c[i])) i++;
            std::string_view name = std::string_view(c).substr(start, i - start);
            bool isRaw = name == "R" || name == "LR" || name == "uR" || name == "UR" || name == "u8R";
            bool isPrefix = name == "L" || name == "u" || name == "U" || name == "u8";
            if (i < n && c[i] == '"' && isRaw) {
                // R"word( anything at all )word"
                size_t open = c.find('(', i + 1);
                if (open == std::string::npos || open - i - 1 > 16) {
                    source.isSound = false;
                    return;
                }
                std::string closer = ")" + c.substr(i + 1, open - i - 1) + "\"";
                size_t close = c.find(closer, open + 1);
                if (close == std::string::npos) {
                    source.isSound = false;
                    return;
                }
                countLines(i, close);
                i = close + closer.size();
                add(Kind::Text, start, i, startLine);
                continue;
            }
            if (!(i < n && (c[i] == '"' || c[i] == '\'') && isPrefix)) {
                add(Kind::Name, start, i, startLine);
                continue;
            }
            // A text or a letter with a prefix (u8"..."): read on as one.
        }
        if (c[i] == '"' || c[i] == '\'') {
            const char quote = c[i];
            i++;
            while (i < n && c[i] != quote) {
                if (c[i] == '\\' && i + 1 < n) {
                    line += c[i + 1] == '\n';
                    i += 2;
                    continue;
                }
                if (c[i] == '\n') {
                    source.isSound = false;                 // never closed on its line
                    return;
                }
                i++;
            }
            if (i >= n) {
                source.isSound = false;
                return;
            }
            i++;
            add(Kind::Text, start, i, startLine);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) || (ch == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(c[i + 1])))) {
            const bool isHex = ch == '0' && i + 1 < n && (c[i + 1] == 'x' || c[i + 1] == 'X');
            i++;
            while (i < n) {
                char d = c[i];
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || d == '_') {
                    i++;
                    continue;
                }
                if (d == '\'' && i + 1 < n && std::isalnum(static_cast<unsigned char>(c[i + 1]))) {      // 1'000'000
                    i += 2;
                    continue;
                }
                char before = c[i - 1];
                bool isExponent = isHex ? (before == 'p' || before == 'P') : (before == 'e' || before == 'E');
                if ((d == '+' || d == '-') && isExponent) {
                    i++;
                    continue;
                }
                break;
            }
            add(Kind::Number, start, i, startLine);
            continue;
        }
        // Signs. Those of two and three characters are kept whole, so that a lone '=' is a setting and a lone ':' a label.
        static const char* const kThree[] = {"<<=", ">>=", "<=>", "...", "->*"};
        static const char* const kTwo[] = {"::", "->", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "&&", "||", "++", "--", "<<", ">>"};
        size_t length = 1;
        for (const char* sign : kThree) {
            if (c.compare(i, 3, sign) == 0) length = 3;
        }
        if (length == 1) {
            for (const char* sign : kTwo) {
                if (c.compare(i, 2, sign) == 0) length = 2;
            }
        }
        i += length;
        add(Kind::Sign, start, i, startLine);
    }
}

// ================================================================ the file, cut into boxes

struct Cutter {
    Source& source;
    Graph graph;
    size_t at = 0;                      // how far into the text the boxes made so far reach
    int order = 0;
    bool hasFailed = false;
    std::vector<size_t> partner;        // for each opening sign, the word that closes it (and back); npos where none

    static constexpr size_t kNone = std::string::npos;

    size_t count() const { return source.words.size(); }
    const Word& word(size_t i) const { return source.words[i]; }

    /// Pairs every ( [ { with what closes it. False if they do not pair up.
    bool pairUp() {
        partner.assign(count(), kNone);
        std::vector<size_t> open;
        for (size_t i = 0; i < count(); i++) {
            if (word(i).kind != Kind::Sign) continue;
            std::string_view sign = source.of(i);
            if (sign == "(" || sign == "[" || sign == "{") {
                open.push_back(i);
            } else if (sign == ")" || sign == "]" || sign == "}") {
                if (open.empty()) return false;
                char wanted = sign == ")" ? '(' : sign == "]" ? '[' : '{';
                if (source.text[word(open.back()).start] != wanted) return false;
                partner[open.back()] = i;
                partner[i] = open.back();
                open.pop_back();
            }
        }
        return open.empty();
    }

    /// The text from where the last box ended up to a place: each character is taken once, in order.
    std::string take(size_t to) {
        if (to < at || to > source.text.size()) {
            hasFailed = true;
            return {};
        }
        std::string out = source.text.substr(at, to - at);
        at = to;
        return out;
    }

    int add(const char* type) { return graph.addNode(type, 0, 0).id; }
    void set(int id, const char* name, std::string value) {
        if (Node* node = graph.find(id)) node->values[name] = std::move(value);
    }
    void link(int from, const char* pin, int to) { graph.links.push_back(Link{from, pin, to, "in"}); }

    void fileBox(size_t leadEnd, size_t codeEnd) {
        int id = add("CppFile");
        set(id, "order", std::to_string(order++));
        set(id, "lead", take(leadEnd));
        set(id, "code", take(codeEnd));
    }

    static bool looksLikeMacro(std::string_view name) {
        if (name.size() < 3) return false;
        for (char c : name) {
            if (!(std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_')) return false;
        }
        return true;
    }

    /// The word after the `>` that closes the `<` at word i (a template's list): signs of two characters count for two.
    size_t afterAngles(size_t i) {
        int depth = 0;
        for (; i < count(); i++) {
            if (word(i).kind != Kind::Sign) continue;
            std::string_view sign = source.of(i);
            if (sign == "(" || sign == "[" || sign == "{") {
                if (partner[i] == kNone) return count();
                i = partner[i];
                continue;
            }
            if (sign == "<") depth++;
            else if (sign == ">") depth--;
            else if (sign == ">>") depth -= 2;
            else if (sign == ";") return i;
            if (depth <= 0) return i + 1;
        }
        return count();
    }

    // ---------------------------------------------------------------- outside the functions

    /// What a file, a namespace or a struct holds, from word i to the brace that closes it (or the file's end).
    size_t scope(size_t i) {
        while (i < count() && !hasFailed) {
            if (source.isSign(i, "}")) return i;
            i = item(i);
        }
        return i;
    }

    /// Whether the brace at word `brace` opens a function's body, given what came before it in its item.
    bool opensBody(size_t first, size_t brace, bool inMemberList) const {
        if (brace == first) return false;
        const Word& before = word(brace - 1);
        std::string_view said = source.of(brace - 1);
        if (before.kind == Kind::Sign) {
            if (said == ")") return true;
            if (said == "}" && inMemberList) return true;               // after a member set with braces
            if (said == "&" || said == "&&" || said == "]") return true;
            return false;
        }
        if (before.kind == Kind::Name) {
            if (said == "const" || said == "noexcept" || said == "override" || said == "final" || said == "try" || said == "mutable" || said == "volatile") return true;
            if (inMemberList) return false;                             // a member's name before its braces
            // A return type written after the arguments: ) -> Type {
            for (size_t k = brace - 1; k > first; k--) {
                if (source.isSign(k, "->")) return true;
                if (word(k).kind == Kind::Sign && (source.of(k) == ")" || source.of(k) == ";" || source.of(k) == "}")) break;
            }
        }
        return false;
    }

    /// One thing at that level: a declaration, a function, a namespace or a struct with what is in it.
    size_t item(size_t first) {
        bool sawArguments = false, sawSetting = false, inMemberList = false, isType = false, isEnum = false, isNamespace = false;
        size_t j = first;
        while (j < count()) {
            const Word& here = word(j);
            std::string_view said = source.of(j);
            if (here.kind == Kind::Name) {
                if (said == "template" && source.isSign(j + 1, "<")) {
                    j = afterAngles(j + 1);
                    continue;
                }
                if (!sawArguments && !sawSetting) {
                    if (said == "namespace") isNamespace = true;
                    else if (said == "struct" || said == "class" || said == "union") isType = true;
                    else if (said == "enum") isType = isEnum = true;
                }
                j++;
                continue;
            }
            if (here.kind != Kind::Sign) {
                j++;
                continue;
            }
            if (said == ";") {
                fileBox(word(first).start, here.end);
                return j + 1;
            }
            if (said == "}") {
                // The scope closes on something unfinished: it is a box all the same.
                if (j > first) fileBox(word(first).start, word(j - 1).end);
                return j;
            }
            if (said == "(" || said == "[") {
                size_t close = partner[j];
                if (said == "(") {
                    sawArguments = true;
                    // A macro on lines of its own, with no semicolon after it: MOLTEN_COMPONENT(...)
                    bool isAlone = j == first + 1 && word(first).kind == Kind::Name && looksLikeMacro(source.of(first));
                    if (isAlone && (close + 1 >= count() || (word(close + 1).line > word(close).line && !source.isSign(close + 1, "{")))) {
                        fileBox(word(first).start, word(close).end);
                        return close + 1;
                    }
                }
                j = close + 1;
                continue;
            }
            if (said == "=" && !(j > first && source.isName(j - 1, "operator"))) sawSetting = true;
            if (said == ":" && sawArguments) inMemberList = true;
            if (said == "{") {
                size_t close = partner[j];
                bool isContainer = !sawArguments && !sawSetting && (isNamespace || (isType && !isEnum));
                if (isContainer) {
                    // Opened, what is in it laid out like the file itself, and closed: three kinds of box.
                    fileBox(word(first).start, here.end);
                    size_t inside = scope(j + 1);
                    if (hasFailed || inside != close) {
                        hasFailed = true;
                        return count();
                    }
                    // A struct closes with what follows its brace up to the semicolon: "};", "} name;".
                    size_t last = close;
                    if (!isNamespace) {
                        size_t k = close + 1;
                        while (k < count() && word(k).kind == Kind::Name) k++;
                        if (source.isSign(k, ";")) last = k;
                    }
                    fileBox(word(close).start, word(last).end);
                    return last + 1;
                }
                if (!sawSetting && sawArguments && opensBody(first, j, inMemberList)) {
                    functionBox(first, j, close);
                    return close + 1;
                }
                // Braces that hold values (a table, an enum's names, a member set with them): passed over.
                j = close + 1;
                if (!sawSetting && !sawArguments && !isType && !source.isSign(j, ";") && (j >= count() || word(j).line > word(close).line)) {
                    fileBox(word(first).start, word(close).end);          // braces alone: extern "C" { ... }
                    return j;
                }
                continue;
            }
            j++;
        }
        // The file ends in the middle of something.
        fileBox(word(first).start, word(count() - 1).end);
        return count();
    }

    void functionBox(size_t first, size_t brace, size_t close) {
        int id = add("CppFunction");
        set(id, "order", std::to_string(order++));
        set(id, "lead", take(word(first).start));
        set(id, "code", take(word(brace - 1).end));
        set(id, "open", take(word(brace).end));
        int child = statements(brace + 1, close);
        set(id, "close", take(word(close).end));
        if (child) graph.links.push_back(Link{id, "then", child, "in"});
    }

    // ---------------------------------------------------------------- inside them

    /// The statements from word i up to word `stop`, each wired to the next. The first one's box, or 0.
    int statements(size_t i, size_t stop) {
        int firstId = 0, last = 0;
        while (i < stop && !hasFailed) {
            int id = statement(i, stop);
            if (!id) break;
            if (!firstId) firstId = id;
            else link(last, "then", id);
            last = id;
        }
        return firstId;
    }

    int statement(size_t& i, size_t stop) {
        if (i >= stop) return 0;
        if (word(i).kind == Kind::Name) {
            std::string_view said = source.of(i);
            if (said == "if") return ifBox(i, stop);
            if ((said == "for" || said == "while" || said == "switch") && source.isSign(i + 1, "(")) return loopBox(i, stop);
            if (said == "do" && (source.isSign(i + 1, "{") || (i + 1 < stop && word(i + 1).kind == Kind::Name))) return doBox(i, stop);
            if (said == "try" && source.isSign(i + 1, "{")) return tryBox(i, stop);
            if (said == "case" || (said == "default" && source.isSign(i + 1, ":"))) return labelBox(i, stop);
        }
        if (source.isSign(i, "{")) return blockBox(i, stop);
        return plainBox(i, stop);
    }

    /// What follows an if, an else, a for: statements in braces, or one statement. The first one's box.
    int branch(int id, const char* openName, const char* closeName, size_t& i, size_t stop) {
        if (i < stop && source.isSign(i, "{")) {
            size_t close = partner[i];
            if (close >= stop) {
                hasFailed = true;
                return 0;
            }
            set(id, openName, take(word(i).end));
            int child = statements(i + 1, close);
            set(id, closeName, take(word(close).end));
            i = close + 1;
            return child;
        }
        set(id, openName, "");
        int child = statement(i, stop);
        set(id, closeName, "");
        return child;
    }

    int ifBox(size_t& i, size_t stop) {
        // if (, if constexpr (, if !consteval {: the last two are rare enough to be left as plain code when they have no "(".
        size_t open = i + 1;
        if (source.isName(open, "constexpr")) open++;
        if (!source.isSign(open, "(") || partner[open] >= stop) return plainBox(i, stop);
        size_t close = partner[open];
        int id = add("CppIf");
        set(id, "lead", take(word(i).start));
        set(id, "pre", take(word(open).end));
        set(id, "code", take(word(close).start));
        set(id, "post", take(word(close).end));
        i = close + 1;
        int child = branch(id, "open", "close", i, stop);
        if (child) graph.links.push_back(Link{id, "true", child, "in"});
        if (i < stop && source.isName(i, "else")) {
            set(id, "hasElse", "1");
            set(id, "else", take(word(i).end));
            i++;
            int other = branch(id, "elseOpen", "elseClose", i, stop);
            if (other) graph.links.push_back(Link{id, "false", other, "in"});
        }
        return id;
    }

    int loopBox(size_t& i, size_t stop) {
        size_t close = partner[i + 1];
        if (close >= stop) return plainBox(i, stop);
        int id = add("CppBlock");
        set(id, "lead", take(word(i).start));
        set(id, "code", take(word(close).end));
        i = close + 1;
        int child = branch(id, "open", "close", i, stop);
        set(id, "tail", "");
        if (child) graph.links.push_back(Link{id, "body", child, "in"});
        return id;
    }

    int doBox(size_t& i, size_t stop) {
        int id = add("CppBlock");
        set(id, "lead", take(word(i).start));
        set(id, "code", take(word(i).end));
        i++;
        int child = branch(id, "open", "close", i, stop);
        // ... while (it holds);
        size_t end = i;
        while (end < stop && !source.isSign(end, ";")) {
            if (word(end).kind == Kind::Sign && partner[end] != kNone && partner[end] > end) end = partner[end];
            end++;
        }
        if (end >= stop) {
            hasFailed = true;
            return 0;
        }
        set(id, "tail", take(word(end).end));
        i = end + 1;
        if (child) graph.links.push_back(Link{id, "body", child, "in"});
        return id;
    }

    int blockBox(size_t& i, size_t stop) {
        int id = add("CppBlock");
        set(id, "lead", take(word(i).start));
        set(id, "code", "");
        int child = branch(id, "open", "close", i, stop);
        set(id, "tail", "");
        if (child) graph.links.push_back(Link{id, "body", child, "in"});
        return id;
    }

    int tryBox(size_t& i, size_t stop) {
        // try { ... } catch (...) { ... }, however many catches: one box, as written.
        size_t j = partner[i + 1] + 1;
        while (j < stop && source.isName(j, "catch") && source.isSign(j + 1, "(")) {
            size_t body = partner[j + 1] + 1;
            if (!source.isSign(body, "{")) break;
            j = partner[body] + 1;
        }
        if (j > stop) {
            hasFailed = true;
            return 0;
        }
        int id = add("CppCode");
        set(id, "lead", take(word(i).start));
        set(id, "code", take(word(j - 1).end));
        i = j;
        return id;
    }

    int labelBox(size_t& i, size_t stop) {
        size_t j = i;
        while (j < stop && !source.isSign(j, ":")) {
            if (word(j).kind == Kind::Sign && partner[j] != kNone && partner[j] > j) j = partner[j];
            if (source.isSign(j, ";")) return plainBox(i, stop);         // not a label after all
            j++;
        }
        if (j >= stop) return plainBox(i, stop);
        int id = add("CppCode");
        set(id, "lead", take(word(i).start));
        set(id, "code", take(word(j).end));
        i = j + 1;
        return id;
    }

    /// A statement that ends with its semicolon. One whose last argument is a function written in
    /// place (`things.each([&](Thing& thing) { ... });`) is a block box: what is in the braces is laid out too.
    int plainBox(size_t& i, size_t stop) {
        size_t end = i, lastOpen = kNone;
        while (end < stop && !source.isSign(end, ";")) {
            if (word(end).kind == Kind::Sign) {
                std::string_view sign = source.of(end);
                if (sign == "{") {
                    lastOpen = end;
                    end = partner[end];
                } else if (sign == "(" || sign == "[") {
                    // Into the brackets word by word: a function written in place is in there.
                } else if (sign == "}") {
                    hasFailed = true;                                   // closes what this statement did not open
                    return 0;
                }
            }
            end++;
        }
        if (end > stop) {
            hasFailed = true;
            return 0;
        }
        size_t lastWord = end < stop ? end : stop - 1;                  // its semicolon, or the block's last word where it has none
        bool isBlock = false;
        if (lastOpen != kNone && lastOpen > i && end < stop) {
            size_t close = partner[lastOpen];
            // After its closing brace nothing but closing brackets; before its opening one, arguments or a capture list.
            bool closesOnly = true;
            for (size_t k = close + 1; k < end; k++) closesOnly = closesOnly && source.isSign(k, ")");
            std::string_view before = source.of(lastOpen - 1);
            bool afterArguments = word(lastOpen - 1).kind == Kind::Sign ? (before == ")" || before == "]") : (before == "mutable" || before == "noexcept");
            if (!afterArguments && word(lastOpen - 1).kind == Kind::Name) {
                for (size_t k = lastOpen - 1; k > i; k--) {
                    if (source.isSign(k, "->")) afterArguments = true;
                    if (word(k).kind == Kind::Sign && (source.of(k) == ")" || source.of(k) == "(" || source.of(k) == ",")) break;
                }
            }
            isBlock = closesOnly && afterArguments;
        }
        if (isBlock) {
            size_t close = partner[lastOpen];
            int id = add("CppBlock");
            set(id, "lead", take(word(i).start));
            set(id, "code", take(word(lastOpen - 1).end));
            set(id, "open", take(word(lastOpen).end));
            int child = statements(lastOpen + 1, close);
            set(id, "close", take(word(close).end));
            set(id, "tail", take(word(end).end));
            if (child) graph.links.push_back(Link{id, "body", child, "in"});
            i = end + 1;
            return id;
        }
        int id = add("CppCode");
        set(id, "lead", take(word(i).start));
        set(id, "code", take(word(lastWord).end));
        i = lastWord + 1;
        return id;
    }
};

// ================================================================ where the boxes are put

std::string valueOf(const Node& node, const char* name, const std::string& otherwise = {}) {
    auto found = node.values.find(name);
    return found != node.values.end() ? found->second : otherwise;
}

/// A text without the indentation its lines share (the first line's own is left out of the count: it starts where the code starts).
std::string withoutIndent(const std::string& code) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= code.size()) {
        size_t end = code.find('\n', start);
        if (end == std::string::npos) end = code.size();
        lines.push_back(code.substr(start, end - start));
        start = end + 1;
    }
    size_t shared = std::string::npos;
    for (size_t i = 1; i < lines.size(); i++) {
        size_t indent = lines[i].find_first_not_of(" \t");
        if (indent == std::string::npos) continue;                      // an empty line says nothing about it
        shared = std::min(shared, indent);
    }
    std::string out = lines.empty() ? std::string() : lines[0];
    for (size_t i = 1; i < lines.size(); i++) {
        out += "\n";
        if (shared != std::string::npos && lines[i].size() >= shared) out += lines[i].substr(shared);
        else out += lines[i];
    }
    return out;
}

struct Layout {
    Graph& graph;
    std::set<int> placed;

    Node* next(const Node& node, const char* pin) {
        const Link* link = graph.linkFrom(node.id, pin);
        return link ? graph.find(link->toNode) : nullptr;
    }

    /// Places a chain of statements one under the other, as code is read, and what each one
    /// branches into to the right of it. Returns how much room downward the whole of it takes.
    float chain(Node* node, float x, float y) {
        const float kGapDown = 22, kGapAcross = 64, kGapBranches = 18;
        const float top = y;
        bool any = false;
        for (; node && !placed.count(node->id); node = next(*node, "then")) {
            placed.insert(node->id);
            any = true;
            float w = 224, h = 80;
            cppBoxSize(*node, w, h);
            node->x = x;
            node->y = y;
            float childY = y;
            const char* const branches[] = {"true", "false", "body"};
            int count = 0;
            for (const char* pin : branches) count += next(*node, pin) != nullptr;
            if (count > 0) {
                w += 54;                        // (the room its ways out take on the board: see the editor's sizeOf)
                h = std::max(h, 30.0f + 26.0f * float(node->type == "CppIf" ? 2 : 1) + 22.0f);
            }
            for (const char* pin : branches) {
                Node* child = next(*node, pin);
                if (!child) continue;
                childY += chain(child, x + w + kGapAcross, childY) + kGapBranches;
            }
            y += std::max(h, childY - kGapBranches - y) + kGapDown;
        }
        return any ? y - kGapDown - top : 0.0f;
    }
};

void layOut(Graph& graph) {
    Layout layout{graph, {}};
    // The file down the left side, in its order; each function's statements under its first line, a step in.
    std::vector<Node*> top;
    for (Node& node : graph.nodes) {
        if (node.type == "CppFile" || node.type == "CppFunction") top.push_back(&node);
    }
    std::sort(top.begin(), top.end(), [](const Node* a, const Node* b) { return std::atoi(valueOf(*a, "order").c_str()) < std::atoi(valueOf(*b, "order").c_str()); });
    float y = 80;
    for (Node* node : top) {
        float w = 224, h = 80;
        cppBoxSize(*node, w, h);
        node->x = 80;
        node->y = y;
        y += h + 22;
        if (node->type == "CppFunction") {
            float used = layout.chain(layout.next(*node, "then"), 80 + 48, y);
            y += used > 0 ? used + 22 : 0;
        }
        y += 18;
    }
}

// ================================================================ and written out again

struct Writer {
    const Graph& graph;
    std::string out;
    std::set<int> written;

    const Node* next(const Node& node, const char* pin) const {
        const Link* link = graph.linkFrom(node.id, pin);
        return link ? graph.find(link->toNode) : nullptr;
    }
    static bool has(const Node& node, const char* name) { return node.values.count(name) > 0; }

    /// What a box says where it was made from a file; what it would say there, where it was made on the board.
    std::string part(const Node& node, const char* name, const std::string& otherwise) const { return has(node, name) ? node.values.at(name) : otherwise; }

    void chain(const Node* node, int depth) {
        for (; node && !written.count(node->id); node = next(*node, "then")) {
            written.insert(node->id);
            statement(*node, depth);
        }
    }

    void statement(const Node& node, int depth) {
        const std::string indent(size_t(depth) * 4, ' '), newLine = "\n" + indent;
        if (node.type == "CppCode") {
            std::string code = valueOf(node, "code");
            // One made on the board may lack its semicolon.
            if (!has(node, "lead")) {
                size_t last = code.find_last_not_of(" \t\n");
                if (last != std::string::npos && code[last] != ';' && code[last] != '}' && code[last] != ':') code += ";";
            }
            out += part(node, "lead", newLine) + code;
        } else if (node.type == "CppIf") {
            out += part(node, "lead", newLine) + part(node, "pre", "if (") + valueOf(node, "code", "true") + part(node, "post", ")") + part(node, "open", " {");
            chain(next(node, "true"), depth + 1);
            out += part(node, "close", newLine + "}");
            const Node* other = next(node, "false");
            if (has(node, "hasElse") || other) {
                out += part(node, "else", " else") + part(node, "elseOpen", " {");
                chain(other, depth + 1);
                out += part(node, "elseClose", newLine + "}");
            }
        } else if (node.type == "CppBlock") {
            out += part(node, "lead", newLine) + valueOf(node, "code") + part(node, "open", valueOf(node, "code").empty() ? "{" : " {");
            chain(next(node, "body"), depth + 1);
            out += part(node, "close", newLine + "}") + part(node, "tail", "");
        } else if (node.type == "CppFunction" || node.type == "CppFile") {
            // (Not statements: a wire into one of them leads nowhere.)
        } else {
            // A box of the other kind among the code: the call that runs it.
            out += newLine + boxAsCppStatement(graph, node);
            const NodeDef* definition = NodeLibrary::instance().find(node.type);
            if (definition) {
                for (const PinDef& pin : definition->outputs) {
                    if (pin.type != PinType::Flow || pin.name == "then") continue;
                    if (const Node* way = next(node, pin.name.c_str())) chain(way, depth);
                }
            }
        }
    }

    void file() {
        std::vector<const Node*> top;
        for (const Node& node : graph.nodes) {
            if (node.type == "CppFile" || node.type == "CppFunction") top.push_back(&node);
        }
        // In the file's order; those made on the board (no order) after the rest, top to bottom as they lie.
        std::stable_sort(top.begin(), top.end(), [](const Node* a, const Node* b) {
            bool hasA = a->values.count("order") > 0, hasB = b->values.count("order") > 0;
            if (hasA != hasB) return hasA;
            if (hasA) return std::atoi(a->values.at("order").c_str()) < std::atoi(b->values.at("order").c_str());
            return a->y < b->y;
        });
        for (const Node* node : top) {
            if (node->type == "CppFile") {
                out += part(*node, "lead", out.empty() ? "" : "\n\n") + valueOf(*node, "code");
            } else {
                out += part(*node, "lead", out.empty() ? "" : "\n\n") + valueOf(*node, "code") + part(*node, "open", " {");
                chain(next(*node, "then"), 1);
                out += part(*node, "close", "\n}");
            }
        }
        if (!out.empty() && out.back() != '\n' && std::none_of(top.begin(), top.end(), [](const Node* node) { return node->values.count("lead") > 0; })) out += "\n";
    }
};

}  // namespace

// ================================================================ what the rest of the program sees

bool isCppBox(const std::string& type) { return type == "CppFile" || type == "CppFunction" || type == "CppCode" || type == "CppIf" || type == "CppBlock"; }

bool hasCppBoxes(const Graph& graph) {
    for (const Node& node : graph.nodes) {
        if (node.type == "CppFile" || node.type == "CppFunction") return true;
    }
    return false;
}

Graph boxesFromCpp(const std::string& code) {
    registerCppBoxes();
    Source source{code, {}, true};
    readWords(source);
    Cutter cutter{source, {}, 0, 0, false, {}};
    bool isCut = source.isSound && cutter.pairUp();
    if (isCut) {
        size_t end = cutter.scope(0);
        isCut = !cutter.hasFailed && end == cutter.count();             // (a closing brace with nothing to close stops it early)
    }
    if (isCut) {
        // What is left after the last thing: the file's last line end, a closing comment.
        if (cutter.at < code.size()) {
            int id = cutter.add("CppFile");
            cutter.set(id, "order", std::to_string(cutter.order++));
            cutter.set(id, "lead", cutter.take(code.size()));
            cutter.set(id, "code", "");
        }
        isCut = !cutter.hasFailed;
    }
    if (!isCut) {
        // Its shape could not be followed: the whole of it in one box, which is still the file.
        Graph whole;
        Node& node = whole.addNode("CppFile", 80, 80);
        node.values["order"] = "0";
        node.values["lead"] = "";
        node.values["code"] = code;
        return whole;
    }
    layOut(cutter.graph);
    return std::move(cutter.graph);
}

std::string cppFromBoxes(const Graph& graph) {
    registerStandardNodes();
    Writer writer{graph, {}, {}};
    writer.file();
    return writer.out;
}

std::string cppBoxText(const Node& node) {
    std::string code = valueOf(node, "code");
    if (node.type == "CppIf") return "if (" + withoutIndent(code) + ")";
    return withoutIndent(code);
}

std::string cppBoxComment(const Node& node) {
    if (node.type == "Code") return node.comment;       // a C++ box among the others: its comment is the box's own
    // The comment lines just above it: the last run of them in what precedes it.
    std::string lead = valueOf(node, "lead"), comment;
    size_t start = 0;
    while (start < lead.size()) {
        size_t end = lead.find('\n', start);
        if (end == std::string::npos) end = lead.size();
        std::string line = lead.substr(start, end - start);
        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            if (end < lead.size()) comment.clear();                     // an empty line: what was above it belongs to something else
        } else if (line.compare(first, 2, "//") == 0) {
            size_t text = line.find_first_not_of("/ ", first);
            comment += (comment.empty() ? "" : "\n") + (text == std::string::npos ? std::string() : line.substr(text));
        } else if (start > 0 || line.compare(first, 1, "#") == 0) {
            comment.clear();
        }
        start = end + 1;
    }
    return comment;
}

bool cppBoxSize(const Node& node, float& width, float& height) {
    if (!isCppBox(node.type) && node.type != "Code") return false;
    std::string text = cppBoxText(node), comment = cppBoxComment(node);
    int lines = 0;
    size_t longest = 0;
    auto measure = [&](const std::string& words) {
        size_t start = 0;
        while (start <= words.size()) {
            size_t end = words.find('\n', start);
            if (end == std::string::npos) end = words.size();
            longest = std::max(longest, end - start);
            lines++;
            start = end + 1;
        }
    };
    if (!text.empty()) measure(text);
    if (!comment.empty()) measure(comment);
    lines = std::clamp(lines, 1, 40);                                   // a long one is cut short on the board, and whole in its editor
    width = std::clamp(float(longest) * 7.4f + 44.0f, 224.0f, 720.0f);
    height = 34.0f + float(lines) * 17.0f + 18.0f;
    return true;
}

void registerCppBoxes() {
    static bool isDone = false;
    if (isDone) return;
    isDone = true;
    auto text = [](const char* name) {
        PinDef pin;
        pin.name = name;
        pin.type = PinType::String;
        pin.defaultValue = Value::string("");
        return pin;
    };
    auto flow = [](const char* name) {
        PinDef pin;
        pin.name = name;
        pin.type = PinType::Flow;
        return pin;
    };
    // They hold code and are not run: a script with them is run by writing it out as C++ and compiling that.
    auto passes = [](ExecContext&) -> const char* { return "then"; };
    NodeLibrary& library = NodeLibrary::instance();
    {
        NodeDef file;
        file.type = "CppFile";
        file.category = "C++";
        file.title = "C++ File";
        file.help = "What a C++ file says outside its functions: an #include, a table of numbers, a struct's fields. Written out as it is, in the file's order.";
        file.inputs = {text("code")};
        library.add(file);
    }
    {
        NodeDef function;
        function.type = "CppFunction";
        function.category = "C++";
        function.title = "C++ Function";
        function.help = "A function of a C++ file: its first line here, what it does in the boxes wired after it.";
        function.inputs = {text("code")};
        function.outputs = {flow("then")};
        function.isEvent = true;
        library.add(function);
    }
    {
        NodeDef code;
        code.type = "CppCode";
        code.category = "C++";
        code.title = "C++";
        code.help = "One statement of C++, as written. Not run on the board: Convert to C++ Script writes the script out, and the game is compiled from that.";
        code.inputs = {flow("in"), text("code")};
        code.outputs = {flow("then")};
        code.execute = passes;
        library.add(code);
    }
    {
        NodeDef branch;
        branch.type = "CppIf";
        branch.category = "C++";
        branch.title = "C++ If";
        branch.help = "if (what is written here): one way when it holds, the other when it does not, and on to what follows either.";
        branch.inputs = {flow("in"), text("code")};
        branch.outputs = {flow("true"), flow("false"), flow("then")};
        branch.execute = passes;
        library.add(branch);
    }
    {
        NodeDef block;
        block.type = "CppBlock";
        block.category = "C++";
        block.title = "C++ Block";
        block.help = "A line of C++ with statements of its own in braces: a for, a while, a function written in place (each([&](Thing& thing) { ... })). Its statements are wired after \"body\".";
        block.inputs = {flow("in"), text("code")};
        block.outputs = {flow("body"), flow("then")};
        block.execute = passes;
        library.add(block);
    }
}

}  // namespace molten::script
