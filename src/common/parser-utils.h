/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AUTOPIPER_COMMON_PARSER_UTILS_H_
#define _AUTOPIPER_COMMON_PARSER_UTILS_H_

#include <iostream>
#include <assert.h>
#include <string>

#include <boost/multiprecision/gmp.hpp>

namespace autopiper {

struct Location {
    std::string filename;
    int line, column;

    Location() {
        filename = "(none)";
        line = 0;
        column = 0;
    }

    std::string ToString() const {
        std::ostringstream os;
        os << filename << ":" << line << ":" << column;
        return os.str();
    }
};

class ErrorCollector {
    public:
        enum Level {
            ERROR,
            WARNING,
            INFO,
        };

        virtual void ReportError(Location loc, Level level,
                                 const std::string& message) = 0;

        virtual bool HasErrors() const = 0;
};

class PeekableStream {
    public:
        PeekableStream(std::istream* in)
            : in_(in), peek_char_(0), have_peek_(false), eof_(false),
              line_(1), col_(0) {
            ReadNext();
        }

        char Peek() const {
            assert(have_peek_);
            return peek_char_;
        }
        bool Have() const {
            return have_peek_;
        }
        bool Eof() const {
            return eof_;
        }

        int Line() const {
            return line_;
        }
        int Col() const {
            return col_;
        }

        // Reads the next char. If a character is already in the peek-slot, it
        // is overwritten. Returns |true| unless EOF/error. If |true| is
        // returned, Have() is true, and Peek() returns the character that was
        // read. Otherwise, Have() is false and Eof() is true.
        bool ReadNext() {
            have_peek_ = false;
            if (eof_) return false;
            char c;
            in_->get(c);
            if (in_->bad() || in_->eof()) {
                eof_ = true;
            } else {
                peek_char_ = c;
                have_peek_ = true;
                if (c == '\n') {
                    line_++;
                    col_ = 0;
                } else if (c == '\r') {
                    col_ = 0;
                } else {
                    col_++;
                }
            }
            return !eof_;
        }

    private:
        std::istream* in_;
        char peek_char_;
        bool have_peek_;
        bool eof_;
        int line_;
        int col_;
};

struct Token {
    enum Type {
        INT_LITERAL,
        IDENT,
        QUOTED_STRING,

        // Punctuation
        LPAREN,
        RPAREN,
        LBRACKET,
        RBRACKET,
        LBRACE,
        RBRACE,
        LANGLE,
        RANGLE,
        BANG,
        AT,
        HASH,
        DOLLAR,
        PERCENT,
        CARET,
        AMPERSAND,
        STAR,
        EQUALS,
        PLUS,
        DASH,
        PIPE,
        SLASH,
        BACKSLASH,
        QUESTION,
        COMMA,
        DOT,
        COLON,
        SEMICOLON,
        TILDE,
        TICK,
        BACKTICK,

        // Compound punctuation
        DOUBLE_EQUAL,  // ==
        NOT_EQUAL,     // !=
        LESS_EQUAL,    // <=
        GREATER_EQUAL, // >=
        LSH,           // <<
        RSH,           // >>

        NEWLINE,
        EOFTOKEN,
    } type;

    typedef boost::multiprecision::mpz_int bignum;

    bignum int_literal;
    std::string s;

    int line, col;

    Token() {
        type = EOFTOKEN;
        line = 0;
        col = 0;
    }

    explicit Token(Token::Type type_) {
        Reset(type_);
    }

    void Reset(Type type_) {
        type = type_;
        int_literal = 0;
        s = "";
    }
    std::string ToString() {
#define S(x) \
        case x: return #x

        switch (type) {
            case INT_LITERAL:
                return std::string("INT_LITERAL(") +
                       std::string(int_literal) + std::string(")");
            case QUOTED_STRING:
                return std::string("QUOTED_STRING(") + s + std::string(")");
            case IDENT:
                return std::string("IDENT(") + s + std::string(")");

            S(LPAREN);
            S(RPAREN);
            S(LBRACKET);
            S(RBRACKET);
            S(LBRACE);
            S(RBRACE);
            S(LANGLE);
            S(RANGLE);
            S(BANG);
            S(AT);
            S(HASH);
            S(DOLLAR);
            S(PERCENT);
            S(CARET);
            S(AMPERSAND);
            S(STAR);
            S(EQUALS);
            S(PLUS);
            S(DASH);
            S(PIPE);
            S(SLASH);
            S(BACKSLASH);
            S(QUESTION);
            S(COMMA);
            S(DOT);
            S(COLON);
            S(SEMICOLON);
            S(TILDE);
            S(TICK);
            S(BACKTICK);

            S(DOUBLE_EQUAL);
            S(NOT_EQUAL);
            S(LESS_EQUAL);
            S(GREATER_EQUAL);
            S(LSH);
            S(RSH);

            S(NEWLINE);
            S(EOFTOKEN);

            default:
                return "Unknown Token";
        }
#undef S
    }
};

class Lexer {
    public:
        Lexer(std::istream* in)
            : stream_(in), have_peek_(false),
              ignore_newline_(false) {
            ReadNext();
        }

