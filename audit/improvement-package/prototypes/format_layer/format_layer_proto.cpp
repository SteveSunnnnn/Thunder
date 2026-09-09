// format_layer_proto.cpp — Thunder 格式层扩展原型（独立可运行，仅标准库）
//
// 对应提升点：
//   P0-1  数组 / 混合容器        { 1 2 3 }、{ 1 2 "x" a = b }
//   P0-2  比较运算符            == != > >= < <=
//   P0-7  日期字面量            n.n.n -> DateCandidate（语义层再定 GameDate）
//   P0-7b 引号文本与未引号文本分离  Text vs Word
//   P1-7  无损回写               verbatim（原始源逐字节）与 pretty（可再解析等价 AST）
//
// 与现有 ThunderScriptParser.cpp 的关系：本原型把"块内只能 key = value"的语法
// （parser 见 ThunderScriptParser.cpp:191-243）扩展为"键值对与裸值并存"。
// 生产合并时保留 SymbolTable interning、诊断、深度/节点上限等既有机制。
//
// 运行：
//   g++ -std=c++23 -O2 -o format_layer_proto format_layer_proto.cpp
//   ./format_layer_proto <file.core>   退出码 0 = 断言全过（ROUNDTRIP_OK）
//   ./format_layer_proto               内置样例自检

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------- 值种类

enum class Kind : std::uint8_t {
    None,
    Number,
    Word,          // 未引号文本/符号：country、UNI、state_region:ID
    Text,          // 引号文本："United Kingdom"（与 Word 语义分离）
    DateCandidate, // 点分三段数字 1836.1.1（类型中立；语义层按期望解释）
    Block,         // 容器：children 可同时含 keyed 与裸值（混合容器）
};

enum class Op : std::uint8_t {
    Assign, // =
    Eq,     // ==
    Ne,     // !=
    Gt,     // >
    Ge,     // >=
    Lt,     // <
    Le,     // <=
};

const char* op_str(Op op) {
    switch (op) {
        case Op::Assign: return "=";
        case Op::Eq: return "==";
        case Op::Ne: return "!=";
        case Op::Gt: return ">";
        case Op::Ge: return ">=";
        case Op::Lt: return "<";
        case Op::Le: return "<=";
    }
    return "?";
}

struct Diagnostic {
    std::string message;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct Node {
    Kind kind = Kind::None;
    std::string key;   // 空 = 裸值
    Op op = Op::Assign; // 键与值之间的关系符（键值对形态下才有意义）
    double number = 0.0;
    std::string text;  // Word / Text / DateCandidate 原文
    bool ymd_valid = false;
    int ymd[3] = {0, 0, 0};
    std::vector<Node> children;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    [[nodiscard]] bool has_key() const noexcept { return !key.empty(); }
    [[nodiscard]] bool is_block() const noexcept { return kind == Kind::Block; }
    // 该块是否为"纯裸值序列"（数组形态：无任何子项带 key）
    [[nodiscard]] bool is_array_like() const noexcept {
        if (kind != Kind::Block || children.empty()) return false;
        for (const auto& c : children)
            if (c.has_key()) return false;
        return true;
    }
};

// ---------------------------------------------------------------- 词法

enum class TokKind : std::uint8_t {
    End, Word, Number, String, Date, LBrace, RBrace, OpT, Invalid,
};

struct Token {
    TokKind kind = TokKind::End;
    std::string_view text; // 指向源（String 指向解码缓冲，见 Lexer::owned_）
    double number = 0.0;
    Op op = Op::Assign;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::size_t begin = 0; // 源偏移（verbatim 使用 / 诊断）
    std::size_t end = 0;
};

class Lexer {
public:
    explicit Lexer(std::string_view src) : src_(src) {}

    Token next() {
        skip_space_and_comments();
        const auto line = line_;
        const auto column = column_;
        const auto begin = pos_;
        if (pos_ >= src_.size()) return {TokKind::End, {}, 0, Op::Assign, line, column, begin, begin};
        const char c = src_[pos_];
        if (c == '{') { advance(); return token(TokKind::LBrace, begin, Op::Assign, line, column); }
        if (c == '}') { advance(); return token(TokKind::RBrace, begin, Op::Assign, line, column); }
        if (c == '"') return string_token();
        if (c == '=' || c == '>' || c == '<' || c == '!') return op_token();
        if ((c >= '0' && c <= '9') || c == '-' || c == '+') return number_or_word();
        if (is_word_char(c)) return word_token();
        advance();
        return token(TokKind::Invalid, begin, Op::Assign, line, column);
    }

private:
    Token token(TokKind k, std::size_t begin, Op op, std::uint32_t line, std::uint32_t column) {
        Token t;
        t.kind = k;
        t.op = op;
        t.line = line;
        t.column = column;
        t.begin = begin;
        t.end = pos_;
        if (k == TokKind::Word || k == TokKind::Date || k == TokKind::Invalid)
            t.text = src_.substr(begin, pos_ - begin);
        return t;
    }

