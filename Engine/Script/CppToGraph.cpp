#include "Script/CppToGraph.hpp"

#include <sstream>

#include "Script/Convert.hpp"
#include "Script/CppBoxes.hpp"
#include "Script/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>

namespace molten::script {

namespace {

// ================================================================ the words of the code

enum class TokenKind { Name, Number, Text, Punct, End };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    int line = 0;
    std::string comment;                // the comment just above it (its own lines), without the slashes
    std::string trailing;               // a comment after it on its line
    size_t offset = 0, end = 0;         // where it is in the code
};

/// A comment's words: without // or /* */ and the spaces round them.
std::string commentWords(const std::string& raw) {
    std::string text = raw;
    if (text.rfind("//", 0) == 0) text = text.substr(2);
    else if (text.rfind("/*", 0) == 0) text = text.substr(2, text.size() >= 4 ? text.size() - 4 : 0);
    while (!text.empty() && (text[0] == '/' || text[0] == ' ' || text[0] == '\t' || text[0] == '*' || text[0] == '<')) text.erase(0, 1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}

/// The code as words: names, numbers, texts and signs, without comments and #lines.
std::vector<Token> tokenize(const std::string& code) {
    static const char* const kLongSigns[] = {"<<=", ">>=", "...", "->", "::", "++", "--", "+=", "-=", "*=", "/=", "%=", "==", "!=",
                                             "<=",  ">=",  "&&",  "||", "<<", ">>", "&=", "|=", "^="};
    std::vector<Token> tokens;
    int line = 1;
    size_t i = 0;
    const size_t n = code.size();
    bool atLineStart = true;
    std::string pending;                // comment lines waiting for the token they are above
    auto noteComment = [&](size_t from, size_t to, int at) {
        std::string words = commentWords(code.substr(from, to - from));
        if (words.empty()) return;
        // After a token on the same line: that token's. Otherwise: the next one's.
        if (!tokens.empty() && tokens.back().line == at && pending.empty()) {
            tokens.back().trailing += (tokens.back().trailing.empty() ? "" : " ") + words;
        } else {
            pending += (pending.empty() ? "" : "\n") + words;
        }
    };
    while (i < n) {
        char c = code[i];
        if (c == '\n') {
            line++;
            i++;
            atLineStart = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            size_t from = i;
            while (i < n && code[i] != '\n') i++;
            noteComment(from, i, line);
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '*') {
            size_t from = i;
            int at = line;
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == '/')) {
                if (code[i] == '\n') line++;
                i++;
            }
            i += 2;
            noteComment(from, std::min(i, n), at);
            continue;
        }
        if (c == '#' && atLineStart) {
            while (i < n && code[i] != '\n') {
                if (code[i] == '\\' && i + 1 < n && code[i + 1] == '\n') {
                    line++;
                    i++;
                }
                i++;
            }
            continue;
        }
        atLineStart = false;
        Token token;
        token.line = line;
        token.offset = i;
        if (c == '"' || c == '\'') {
            i++;
            while (i < n && code[i] != c) {
                if (code[i] == '\\' && i + 1 < n) {
                    char escaped = code[i + 1];
                    token.text += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                    i += 2;
                    continue;
                }
                if (code[i] == '\n') line++;
                token.text += code[i++];
            }
            i++;
            token.kind = TokenKind::Text;
            // "one" "two" is one text.
            if (!tokens.empty() && tokens.back().kind == TokenKind::Text) {
                tokens.back().text += token.text;
                tokens.back().end = i;
            } else {
                token.end = i;
                token.comment = std::move(pending);
                pending.clear();
                tokens.push_back(token);
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(code[i + 1])))) {
            size_t start = i;
            while (i < n) {
                char d = code[i];
                bool exponentSign = (d == '+' || d == '-') && i > start && (code[i - 1] == 'e' || code[i - 1] == 'E') &&
                                    !(code.size() > start + 1 && (code[start + 1] == 'x' || code[start + 1] == 'X'));
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || exponentSign) i++;
                else break;
            }
            token.kind = TokenKind::Number;
            token.text = code.substr(start, i - start);
            token.end = i;
            token.comment = std::move(pending);
            pending.clear();
            tokens.push_back(token);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '_')) i++;
            token.kind = TokenKind::Name;
            token.text = code.substr(start, i - start);
            token.end = i;
            token.comment = std::move(pending);
            pending.clear();
            tokens.push_back(token);
            continue;
        }
        token.kind = TokenKind::Punct;
        token.text = std::string(1, c);
        for (const char* sign : kLongSigns) {
            size_t length = std::char_traits<char>::length(sign);
            if (code.compare(i, length, sign) == 0) {
                token.text = sign;
                break;
            }
        }
        i += token.text.size();
        token.end = i;
        token.comment = std::move(pending);
        pending.clear();
        tokens.push_back(token);
    }
    return tokens;
}

// ================================================================ what the code says

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    enum class Kind { Number, Text, Bool, Name, Member, Call, Unary, Binary, Ternary, List };
    Kind kind = Kind::Name;
    std::string text;                   // the number as written, the text, the name, the member, the sign
    std::string templateArgument;       // get<Transform>(self): "Transform"
    std::vector<ExprPtr> parts;         // Member: the object. Call: what is called, then the arguments.
                                        // Unary: the operand. Binary: left, right. Ternary: the three. List: the items.
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct Stmt {
    enum class Kind { Block, If, Declare, Assign, Do, Return, While, For, Other };
    Kind kind = Kind::Other;
    std::vector<StmtPtr> body;          // Block: the statements. If: then, and else if there is one.
    ExprPtr condition;                  // If
    std::string type, name;             // Declare; an If that declares: if (Transform* t = ...)
    ExprPtr value;                      // Declare: the first value. Assign: the right side. Do: the expression.
    ExprPtr target;                     // Assign: the left side
    std::string op;                     // Assign: "=", "+=", "-=", "*=", "/=", "++", "--"
    StmtPtr init, step;                 // For: what is done before it, and after each turn (While and For: `condition`, and `body[0]`)
    int line = 0;
    std::string source;                 // as written, to say what was not converted
    std::string comment;                // the comment above it and at the end of its line
    std::string raw;                    // exactly as written, its own lines: for a statement no box says
};

struct Method {
    std::string name;
    std::vector<std::string> parameters;        // their names
    StmtPtr body;
    int line = 0;
    std::string raw;                            // as written
};

struct Field {
    std::string type, name;
    ExprPtr value;
    int line = 0;
    std::string comment;
    std::string raw;
};

struct Component {
    std::string name;
    std::vector<Field> fields;
    std::vector<Method> methods;
    std::vector<std::string> skipped;           // members that are neither
    std::vector<std::string> memberSources;     // each member as it is written, for those kept as C++
    std::vector<std::string> fieldsShown;       // the fields MOLTEN_COMPONENT names (all of them, if it names none)
    size_t begin = 0, end = 0;                  // where the struct is in the code, from "struct" to its "};"
};

bool isKeyword(const std::string& word) {
    static const std::set<std::string> kWords = {"return", "if", "else", "for", "while", "do", "switch", "case", "break", "continue",
                                                 "new", "delete", "throw", "goto", "using", "typedef", "namespace", "template", "default"};
    return kWords.count(word) > 0;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, const std::string& code) : tokens_(std::move(tokens)), code_(code) {
        Token end;
        end.kind = TokenKind::End;
        end.line = tokens_.empty() ? 0 : tokens_.back().line;
        end.offset = end.end = code.size();
        tokens_.push_back(end);
    }

    /// The code from token `from` to here, exactly as written.
    std::string raw(size_t from) const {
        if (p_ <= from || from >= tokens_.size()) return {};
        size_t begin = tokens_[from].offset, stop = tokens_[std::min(p_, tokens_.size()) - 1].end;
        return stop > begin ? code_.substr(begin, stop - begin) : std::string();
    }
    /// The comment above token `from`, and the one at the end of the last token read.
    std::string commentOf(size_t from) const {
        std::string out = from < tokens_.size() ? tokens_[from].comment : std::string();
        if (p_ > from && p_ - 1 < tokens_.size() && !tokens_[p_ - 1].trailing.empty()) {
            out += (out.empty() ? "" : "\n") + tokens_[p_ - 1].trailing;
        }
        return out;
    }

    /// The first struct or class with a start, an update or a collision in it.
    std::optional<Component> component() {
        for (p_ = 0; peek().kind != TokenKind::End; p_++) {
            if (!(is("struct") || is("class")) || peek(1).kind != TokenKind::Name) continue;
            size_t at = p_;
            std::string name = peek(1).text;
            p_ += 2;
            if (accept(":")) {
                while (!is("{") && !is(";") && peek().kind != TokenKind::End) p_++;
            }
            if (!accept("{")) {
                p_ = at;
                continue;
            }
            Component found;
            found.name = name;
            found.begin = tokens_[at].offset;
            members(found);
            accept("}");
            accept(";");
            found.end = tokens_[std::max<size_t>(p_, 1) - 1].end;
            bool isComponent = false;
            for (const Method& method : found.methods) {
                isComponent |= method.name == "start" || method.name == "update" || method.name == "onCollision" || method.name == "onTriggerEnter";
            }
            if (isComponent) return found;
            p_ = at + 1;
        }
        return std::nullopt;
    }

