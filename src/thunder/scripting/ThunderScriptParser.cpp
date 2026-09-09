#include "thunder/scripting/ThunderScriptParser.hpp"
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace thunder {

const ScriptNode* ScriptNode::find(SymbolId wanted) const noexcept {
    for (const auto& child : children) if (child.key == wanted) return &child;
    return nullptr;
}

namespace {

enum class TokenKind : std::uint8_t {
    End, Word, Number, String, LBrace, RBrace, Equals, Invalid,
    Plus, Minus, Mul, Div, LPar, RPar,
    Less, Greater, LessEqual, GreaterEqual, NotEqual
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string_view text;
    double number = 0.0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        skip_space_and_comments();
        if (pos_ >= source_.size()) return {TokenKind::End, {}, 0.0, line_, column_};
        const auto line = line_;
        const auto column = column_;
        const char c = source_[pos_];
        if (c == '{') { advance(); return {TokenKind::LBrace, "{", 0.0, line, column}; }
        if (c == '}') { advance(); return {TokenKind::RBrace, "}", 0.0, line, column}; }
        if (c == '=') { advance(); return {TokenKind::Equals, "=", 0.0, line, column}; }
        if (c == '*') { advance(); return {TokenKind::Mul, "*", 0.0, line, column}; }
        if (c == '/') { advance(); return {TokenKind::Div, "/", 0.0, line, column}; }
        if (c == '(') { advance(); return {TokenKind::LPar, "(", 0.0, line, column}; }
        if (c == ')') { advance(); return {TokenKind::RPar, ")", 0.0, line, column}; }
        if (c == '<') {
            advance();
            if (c_pos_is('=')) { advance(); return {TokenKind::LessEqual, "<=", 0.0, line, column}; }
            return {TokenKind::Less, "<", 0.0, line, column};
        }
        if (c == '>') {
            advance();
            if (c_pos_is('=')) { advance(); return {TokenKind::GreaterEqual, ">=", 0.0, line, column}; }
            return {TokenKind::Greater, ">", 0.0, line, column};
        }
        if (c == '!') {
            advance();
            if (c_pos_is('=')) { advance(); return {TokenKind::NotEqual, "!=", 0.0, line, column}; }
            return {TokenKind::Invalid, "!", 0.0, line, column};
        }
        if (c == '"') return string_token();
        if (c == '+' || c == '-') {
            // Sign attached to a number/word keeps the legacy literal behaviour
            // (-5, +3.5, -state-id). A detached '+'/'-' is an arithmetic operator
            // (binary expressions require whitespace around + and -).
            const auto next_char = pos_ + 1u < source_.size() ? source_[pos_ + 1u] : '\0';
            if (!is_word_char(next_char) && next_char != '.') {
                advance();
                return {c == '+' ? TokenKind::Plus : TokenKind::Minus,
                        source_.substr(pos_ - 1u, 1u), 0.0, line, column};
            }
            return number_or_word();
        }
        if ((c >= '0' && c <= '9')) return number_or_word();
        if (is_word_char(c)) return word_token();
        advance();
        return {TokenKind::Invalid, source_.substr(pos_ - 1u, 1u), 0.0, line, column};
    }

private:
    // One-char lookahead guard used by the two-char comparison tokens; pos_ has
    // already been advanced past the leading '<' '>' '!'.
    [[nodiscard]] bool c_pos_is(char expected) const noexcept {
        return pos_ < source_.size() && source_[pos_] == expected;
    }

    static bool is_word_char(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-' || c == '/';
    }

    void advance() {
        if (source_[pos_] == '\n') { ++line_; column_ = 1; } else { ++column_; }
        ++pos_;
    }

    void skip_space_and_comments() {
        for (;;) {
            while (pos_ < source_.size()) {
                const char c = source_[pos_];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') advance(); else break;
            }
            if (pos_ >= source_.size()) return;
            if (source_[pos_] == '#') {
                while (pos_ < source_.size() && source_[pos_] != '\n') advance();
                continue;
            }
            if (source_[pos_] == '/' && pos_ + 1u < source_.size() && source_[pos_ + 1u] == '/') {
                advance(); advance();
                while (pos_ < source_.size() && source_[pos_] != '\n') advance();
                continue;
            }
            return;
        }
    }

    Token word_token() {
        const auto start = pos_;
        const auto line = line_;
        const auto column = column_;
        while (pos_ < source_.size() && is_word_char(source_[pos_])) advance();
        return {TokenKind::Word, source_.substr(start, pos_ - start), 0.0, line, column};
    }

    Token number_or_word() {
        const auto start = pos_;
        const auto line = line_;
        const auto column = column_;
        while (pos_ < source_.size() && is_word_char(source_[pos_])) advance();
        const auto text = source_.substr(start, pos_ - start);
        // Dotted dates and identifiers beginning with digits are words, not numeric constants.
        if (text.find_first_of("_:/") != std::string_view::npos ||
            (text.find('.') != std::string_view::npos && text.find('.', text.find('.') + 1u) != std::string_view::npos)) {
            return {TokenKind::Word, text, 0.0, line, column};
        }
        std::string owned{text};
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(owned.c_str(), &end);
        if (end != owned.c_str() + static_cast<std::ptrdiff_t>(owned.size())) {
            return {TokenKind::Word, text, 0.0, line, column};
        }
        if (errno == ERANGE || !std::isfinite(value))
            return {TokenKind::Invalid, text, 0.0, line, column};
        return {TokenKind::Number, text, value, line, column};
    }

    Token string_token() {
        const auto line = line_;
        const auto column = column_;
        advance();
        const auto start = pos_;
        while (pos_ < source_.size() && source_[pos_] != '"') {
            if (source_[pos_] == '\n') return {TokenKind::Invalid, source_.substr(start, pos_ - start), 0.0, line, column};
            advance();
        }
        if (pos_ >= source_.size()) return {TokenKind::Invalid, source_.substr(start), 0.0, line, column};
        const auto text = source_.substr(start, pos_ - start);
        advance();
        return {TokenKind::String, text, 0.0, line, column};
    }

    std::string_view source_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

class ParserImpl {
public:
    ParserImpl(SymbolTable& symbols, std::string_view source, std::string_view source_name)
        : symbols_(symbols), lexer_(source), source_name_(source_name) { advance(); }

    ScriptParseResult run() {
        while (current_.kind != TokenKind::End) {
            if (current_.kind != TokenKind::Word) {
                error("expected top-level object type");
                recover_top_level();
                continue;
            }
            ScriptObject object;
            object.type = symbols_.intern(current_.text);
            object.line = current_.line;
            advance();
            if (current_.kind != TokenKind::Word && current_.kind != TokenKind::String) {
                error("expected top-level object name");
                recover_top_level();
                continue;
            }
            object.name = symbols_.intern(current_.text);
            advance();
            if (!accept(TokenKind::LBrace)) {
                error("expected '{' after top-level object name");
                recover_top_level();
                continue;
            }
            const auto diag_before = result_.diagnostics.size();
            object.fields = parse_block(1u);
            if (result_.diagnostics.size() == diag_before) {
                result_.objects.push_back(std::move(object));
            }
            if (node_count_ >= max_ast_nodes) break;
        }
        return std::move(result_);
    }

private:
    void advance() { current_ = lexer_.next(); }
    bool accept(TokenKind kind) {
        if (current_.kind != kind) return false;
        advance();
        return true;
    }

    void skip_block_contents() {
        std::size_t depth = 0u;
        while (current_.kind != TokenKind::End) {
            if (current_.kind == TokenKind::LBrace) {
                ++depth;
            } else if (current_.kind == TokenKind::RBrace) {
                if (depth == 0u) {
                    advance();
                    return;
                }
                --depth;
            }
            advance();
        }
    }

    std::vector<ScriptNode> parse_block(std::size_t depth) {
        std::vector<ScriptNode> nodes;
        nodes.reserve(8u);
        if (depth > max_ast_depth) {
            error("maximum ThunderScript block depth exceeded");
            skip_block_contents();
            return nodes;
        }
        while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace) {
            // Numeric keys are legal for weighted constructs such as
            // `random_list = { 10 = { ... } 90 = { ... } }` where the key is
            // the branch weight; the key text is interned like any symbol.
            if (current_.kind != TokenKind::Word && current_.kind != TokenKind::Number) {
                error("expected property name");
                advance();
                continue;
            }
            ScriptNode node;
            ++node_count_;
            if (node_count_ > max_ast_nodes) {
                error("maximum ThunderScript AST node count exceeded");
                skip_block_contents();
                return nodes;
            }
            const bool key_is_word = current_.kind == TokenKind::Word;
            node.key = symbols_.intern(current_.text);
            node.line = current_.line;
            node.column = current_.column;
            advance();
            // Comparison trigger lines: `population > 1000`, `value:income - 50 >= 40`.
            // No '=' is present: the key becomes the first expression operand,
            // the line must contain a comparison operator, and everything
            // lowers into the scoped-condition algebra. A key followed by an
            // arithmetic operator WITHOUT any comparison is still the legacy
            // "expected '='" error path.
            if (key_is_word &&
                (current_.kind == TokenKind::Less || current_.kind == TokenKind::Greater ||
                 current_.kind == TokenKind::LessEqual || current_.kind == TokenKind::GreaterEqual ||
                 current_.kind == TokenKind::NotEqual || current_.kind == TokenKind::Plus ||
                 current_.kind == TokenKind::Minus || current_.kind == TokenKind::Mul ||
                 current_.kind == TokenKind::Div)) {
                ScriptNode lhs;
                lhs.key = node.key;
                lhs.kind = ScriptValueKind::Symbol;
                lhs.symbol = node.key;
                lhs.line = node.line;
                lhs.column = node.column;
                ScriptNode lhs_expression;
                if (!parse_symbolic_expression_from(std::move(lhs), depth, lhs_expression)) {
                    skip_to_property_boundary();
                    continue;
                }
                if (current_.kind != TokenKind::Less && current_.kind != TokenKind::Greater &&
                    current_.kind != TokenKind::LessEqual && current_.kind != TokenKind::GreaterEqual &&
                    current_.kind != TokenKind::NotEqual) {
                    error("expected '=' after property name");
                    skip_to_property_boundary();
                    continue;
                }
                const auto op_text = comparison_op_text(current_.kind);
                advance();
                ScriptNode rhs;
                if (!parse_symbolic_expression(rhs, depth)) {
                    skip_to_property_boundary();
                    continue;
                }
                ScriptNode compare;
                compare.key = symbols_.intern(op_text);
                compare.kind = ScriptValueKind::Expression;
                compare.line = node.line;
                compare.column = node.column;
                ++node_count_;
                if (node_count_ > max_ast_nodes) {
                    error("maximum ThunderScript AST node count exceeded");
                    skip_block_contents();
                    return nodes;
                }
                compare.children.push_back(std::move(lhs_expression));
                compare.children.push_back(std::move(rhs));
                nodes.push_back(std::move(compare));
                continue;
            }
            if (!accept(TokenKind::Equals)) {
                error("expected '=' after property name");
                skip_to_property_boundary();
                continue;
            }
            if (current_.kind == TokenKind::LBrace) {
                node.kind = ScriptValueKind::Block;
                advance();
                node.children = parse_block(depth + 1u);
            } else if (current_.kind == TokenKind::Number || current_.kind == TokenKind::Plus ||
                       current_.kind == TokenKind::Minus || current_.kind == TokenKind::LPar) {
                // Value expression. Constant-only expressions fold at parse time
                // exactly as before (deterministic, zero runtime cost); an
                // expression containing operands (value sources, scripted value
                // or variable references) is kept as an Expression tree for the
                // compiler to lower into `ScriptValueBytecode`. The symbolic
                // tree replaces the field node (its key becomes the root
                // operator; the compiler dispatches on kind), but the source
                // position is preserved for diagnostics.
                ScriptNode expression;
                if (!parse_symbolic_expression(expression, depth + 1u)) {
                    skip_to_property_boundary();
                    continue;
                }
                double folded = 0.0;
                const int fold_state = fold_constant_expression(expression, folded);
                if (fold_state < 0) {
                    skip_to_property_boundary();
                    continue;
                }
                const auto key_line = node.line;
                const auto key_column = node.column;
                if (fold_state > 0) {
                    node.kind = ScriptValueKind::Number;
                    node.number = folded;
                } else {
                    node = std::move(expression);
                }
                node.line = key_line;
                node.column = key_column;
            } else if (current_.kind == TokenKind::Word || current_.kind == TokenKind::String) {
                node.kind = ScriptValueKind::Symbol;
                node.symbol = symbols_.intern(current_.text);
                advance();
            } else {
                error("expected number, identifier, string or block value");
                advance();
                continue;
            }
            nodes.push_back(std::move(node));
        }
        if (current_.kind == TokenKind::RBrace) advance();
        else error("unterminated block");
        return nodes;
    }

    void skip_to_property_boundary() {
        while (current_.kind != TokenKind::End && current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Word) advance();
    }

    // Symbolic value expressions. The same grammar as the historical constant
    // folder, but operands may be identifiers (value sources, `value:name`
    // scripted values, `var:name` variables), so the tree is kept for the
    // compiler to lower into `ScriptValueBytecode`:
    //   expr    := term (('+'|'-') term)*
    //   term    := unary (('*'|'/') unary)*
    //   unary   := ('+'|'-') unary | primary
    //   primary := Number | Word | '(' expr ')'
    // Binary +/- require surrounding whitespace; a sign attached to a number
    // or word keeps the legacy literal behaviour (-5, +3.5, -state-id). '/'
    // is a word character, so division likewise requires surrounding spaces.
    // Comparisons are a trigger-line form only and never nest here.
    bool parse_symbolic_expression(ScriptNode& out, std::size_t depth) {
        ScriptNode lhs;
        if (!parse_symbolic_unary(lhs, depth)) return false;
        return parse_symbolic_expression_from(std::move(lhs), depth, out);
    }

    // Continues an expression whose first operand is already parsed (the key
    // of a trigger comparison line). Standard precedence is preserved: the
    // incoming operand completes its term first, then +/- fold full terms.
    bool parse_symbolic_expression_from(ScriptNode lhs, std::size_t depth, ScriptNode& out) {
        ScriptNode current = std::move(lhs);
        while (current_.kind == TokenKind::Mul || current_.kind == TokenKind::Div) {
            const auto op_kind = current_.kind;
            advance();
            ScriptNode rhs;
            if (!parse_symbolic_unary(rhs, depth)) return false;
            if (!combine_symbolic(current, op_kind, std::move(rhs))) return false;
        }
        while (current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus) {
            const auto op_kind = current_.kind;
            advance();
            ScriptNode rhs;
            if (!parse_symbolic_term(rhs, depth)) return false;
            if (!combine_symbolic(current, op_kind, std::move(rhs))) return false;
        }
        out = std::move(current);
        return true;
    }

    bool parse_symbolic_term(ScriptNode& out, std::size_t depth) {
        ScriptNode lhs;
        if (!parse_symbolic_unary(lhs, depth)) return false;
        while (current_.kind == TokenKind::Mul || current_.kind == TokenKind::Div) {
            const auto op_kind = current_.kind;
            advance();
            ScriptNode rhs;
            if (!parse_symbolic_unary(rhs, depth)) return false;
            if (!combine_symbolic(lhs, op_kind, std::move(rhs))) return false;
        }
        out = std::move(lhs);
        return true;
    }

    bool combine_symbolic(ScriptNode& lhs, TokenKind op_kind, ScriptNode rhs) {
        ++node_count_;
        if (node_count_ > max_ast_nodes) {
            error("maximum ThunderScript AST node count exceeded");
            return false;
        }
        ScriptNode binary;
        binary.key = symbols_.intern(op_kind == TokenKind::Plus   ? "+"
                                     : op_kind == TokenKind::Minus ? "-"
                                     : op_kind == TokenKind::Mul   ? "*"
                                                                   : "/");
        binary.kind = ScriptValueKind::Expression;
        binary.children.push_back(std::move(lhs));
        binary.children.push_back(std::move(rhs));
        lhs = std::move(binary);
        return true;
    }

    bool parse_symbolic_unary(ScriptNode& out, std::size_t depth) {
        if (depth > max_ast_depth) {
            error("maximum ThunderScript block depth exceeded");
            return false;
        }
        if (current_.kind == TokenKind::Minus) {
            advance();
            ScriptNode operand;
            if (!parse_symbolic_unary(operand, depth)) return false;
            ScriptNode negation;
            negation.key = symbols_.intern("neg");
            negation.kind = ScriptValueKind::Expression;
            negation.children.push_back(std::move(operand));
            ++node_count_;
            if (node_count_ > max_ast_nodes) {
                error("maximum ThunderScript AST node count exceeded");
                return false;
            }
            out = std::move(negation);
            return true;
        }
        if (current_.kind == TokenKind::Plus) {
            advance();
            return parse_symbolic_unary(out, depth);
        }
        return parse_symbolic_primary(out, depth);
    }

    bool parse_symbolic_primary(ScriptNode& out, std::size_t depth) {
        if (current_.kind == TokenKind::Number) {
            out.kind = ScriptValueKind::Number;
            out.number = current_.number;
            advance();
            return true;
        }
        if (current_.kind == TokenKind::Word) {
            out.kind = ScriptValueKind::Symbol;
            out.symbol = symbols_.intern(current_.text);
            advance();
            return true;
        }
        if (current_.kind == TokenKind::LPar) {
            advance();
            if (!parse_symbolic_expression(out, depth + 1u)) return false;
            if (current_.kind != TokenKind::RPar) {
                error("expected ')' in value expression");
                return false;
            }
            advance();
            return true;
        }
        error("expected number, operand or '(' in value expression");
        return false;
    }

    // Constant folding over a symbolic tree, preserving the historical
    // deterministic left-associative evaluation. Returns 1 when the tree is a
    // pure constant (out_value set), 0 when it references non-constant
    // operands (caller keeps the Expression tree), -1 after emitting a hard
    // parse error (caller drops the property, matching the historical folder).
    [[nodiscard]] int fold_constant_expression(const ScriptNode& node, double& out_value) {
        switch (node.kind) {
        case ScriptValueKind::Number:
            out_value = node.number;
            return 1;
        case ScriptValueKind::Symbol:
            return 0;
        case ScriptValueKind::Expression:
            break;
        default:
            return 0;
        }
        const auto op = symbols_.text(node.key);
        double lhs = 0.0;
        if (node.children.empty()) return 0;
        const int lhs_state = fold_constant_expression(node.children[0], lhs);
        if (lhs_state < 0) return -1;
        if (lhs_state == 0) return 0;
        if (op == "neg") {
            out_value = -lhs;
            return 1;
        }
        double rhs = 0.0;
        if (node.children.size() != 2u) return 0;
        const int rhs_state = fold_constant_expression(node.children[1], rhs);
        if (rhs_state < 0) return -1;
        if (rhs_state == 0) return 0;
        if (op == "+") out_value = lhs + rhs;
        else if (op == "-") out_value = lhs - rhs;
        else if (op == "*") out_value = lhs * rhs;
        else if (op == "/") {
            if (rhs == 0.0) {
                error("division by zero in value expression");
                return -1;
            }
            out_value = lhs / rhs;
        } else return 0;
        return 1;
    }

    [[nodiscard]] static std::string_view comparison_op_text(TokenKind kind) noexcept {
        switch (kind) {
        case TokenKind::Less: return "<";
        case TokenKind::Greater: return ">";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::NotEqual: return "!=";
        default: return "";
        }
    }

    void recover_top_level() {
        int depth = 0;
        while (current_.kind != TokenKind::End) {
            if (current_.kind == TokenKind::LBrace) ++depth;
            else if (current_.kind == TokenKind::RBrace) { if (depth == 0) { advance(); return; } --depth; }
            advance();
            if (depth == 0 && current_.kind == TokenKind::Word) return;
        }
    }

    void error(std::string message) {
        if (!source_name_.empty()) message = std::string{source_name_} + ": " + message;
        result_.diagnostics.push_back({std::move(message), current_.line, current_.column});
    }

    SymbolTable& symbols_;
    static constexpr std::size_t max_ast_depth = 128u;
    static constexpr std::size_t max_ast_nodes = 1'000'000u;
    Lexer lexer_;
    std::string_view source_name_;
    Token current_;
    ScriptParseResult result_;
    std::size_t node_count_ = 0u;
};

} // namespace

ScriptParseResult ThunderScriptParser::parse(std::string_view source, std::string_view source_name) {
    return ParserImpl{symbols_, source, source_name}.run();
}

} // namespace thunder