    static bool is_word_char(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' ||
               c == '-' || c == '/';
    }

    void advance() {
        if (pos_ < src_.size() && src_[pos_] == '\n') { ++line_; column_ = 1; } else { ++column_; }
        ++pos_;
    }

    void skip_space_and_comments() {
        for (;;) {
            while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' ||
                                          src_[pos_] == '\r' || src_[pos_] == '\n'))
                advance();
            if (pos_ >= src_.size()) return;
            if (src_[pos_] == '#') {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                continue;
            }
            if (src_[pos_] == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                advance(); advance();
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                continue;
            }
            return;
        }
    }

    // 三段数字点分、首段 3 或 4 位 => DateCandidate
    static bool is_date(std::string_view t) noexcept {
        int parts = 0;
        std::size_t first_len = 0;
        bool numeric = true;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '.') { ++parts; }
            else {
                if (!std::isdigit(static_cast<unsigned char>(t[i]))) { numeric = false; break; }
                if (parts == 0) ++first_len;
            }
        }
        if (!numeric || parts != 2) return false;
        return first_len == 4 || first_len == 3;
    }

    Token word_token() {
        const auto line = line_, column = column_;
        const auto begin = pos_;
        while (pos_ < src_.size() && is_word_char(src_[pos_])) advance();
        const auto text = src_.substr(begin, pos_ - begin);
        Token t;
        t.line = line;
        t.column = column;
        t.begin = begin;
        t.end = pos_;
        if (is_date(text)) { t.kind = TokKind::Date; t.text = text; }
        else { t.kind = TokKind::Word; t.text = text; }
        return t;
    }

    Token number_or_word() {
        const auto line = line_, column = column_;
        const auto begin = pos_;
        while (pos_ < src_.size() && is_word_char(src_[pos_])) advance();
        const auto text = src_.substr(begin, pos_ - begin);
        Token t;
        t.line = line;
        t.column = column;
        t.begin = begin;
        t.end = pos_;
        if (text.find_first_of("_:/") != std::string_view::npos) { t.kind = TokKind::Word; t.text = text; return t; }
        if (is_date(text)) { t.kind = TokKind::Date; t.text = text; return t; }
        const auto dots = std::count(text.begin(), text.end(), '.');
        if (dots >= 2) { t.kind = TokKind::Word; t.text = text; return t; }
        std::string owned{text};
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(owned.c_str(), &end);
        if (end != owned.c_str() + static_cast<std::ptrdiff_t>(owned.size())) { t.kind = TokKind::Word; t.text = text; return t; }
        if (errno == ERANGE || !std::isfinite(v)) { t.kind = TokKind::Invalid; t.text = text; return t; }
        t.kind = TokKind::Number;
        t.number = v;
        t.text = text;
        return t;
    }

    Token string_token() {
        const auto line = line_, column = column_;
        const auto begin = pos_;
        advance(); // 开引号
        std::string out;
        bool terminated = false;
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == '"') { terminated = true; advance(); break; }
            if (c == '\n') break;
            if (c == '\\' && pos_ + 1 < src_.size()) {
                const char n = src_[pos_ + 1];
                if (n == '"' || n == '\\' || n == 'n' || n == 't') {
                    if (n == 'n') out.push_back('\n');
                    else if (n == 't') out.push_back('\t');
                    else out.push_back(n);
                    advance(); advance();
                    continue;
                }
            }
            out.push_back(c);
            advance();
        }
        Token t;
        t.kind = terminated ? TokKind::String : TokKind::Invalid;
        t.line = line;
        t.column = column;
        t.begin = begin;
        t.end = pos_;
        owned_.push_back(std::move(out));
        t.text = owned_.back();
        return t;
    }

    Token op_token() {
        const auto line = line_, column = column_;
        const auto begin = pos_;
        const char c = src_[pos_];
        advance();
        if (pos_ < src_.size() && src_[pos_] == '=' &&
            (c == '=' || c == '!' || c == '>' || c == '<')) {
            advance();
            Op op = Op::Assign;
            if (c == '=') op = Op::Eq;
            else if (c == '!') op = Op::Ne;
            else if (c == '>') op = Op::Ge;
            else op = Op::Le;
            return token(TokKind::OpT, begin, op, line, column);
        }
        Op op = Op::Assign;
        if (c == '>') op = Op::Gt;
        else if (c == '<') op = Op::Lt;
        return token(TokKind::OpT, begin, op, line, column);
    }

    std::string_view src_;
    std::vector<std::string> owned_; // 引号文本解码结果生命周期锚
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