private:
    const Token& peek(size_t ahead = 0) const { return tokens_[std::min(p_ + ahead, tokens_.size() - 1)]; }
    bool is(const char* text, size_t ahead = 0) const {
        const Token& token = peek(ahead);
        return (token.kind == TokenKind::Punct || token.kind == TokenKind::Name) && token.text == text;
    }
    bool accept(const char* text) {
        if (!is(text)) return false;
        p_++;
        return true;
    }
    bool atEnd() const { return peek().kind == TokenKind::End; }

    /// The tokens from `from` to here, as they read.
    std::string sourceFrom(size_t from, size_t limit = 110) const {
        std::string out;
        for (size_t i = from; i < p_ && i < tokens_.size(); i++) {
            const Token& token = tokens_[i];
            std::string word = token.kind == TokenKind::Text ? "\"" + token.text + "\"" : token.text;
            bool tight = word == "(" || word == ")" || word == "," || word == ";" || word == "." || word == "->" || word == "::" || word == "]" ||
                         word == "[" || word == "++" || word == "--";
            bool afterTight = !out.empty() && (out.back() == '(' || out.back() == '.' || out.back() == '[' || out.back() == '!' ||
                                               (out.size() >= 2 && (out.compare(out.size() - 2, 2, "->") == 0 || out.compare(out.size() - 2, 2, "::") == 0)));
            if (!out.empty() && !tight && !afterTight) out += ' ';
            out += word;
            if (out.size() > limit) return out.substr(0, limit) + "...";
        }
        return out;
    }

    /// Past the statement that starts here, whatever it is.
    void skipStatement() {
        int depth = 0;
        while (!atEnd()) {
            if (is("{") && depth == 0) {
                skipBraces();
                accept(";");
                return;
            }
            if (is("(") || is("[")) depth++;
            else if (is(")") || is("]")) depth--;
            else if (is("}") && depth <= 0) return;
            else if (is(";") && depth <= 0) {
                p_++;
                return;
            }
            p_++;
        }
    }

    void skipBraces() {
        int depth = 0;
        do {
            if (is("{")) depth++;
            else if (is("}")) depth--;
            p_++;
        } while (depth > 0 && !atEnd());
    }

    /// get<Transform>(...): the < starts a template's arguments, not a comparison.
    bool templateFollows() const {
        if (!is("<")) return false;
        int depth = 0;
        for (size_t i = 0; i < 24; i++) {
            const Token& token = peek(i);
            if (token.kind == TokenKind::End) return false;
            if (token.text == "<") depth++;
            else if (token.text == ">") {
                depth--;
                if (depth == 0) return is("(", i + 1) || is("{", i + 1) || is("::", i + 1);
            } else if (token.text == ">>") {
                depth -= 2;
                if (depth <= 0) return depth == 0 && (is("(", i + 1) || is("{", i + 1));
            } else if (token.kind == TokenKind::Punct && token.text != "::" && token.text != "," && token.text != "*" && token.text != "&") {
                return false;
            }
        }
        return false;
    }

    std::string templateArgument() {
        std::string text;
        int depth = 0;
        do {
            if (is("<")) depth++;
            else if (is(">")) depth--;
            else if (is(">>")) depth -= 2;
            if (!(depth == 1 && is("<") && text.empty())) text += peek().text;
            p_++;
        } while (depth > 0 && !atEnd());
        if (!text.empty() && text.back() == '>') text.pop_back();
        return text;
    }

    // ---------------------------------------------------------------- the members of the struct

    void members(Component& component) {
        while (!atEnd() && !is("}")) {
            if ((is("public") || is("private") || is("protected")) && is(":", 1)) {
                p_ += 2;
                continue;
            }
            size_t start = p_;
            // Up to the name: the type, as words.
            std::string type;
            while (!atEnd() && !is("(") && !is("=") && !is(";") && !is("{") && !is("}")) {
                if (peek().kind == TokenKind::Name && (is("(", 1) || is("=", 1) || is(";", 1) || is("{", 1))) break;
                type += (type.empty() ? "" : " ") + peek().text;
                p_++;
            }
            if (peek().kind != TokenKind::Name) {
                p_ = start;
                skipStatement();
                component.skipped.push_back(sourceFrom(start));
                component.memberSources.push_back(raw(start));
                continue;
            }
            std::string name = peek().text;
            int line = peek().line;
            p_++;
            if (accept("(")) {
                Method method;
                method.name = name;
                method.line = line;
                // The parameters' names: the last name of each.
                int depth = 1;
                std::string last;
                while (!atEnd() && depth > 0) {
                    if (is("(")) depth++;
                    else if (is(")")) {
                        depth--;
                        if (depth == 0) break;
                    } else if (is(",") && depth == 1) {
                        method.parameters.push_back(last);
                        last.clear();
                    } else if (peek().kind == TokenKind::Name && depth == 1) {
                        last = peek().text;
                    }
                    p_++;
                }
                if (!last.empty()) method.parameters.push_back(last);
                accept(")");
                while (!atEnd() && !is("{") && !is(";")) p_++;     // const, override, noexcept
                if (is("{")) {
                    method.body = block();
                    accept(";");
                    method.raw = raw(start);
                    component.methods.push_back(method);
                } else {
                    accept(";");
                    component.memberSources.push_back(raw(start));
                }
                continue;
            }
            Field field;
            field.type = type;
            field.name = name;
            field.line = line;
            if (accept("=")) field.value = expression();
            else if (is("{")) field.value = primary();
            if (!accept(";")) {
                p_ = start;
                skipStatement();
                component.skipped.push_back(sourceFrom(start));
                component.memberSources.push_back(raw(start));
                continue;
            }
            field.comment = commentOf(start);
            field.raw = raw(start);
            component.fields.push_back(field);
        }
    }

    // ---------------------------------------------------------------- statements

    StmtPtr block() {
        auto out = std::make_shared<Stmt>();
        out->kind = Stmt::Kind::Block;
        out->line = peek().line;
        accept("{");
        while (!atEnd() && !is("}")) {
            size_t from = p_;
            StmtPtr inside = statement();
            inside->comment = commentOf(from);
            inside->raw = raw(from);
            out->body.push_back(inside);
        }
        accept("}");
        return out;
    }

    bool looksLikeDeclaration() const {
        size_t i = 0;
        while (is("const", i) || is("static", i) || is("constexpr", i)) i++;
        if (peek(i).kind != TokenKind::Name || isKeyword(peek(i).text)) return false;
        i++;
        while (is("::", i) && peek(i + 1).kind == TokenKind::Name) i += 2;
        if (is("<", i)) {
            int depth = 0;
            for (; i < 40; i++) {
                if (is("<", i)) depth++;
                else if (is(">", i) && --depth == 0) break;
                else if (peek(i).kind == TokenKind::End || is(";", i)) return false;
            }
            i++;
        }
        while (is("*", i) || is("&", i) || is("const", i)) i++;
        if (peek(i).kind != TokenKind::Name || isKeyword(peek(i).text)) return false;
        return is("=", i + 1) || is(";", i + 1) || is("{", i + 1) || is("(", i + 1);
    }

    /// type name = value: what an `if` or a statement declares.
    bool declaration(Stmt& out) {
        std::string type;
        while (!atEnd()) {
            if (peek().kind == TokenKind::Name && (is("=", 1) || is(";", 1) || is("{", 1) || is("(", 1) || is(")", 1)) && !type.empty() &&
                type.back() != ':') {
                break;
            }
            type += peek().text;
            p_++;
        }
        if (peek().kind != TokenKind::Name) return false;
        out.type = type;
        out.name = peek().text;
        p_++;
        if (accept("=")) {
            out.value = expression();
        } else if (is("{")) {
            out.value = primary();
        } else if (is("(")) {
            // Vec3 v(1, 2, 3): the same as Vec3{1, 2, 3}.
            auto call = std::make_shared<Expr>();
            call->kind = Expr::Kind::Call;
            auto callee = std::make_shared<Expr>();
            callee->kind = Expr::Kind::Name;
            callee->text = type;
            call->parts.push_back(callee);
            p_++;
            while (!atEnd() && !is(")")) {
                ExprPtr argument = expression();
                if (!argument) return false;
                call->parts.push_back(argument);
                if (!accept(",")) break;
            }
            if (!accept(")")) return false;
            out.value = call;
        }
        return out.value != nullptr || is(";");
    }

    StmtPtr statement() {
        size_t start = p_;
        int line = peek().line;
        auto out = std::make_shared<Stmt>();
        out->line = line;
        auto fail = [&]() {
            p_ = start;
            skipStatement();
            auto other = std::make_shared<Stmt>();
            other->kind = Stmt::Kind::Other;
            other->line = line;
            other->source = sourceFrom(start);
            if (p_ == start) p_++;          // never stuck
            return other;
        };

        if (is("{")) return block();
        if (accept(";")) {
            out->kind = Stmt::Kind::Block;
            return out;
        }
        if (accept("if")) {
            out->kind = Stmt::Kind::If;
            accept("constexpr");
            if (!accept("(")) return fail();
            if (looksLikeDeclaration()) {
                if (!declaration(*out)) return fail();
                if (accept(";")) out->condition = expression();
            } else {
                out->condition = expression();
            }
            if (!accept(")")) return fail();
            out->source = sourceFrom(start) + " ...";
            out->body.push_back(statement());
            if (accept("else")) out->body.push_back(statement());
            return out;
        }
        if (accept("return")) {
            skipStatement();
            out->kind = Stmt::Kind::Return;
            out->source = sourceFrom(start);
            return out;
        }
        if (accept("while")) {
            out->kind = Stmt::Kind::While;
            if (!accept("(")) return fail();
            out->condition = expression();
            if (!out->condition || !accept(")")) return fail();
            out->source = sourceFrom(start) + " ...";
            out->body.push_back(statement());
            return out;
        }
        if (accept("for")) {
            // for (init; condition; step) body. A for over a list (for (x : list)) is not one of these.
            out->kind = Stmt::Kind::For;
            if (!accept("(")) return fail();
            if (!is(";")) {
                size_t from = p_;
                out->init = statement();          // takes its ;
                if (!out->init || out->init->kind == Stmt::Kind::Other) return fail();
                out->init->raw = raw(from);
            } else {
                accept(";");
            }
            if (!is(";")) out->condition = expression();
            if (!accept(";")) return fail();
            if (!is(")")) {
                ExprPtr step = expression();
                if (!step) return fail();
                auto made = std::make_shared<Stmt>();
                made->line = line;
                static const char* const kAssignments[] = {"=", "+=", "-=", "*=", "/="};
                bool assigned = false;
                for (const char* sign : kAssignments) {
                    if (accept(sign)) {
                        made->kind = Stmt::Kind::Assign;
                        made->target = step;
                        made->op = sign;
                        made->value = expression();
                        assigned = true;
                        break;
                    }
                }
                if (!assigned) {
                    if (step->kind == Expr::Kind::Unary && (step->text == "++" || step->text == "--" || step->text == "post++" || step->text == "post--")) {
                        made->kind = Stmt::Kind::Assign;
                        made->target = step->parts[0];
                        made->op = step->text.size() > 2 ? step->text.substr(4) : step->text;
                    } else {
                        made->kind = Stmt::Kind::Do;
                        made->value = step;
                    }
                }
                out->step = made;
            }
            if (!accept(")")) return fail();
            out->source = sourceFrom(start) + " ...";
            out->body.push_back(statement());
            return out;
        }
        if (is("do") || is("switch") || is("break") || is("continue") || is("case") || is("default") || is("goto") || is("try")) {
            return fail();
        }
        if (looksLikeDeclaration()) {
            out->kind = Stmt::Kind::Declare;
            if (!declaration(*out) || !accept(";")) return fail();
            out->source = sourceFrom(start);
            return out;
        }
        ExprPtr expr = expression();
        if (!expr) return fail();
        static const char* const kAssignments[] = {"=", "+=", "-=", "*=", "/="};
        for (const char* sign : kAssignments) {
            if (accept(sign)) {
                out->kind = Stmt::Kind::Assign;
                out->target = expr;
                out->op = sign;
                out->value = expression();
                if (!out->value || !accept(";")) return fail();
                out->source = sourceFrom(start);
                return out;
            }
        }
        if (!accept(";")) return fail();
        out->source = sourceFrom(start);
        if (expr->kind == Expr::Kind::Unary && (expr->text == "++" || expr->text == "--" || expr->text == "post++" || expr->text == "post--")) {
            out->kind = Stmt::Kind::Assign;
            out->target = expr->parts[0];
            out->op = expr->text.size() > 2 ? expr->text.substr(4) : expr->text;
            return out;
        }
        out->kind = Stmt::Kind::Do;
        out->value = expr;
        return out;
    }

    // ---------------------------------------------------------------- expressions

    static int precedenceOf(const std::string& sign) {
        if (sign == "?") return 2;
        if (sign == "||") return 3;
        if (sign == "&&") return 4;
        if (sign == "|") return 5;
        if (sign == "^") return 6;
        if (sign == "&") return 7;
        if (sign == "==" || sign == "!=") return 8;
        if (sign == "<" || sign == ">" || sign == "<=" || sign == ">=") return 9;
        if (sign == "<<" || sign == ">>") return 10;
        if (sign == "+" || sign == "-") return 11;
        if (sign == "*" || sign == "/" || sign == "%") return 12;
        return -1;
    }

    static ExprPtr make(Expr::Kind kind, std::string text, std::vector<ExprPtr> parts = {}) {
        auto expr = std::make_shared<Expr>();
        expr->kind = kind;
        expr->text = std::move(text);
        expr->parts = std::move(parts);
        return expr;
    }

