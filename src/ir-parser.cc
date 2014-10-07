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

#include "ir.h"
#include "compiler.h"

#include <map>
#include <iostream>
#include <assert.h>
#include <ctype.h>

using namespace std;
using namespace autopiper;

namespace {

class PeekableStream {
    public:
        PeekableStream(istream* in)
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
        istream* in_;
        char peek_char_;
        bool have_peek_;
        bool eof_;
        int line_;
        int col_;
};

struct Token {
    enum Type {
        PERCENT,
        INT_LITERAL,
        LBRACKET,
        RBRACKET,
        EQUALS,
        COMMA,
        COLON,
        AT,
        PLUS,
        IDENT,
        QUOTED_STRING,
        NEWLINE,
        EOFTOKEN,
    } type;

    bignum int_literal;
    string s;

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
    string ToString() {
#define S(x) \
        case x: return #x

        switch (type) {
            case INT_LITERAL:
                return string("INT_LITERAL(") + string(int_literal) + string(")");
            case QUOTED_STRING:
                return string("QUOTED_STRING(") + s + string(")");
            case IDENT:
                return string("IDENT(") + s + string(")");
            S(PERCENT);
            S(LBRACKET);
            S(RBRACKET);
            S(EQUALS);
            S(COMMA);
            S(COLON);
            S(AT);
            S(PLUS);
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
        Lexer(istream* in)
            : stream_(in), have_peek_(false) {
            ReadNext();
        }

        Token Peek() const {
            assert(have_peek_);
            return token_;
        }
        bool Have() const {
            return have_peek_;
        }

        bool ReadNext() {
            have_peek_ = false;
            if (stream_.Eof()) return false;

            LexState state = S_INIT;
            string cur_token;
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
                        else if (c == '#') {
                            state = S_COMMENT;
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '\n') {
                            Emit(line, col, Token::NEWLINE);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (isspace(c)) {
                            stream_.ReadNext();
                            continue;
                        }
                        else if (isdigit(c)) {
                            state = S_INTLIT;  // do not consume
                            continue;
                        }
                        else if (c == '-') {
                            cur_token = "-";
                            state = S_INTLIT;
                            stream_.ReadNext();
                            continue;
                        }
                        else if (isalpha(c) || c == '_') {
                            state = S_IDENT;
                            continue;
                        }
                        else if (c == '"') {
                            state = S_QUOTE;
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '%') {
                            Emit(line, col, Token::PERCENT);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '[') {
                            Emit(line, col, Token::LBRACKET);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == ']') {
                            Emit(line, col, Token::RBRACKET);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '=') {
                            Emit(line, col, Token::EQUALS);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == ',') {
                            Emit(line, col, Token::COMMA);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == ':') {
                            Emit(line, col, Token::COLON);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '@') {
                            Emit(line, col, Token::AT);
                            stream_.ReadNext();
                            continue;
                        }
                        else if (c == '+') {
                            Emit(line, col, Token::PLUS);
                            stream_.ReadNext();
                            continue;
                        }
                        else {
                            stream_.ReadNext();
                            continue;
                        }
                        break;
                    case S_INTLIT:
                        if (!eof && ((isdigit(c) || c == 'x' || c == 'X') ||
                                      (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
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
            return Have();
        }
    private:
        PeekableStream stream_;
        Token token_;
        bool have_peek_;

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
        void Emit(int line, int col, Token::Type type, bignum literal) {
            Emit(line, col, type);
            token_.int_literal = literal;
        }
        void Emit(int line, int col, Token::Type type, string s) {
            Emit(line, col, type);
            token_.s = s;
        }

        bignum ParseInt(string s) {
            try {
                bignum n(s);
                return n;
            } catch (std::runtime_error) {
                bignum n(0);
                return n;
            }
        }
};

class Parser {
    public:
        Parser(string filename, Lexer* lexer, ErrorCollector* collector)
            : filename_(filename), lexer_(lexer), collector_(collector) {}

        bool ParseProgram(IRProgram* program);

    protected:
        bool ParseBB(IRProgram* program);
        bool ParseBBLabel(IRProgram* program, string* label,
                          bool* is_entry, Location* loc);

        bool ParseIRStmt(IRProgram* program, IRBB* bb);
        bool ParseIRStmtTimingAnchor(IRProgram* program, IRStmt* stmt);

    //private:
    public:
        string filename_;
        Lexer* lexer_;
        ErrorCollector* collector_;
        bool have_errors_;
        map<string, IRBB*> bb_map_;  // BB label to BB
        map<int, IRStmt*> ir_map_;  // valnum to stmt

        Token CurToken() const {
            if (lexer_->Have()) {
                return lexer_->Peek();
            } else {
                static Token eof_token(Token::EOFTOKEN);
                return eof_token;
            }
        }

        Location CurLocation() const {
            Location loc;
            loc.filename = filename_;
            loc.line = CurToken().line;
            loc.column = CurToken().col;
            return loc;
        }

        void Error(string message) {
            collector_->ReportError(CurLocation(),
                    ErrorCollector::ERROR, message);
            have_errors_ = true;
        }

        bool TryExpect(Token::Type type) {
            return (CurToken().type == type);
        }
        bool Expect(Token::Type type) {
            if (!TryExpect(type)) {
                Error(string("Unexpected token ") + CurToken().ToString());
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

bool Parser::ParseProgram(IRProgram* program) {
    while (ParseBB(program)) /* nothing */ ;
    return true;
}

bool Parser::ParseBB(IRProgram* program) {
    string label;
    bool is_entry = false;
    unique_ptr<IRBB> bb(new IRBB);
    if (!ParseBBLabel(program, &label, &is_entry, &bb->location)) return false;

    bb->label = label;
    bb->is_entry = is_entry;
    while (ParseIRStmt(program, bb.get())) /* nothing */ ;

    bb_map_[label] = bb.get();
    if (bb->is_entry) {
        program->entries.push_back(bb.get());
    }
    program->bbs.push_back(move(bb));
    return true;
}
bool Parser::ParseBBLabel(IRProgram* program, string* label,
        bool* is_entry, Location* loc) {
    while (TryConsume(Token::NEWLINE)) /* nothing */ ;
    *loc = CurLocation();
    if (!TryExpect(Token::IDENT)) return false;
    if (CurToken().s == "entry") {
        *is_entry = true;
        Consume();
        if (!Expect(Token::IDENT)) return false;
    }
    *label = CurToken().s;
    Consume();
    if (!Consume(Token::COLON)) return false;
    if (!Consume(Token::NEWLINE)) return false;
    return true;
}

enum StmtArg {
    StmtArgConst,
    StmtArgValnum,
    StmtArgValnums,
    StmtArgPortname,
    StmtArgBBname,
    StmtArgBBnameValnumPairs,
    StmtArgNone,
};

bool IdentToStatementType(string ident,
                          IRStmtType* type,
                          IRStmtOp* op,
                          vector<StmtArg>* args) {
#define S_(keyword_, type_, op_, ...)   \
    do {                                \
        if (ident == keyword_) {        \
            *type = type_;              \
            *op = op_;                  \
            *args = { __VA_ARGS__ };    \
            return true;                \
        }                               \
    } while(0)
#define SE(keyword_, op_, ...) S_(keyword_, IRStmtExpr, op_, __VA_ARGS__)
#define S(keyword_, type_, ...) S_(keyword_, type_, IRStmtOpNone, __VA_ARGS__)

    SE("const", IRStmtOpConst,  StmtArgConst);
    SE("add", IRStmtOpAdd,  StmtArgValnum, StmtArgValnum);
    SE("sub", IRStmtOpSub,  StmtArgValnum, StmtArgValnum);
    SE("mul", IRStmtOpMul,  StmtArgValnum, StmtArgValnum);
    SE("div", IRStmtOpDiv,  StmtArgValnum, StmtArgValnum);
    SE("rem", IRStmtOpRem,  StmtArgValnum, StmtArgValnum);
    SE("and", IRStmtOpAnd,  StmtArgValnum, StmtArgValnum);
    SE("or",  IRStmtOpOr,   StmtArgValnum, StmtArgValnum);
    SE("xor", IRStmtOpXor,  StmtArgValnum, StmtArgValnum);
    SE("not", IRStmtOpNot,  StmtArgValnum);
    SE("lsh", IRStmtOpLsh,  StmtArgValnum, StmtArgValnum);
    SE("rsh", IRStmtOpRsh,  StmtArgValnum, StmtArgValnum);
    SE("bsl", IRStmtOpBitslice,  StmtArgValnum, StmtArgValnum, StmtArgValnum);
    SE("cat", IRStmtOpConcat,  StmtArgValnums);
    SE("sel", IRStmtOpConcat,  StmtArgValnum, StmtArgValnum, StmtArgValnum);
    SE("cmplt", IRStmtOpCmpLT,  StmtArgValnum, StmtArgValnum);
    SE("cmple", IRStmtOpCmpLE,  StmtArgValnum, StmtArgValnum);
    SE("cmpeq", IRStmtOpCmpEQ,  StmtArgValnum, StmtArgValnum);
    SE("cmpne", IRStmtOpCmpNE,  StmtArgValnum, StmtArgValnum);
    SE("cmpgt", IRStmtOpCmpGT,  StmtArgValnum, StmtArgValnum);
    SE("cmpge", IRStmtOpCmpGE,  StmtArgValnum, StmtArgValnum);

    S("phi", IRStmtPhi,  StmtArgBBnameValnumPairs);
    S("if", IRStmtIf,  StmtArgValnum, StmtArgBBname, StmtArgBBname);
    S("jmp", IRStmtJmp,  StmtArgBBname);

    S("portread", IRStmtPortRead,  StmtArgPortname);
    S("portwrite", IRStmtPortWrite,  StmtArgPortname, StmtArgValnum);
    S("chanread", IRStmtChanRead, StmtArgPortname);
    S("chanwrite", IRStmtChanWrite, StmtArgPortname, StmtArgValnum);
    S("portexport", IRStmtPortExport, StmtArgPortname);

    S("regread", IRStmtRegRead, StmtArgPortname);
    S("regwrite", IRStmtRegWrite, StmtArgPortname, StmtArgValnum);
    S("arrayread", IRStmtArrayRead,  StmtArgPortname, StmtArgValnum);
    S("arraywrite", IRStmtArrayWrite,  StmtArgPortname, StmtArgValnum, StmtArgValnum);

    S("provide", IRStmtProvide, StmtArgPortname, StmtArgValnum, StmtArgValnum);
    S("unprovide", IRStmtUnprovide, StmtArgValnum);  // arg is provide's return value
    S("ask", IRStmtAsk, StmtArgPortname, StmtArgValnum);

    S("spawn", IRStmtSpawn,  StmtArgBBname);
    S("kill", IRStmtKill,  StmtArgNone);
    S("killyounger", IRStmtKillYounger, StmtArgNone);
    S("done", IRStmtDone, StmtArgNone);

    S("timing_barrier", IRStmtTimingBarrier, StmtArgNone);

    return false;

#undef S
#undef SO
}

bool Parser::ParseIRStmt(IRProgram* program, IRBB* bb) {
    while (TryConsume(Token::NEWLINE)) /* nothing */ ;
    if (!TryExpect(Token::PERCENT)) return false;
    if (!Consume()) return false;
    if (!Expect(Token::INT_LITERAL)) return false;
    int value_number = static_cast<int>(CurToken().int_literal);
    if (value_number <= 0) {
        Error("Value number must be positive");
        return false;
    }
    int width = 0;
    if (!Consume()) return false;
    if (TryExpect(Token::LBRACKET)) {
        Consume();
        if (TryExpect(Token::INT_LITERAL)) {
            width = static_cast<int>(CurToken().int_literal);
            Consume();
        } else if (TryExpect(Token::IDENT) && CurToken().s == "txn") {
            width = kIRStmtWidthTxnID;
            Consume();
        } else {
            Error("Width must be an integer number of bits or 'txn'");
        }
        Consume(Token::RBRACKET);
    }
    if (!Consume(Token::EQUALS)) return false;
    if (!Expect(Token::IDENT)) return false;

    IRStmtType stmt_type;
    IRStmtOp stmt_op;
    vector<StmtArg> args;
    if (!IdentToStatementType(CurToken().s, &stmt_type, &stmt_op, &args)) {
        Error(string("Unknown IR statement type '") + CurToken().s + string("'"));
        return false;
    }
    Location loc = CurLocation();
    if (!Consume(Token::IDENT)) return false;

    unique_ptr<IRStmt> stmt(new IRStmt);
    stmt->valnum = value_number;
    if (value_number >= program->next_valnum) {
        program->next_valnum = value_number + 1;
    }
    stmt->type = stmt_type;
    stmt->op = stmt_op;
    stmt->bb = bb;
    stmt->width = width;
    stmt->location = loc;

    bool first = true;
    for (auto arg : args) {
        if (!first) {
            if (!Consume(Token::COMMA)) return false;
        }
        first = false;

        switch (arg) {
            case StmtArgConst:
                if (!Expect(Token::INT_LITERAL)) return false;
                stmt->constant = CurToken().int_literal;
                stmt->has_constant = true;
                Consume();
                break;

            case StmtArgValnum:
                if (!Consume(Token::PERCENT)) return false;
                if (!Expect(Token::INT_LITERAL)) return false;
                stmt->arg_nums.push_back(static_cast<int>(CurToken().int_literal));
                Consume();
                break;

            case StmtArgValnums:
                while (true) {
                    if (!TryConsume(Token::PERCENT)) break;
                    if (!Expect(Token::INT_LITERAL)) return false;
                    stmt->arg_nums.push_back(static_cast<int>(CurToken().int_literal));
                    Consume();
                    if (!TryConsume(Token::COMMA)) break;
                }
                break;

            case StmtArgPortname:
                if (!Expect(Token::QUOTED_STRING)) return false;
                stmt->port_name = CurToken().s;
                Consume();
                break;

            case StmtArgBBname:
                if (!Expect(Token::IDENT)) return false;
                stmt->target_names.push_back(CurToken().s);
                Consume();
                break;

            case StmtArgBBnameValnumPairs:
                while (true) {
                    if (!TryExpect(Token::IDENT)) break;
                    string bbname = CurToken().s;
                    Consume();
                    if (!Consume(Token::COMMA)) return false;
                    if (!Consume(Token::PERCENT)) return false;
                    if (!Expect(Token::INT_LITERAL)) return false;
                    int valnum = static_cast<int>(CurToken().int_literal);
                    Consume();
                    stmt->target_names.push_back(bbname);
                    stmt->arg_nums.push_back(valnum);
                    if (!TryConsume(Token::COMMA)) break;
                }
                break;

            case StmtArgNone:
                break;
        }
    }

    if (TryExpect(Token::AT)) {
        if (!ParseIRStmtTimingAnchor(program, stmt.get())) return false;
    }

    if (!Consume(Token::NEWLINE)) return false;

    ir_map_[value_number] = stmt.get();
    bb->stmts.push_back(move(stmt));
    return true;
}

bool Parser::ParseIRStmtTimingAnchor(IRProgram* program, IRStmt* stmt) {
    if (!Consume(Token::AT)) return false;
    if (!Consume(Token::LBRACKET)) return false;
    if (!Expect(Token::IDENT)) return false;
    string anchor_name = CurToken().s;
    Consume();
    int offset = 0;
    if (TryConsume(Token::PLUS)) {
        if (!Expect(Token::INT_LITERAL)) return false;
        offset = static_cast<int>(CurToken().int_literal);
        Consume();
    }
    if (!Consume(Token::RBRACKET)) return false;

    IRTimeVar* timevar;
    if (program->timevar_map.find(anchor_name) == program->timevar_map.end()) {
        unique_ptr<IRTimeVar> new_timevar(new IRTimeVar());
        timevar = new_timevar.get();
        new_timevar->name = anchor_name;
        program->timevar_map[anchor_name] = new_timevar.get();
        program->timevars.push_back(move(new_timevar));
    } else {
        timevar = program->timevar_map[anchor_name];
    }
    stmt->timevar = timevar;
    stmt->time_offset = offset;
    timevar->uses.push_back(stmt);

    return true;
}

}  // anonymous namespace

unique_ptr<IRProgram> IRProgram::Parse(const std::string& filename,
                                       std::istream* in,
                                       ErrorCollector* collector) {
    Lexer lex(in);
    Parser parse(filename, &lex, collector);

    unique_ptr<IRProgram> ptr(new IRProgram);
    if (parse.ParseProgram(ptr.get()) && !collector->HasErrors()) {
        return move(ptr);
    } else {
        ptr.reset();
        return move(ptr);
    }
}
