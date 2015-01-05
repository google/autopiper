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

#include "backend/ir.h"
#include "backend/compiler.h"

#include "common/parser-utils.h"

#include <map>
#include <iostream>
#include <assert.h>
#include <ctype.h>

using namespace std;
using namespace autopiper;

namespace {

class Parser : public ParserBase {
    public:
        Parser(string filename, Lexer* lexer, ErrorCollector* collector)
            : ParserBase(filename, lexer, collector) {}

        bool ParseProgram(IRProgram* program);

    protected:
        bool ParseBB(IRProgram* program);
        bool ParseBBLabel(IRProgram* program, string* label,
                          bool* is_entry, Location* loc);

        bool ParseIRStmt(IRProgram* program, IRBB* bb);
        bool ParseIRStmtTimingAnchor(IRProgram* program, IRStmt* stmt);

    //private:
    public:
        map<string, IRBB*> bb_map_;  // BB label to BB
        map<int, IRStmt*> ir_map_;  // valnum to stmt
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
    S("killif", IRStmtKillIf, StmtArgValnum);

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
    LexerImpl lex(in);
    Parser parse(filename, &lex, collector);

    unique_ptr<IRProgram> ptr(new IRProgram);
    if (parse.ParseProgram(ptr.get()) && !collector->HasErrors()) {
        return move(ptr);
    } else {
        ptr.reset();
        return move(ptr);
    }
}