public:
    ExprPtr expression(int least = 0) {
        ExprPtr left = unary();
        if (!left) return nullptr;
        while (peek().kind == TokenKind::Punct) {
            std::string sign = peek().text;
            int precedence = precedenceOf(sign);
            if (precedence < 0 || precedence < least) break;
            p_++;
            if (sign == "?") {
                ExprPtr yes = expression(0);
                if (!yes || !accept(":")) return nullptr;
                ExprPtr no = expression(precedence);
                if (!no) return nullptr;
                left = make(Expr::Kind::Ternary, "?", {left, yes, no});
                continue;
            }
            ExprPtr right = expression(precedence + 1);
            if (!right) return nullptr;
            left = make(Expr::Kind::Binary, sign, {left, right});
        }
        return left;
    }

private:
    ExprPtr unary() {
        static const char* const kSigns[] = {"!", "-", "+", "*", "&", "~", "++", "--"};
        for (const char* sign : kSigns) {
            if (accept(sign)) {
                ExprPtr operand = unary();
                return operand ? make(Expr::Kind::Unary, sign, {operand}) : nullptr;
            }
        }
        // (float)x: a cast says nothing a box needs.
        if (is("(") && peek(1).kind == TokenKind::Name && is(")", 2) &&
            (peek(1).text == "float" || peek(1).text == "int" || peek(1).text == "double" || peek(1).text == "bool")) {
            p_ += 3;
            return unary();
        }
        return postfix(primary());
    }

    ExprPtr primary() {
        const Token& token = peek();
        if (token.kind == TokenKind::Number) {
            p_++;
            return make(Expr::Kind::Number, token.text);
        }
        if (token.kind == TokenKind::Text) {
            p_++;
            return make(Expr::Kind::Text, token.text);
        }
        if (accept("(")) {
            ExprPtr inside = expression();
            if (!inside || !accept(")")) return nullptr;
            return inside;
        }
        if (accept("{")) {
            ExprPtr list = make(Expr::Kind::List, "{}");
            while (!atEnd() && !is("}")) {
                ExprPtr item = expression();
                if (!item) return nullptr;
                list->parts.push_back(item);
                if (!accept(",")) break;
            }
            if (!accept("}")) return nullptr;
            return list;
        }
        if (token.kind != TokenKind::Name || isKeyword(token.text)) return nullptr;
        if (token.text == "true" || token.text == "false") {
            p_++;
            return make(Expr::Kind::Bool, token.text);
        }
        std::string name = token.text;
        p_++;
        ExprPtr expr = make(Expr::Kind::Name, "");
        while (true) {
            if (templateFollows()) expr->templateArgument = templateArgument();
            if (is("::") && peek(1).kind == TokenKind::Name) {
                name += "::" + peek(1).text;
                p_ += 2;
                continue;
            }
            break;
        }
        expr->text = name;
        if (is("{")) {
            // Vec3{1, 2, 3}: built from its parts, like a call.
            ExprPtr list = primary();
            if (!list) return nullptr;
            ExprPtr call = make(Expr::Kind::Call, "()", {expr});
            for (const ExprPtr& item : list->parts) call->parts.push_back(item);
            return call;
        }
        return expr;
    }

    ExprPtr postfix(ExprPtr expr) {
        while (expr) {
            if (accept("(")) {
                ExprPtr call = make(Expr::Kind::Call, "()", {expr});
                while (!atEnd() && !is(")")) {
                    ExprPtr argument = expression();
                    if (!argument) return nullptr;
                    call->parts.push_back(argument);
                    if (!accept(",")) break;
                }
                if (!accept(")")) return nullptr;
                expr = call;
            } else if (is(".") || is("->")) {
                p_++;
                if (peek().kind != TokenKind::Name) return nullptr;
                ExprPtr member = make(Expr::Kind::Member, peek().text, {expr});
                p_++;
                if (templateFollows()) member->templateArgument = templateArgument();
                expr = member;
            } else if (accept("[")) {
                ExprPtr index = expression();
                if (!index || !accept("]")) return nullptr;
                expr = make(Expr::Kind::Binary, "[]", {expr, index});
            } else if (is("++") || is("--")) {
                expr = make(Expr::Kind::Unary, "post" + peek().text, {expr});
                p_++;
            } else {
                break;
            }
        }
        return expr;
    }

    std::vector<Token> tokens_;
    const std::string& code_;
    size_t p_ = 0;
};

// ================================================================ boxes and wires

/// A value as boxes see it: typed into a pin, or on a wire from a node's output.
struct Ref {
    enum class Kind { None, Literal, Wire };
    Kind kind = Kind::None;
    Value literal;
    int node = 0;
    std::string pin;
    PinType type = PinType::Float;

    bool isNone() const { return kind == Kind::None; }
    bool isLiteral() const { return kind == Kind::Literal; }
    static Ref of(Value value) {
        Ref ref;
        ref.kind = Kind::Literal;
        ref.type = value.type;
        ref.literal = value;
        return ref;
    }
    static Ref wire(int node, std::string pin, PinType type) {
        Ref ref;
        ref.kind = Kind::Wire;
        ref.node = node;
        ref.pin = std::move(pin);
        ref.type = type;
        return ref;
    }
};

/// What a name in the code stands for.
struct Binding {
    enum class Kind { Value, Variable, Transform, Body, Animator, Ignore, Taken };
    Kind kind = Kind::Value;
    Ref value;
    int node = 0;                       // Taken: the node that left by one of its ways
};

struct Cursor {
    int node = 0;
    std::string pin;
};

/// "1.5f" -> 1.5.
float numberOf(const std::string& written) {
    std::string digits;
    for (char c : written) {
        if (c == 'f' || c == 'F' || c == 'u' || c == 'U' || c == 'l' || c == 'L') {
            if (written.size() > 1 && (written[1] == 'x' || written[1] == 'X')) digits += c;     // a hex digit
            continue;
        }
        if (c != '\'') digits += c;
    }
    return std::strtof(digits.c_str(), nullptr);
}

bool isNumberType(const std::string& type) {
    std::string bare;
    for (char c : type) {
        if (c != ' ' && c != '&' && c != '*') bare += c;
    }
    if (bare.rfind("const", 0) == 0) bare = bare.substr(5);
    if (bare.rfind("static", 0) == 0) bare = bare.substr(6);
    return bare == "float" || bare == "int" || bare == "double" || bare == "bool" || bare == "unsigned" || bare == "uint32_t" || bare == "int32_t" ||
           bare == "size_t";
}

/// The name of a component the code asks the entity for: get<Transform>(self) -> "Transform".
std::string componentAsked(const Expr& expr) {
    if (expr.kind != Expr::Kind::Call || expr.parts.empty()) return {};
    const Expr& callee = *expr.parts[0];
    std::string method = callee.kind == Expr::Kind::Member || callee.kind == Expr::Kind::Name ? callee.text : std::string();
    size_t colon = method.rfind("::");
    if (colon != std::string::npos) method = method.substr(colon + 2);
    if (method != "get" && method != "tryGet" && method != "find" && method != "component" && method != "getComponent") return {};
    std::string asked = callee.templateArgument;
    size_t at = asked.rfind("::");
    return at == std::string::npos ? asked : asked.substr(at + 2);
}

/// The last part of a name: "script::Value::number" -> "number".
std::string lastPart(const std::string& name) {
    size_t colon = name.rfind("::");
    return colon == std::string::npos ? name : name.substr(colon + 2);
}

class Builder {
public:
    Graph graph;
    std::vector<std::string> notConverted;

    explicit Builder(const Component& component) : component_(component) {}

    void build() {
        // The timers in update: a number counted up by dt and checked. They become On Timer, not variables.
        if (const Method* update = find("update")) findTimers(*update);
        for (const Field& field : component_.fields) declareField(field);

        // On Start: the first values that are not plain values (worked out from others), then start.
        const Method* start = find("start");
        if (start || !startValues_.empty()) {
            beginEvent("OnStart", {});
            if (start) bindParameters(*start, {"", "self"});
            for (const Field* field : startValues_) {
                Ref value = valueOf(*field->value);
                if (value.isNone()) continue;
                int node = act("SetVariable", {{"name", Ref::of(Value::string(field->name))}, {"value", value}});
                if (Node* made = graph.find(node)) made->comment = "The first value of " + field->name;
            }
            if (start) body(*start->body, false);
            endEvent();
        }
        if (const Method* update = find("update")) {
            beginEvent("OnUpdate", {});
            bindParameters(*update, {"", "self", "dt"});
            body(*update->body, true);
            endEvent();
        }
        if (const Method* collision = find("onCollision")) {
            beginEvent("OnCollision", {});
            bindParameters(*collision, {"", "self", "other"});
            body(*collision->body, false);
            endEvent();
        }
        if (const Method* trigger = find("onTriggerEnter")) {
            beginEvent("OnTriggerEnter", {});
            bindParameters(*trigger, {"", "self", "other"});
            body(*trigger->body, false);
            endEvent();
        }
        // What has no box (a function of its own, a table, a pointer) is kept as it is written.
        for (const Method& method : component_.methods) {
            if (method.name != "start" && method.name != "update" && method.name != "onCollision" && method.name != "onTriggerEnter") {
                notConverted.push_back("line " + std::to_string(method.line) + ": " + method.name + "(...): a function of its own has no event to start it");
                graph.code.push_back(method.raw);
            }
        }
        for (const std::string& skipped : component_.skipped) notConverted.push_back(skipped);
        for (const std::string& member : component_.memberSources) {
            if (!member.empty()) graph.code.push_back(member);
        }
    }

private:
    const Method* find(const std::string& name) const {
        for (const Method& method : component_.methods) {
            if (method.name == name && method.body) return &method;
        }
        return nullptr;
    }

    void bindParameters(const Method& method, const std::vector<std::string>& roles) {
        for (size_t i = 0; i < roles.size() && i < method.parameters.size(); i++) {
            if (roles[i] == "self") selfName_ = method.parameters[i];
            else if (roles[i] == "dt") dtName_ = method.parameters[i];
            else if (roles[i] == "other") otherName_ = method.parameters[i];
        }
    }