        void SetIgnoreNewline(bool ignore_newline) {
            ignore_newline_ = ignore_newline;
            if (Have() && Peek().type == Token::NEWLINE) {
                ReadNext();
            }
        }

        Token Peek() const {
            assert(have_peek_);
            return token_;
        }
        bool Have() const {
            return have_peek_;
        }

        bool StartsWith(std::string s, std::string prefix) {
            return s.size() >= prefix.size() &&
                   s.substr(0, prefix.size()) == prefix;
        }

        bool ReadNext() {
            have_peek_ = false;
            if (stream_.Eof()) return false;

            LexState state = S_INIT;
            std::string cur_token;
            int line = 0, col = 0;
            while (!have_peek_ && (stream_.Have() ||
                                   state != S_INIT)) {
                char c = 0;
                bool eof = stream_.Eof();
                if (stream_.Have()) {
                    c = stream_.Peek();
                }

                // We evaluate a state machine input when either 'eof' is true
                // or character 'c' is received. We continue evaluating as long
                // as we don't have a token, and either another character is
                // available or we're at EOF and the state machine has not
                // returned to S_INIT.
                switch (state) {
                    case S_INIT:
                        line = stream_.Line();
                        col = stream_.Col();
                        if (eof) {
                            break;
                        }
                        if (c == '#') {
                            state = S_COMMENT;
                            stream_.ReadNext();
                            continue;
                        }
                        if (c == '\n') {
                            Emit(line, col, Token::NEWLINE);
                            stream_.ReadNext();
                            continue;
                        }
                        if (isspace(c)) {
                            stream_.ReadNext();
                            continue;
                        }
                        if (isdigit(c)) {
                            state = S_INTLIT;  // do not consume
                            continue;
                        }
                        if (isalpha(c) || c == '_') {
                            state = S_IDENT;
                            continue;
                        }
                        if (c == '"') {
                            state = S_QUOTE;
                            stream_.ReadNext();
                            continue;
                        }

#define P(token, char_lit)                                                    \
                        if (c == char_lit) {                                  \
                            Emit(line, col, Token :: token);                  \
                            stream_.ReadNext();                               \
                            continue;                                         \
                        }                                                     \

                        P(PERCENT, '%');
                        P(LPAREN, '(');
                        P(RPAREN, ')');
                        P(LBRACKET, '[');
                        P(RBRACKET, ']');
                        P(LBRACE, '{');
                        P(RBRACE, '}');
                        P(AT, '@');
                        // no HASH -- interpreted as start of comment
                        P(DOLLAR, '$');
                        P(PERCENT, '%');
                        P(CARET, '^');
                        P(AMPERSAND, '&');
                        P(STAR, '*');
                        P(PLUS, '+');
                        P(DASH, '-');
                        P(PIPE, '|');
                        P(SLASH, '/');
                        P(BACKSLASH, '\\');
                        P(QUESTION, '?');
                        P(COMMA, ',');
                        P(DOT, '.');
                        P(COLON, ':');
                        P(SEMICOLON, ';');
                        P(TILDE, '~');
                        P(TICK, '\'');
                        P(BACKTICK, '`');

#undef P

                        // Compound punctuation: handled specially here by
                        // consuming and peeking at the next char.
                        if (c == '=') {
                          stream_.ReadNext();
                          c = stream_.Peek();
                          if (stream_.Have() && c == '=') {
                            stream_.ReadNext();
                            Emit(line, col, Token::DOUBLE_EQUAL);
                          } else {
                            Emit(line, col, Token::EQUALS);
                          }
                          continue;
                        }
                        if (c == '!') {
                          stream_.ReadNext();
                          c = stream_.Peek();
                          if (stream_.Have() && c == '=') {
                            stream_.ReadNext();
                            Emit(line, col, Token::NOT_EQUAL);
                          } else {
                            Emit(line, col, Token::BANG);
                          }
                          continue;
                        }
                        if (c == '<') {
                          stream_.ReadNext();
                          c = stream_.Peek();
                          if (stream_.Have() && c == '=') {
                            stream_.ReadNext();
                            Emit(line, col, Token::LESS_EQUAL);
                          } else if (stream_.Have() && c == '<') {
                            stream_.ReadNext();
                            Emit(line, col, Token::LSH);
                          } else {
                            Emit(line, col, Token::LANGLE);
                          }
                          continue;
                        }
                        if (c == '>') {
                          stream_.ReadNext();
                          c = stream_.Peek();
                          if (stream_.Have() && c == '=') {
                            stream_.ReadNext();
                            Emit(line, col, Token::GREATER_EQUAL);
                            continue;
                          } else if (stream_.Have() && c == '>') {
                            stream_.ReadNext();
                            Emit(line, col, Token::RSH);
                            continue;
                          } else {
                            Emit(line, col, Token::RANGLE);
                            continue;
                          }
                        }

                        stream_.ReadNext();
                        continue;

                        break;
                    case S_INTLIT:
                        if (!eof && IsValidNextIntLitChar(cur_token, c)) {
                            cur_token += c;
                            stream_.ReadNext();
                            continue;
                        }
                        Emit(line, col, Token::INT_LITERAL, ParseInt(cur_token));
                        cur_token.clear();
                        state = S_INIT;
                        break;
                    case S_IDENT:
                        if (!eof && (isalnum(c) || c == '_')) {
                            cur_token += c;
                            stream_.ReadNext();
                            continue;
                        }
                        Emit(line, col, Token::IDENT, cur_token);
                        cur_token.clear();
                        state = S_INIT;
                        break;
                    case S_QUOTE:
                        if (!eof && c == '\\') {
                            stream_.ReadNext();
                            state = S_QUOTE_BACKSLASH;
                            continue;
                        } else if (!eof && c != '"') {
                            cur_token += c;
                            stream_.ReadNext();
                            continue;
                        }
                        stream_.ReadNext();
                        Emit(line, col, Token::QUOTED_STRING, cur_token);
                        cur_token.clear();
                        state = S_INIT;
                        break;
                    case S_QUOTE_BACKSLASH:
                        if (!eof && c == '\\') {
                            stream_.ReadNext();
                            cur_token += '\\';
                            state = S_QUOTE;
                            continue;
                        } else if (!eof && c == '"') {
                            stream_.ReadNext();
                            cur_token += '"';
                            state = S_QUOTE;
                            continue;
                        }
                        break;
                    case S_COMMENT:
                        if (eof) {
                            state = S_INIT;
                            continue;
                        }
                        stream_.ReadNext();
                        if (c == '\n') {
                            state = S_INIT;
                            continue;
                        }
                        break;
                }
            }

            if (Have() &&
                Peek().type == Token::NEWLINE &&
                ignore_newline_) {
                return ReadNext();
            }

            return Have();
        }
    private:
        PeekableStream stream_;
        Token token_;
        bool have_peek_;
        bool ignore_newline_;