// ---------------------------------------------------------------- 语法

struct ParseResult {
    std::vector<Node> objects;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class Parser {
public:
    // 一次性全部词法化，随后对 token 向量做递归下降。
    ParseResult run(std::string_view src, std::string_view name) {
        tokens_.clear();
        Lexer lex(src);
        for (;;) {
            Token t = lex.next();
            const bool end = t.kind == TokKind::End;
            tokens_.push_back(std::move(t));
            if (end) break;
        }
        name_ = name;
        pos_ = 0;
        ParseResult res;
        while (!at_end()) {
            Node item;
            if (cur().kind == TokKind::Word && peek_kind(1) == TokKind::Word &&
                peek_kind(2) == TokKind::LBrace) {
                res.objects.push_back(parse_object_header());
                continue;
            }
            if (parse_entry(item)) {
                res.objects.push_back(std::move(item));
            } else {
                diag("expected object header, property or value");
                recover();
            }
        }
        res.diagnostics = std::move(diags_);
        return res;
    }

private:
    const Token& cur() const { return tokens_[pos_]; }
    const Token& at(std::size_t k) const {
        return k < tokens_.size() ? tokens_[k] : tokens_.back();
    }
    TokKind peek_kind(std::size_t k) const { return at(pos_ + k).kind; }
    bool at_end() const { return cur().kind == TokKind::End; }
    void advance() { if (!at_end()) ++pos_; }

    void diag(const std::string& m) {
        std::string full = m;
        if (!name_.empty()) full = std::string{name_} + ": " + m;
        diags_.push_back({std::move(full), cur().line, cur().column});
    }

    // 顶层对象形态：type name { ... }
    Node parse_object_header() {
        Node obj;
        obj.kind = Kind::Block;
        obj.line = cur().line;
        obj.column = cur().column;
        obj.key = std::string{cur().text};
        advance();
        obj.key += ".";
        if (cur().kind == TokKind::Word) {
            obj.key += std::string{cur().text};
            advance();
        }
        if (cur().kind == TokKind::LBrace) advance();
        else diag("expected '{' after object header");
        obj.children = parse_block_body();
        return obj;
    }

    std::vector<Node> parse_block_body() {
        std::vector<Node> out;
        while (!at_end() && cur().kind != TokKind::RBrace) {
            Node item;
            if (parse_entry(item)) {
                out.push_back(std::move(item));
            } else {
                diag("expected property or value");
                recover();
            }
        }
        if (cur().kind == TokKind::RBrace) advance();
        else diag("unterminated block");
        return out;
    }

    // 块内条目统一入口：区分三种形态
    //   1) Word/Date 后随运算符 => 键值/比较对  key = value | key >= value | 1836.1.1 = { }
    //   2) 其余任何值          => 裸值（数组元素）     4 | "x" | flag | 1836.1.1
    //   3) LBrace              => 裸块（子容器）       { ... }
    bool parse_entry(Node& out) {
        const bool key_candidate =
            cur().kind == TokKind::Word || cur().kind == TokKind::Date;
        if (key_candidate && peek_kind(1) == TokKind::OpT) {
            out.kind = Kind::None;
            out.key = std::string{cur().text};
            out.line = cur().line;
            out.column = cur().column;
            advance(); // key
            out.op = cur().op;
            advance(); // op
            if (!parse_value(out)) {
                diag("expected value after operator");
                return false;
            }
            return true;
        }
        return parse_value(out);
    }

    // 把值解析进 out（键/运算符字段保持调用者设定或置空）。
    bool parse_value(Node& out) {
        switch (cur().kind) {
            case TokKind::Number: {
                out.kind = Kind::Number;
                out.number = cur().number;
                out.line = cur().line;
                out.column = cur().column;
                advance();
                return true;
            }
            case TokKind::Word: {
                out.kind = Kind::Word;
                out.text = std::string{cur().text};
                out.line = cur().line;
                out.column = cur().column;
                advance();
                return true;
            }
            case TokKind::Date: {
                out.kind = Kind::DateCandidate;
                out.text = std::string{cur().text};
                out.line = cur().line;
                out.column = cur().column;
                out.ymd_valid = parse_ymd(out.text, out.ymd);
                advance();
                return true;
            }
            case TokKind::String: {
                out.kind = Kind::Text;
                out.text = std::string{cur().text};
                out.line = cur().line;
                out.column = cur().column;
                advance();
                return true;
            }
            case TokKind::LBrace: {
                out.kind = Kind::Block;
                out.line = cur().line;
                out.column = cur().column;
                advance();
                out.children = parse_block_body();
                return true;
            }
            default:
                return false;
        }
    }

    static bool parse_ymd(const std::string& t, int* ymd) {
        int vals[3] = {0, 0, 0};
        int idx = 0;
        int acc = 0;
        bool have = false;
        for (char c : t) {
            if (c == '.') {
                if (idx < 3) vals[idx++] = acc;
                acc = 0;
                have = false;
            } else {
                acc = acc * 10 + (c - '0');
                have = true;
            }
        }
        if (have && idx < 3) vals[idx] = acc;
        for (int i = 0; i < 3; ++i) ymd[i] = vals[i];
        return idx == 2;
    }

    void recover() {
        // 跳到下一个块闭合或一个可辨识的条目起点（Word/数值）
        while (!at_end()) {
            if (cur().kind == TokKind::RBrace) return;
            if (cur().kind == TokKind::Word && peek_kind(1) != TokKind::Word)
                return; // 潜在 key/value 起点
            advance();
        }
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    std::string_view name_;
    std::vector<Diagnostic> diags_;
};

// ---------------------------------------------------------------- 写出

// verbatim：未做编辑的文档 => 逐字节还原（注释/空白 100% 保留）
std::string write_verbatim(std::string_view src) { return std::string{src}; }

std::string escape_text(std::string_view s); // 前向声明（定义见下）

// pretty：确定性排版；仅含本原型支持的字面量集合
std::string fmt_number(double v) {
    if (v == static_cast<long long>(v) && std::fabs(v) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
        return b;
    }
    char b[40];
    std::snprintf(b, sizeof b, "%.10g", v);
    return b;
}

void write_node(std::string& out, const Node& n, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    if (!n.has_key()) {
        // 裸值（数组元素 / 顶层裸量）
        switch (n.kind) {
            case Kind::Number: out += pad + fmt_number(n.number); break;
            case Kind::Word: out += pad + n.text; break;
            case Kind::Text: out += pad + escape_text(n.text); break;
            case Kind::DateCandidate: out += pad + n.text; break;
            case Kind::Block: {
                out += pad + "{\n";
                for (const auto& c : n.children) { write_node(out, c, indent + 1); out += "\n"; }
                out += pad + "}";
                break;
            }
            case Kind::None: break;
        }
        return;
    }
    // 键值/比较对：key op value
    out += pad + n.key + " " + op_str(n.op) + " ";
    switch (n.kind) {
        case Kind::Number: out += fmt_number(n.number); break;
        case Kind::Word: out += n.text; break;
        case Kind::Text: out += escape_text(n.text); break;
        case Kind::DateCandidate: out += n.text; break;
        case Kind::Block: {
            out += "{\n";
            for (const auto& c : n.children) { write_node(out, c, indent + 1); out += "\n"; }
            out += pad + "}";
            break;
        }
        case Kind::None: out += "none"; break;
    }
}

std::string escape_text(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    out += "\"";
    return out;
}

// ---------------------------------------------------------------- 测试工具

bool ast_equal(const Node& a, const Node& b) {
    if (a.kind != b.kind || a.key != b.key || a.op != b.op) return false;
    if (a.kind == Kind::Number && a.number != b.number) return false;
    if ((a.kind == Kind::Word || a.kind == Kind::Text || a.kind == Kind::DateCandidate) &&
        a.text != b.text)
        return false;
    if (a.kind == Kind::DateCandidate && (a.ymd_valid != b.ymd_valid ||
        a.ymd[0] != b.ymd[0] || a.ymd[1] != b.ymd[1] || a.ymd[2] != b.ymd[2]))
        return false;
    if (a.children.size() != b.children.size()) return false;
    for (std::size_t i = 0; i < a.children.size(); ++i)
        if (!ast_equal(a.children[i], b.children[i])) return false;
    return true;
}

void dump_node(const Node& n, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    std::string head;
    if (n.has_key()) head = n.key + " " + op_str(n.op);
    switch (n.kind) {
        case Kind::None: std::printf("%s%s none\n", pad.c_str(), head.c_str()); break;
        case Kind::Number: std::printf("%s%s number %s\n", pad.c_str(), head.c_str(), fmt_number(n.number).c_str()); break;
        case Kind::Word: std::printf("%s%s word %s\n", pad.c_str(), head.c_str(), n.text.c_str()); break;
        case Kind::Text: std::printf("%s%s text \"%s\"\n", pad.c_str(), head.c_str(), n.text.c_str()); break;
        case Kind::DateCandidate: {
            const char* y = n.ymd_valid ? "date" : "date-invalid";
            std::printf("%s%s %s %s (y=%d m=%d d=%d)\n", pad.c_str(), head.c_str(), y,
                        n.text.c_str(), n.ymd[0], n.ymd[1], n.ymd[2]);
            break;
        }
        case Kind::Block: {
            std::printf("%s%s block%s%s\n", pad.c_str(), head.c_str(),
                        n.is_array_like() ? " [array]" : "",
                        n.children.empty() ? " {}" : "");
            for (const auto& c : n.children) dump_node(c, indent + 1);
            break;
        }
    }
}

// 内置样例：覆盖全部新语法形态
const char* builtin_sample = R"core(# 内置样例：格式层扩展语法演示
country UNI {
    color = { 128 64 32 }
    capital = 4711
    treasury = 125.5
    trigger = {
        treasury > 100
        population >= 2000000
        has_law == "universal_suffrage"
        national_debt <= 4000
        founding_date = 1836.1.1
        techs = { nationalism romanticism }
        culture_set = { 5 "north_german" 2 }
    }
    description = "United {COUNTRY} of the Isles"
    goods = { coal iron 6 }
}
)core";

} // namespace