    void declareField(const Field& field) {
        if (field.type.find("ScriptState") != std::string::npos || field.type.find("Call") != std::string::npos) {
            names_[field.name] = Binding{Binding::Kind::Ignore, {}, 0};
            return;
        }
        if (timers_.count(field.name)) return;
        // A number, a yes-no, a point or a text: one of the script's variables, with its first value and comment.
        PinType type = PinType::Flow;
        std::string written;
        for (char c : field.type) {
            if (c != ' ') written += c;
        }
        if (written == "bool") type = PinType::Bool;
        else if (written == "Vec3" || written == "float3" || written == "simd::float3") type = PinType::Vec3;
        else if (written == "std::string" || written == "string") type = PinType::String;
        else if (isNumberType(field.type)) type = PinType::Float;
        if (type != PinType::Flow) {
            variables_.insert(field.name);
            names_[field.name] = Binding{Binding::Kind::Variable, {}, 0};
            Variable declared;
            declared.name = field.name;
            declared.type = type;
            declared.comment = field.comment;
            declared.isShown = component_.fieldsShown.empty() ||
                               std::find(component_.fieldsShown.begin(), component_.fieldsShown.end(), field.name) != component_.fieldsShown.end();
            declared.value = Value::fromText(type, "").toText();
            if (field.value) {
                Graph saved = graph;
                Ref value = valueOf(*field.value);
                graph = std::move(saved);       // (only a plain value is wanted: any box it made is not)
                failed_ = false;
                if (!value.isNone() && value.isLiteral()) declared.value = converted(value.literal, type).toText();
                else startValues_.push_back(&field);
            }
            graph.variables.push_back(declared);
            return;
        }
        // Another kind of field with a value: the value, wherever the field is read.
        if (field.value) {
            Ref value = valueOf(*field.value);
            if (!value.isNone() && value.isLiteral()) {
                names_[field.name] = Binding{Binding::Kind::Value, value, 0};
                return;
            }
        }
        notConverted.push_back("line " + std::to_string(field.line) + ": " + field.type + " " + field.name +
                               ": a script's variables are numbers, yes-no, points and texts; this field is none of them");
        graph.code.push_back(field.raw);
    }

    // ---------------------------------------------------------------- events, and where things go on the board

    void beginEvent(const std::string& type, const std::map<std::string, std::string>& values) {
        float y = bottom_ > 0 ? bottom_ + 240.0f : 0.0f;
        Node& node = graph.addNode(type, 0, y);
        for (const auto& [pin, text] : values) node.values[pin] = text;
        eventNode_ = node.id;
        eventType_ = type;
        cursors_ = {{node.id, "then"}};
        x_ = 300;
        y_ = y;
        bottom_ = std::max(bottom_, y + 120);
    }

    /// An event nothing follows is not kept.
    void endEvent() {
        if (eventNode_ && !graph.linkFrom(eventNode_, "then")) graph.removeNode(eventNode_);
        eventNode_ = 0;
    }

    struct EventState {
        std::vector<Cursor> cursors;
        int node;
        std::string type;
        float x, y;
    };
    EventState saveEvent() const { return {cursors_, eventNode_, eventType_, x_, y_}; }
    void restoreEvent(const EventState& state) {
        cursors_ = state.cursors;
        eventNode_ = state.node;
        eventType_ = state.type;
        x_ = state.x;
        y_ = state.y;
    }

    const NodeDef* definition(const std::string& type) const { return NodeLibrary::instance().find(type); }

    Value converted(const Value& value, PinType to) {
        if (value.type == to) return value;
        switch (to) {
            case PinType::Vec3: {
                float x, y, z;
                value.asVector(x, y, z);
                return Value::vector(x, y, z);
            }
            case PinType::Bool: return Value::boolean(value.asBool());
            case PinType::String: return Value::string(value.asString());
            default: return Value::number(value.asFloat());
        }
    }

    bool setInput(int node, const std::string& pin, const Ref& ref) {
        if (ref.isNone()) return true;      // left as the node's own default
        Node* target = graph.find(node);
        const NodeDef* kind = target ? definition(target->type) : nullptr;
        const PinDef* input = kind ? kind->findInput(pin) : nullptr;
        if (!input) return false;
        if (ref.isLiteral()) {
            target->values[pin] = converted(ref.literal, input->type).toText();
            return true;
        }
        return graph.connect(ref.node, ref.pin, node, pin);
    }

    using Inputs = std::vector<std::pair<std::string, Ref>>;

    /// A box that gives a value. Where it goes: under the action it is for.
    Ref pure(const std::string& type, const Inputs& inputs, const std::string& output = {}) {
        const NodeDef* kind = definition(type);
        if (!kind || kind->outputs.empty()) return fail();
        Node& node = graph.addNode(type, x_ - 40.0f, y_ + 150.0f + float(valueSlot_++) * 92.0f);
        int id = node.id;
        bottom_ = std::max(bottom_, node.y + 80.0f);
        for (const auto& [pin, ref] : inputs) {
            if (!setInput(id, pin, ref)) return fail();
        }
        const PinDef* out = output.empty() ? &kind->outputs.front() : kind->findOutput(output);
        if (!out) return fail();
        return Ref::wire(id, out->name, out->type);
    }

    /// A box that does something, after what came before. Returns its id, or 0.
    int act(const std::string& type, const Inputs& inputs) {
        const NodeDef* kind = definition(type);
        if (!kind || kind->isEvent) {
            failed_ = true;
            return 0;
        }
        valueSlot_ = 0;
        Node& node = graph.addNode(type, x_, y_);
        int id = node.id;
        x_ += 280.0f;
        for (const auto& [pin, ref] : inputs) {
            if (!setInput(id, pin, ref)) {
                failed_ = true;
                return 0;
            }
        }
        for (const Cursor& cursor : cursors_) graph.connect(cursor.node, cursor.pin, id, "in");
        std::vector<std::string> exits;
        for (const PinDef& pin : kind->outputs) {
            if (pin.type == PinType::Flow) exits.push_back(pin.name);
        }
        cursors_.clear();
        if (exits.size() == 1) cursors_.push_back({id, exits.front()});
        return id;
    }

    Ref fail() {
        failed_ = true;
        return {};
    }

    /// The length of the frame, as the code names it.
    bool isDt(const Expr& expr) const {
        if (expr.kind == Expr::Kind::Name) return expr.text == dtName_;
        return expr.kind == Expr::Kind::Member && (expr.text == "delta" || expr.text == "dt") && expr.parts[0]->kind == Expr::Kind::Member && expr.parts[0]->text == "time";
    }

    // ---------------------------------------------------------------- numbers, points and yes-or-no

    Ref number(float value) { return Ref::of(Value::number(value)); }
    Ref text(const std::string& value) { return Ref::of(Value::string(value)); }

    bool isVector(const Ref& ref) const { return ref.type == PinType::Vec3; }