        enum LexState {
            S_INIT,
            S_INTLIT,
            S_IDENT,
            S_QUOTE,
            S_QUOTE_BACKSLASH,
            S_COMMENT,
        };

        void Emit(int line, int col, Token::Type type) {
            token_.Reset(type);
            token_.line = line;
            token_.col = col;
            have_peek_ = true;
        }
        void Emit(int line, int col, Token::Type type, Token::bignum literal) {
            Emit(line, col, type);
            token_.int_literal = literal;
        }
        void Emit(int line, int col, Token::Type type, std::string s) {
            Emit(line, col, type);
            token_.s = s;
        }

        Token::bignum ParseInt(std::string s) {
            try {
                Token::bignum n(s);
                return n;
            } catch (std::runtime_error) {
                Token::bignum n(0);
                return n;
            }
        }

        bool IsValidNextIntLitChar(std::string cur_token, char c) {
            if (cur_token == "0" && (c == 'x' || c == 'X')) return true;
            if (StartsWith(cur_token, "0x") ||
                StartsWith(cur_token, "0X")) {
                if ((c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F'))
                    return true;
            }
            if (isdigit(c)) return true;
            return false;
        }
};

class ParserBase {
    protected:
        std::string filename_;
        Lexer* lexer_;
        ErrorCollector* collector_;
        bool have_errors_;

        ParserBase(std::string filename, Lexer* lexer,
                   ErrorCollector* collector)
            : filename_(filename), lexer_(lexer), collector_(collector)
        {}

        Location CurLocation() const {
            Location loc;
            loc.filename = filename_;
            loc.line = CurToken().line;
            loc.column = CurToken().col;
            return loc;
        }

        Token CurToken() const {
            if (lexer_->Have()) {
                return lexer_->Peek();
            } else {
                static Token eof_token(Token::EOFTOKEN);
                return eof_token;
            }
        }

        void Error(std::string message) {
            collector_->ReportError(CurLocation(),
                    ErrorCollector::ERROR, message);
            have_errors_ = true;
        }

        bool TryExpect(Token::Type type) {
            return (CurToken().type == type);
        }
        bool Expect(Token::Type type) {
            if (!TryExpect(type)) {
                Error(std::string("Unexpected token ") + CurToken().ToString());
                return false;
            }
            return true;
        }

        bool Consume() {
            if (!lexer_->Have()) return false;
            lexer_->ReadNext();
            return true;
        }

        bool Consume(Token::Type type) {
            if (!Expect(type)) return false;
            return Consume();
        }

        bool TryConsume(Token::Type type) {
            if (!TryExpect(type)) return false;
            return Consume();
        }
};

}  // namespace autopiper

#endif  // _AUTOPIPER_COMMON_PARSER_UTILS_H_