int main(int argc, char** argv) {
    std::string source = builtin_sample;
    std::string name = "<builtin>";
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
        std::ostringstream ss;
        ss << in.rdbuf();
        source = ss.str();
        name = argv[1];
    }

    Parser parser;
    const auto result = parser.run(source, name);

    std::printf("== parse diagnostics: %zu ==\n", result.diagnostics.size());
    for (const auto& d : result.diagnostics)
        std::printf("  [%u:%u] %s\n", d.line, d.column, d.message.c_str());

    std::printf("== top-level objects: %zu ==\n", result.objects.size());
    for (const auto& o : result.objects) {
        std::printf("object key=%s\n", o.key.c_str());
        dump_node(o, 1);
    }

    // 断言 1：verbatim 写出 == 输入逐字节
    const std::string verbatim = write_verbatim(source);
    if (verbatim != source) {
        std::fprintf(stderr, "FAIL: verbatim round-trip mismatch\n");
        return 1;
    }
    std::printf("== verbatim round-trip: OK (%zu bytes) ==\n", verbatim.size());

    // 断言 2：pretty 写出后可再解析出等价 AST（语义 round-trip）
    std::string pretty;
    for (const auto& o : result.objects) {
        write_node(pretty, o, 0);
        pretty += "\n";
    }
    Parser re;
    const auto again = re.run(pretty, "<pretty>");
    bool equal = again.diagnostics.empty() && again.objects.size() == result.objects.size();
    for (std::size_t i = 0; equal && i < result.objects.size(); ++i)
        equal = ast_equal(result.objects[i], again.objects[i]);
    if (!equal) {
        std::fprintf(stderr, "FAIL: pretty round-trip AST mismatch\n");
        std::printf("---- pretty output ----\n%s\n", pretty.c_str());
        return 1;
    }
    std::printf("== pretty round-trip: OK ==\n");
    std::printf("---- pretty output ----\n%s\n", pretty.c_str());
    std::printf("ROUNDTRIP_OK\n");
    return 0;
}