    Ref add(const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone()) return fail();
        if (isVector(a) || isVector(b)) {
            if (a.isLiteral() && b.isLiteral()) {
                float ax, ay, az, bx, by, bz;
                a.literal.asVector(ax, ay, az);
                b.literal.asVector(bx, by, bz);
                return Ref::of(Value::vector(ax + bx, ay + by, az + bz));
            }
            return pure("AddVectors", {{"a", a}, {"b", b}});
        }
        if (a.isLiteral() && b.isLiteral()) return number(a.literal.asFloat() + b.literal.asFloat());
        if (b.isLiteral() && b.literal.asFloat() == 0) return a;
        return pure("Add", {{"a", a}, {"b", b}});
    }

    Ref subtract(const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone()) return fail();
        if (isVector(a) || isVector(b)) return add(a, negate(b));
        if (a.isLiteral() && b.isLiteral()) return number(a.literal.asFloat() - b.literal.asFloat());
        return pure("Subtract", {{"a", a}, {"b", b}});
    }

    Ref multiply(const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone()) return fail();
        if (isVector(a) && isVector(b)) return fail();         // a product of two points has no box
        if (isVector(a) || isVector(b)) {
            const Ref& vector = isVector(a) ? a : b;
            const Ref& by = isVector(a) ? b : a;
            if (vector.isLiteral() && by.isLiteral()) {
                float x, y, z, s = by.literal.asFloat();
                vector.literal.asVector(x, y, z);
                return Ref::of(Value::vector(x * s, y * s, z * s));
            }
            return pure("ScaleVector", {{"vector", vector}, {"by", by}});
        }
        if (a.isLiteral() && b.isLiteral()) return number(a.literal.asFloat() * b.literal.asFloat());
        if (a.isLiteral() && a.literal.asFloat() == 1) return b;
        if (b.isLiteral() && b.literal.asFloat() == 1) return a;
        return pure("Multiply", {{"a", a}, {"b", b}});
    }

    Ref divide(const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone() || isVector(b)) return fail();
        if (isVector(a)) return multiply(a, divide(number(1), b));
        if (a.isLiteral() && b.isLiteral()) {
            float by = b.literal.asFloat();
            return number(by == 0 ? 0 : a.literal.asFloat() / by);
        }
        return pure("Divide", {{"a", a}, {"b", b}});
    }

    Ref negate(const Ref& a) {
        if (a.isNone()) return fail();
        if (a.isLiteral()) {
            if (isVector(a)) {
                float x, y, z;
                a.literal.asVector(x, y, z);
                return Ref::of(Value::vector(-x, -y, -z));
            }
            return number(-a.literal.asFloat());
        }
        return isVector(a) ? multiply(a, number(-1)) : pure("Negate", {{"value", a}});
    }

    Ref logic(const std::string& type, const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone()) return fail();
        if (a.isLiteral() && b.isLiteral()) {
            bool x = a.literal.asBool(), y = b.literal.asBool();
            return Ref::of(Value::boolean(type == "And" ? (x && y) : (x || y)));
        }
        return pure(type, {{"a", a}, {"b", b}});
    }

    Ref negation(const Ref& a) {
        if (a.isNone()) return fail();
        if (a.isLiteral()) return Ref::of(Value::boolean(!a.literal.asBool()));
        return pure("Not", {{"value", a}});
    }

    Ref compare(const std::string& sign, const Ref& a, const Ref& b) {
        if (a.isNone() || b.isNone()) return fail();
        if (sign == ">") return pure("Greater", {{"a", a}, {"b", b}});
        if (sign == "<") return pure("Less", {{"a", a}, {"b", b}});
        if (sign == ">=") return pure("GreaterOrEqual", {{"a", a}, {"b", b}});
        if (sign == "<=") return pure("LessOrEqual", {{"a", a}, {"b", b}});
        if (a.type == PinType::String || b.type == PinType::String) {
            Ref same = pure("TextEquals", {{"a", a}, {"b", b}});
            return sign == "==" ? same : negation(same);
        }
        return pure(sign == "==" ? "Equal" : "NotEqual", {{"a", a}, {"b", b}});
    }

    Ref select(const Ref& condition, const Ref& yes, const Ref& no) {
        if (condition.isNone() || yes.isNone() || no.isNone() || isVector(yes) || isVector(no)) return fail();
        return pure("Select", {{"condition", condition}, {"ifYes", yes}, {"ifNo", no}});
    }

    Ref vectorOf(const Ref& x, const Ref& y, const Ref& z) {
        if (x.isNone() || y.isNone() || z.isNone()) return fail();
        if (x.isLiteral() && y.isLiteral() && z.isLiteral()) return Ref::of(Value::vector(x.literal.asFloat(), y.literal.asFloat(), z.literal.asFloat()));
        return pure("Vector", {{"x", x}, {"y", y}, {"z", z}});
    }

    Ref component(const Ref& vector, const std::string& axis) {
        if (vector.isNone()) return fail();
        if (vector.isLiteral()) {
            float x, y, z;
            vector.literal.asVector(x, y, z);
            return number(axis == "x" ? x : axis == "y" ? y : z);
        }
        return pure("Split", {{"vector", vector}}, axis);
    }

    Ref dt() {
        if (eventType_ == "OnUpdate") return Ref::wire(eventNode_, "dt", PinType::Float);
        return pure("Time", {}, "dt");
    }

    /// A variable's value: a Get Variable box, of the variable's type (a point is a point).
    Ref variable(const std::string& name) {
        Ref got = pure("GetVariable", {{"name", text(name)}});
        if (const Variable* declared = graph.variable(name); declared && !got.isNone()) got.type = declared->type;
        return got;
    }

    /// Radians in the code, degrees in the boxes.
    Ref degrees(const Expr& angle) {
        if (angle.kind == Expr::Kind::Call && !angle.parts.empty() && lastPart(angle.parts[0]->text) == "radians" && angle.parts.size() == 2) {
            return valueOf(*angle.parts[1]);
        }
        return multiply(valueOf(angle), number(57.29578f));
    }

    std::string keyNamed(const Expr& expr) {
        if (expr.kind == Expr::Kind::Name && expr.text.rfind("Key::", 0) == 0) return expr.text.substr(5);
        if (expr.kind == Expr::Kind::Call && expr.parts.size() == 2 && lastPart(expr.parts[0]->text) == "keyByName" && expr.parts[1]->kind == Expr::Kind::Text) {
            return expr.parts[1]->text;
        }
        return {};
    }

    static bool isPointer(const Expr& expr) { return expr.kind == Expr::Kind::Name && expr.text.rfind("MouseButton::", 0) == 0; }

    /// What a name stands for, if it is one of the entity's components.
    const Binding* aliasOf(const Expr& expr) const {
        if (expr.kind != Expr::Kind::Name) return nullptr;
        auto found = names_.find(expr.text);
        if (found == names_.end()) return nullptr;
        Binding::Kind kind = found->second.kind;
        return kind == Binding::Kind::Transform || kind == Binding::Kind::Body || kind == Binding::Kind::Animator ? &found->second : nullptr;
    }
    bool isAlias(const Expr& expr, Binding::Kind kind) const {
        const Binding* alias = aliasOf(expr);
        if (alias) return alias->kind == kind;
        // world.get<Transform>(self)->position, without a name in between.
        std::string asked = componentAsked(expr);
        return (kind == Binding::Kind::Transform && asked == "Transform") || (kind == Binding::Kind::Body && asked == "RigidBody") ||
               (kind == Binding::Kind::Animator && asked == "Animator");
    }

    /// The inputs of a call made by code made from a graph: {{"degrees", value}, ...}.
    bool inputsOf(const Expr& list, Inputs& inputs) {
        if (list.kind != Expr::Kind::List) return false;
        for (const ExprPtr& item : list.parts) {
            if (item->kind != Expr::Kind::List || item->parts.size() != 2 || item->parts[0]->kind != Expr::Kind::Text) return false;
            const Expr& value = *item->parts[1];
            // script::Value{}: nothing typed there.
            if (value.kind == Expr::Kind::Call && value.parts.size() == 1 && lastPart(value.parts[0]->text) == "Value") continue;
            Ref ref = valueOf(value);
            if (ref.isNone()) return false;
            inputs.push_back({item->parts[0]->text, ref});
        }
        return true;
    }

    // ---------------------------------------------------------------- values

    Ref valueOf(const Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::Number: return number(numberOf(expr.text));
            case Expr::Kind::Bool: return Ref::of(Value::boolean(expr.text == "true"));
            case Expr::Kind::Text: return text(expr.text);
            case Expr::Kind::Name: return nameValue(expr);
            case Expr::Kind::Member: return memberValue(expr);
            case Expr::Kind::Call: return callValue(expr);
            case Expr::Kind::Unary: {
                if (expr.text == "!") return negation(valueOf(*expr.parts[0]));
                if (expr.text == "-") return negate(valueOf(*expr.parts[0]));
                if (expr.text == "+" || expr.text == "*") return valueOf(*expr.parts[0]);
                return fail();
            }
            case Expr::Kind::Binary: return binaryValue(expr);
            case Expr::Kind::Ternary: return select(valueOf(*expr.parts[0]), valueOf(*expr.parts[1]), valueOf(*expr.parts[2]));
            case Expr::Kind::List: {
                if (expr.parts.size() == 3) return vectorOf(valueOf(*expr.parts[0]), valueOf(*expr.parts[1]), valueOf(*expr.parts[2]));
                if (expr.parts.size() == 1) return valueOf(*expr.parts[0]);
                return fail();
            }
        }
        return fail();
    }

    Ref nameValue(const Expr& expr) {
        const std::string& name = expr.text;
        if (name == dtName_) return dt();
        if (name == otherName_ && (eventType_ == "OnCollision" || eventType_ == "OnTriggerEnter")) return Ref::wire(eventNode_, "other", PinType::String);
        if (name.rfind("Key::", 0) == 0) return text(name.substr(5));
        if (name == "M_PI" || name == "kPi" || name == "pi") return number(3.14159265f);
        auto found = names_.find(name);
        if (found != names_.end()) {
            if (found->second.kind == Binding::Kind::Variable) return variable(name);
            if (found->second.kind == Binding::Kind::Value) return found->second.value;
        }
        return fail();
    }

    Ref memberValue(const Expr& expr) {
        const Expr& object = *expr.parts[0];
        const std::string& member = expr.text;
        if (isAlias(object, Binding::Kind::Transform) && member == "position") return pure("GetPosition", {});
        if (isAlias(object, Binding::Kind::Body) && member == "velocity") return pure("GetVelocity", {});
        if (member == "x" || member == "y" || member == "z") return component(valueOf(object), member);
        // world.time.elapsed, engine.time.seconds: the time since the game started.
        if (object.kind == Expr::Kind::Member && object.text == "time") {
            if (member == "elapsed" || member == "seconds" || member == "now") return pure("Time", {}, "seconds");
            if (member == "delta" || member == "dt" || member == "unscaledDelta") return dt();
        }
        return fail();
    }

    Ref binaryValue(const Expr& expr) {
        const std::string& sign = expr.text;
        const Expr& left = *expr.parts[0];
        const Expr& right = *expr.parts[1];
        // The other thing's name, compared with a name: Other Is.
        if (sign == "==" || sign == "!=") {
            const Expr* name = right.kind == Expr::Kind::Text ? &right : left.kind == Expr::Kind::Text ? &left : nullptr;
            const Expr* other = name == &right ? &left : &right;
            if (name && isOtherName(*other)) {
                Ref is = pure("OtherIs", {{"name", text(name->text)}});
                return sign == "==" ? is : negation(is);
            }
        }
        if (sign == "&&") return logic("And", valueOf(left), valueOf(right));
        if (sign == "||") return logic("Or", valueOf(left), valueOf(right));
        Ref a = valueOf(left);
        if (a.isNone()) return fail();
        Ref b = valueOf(right);
        if (b.isNone()) return fail();
        if (sign == "+") return add(a, b);
        if (sign == "-") return subtract(a, b);
        if (sign == "*") return multiply(a, b);
        if (sign == "/") return divide(a, b);
        if (sign == ">" || sign == "<" || sign == ">=" || sign == "<=" || sign == "==" || sign == "!=") return compare(sign, a, b);
        return fail();
    }

    bool isOtherName(const Expr& expr) const {
        if (expr.kind == Expr::Kind::Name && expr.text == otherName_) return true;
        if (expr.kind == Expr::Kind::Call && !expr.parts.empty() && expr.parts[0]->kind == Expr::Kind::Member && expr.parts[0]->text == "otherName") return true;
        if (expr.kind == Expr::Kind::Member && expr.text == "asString") return isOtherName(*expr.parts[0]);
        if (expr.kind == Expr::Kind::Call && !expr.parts.empty() && expr.parts[0]->kind == Expr::Kind::Member && expr.parts[0]->text == "asString") {
            return isOtherName(*expr.parts[0]->parts[0]);
        }
        return false;
    }

    Ref callValue(const Expr& expr) {
        const Expr& callee = *expr.parts[0];
        std::vector<const Expr*> arguments;
        for (size_t i = 1; i < expr.parts.size(); i++) arguments.push_back(expr.parts[i].get());
        auto argument = [&](size_t i) { return i < arguments.size() ? valueOf(*arguments[i]) : fail(); };

        if (callee.kind == Expr::Kind::Member) {
            const Expr& object = *callee.parts[0];
            const std::string& method = callee.text;
            // From code made from a graph: call.value("Type", {{"pin", value}}, "output").
            if (method == "value" && object.kind == Expr::Kind::Name && object.text == "call" && !arguments.empty() && arguments[0]->kind == Expr::Kind::Text) {
                Inputs inputs;
                if (arguments.size() > 1 && !inputsOf(*arguments[1], inputs)) return fail();
                std::string output = arguments.size() > 2 && arguments[2]->kind == Expr::Kind::Text ? arguments[2]->text : std::string();
                return pure(arguments[0]->text, inputs, output);
            }
            if (method == "otherName" && (eventType_ == "OnCollision" || eventType_ == "OnTriggerEnter")) return Ref::wire(eventNode_, "other", PinType::String);
            if (method == "asBool" || method == "asFloat" || method == "asString" || method == "asVector") return valueOf(object);
            if (isAlias(object, Binding::Kind::Transform)) {
                if (method == "forward") return pure("GetForward", {});
                if (method == "right" || method == "up") return fail();
            }
            if (method == "isDown" && arguments.size() == 1) {
                if (isPointer(*arguments[0])) return pure("PointerIsDown", {});
                // By name, it is an action of the Input settings; by Key::, a key.
                if (arguments[0]->kind == Expr::Kind::Text) return pure("ActionIsDown", {{"action", text(arguments[0]->text)}});
                std::string key = keyNamed(*arguments[0]);
                if (!key.empty()) return pure("KeyIsDown", {{"key", text(key)}});
            }
            if (method == "axis" && arguments.size() == 1 && arguments[0]->kind == Expr::Kind::Text) return pure("Axis", {{"axis", text(arguments[0]->text)}});
            if (method == "isGrounded") return pure("IsGrounded", {});
            if (method == "value" && arguments.size() == 1 && arguments[0]->kind == Expr::Kind::Text) return pure("GetValue", {{"name", text(arguments[0]->text)}});
            if (isAlias(object, Binding::Kind::Animator)) {
                if ((method == "parameter" || method == "getFloat" || method == "getParameter") && arguments.size() == 1) {
                    return pure("GetAnimatorFloat", {{"parameter", argument(0)}});
                }
                if (method == "isPlayingAnimation" && arguments.size() == 1) return pure("IsAnimationPlaying", {{"name", argument(0)}});
            }
            return fail();
        }

        const std::string name = lastPart(callee.text);
        const std::string& full = callee.text;
        // script::Value::number(3), Value::vector(0, 1, 0), ...: the value itself.
        if (full.find("Value::") != std::string::npos) {
            if ((name == "number" || name == "boolean" || name == "string") && arguments.size() == 1) return argument(0);
            if (name == "vector" && arguments.size() == 3) return vectorOf(argument(0), argument(1), argument(2));
            return fail();
        }
        if (name == "Vec3" || name == "float3" || name == "simd_make_float3" || name == "vec3") {
            if (arguments.size() == 3) return vectorOf(argument(0), argument(1), argument(2));
            if (arguments.size() == 1) {
                Ref same = argument(0);
                return vectorOf(same, same, same);
            }
            if (arguments.empty()) return Ref::of(Value::vector(0, 0, 0));
            return fail();
        }
        if (name == "static_cast" || name == "float" || name == "double" || name == "int" || name == "bool" || name == "asValue" || name == "asVec3") return argument(0);
        if (name == "keyByName" && arguments.size() == 1) return argument(0);
        if ((name == "sin" || name == "sinf") && arguments.size() == 1) return pure("Sin", {{"degrees", degrees(*arguments[0])}});
        if ((name == "cos" || name == "cosf") && arguments.size() == 1) return pure("Cos", {{"degrees", degrees(*arguments[0])}});
        if ((name == "floor" || name == "floorf") && arguments.size() == 1) return pure("Floor", {{"value", argument(0)}});
        if (name == "radians" && arguments.size() == 1) return multiply(argument(0), number(0.01745329f));
        if (name == "degrees" && arguments.size() == 1) return multiply(argument(0), number(57.29578f));
        if (name == "distance" && arguments.size() == 2) return pure("Distance", {{"a", argument(0)}, {"b", argument(1)}});
        if (name == "length" && arguments.size() == 1) return pure("Length", {{"vector", argument(0)}});
        if (name == "dot" && arguments.size() == 2) return pure("Dot", {{"a", argument(0)}, {"b", argument(1)}});
        if (name == "cross" && arguments.size() == 2) return pure("Cross", {{"a", argument(0)}, {"b", argument(1)}});
        if ((name == "lerp" || name == "mix") && arguments.size() == 3) {
            Ref a = argument(0), b = argument(1), t = argument(2);
            if (a.isNone() || b.isNone() || t.isNone()) return fail();
            return isVector(a) || isVector(b) ? pure("LerpVectors", {{"a", a}, {"b", b}, {"t", t}}) : pure("Lerp", {{"a", a}, {"b", b}, {"t", t}});
        }
        if (name == "normalize" && arguments.size() == 1) {
            const Expr& inside = *arguments[0];
            if (inside.kind == Expr::Kind::Binary && inside.text == "-") {
                return pure("Direction", {{"from", valueOf(*inside.parts[1])}, {"to", valueOf(*inside.parts[0])}});
            }
            return pure("Normalize", {{"vector", argument(0)}});
        }
        if ((name == "random" || name == "randomRange" || name == "range" || name == "randomBetween") && arguments.size() == 2) {
            return pure("Random", {{"from", argument(0)}, {"to", argument(1)}});
        }
        if ((name == "abs" || name == "fabs" || name == "fabsf") && arguments.size() == 1) return pure("Abs", {{"value", argument(0)}});
        if ((name == "max" || name == "fmax" || name == "fmaxf") && arguments.size() == 2) return pure("Max", {{"a", argument(0)}, {"b", argument(1)}});
        if ((name == "min" || name == "fmin" || name == "fminf") && arguments.size() == 2) return pure("Min", {{"a", argument(0)}, {"b", argument(1)}});
        if (name == "clamp" && arguments.size() == 3) return pure("Clamp", {{"value", argument(0)}, {"least", argument(1)}, {"most", argument(2)}});
        if ((name == "saturate") && arguments.size() == 1) return pure("Clamp", {{"value", argument(0)}, {"least", number(0)}, {"most", number(1)}});
        if ((name == "sqrt" || name == "sqrtf") && arguments.size() == 1) return pure("Sqrt", {{"value", argument(0)}});
        if ((name == "pow" || name == "powf") && arguments.size() == 2) return pure("Power", {{"a", argument(0)}, {"b", argument(1)}});
        if ((name == "round" || name == "roundf") && arguments.size() == 1) return pure("Round", {{"value", argument(0)}});
        if ((name == "fmod" || name == "fmodf") && arguments.size() == 2) return pure("Modulo", {{"a", argument(0)}, {"b", argument(1)}});
        return fail();
    }

    // ---------------------------------------------------------------- statements

    /// One statement; if it has no boxes, everything it made is taken back and it is named.
    void run(const Stmt& statement) {
        int firstNew = graph.nextId;
        if (statement.kind == Stmt::Kind::Other) {
            notConverted.push_back("line " + std::to_string(statement.line) + ": " + statement.source);
            keepAsCode(statement);
            return;
        }
        Graph savedGraph = graph;
        std::vector<Cursor> savedCursors = cursors_;
        std::map<std::string, Binding> savedNames = names_;
        std::set<std::string> savedVariables = variables_;
        float savedX = x_, savedY = y_, savedBottom = bottom_;
        size_t reported = notConverted.size();
        bool outer = failed_;
        failed_ = false;
        perform(statement);
        if (failed_) {
            graph = std::move(savedGraph);
            cursors_ = std::move(savedCursors);
            names_ = std::move(savedNames);
            variables_ = std::move(savedVariables);
            x_ = savedX;
            y_ = savedY;
            bottom_ = savedBottom;
            notConverted.resize(reported);
            notConverted.push_back("line " + std::to_string(statement.line) + ": " + statement.source);
            failed_ = false;
            keepAsCode(statement);
            failed_ = outer;
            return;
        }
        failed_ = outer;
        // Its comment goes on the first box it made that does something (or on the first of all).
        if (!statement.comment.empty() && statement.kind != Stmt::Kind::Block) {
            Node* chosen = nullptr;
            for (Node& node : graph.nodes) {
                if (node.id < firstNew || !node.comment.empty()) continue;
                const NodeDef* kind = definition(node.type);
                bool acts = kind && kind->findInput("in");
                if (!chosen || (acts && !(definition(chosen->type) && definition(chosen->type)->findInput("in")))) chosen = &node;
                if (acts) break;
            }
            if (chosen) chosen->comment = statement.comment;
        }
    }

    /// A statement no box says: a C++ box with it as it is written, in its place in the flow.
    void keepAsCode(const Stmt& statement) {
        std::string code = statement.raw.empty() ? statement.source : statement.raw;
        if (code.empty()) return;
        int node = act("Code", {{"code", text(code)}});
        if (Node* made = graph.find(node)) made->comment = statement.comment;
    }

    void perform(const Stmt& statement) {
        switch (statement.kind) {
            case Stmt::Kind::Block:
                for (const StmtPtr& inside : statement.body) run(*inside);
                return;
            case Stmt::Kind::Return: cursors_.clear(); return;           // the flow ends here
            case Stmt::Kind::Declare: declare(statement); return;
            case Stmt::Kind::If: branch(statement); return;
            case Stmt::Kind::Assign: assign(statement); return;
            case Stmt::Kind::Do: perform(*statement.value); return;
            case Stmt::Kind::While: loop(statement); return;
            case Stmt::Kind::For: countedLoop(statement); return;
            case Stmt::Kind::Other: failed_ = true; return;
        }
    }

    bool bindComponent(const std::string& name, const Expr* value) {
        if (!value) return false;
        std::string asked = componentAsked(*value);
        if (asked == "Transform") names_[name] = Binding{Binding::Kind::Transform, {}, 0};
        else if (asked == "RigidBody") names_[name] = Binding{Binding::Kind::Body, {}, 0};
        else if (asked == "Animator") names_[name] = Binding{Binding::Kind::Animator, {}, 0};
        else return false;
        return true;
    }

    void declare(const Stmt& statement) {
        if (statement.type.find("Call") != std::string::npos || statement.type.find("ScriptState") != std::string::npos) {
            names_[statement.name] = Binding{Binding::Kind::Ignore, {}, 0};
            return;
        }
        if (bindComponent(statement.name, statement.value.get())) return;
        // std::string taken4 = call("Type", ...): a box that leaves by one of several ways.
        if (statement.value && isActionCall(*statement.value)) {
            int node = actionCall(*statement.value);
            if (!node) return;
            names_[statement.name] = Binding{Binding::Kind::Taken, {}, node};
            return;
        }
        Ref value = statement.value ? valueOf(*statement.value) : number(0);
        if (value.isNone()) {
            failed_ = true;
            return;
        }
        bool isNumber = isNumberType(statement.type) || (statement.type.find("auto") != std::string::npos && !isVector(value) && value.type != PinType::String);
        if (isNumber) {
            variables_.insert(statement.name);
            names_[statement.name] = Binding{Binding::Kind::Variable, {}, 0};
            // A local of the code: a variable of the script too, not shown in the Inspector.
            if (!graph.variable(statement.name)) {
                Variable local;
                local.name = statement.name;
                local.value = "0";
                local.comment = statement.comment;
                local.isShown = false;
                graph.variables.push_back(local);
            }
            act("SetVariable", {{"name", text(statement.name)}, {"value", value}});
            return;
        }
        names_[statement.name] = Binding{Binding::Kind::Value, value, 0};
    }

    /// while (condition) body: a While box, its body after "do", the rest after "done".
    void loop(const Stmt& statement, const Stmt* step = nullptr) {
        Ref test = statement.condition ? valueOf(*statement.condition) : Ref::of(Value::boolean(true));
        if (test.isNone()) {
            failed_ = true;
            return;
        }
        int node = act("While", {{"condition", test}});
        if (!node) return;
        const float loopY = y_;
        cursors_ = {{node, "do"}};
        run(*statement.body[0]);
        if (step) run(*step);
        cursors_ = {{node, "done"}};
        y_ = loopY;
    }

    /// for (float i = a; i < b; i += s) body: a For box, i its index. Any other for: its first
    /// statement, then a While with the step at the end of each turn.
    void countedLoop(const Stmt& statement) {
        const Stmt* init = statement.init.get();
        const Expr* condition = statement.condition.get();
        const Stmt* step = statement.step.get();
        bool isCounted = init && init->kind == Stmt::Kind::Declare && init->value && condition && step && condition->kind == Expr::Kind::Binary &&
                         (condition->text == "<" || condition->text == ">") && condition->parts[0]->kind == Expr::Kind::Name &&
                         condition->parts[0]->text == init->name && step->kind == Stmt::Kind::Assign && step->target->kind == Expr::Kind::Name &&
                         step->target->text == init->name && (step->op == "++" || step->op == "--" || step->op == "+=" || step->op == "-=");
        if (isCounted) {
            Ref from = valueOf(*init->value), to = valueOf(*condition->parts[1]);
            Ref by = step->op == "++" ? number(1) : step->op == "--" ? number(-1) : step->op == "+=" ? valueOf(*step->value) : negate(valueOf(*step->value));
            if (from.isNone() || to.isNone() || by.isNone()) {
                failed_ = true;
                return;
            }
            int node = act("For", {{"from", from}, {"to", to}, {"step", by}});
            if (!node) return;
            Binding before = names_.count(init->name) ? names_[init->name] : Binding{Binding::Kind::Ignore, {}, 0};
            bool had = names_.count(init->name) > 0;
            names_[init->name] = Binding{Binding::Kind::Value, Ref::wire(node, "index", PinType::Float), 0};
            const float loopY = y_;
            cursors_ = {{node, "do"}};
            run(*statement.body[0]);
            cursors_ = {{node, "done"}};
            y_ = loopY;
            if (had) names_[init->name] = before;
            else names_.erase(init->name);
            return;
        }
        if (init) run(*init);
        loop(statement, step);
    }

    void branch(const Stmt& statement) {
        // if (Transform* transform = world.get<Transform>(self)) { ... }: the component is there.
        if (!statement.name.empty()) {
            if (!bindComponent(statement.name, statement.value.get())) {
                failed_ = true;
                return;
            }
            run(*statement.body[0]);
            return;
        }
        const Expr& condition = *statement.condition;
        // if (!transform) return;: the component is there in a visual script, which does nothing without it.
        if (condition.kind == Expr::Kind::Unary && condition.text == "!" && condition.parts[0]->kind == Expr::Kind::Name && statement.body.size() == 1 &&
            statement.body[0]->kind == Stmt::Kind::Return) {
            auto found = names_.find(condition.parts[0]->text);
            if (found != names_.end() && (found->second.kind == Binding::Kind::Transform || found->second.kind == Binding::Kind::Body ||
                                          found->second.kind == Binding::Kind::Animator)) {
                return;
            }
        }
        // if (taken4 == "then") { ... }: the way out of the box it was taken from.
        if (condition.kind == Expr::Kind::Binary && condition.text == "==" && condition.parts[0]->kind == Expr::Kind::Name &&
            condition.parts[1]->kind == Expr::Kind::Text) {
            auto found = names_.find(condition.parts[0]->text);
            if (found != names_.end() && found->second.kind == Binding::Kind::Taken) {
                std::vector<Cursor> before = cursors_;
                cursors_ = {{found->second.node, condition.parts[1]->text}};
                run(*statement.body[0]);
                for (const Cursor& end : cursors_) before.push_back(end);
                cursors_ = before;
                return;
            }
        }
        Ref test = valueOf(condition);
        if (test.isNone()) {
            failed_ = true;
            return;
        }
        int node = act("Branch", {{"condition", test}});
        if (!node) return;
        const float branchX = x_, branchY = y_;
        cursors_ = {{node, "true"}};
        run(*statement.body[0]);
        std::vector<Cursor> ends = cursors_;
        float endX = x_;
        cursors_ = {{node, "false"}};
        if (statement.body.size() > 1) {
            x_ = branchX;
            y_ = bottom_ + 170.0f;
            run(*statement.body[1]);
        }
        for (const Cursor& end : cursors_) ends.push_back(end);
        cursors_ = ends;
        x_ = std::max(endX, x_);
        y_ = branchY;
    }

    static bool isActionCall(const Expr& expr) {
        return expr.kind == Expr::Kind::Call && expr.parts.size() >= 2 && expr.parts[0]->kind == Expr::Kind::Name && expr.parts[0]->text == "call" &&
               expr.parts[1]->kind == Expr::Kind::Text;
    }

    /// call("Rotate", {{"degrees", ...}}): the box of that name.
    int actionCall(const Expr& expr) {
        Inputs inputs;
        if (expr.parts.size() > 2 && !inputsOf(*expr.parts[2], inputs)) {
            failed_ = true;
            return 0;
        }
        return act(expr.parts[1]->text, inputs);
    }

    void assign(const Stmt& statement) {
        const Expr& target = *statement.target;
        const std::string& op = statement.op;
        auto right = [&]() { return op == "++" || op == "--" ? number(1) : valueOf(*statement.value); };
        auto combined = [&](const Ref& old, const Ref& by) {
            if (op == "=") return by;
            if (op == "+=" || op == "++") return add(old, by);
            if (op == "-=" || op == "--") return subtract(old, by);
            if (op == "*=") return multiply(old, by);
            if (op == "/=") return divide(old, by);
            return fail();
        };

        if (target.kind == Expr::Kind::Name) {
            auto found = names_.find(target.text);
            if (found == names_.end()) {
                failed_ = true;
                return;
            }
            if (found->second.kind == Binding::Kind::Variable) {
                Ref by = right();
                // score += 1: Change Variable, as a graph says it.
                if ((op == "+=" || op == "-=" || op == "++" || op == "--") && !by.isNone() && !isVector(by) && by.type != PinType::String) {
                    act("AddToVariable", {{"name", text(target.text)}, {"amount", op == "+=" || op == "++" ? by : negate(by)}});
                    return;
                }
                Ref value = op == "=" ? by : combined(variable(target.text), by);
                if (value.isNone()) return;
                act("SetVariable", {{"name", text(target.text)}, {"value", value}});
                return;
            }
            if (found->second.kind == Binding::Kind::Value) {
                // A point kept in the code, not in a variable: the new one is what is used from here on.
                Ref value = combined(found->second.value, right());
                if (!value.isNone()) found->second.value = value;
                return;
            }
            failed_ = true;
            return;
        }
        if (target.kind != Expr::Kind::Member) {
            failed_ = true;
            return;
        }
        const Expr& object = *target.parts[0];
        const std::string& member = target.text;

        if (isAlias(object, Binding::Kind::Transform)) {
            if (member == "position") {
                if (op == "+=" || op == "-=") {
                    // position += offset * dt: so far a second.
                    const Expr& moved = *statement.value;
                    if (moved.kind == Expr::Kind::Binary && moved.text == "*" && isDt(*moved.parts[1])) {
                        Ref by = valueOf(*moved.parts[0]);
                        act("Translate", {{"offset", op == "+=" ? by : negate(by)}, {"perSecond", Ref::of(Value::boolean(true))}, {"local", Ref::of(Value::boolean(false))}});
                        return;
                    }
                    Ref offset = op == "+=" ? right() : negate(right());
                    act("Translate", {{"offset", offset}, {"perSecond", Ref::of(Value::boolean(false))}, {"local", Ref::of(Value::boolean(false))}});
                } else if (op == "=") {
                    act("SetPosition", {{"position", right()}});
                } else {
                    failed_ = true;
                }
                return;
            }
            if (member == "scale" && op == "=") {
                act("SetScale", {{"scale", right()}});
                return;
            }
            if (member == "rotation") {
                // rotation = rotation * axisAngle(axis, angle), or rotation *= axisAngle(...): a turn.
                const Expr* turn = nullptr;
                if (op == "*=") {
                    turn = statement.value.get();
                } else if (op == "=" && statement.value->kind == Expr::Kind::Binary && statement.value->text == "*") {
                    const Expr& a = *statement.value->parts[0];
                    const Expr& b = *statement.value->parts[1];
                    if (a.kind == Expr::Kind::Member && a.text == "rotation") turn = &b;
                    else if (b.kind == Expr::Kind::Member && b.text == "rotation") turn = &a;
                }
                if (turn && turn->kind == Expr::Kind::Call && turn->parts.size() == 3 && lastPart(turn->parts[0]->text) == "axisAngle") {
                    // radians(degrees * dt): so many degrees a second.
                    const Expr* angle = turn->parts[2].get();
                    bool perSecond = false;
                    if (angle->kind == Expr::Kind::Call && angle->parts.size() == 2 && lastPart(angle->parts[0]->text) == "radians") {
                        const Expr& inside = *angle->parts[1];
                        if (inside.kind == Expr::Kind::Binary && inside.text == "*" && isDt(*inside.parts[1])) {
                            perSecond = true;
                            act("Rotate", {{"axis", valueOf(*turn->parts[1])}, {"degrees", valueOf(*inside.parts[0])}, {"perSecond", Ref::of(Value::boolean(true))}});
                            return;
                        }
                    }
                    act("Rotate", {{"axis", valueOf(*turn->parts[1])}, {"degrees", degrees(*turn->parts[2])}, {"perSecond", Ref::of(Value::boolean(perSecond))}});
                    return;
                }
                failed_ = true;
                return;
            }
        }
        // position.y += 2: a move along one axis; position.y = 2: the point, with that axis changed.
        if ((member == "x" || member == "y" || member == "z") && object.kind == Expr::Kind::Member) {
            const Expr& owner = *object.parts[0];
            bool isPosition = object.text == "position" && isAlias(owner, Binding::Kind::Transform);
            bool isVelocity = object.text == "velocity" && isAlias(owner, Binding::Kind::Body);
            if (isPosition || isVelocity) {
                Ref by = right();
                if (by.isNone()) return;
                auto along = [&](const Ref& amount) {
                    return vectorOf(member == "x" ? amount : number(0), member == "y" ? amount : number(0), member == "z" ? amount : number(0));
                };
                if (op == "+=" || op == "-=" || op == "++" || op == "--") {
                    Ref amount = op == "+=" || op == "++" ? by : negate(by);
                    if (isPosition) act("Translate", {{"offset", along(amount)}, {"perSecond", Ref::of(Value::boolean(false))}, {"local", Ref::of(Value::boolean(false))}});
                    else act("AddVelocity", {{"velocity", along(amount)}});
                    return;
                }
                if (op == "=") {
                    Ref now = pure(isPosition ? "GetPosition" : "GetVelocity", {});
                    Ref point = vectorOf(member == "x" ? by : component(now, "x"), member == "y" ? by : component(now, "y"), member == "z" ? by : component(now, "z"));
                    act(isPosition ? "SetPosition" : "SetVelocity", {{isPosition ? "position" : "velocity", point}});
                    return;
                }
            }
        }
        if (isAlias(object, Binding::Kind::Body) && member == "velocity") {
            if (op == "=") act("SetVelocity", {{"velocity", right()}});
            else if (op == "+=") act("AddVelocity", {{"velocity", right()}});
            else if (op == "-=") act("AddVelocity", {{"velocity", negate(right())}});
            else failed_ = true;
            return;
        }
        if ((member == "visible" || member == "isVisible") && op == "=") {
            act("SetVisible", {{"visible", right()}});
            return;
        }
        failed_ = true;
    }

    /// A call on its own line: something done.
    void perform(const Expr& expr) {
        if (expr.kind != Expr::Kind::Call) {
            failed_ = true;
            return;
        }
        if (isActionCall(expr)) {
            actionCall(expr);
            return;
        }
        const Expr& callee = *expr.parts[0];
        std::vector<const Expr*> arguments;
        for (size_t i = 1; i < expr.parts.size(); i++) arguments.push_back(expr.parts[i].get());
        auto argument = [&](size_t i) { return i < arguments.size() ? valueOf(*arguments[i]) : fail(); };
        auto isSelf = [&](size_t i) { return i < arguments.size() && arguments[i]->kind == Expr::Kind::Name && arguments[i]->text == selfName_; };

        if (callee.kind == Expr::Kind::Member) {
            const Expr& object = *callee.parts[0];
            const std::string& method = callee.text;
            if (isAlias(object, Binding::Kind::Animator)) {
                if (method == "play" && arguments.size() == 1) act("PlayAnimation", {{"name", argument(0)}});
                else if (method == "stop" && arguments.empty()) act("StopAnimation", {});
                else if (method == "pause" && arguments.empty()) act("PauseAnimation", {});
                else if (method == "resume" && arguments.empty()) act("ResumeAnimation", {});
                else if (method == "setFloat" && arguments.size() == 2) act("SetAnimatorFloat", {{"parameter", argument(0)}, {"value", argument(1)}});
                else if (method == "setBool" && arguments.size() == 2) act("SetAnimatorBool", {{"parameter", argument(0)}, {"value", argument(1)}});
                else if (method == "setTrigger" && arguments.size() == 1) act("SetAnimatorTrigger", {{"parameter", argument(0)}});
                else if (method == "crossFade" && !arguments.empty()) act("CrossFade", {{"state", argument(0)}, {"seconds", arguments.size() > 1 ? argument(1) : number(0.2f)}});
                else if (method == "playState" && arguments.size() == 1) act("CrossFade", {{"state", argument(0)}, {"seconds", number(0)}});
                else failed_ = true;
                return;
            }
            if (method == "destroy" && isSelf(0)) {
                act("DestroySelf", {});
                return;
            }
            if (method == "destroy" && arguments.size() == 1 && arguments[0]->kind == Expr::Kind::Name && arguments[0]->text == otherName_) {
                act("Destroy", {{"on", text("other")}});
                return;
            }
            if (method == "send" && !arguments.empty()) {
                Inputs inputs = {{"message", argument(0)}};
                if (arguments.size() > 1) {
                    // To an entity found by its name; to nobody in particular: everyone.
                    const Expr& to = *arguments[1];
                    if (to.kind == Expr::Kind::Call && to.parts.size() == 2 && to.parts[0]->kind == Expr::Kind::Member && to.parts[0]->text == "find" &&
                        to.parts[1]->kind == Expr::Kind::Text) {
                        inputs.push_back({"to", text(to.parts[1]->text)});
                    }
                }
                if (arguments.size() > 2) inputs.push_back({"number", argument(2)});
                if (arguments.size() > 3) inputs.push_back({"text", argument(3)});
                act("SendMessage", inputs);
                return;
            }
            if (method == "loadScene" && arguments.size() == 1) {
                act("LoadScene", {{"scene", argument(0)}});
                return;
            }
            if ((method == "reloadScene" || method == "restartScene") && arguments.empty()) {
                act("ReloadScene", {});
                return;
            }
            if (method == "setValue" && arguments.size() == 2) {
                act("SetValue", {{"name", argument(0)}, {"value", argument(1)}});
                return;
            }
            if (method == "playSound" && !arguments.empty()) {
                act("PlaySound", {{"sound", argument(0)}, {"volume", arguments.size() > 1 ? argument(1) : number(1)}});
                return;
            }
            failed_ = true;
            return;
        }
        std::string name = lastPart(callee.text);
        if ((name == "logInfo" || name == "log" || name == "printf" || name == "puts" || name == "logWarning") && !arguments.empty() &&
            arguments[0]->kind == Expr::Kind::Text) {
            // (printf's %% is one %.)
            std::string message = arguments[0]->text;
            for (size_t at = message.find("%%"); at != std::string::npos; at = message.find("%%", at + 1)) message.erase(at, 1);
            act("Log", {{"message", text(message)}});
            return;
        }
        failed_ = true;
    }

    // ---------------------------------------------------------------- the body of an event

    /// A key or the mouse button pressed or let go: the event it is, if it is one.
    bool inputEvent(const Expr& condition, std::string& type, std::map<std::string, std::string>& values) {
        if (condition.kind != Expr::Kind::Call || condition.parts.size() != 2 || condition.parts[0]->kind != Expr::Kind::Member) return false;
        const std::string& method = condition.parts[0]->text;
        if (method != "wasPressed" && method != "wasReleased") return false;
        const Expr& what = *condition.parts[1];
        if (isPointer(what)) {
            if (method != "wasPressed") return false;
            type = "OnPointerPressed";
            return true;
        }
        if (what.kind == Expr::Kind::Text && method == "wasPressed") {
            type = "OnActionPressed";
            values["action"] = what.text;
            return true;
        }
        std::string key = keyNamed(what);
        if (key.empty()) return false;
        type = method == "wasPressed" ? "OnKeyPressed" : "OnKeyReleased";
        values["key"] = key;
        return true;
    }

    static std::vector<const Stmt*> statementsOf(const Stmt& statement) {
        std::vector<const Stmt*> out;
        if (statement.kind == Stmt::Kind::Block) {
            for (const StmtPtr& inside : statement.body) out.push_back(inside.get());
        } else {
            out.push_back(&statement);
        }
        return out;
    }

    /// t += dt; if (t >= 2) { t -= 2; ... }: the seconds, if these two statements are a timer.
    bool timerAt(const std::vector<const Stmt*>& statements, size_t i, std::string& name, float& seconds) const {
        if (i + 1 >= statements.size()) return false;
        const Stmt& count = *statements[i];
        const Stmt& check = *statements[i + 1];
        if (count.kind != Stmt::Kind::Assign || count.op != "+=" || count.target->kind != Expr::Kind::Name || count.value->kind != Expr::Kind::Name ||
            count.value->text != dtName_) {
            return false;
        }
        if (check.kind != Stmt::Kind::If || !check.condition || check.body.size() != 1) return false;
        const Expr& test = *check.condition;
        if (test.kind != Expr::Kind::Binary || (test.text != ">=" && test.text != ">") || test.parts[0]->kind != Expr::Kind::Name ||
            test.parts[0]->text != count.target->text || test.parts[1]->kind != Expr::Kind::Number) {
            return false;
        }
        name = count.target->text;
        seconds = numberOf(test.parts[1]->text);
        return seconds > 0;
    }

    void findTimers(const Method& update) {
        dtName_ = update.parameters.size() > 2 ? update.parameters[2] : "dt";
        std::vector<const Stmt*> statements = statementsOf(*update.body);
        for (size_t i = 0; i < statements.size(); i++) {
            std::string name;
            float seconds = 0;
            if (timerAt(statements, i, name, seconds)) timers_.insert(name);
        }
    }

    void body(const Stmt& statement, bool isUpdate) {
        std::vector<const Stmt*> statements = statementsOf(statement);
        for (size_t i = 0; i < statements.size(); i++) {
            const Stmt& s = *statements[i];
            if (isUpdate) {
                // The component asked for around everything: what is inside is still the top of update.
                if (s.kind == Stmt::Kind::If && !s.name.empty() && bindComponent(s.name, s.value.get())) {
                    body(*s.body[0], true);
                    continue;
                }
                std::string type;
                std::map<std::string, std::string> values;
                if (s.kind == Stmt::Kind::If && s.name.empty() && s.body.size() == 1 && inputEvent(*s.condition, type, values)) {
                    sideEvent(type, values, *s.body[0]);
                    continue;
                }
                std::string timer;
                float seconds = 0;
                if (timerAt(statements, i, timer, seconds)) {
                    // Inside: the first statement takes the seconds off again; the timer does that itself.
                    std::vector<const Stmt*> inside = statementsOf(*statements[i + 1]->body[0]);
                    Stmt rest;
                    rest.kind = Stmt::Kind::Block;
                    for (const Stmt* each : inside) {
                        bool isReset = each->kind == Stmt::Kind::Assign && each->op == "-=" && each->target->kind == Expr::Kind::Name && each->target->text == timer;
                        if (!isReset) rest.body.push_back(std::make_shared<Stmt>(*each));
                    }
                    char text[32];
                    std::snprintf(text, sizeof(text), "%g", double(seconds));
                    sideEvent("OnTimer", {{"seconds", text}}, rest);
                    i++;
                    continue;
                }
            }
            run(s);
        }
    }

    void sideEvent(const std::string& type, const std::map<std::string, std::string>& values, const Stmt& statement) {
        EventState main = saveEvent();
        beginEvent(type, values);
        run(statement);
        endEvent();
        restoreEvent(main);
    }

    const Component& component_;
    std::vector<const Field*> startValues_;     // fields whose first value is worked out, set in On Start
    std::map<std::string, Binding> names_;
    std::set<std::string> variables_, timers_;
    std::string selfName_ = "self", dtName_ = "dt", otherName_ = "other";
    std::vector<Cursor> cursors_;
    int eventNode_ = 0;
    std::string eventType_;
    float x_ = 0, y_ = 0, bottom_ = 0;
    int valueSlot_ = 0;
    bool failed_ = false;
};

}  // namespace

CppToGraphResult graphFromCpp(const std::string& code) {
    registerStandardNodes();
    CppToGraphResult result;
    // Made from a graph and not changed since: that graph, exactly as it was laid out.
    bool wasChanged = true;
    std::string keptName;
    if (std::optional<Graph> kept = graphKeptIn(code, &wasChanged, &keptName); kept && !wasChanged) {
        result.graph = std::move(kept);
        result.name = keptName;
        result.isTheKeptGraph = true;
        return result;
    }
    // The comment with the kept graph is not code: what is read is what is above it.
    std::string readable = code;
    if (size_t kept = readable.rfind("/* molten-graph "); kept != std::string::npos) readable = readable.substr(0, kept);

    // The boxes that mean what the code means (Rotate, Set Position...), where every line of it has one.
    Parser parser(tokenize(readable), readable);
    std::optional<Component> component = parser.component();
    if (component) {
        result.name = component->name;
        // The fields MOLTEN_COMPONENT shows in the Inspector: MOLTEN_FIELD(name) of each.
        if (size_t declared = readable.find("MOLTEN_COMPONENT(" + component->name); declared != std::string::npos) {
            int depth = 0;
            size_t end = declared;
            for (; end < readable.size(); end++) {
                if (readable[end] == '(') depth++;
                else if (readable[end] == ')' && --depth == 0) break;
            }
            std::string list = readable.substr(declared, end - declared);
            for (size_t at = list.find("MOLTEN_FIELD("); at != std::string::npos; at = list.find("MOLTEN_FIELD(", at + 1)) {
                size_t close = list.find(')', at);
                if (close != std::string::npos) component->fieldsShown.push_back(list.substr(at + 13, close - at - 13));
            }
        }
        // Only a file that is the component (and its #includes, and MOLTEN_COMPONENT) is the
        // component's boxes: one with more beside it (functions, tables, other structs) is laid out
        // whole as code boxes, so that nothing of it is lost.
        std::vector<std::string> includes;
        bool isOnlyTheComponent = true;
        {
            std::string outside = readable.substr(0, component->begin) + "\n" + (component->end < readable.size() ? readable.substr(component->end) : std::string());
            std::istringstream lines(outside);
            std::string line, rest;
            while (std::getline(lines, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t\r"));
                if (trimmed.rfind("#include", 0) == 0) {
                    // Those code made from a graph always has are not kept: it writes them again.
                    bool always = false;
                    for (const char* written : {"MoltenMetal.hpp", "Script/Convert.hpp", "<algorithm>", "<cmath>", "<string>"}) always |= trimmed.find(written) != std::string::npos;
                    if (!always) includes.push_back(trimmed);
                    continue;
                }
                if (trimmed.rfind("#pragma", 0) == 0) continue;
                rest += line + "\n";
            }
            // What is left, without comments, `using namespace ...;` and MOLTEN_COMPONENT(...): nothing, or not.
            std::vector<Token> words = tokenize(rest);
            for (size_t i = 0; i < words.size(); i++) {
                if (words[i].text == "using" && i + 1 < words.size() && words[i + 1].text == "namespace") {
                    while (i < words.size() && words[i].text != ";") i++;
                    continue;
                }
                if (words[i].text == "MOLTEN_COMPONENT") {
                    int depth = 0;
                    for (i++; i < words.size(); i++) {
                        if (words[i].text == "(") depth++;
                        else if (words[i].text == ")" && --depth == 0) break;
                    }
                    continue;
                }
                if (words[i].text == ";") continue;
                isOnlyTheComponent = false;
                break;
            }
        }
        Builder builder(*component);
        if (isOnlyTheComponent) builder.build();
        builder.graph.code.insert(builder.graph.code.begin(), includes.begin(), includes.end());
        // Every statement is a box: those with no box of their own a C++ box, in its place.
        if (isOnlyTheComponent && (!builder.graph.nodes.empty() || !builder.graph.variables.empty())) {
            result.graph = std::move(builder.graph);
            result.notConverted = std::move(builder.notConverted);
            return result;
        }
    }
    // Anything else, and that is most C++: the code itself as boxes, a statement to a box, wired
    // in the shape the code has (Script/CppBoxes.hpp). Nothing is left out, and written back it
    // is the same file.
    result.graph = boxesFromCpp(code);
    result.isCodeAsBoxes = true;
    result.notConverted.clear();
    return result;
}

}  // namespace molten::script
