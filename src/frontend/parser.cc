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

#include "frontend/ast.h"
#include "frontend/parser.h"

// TODO: make this a little nicer with some parser combinator-like helpers.
// Each term function should return a unique_ptr of its parse node or failure.
// We should have an easy helper (macro) to call a term function and place the
// result in a field in the result, returning a failure if term function
// returns a failure.

namespace autopiper {
namespace frontend {

bool Parser::Parse(AST* ast) {
    // A program is a series of defs.
    if (TryConsume(Token::EOFTOKEN)) {
        return true;
    }

    Expect(Token::IDENT);

    if (CurToken().s == "type") {
        Consume();
        ASTRef<ASTTypeDef> td(new ASTTypeDef());
        if (!ParseTypeDef(td.get())) {
            return false;
        }
        ast->types.push_back(move(td));
    } else if (CurToken().s == "func") {
        Consume();
        ASTRef<ASTFunctionDef> fd(new ASTFunctionDef());
        if (!ParseFuncDef(fd.get())) {
            return false;
        }
        ast->functions.push_back(move(fd));
    } else {
        Error("Expected 'type' or 'func' keyword.");
        return false;
    }

    return true;
}

bool Parser::ParseFuncDef(ASTFunctionDef* def) {
    def->name.reset(new ASTIdent());
    if (!Expect(Token::IDENT)) {
        return false;
    }
    if (CurToken().s == "entry") {
        def->is_entry = true;
        Consume();
        if (!Expect(Token::IDENT)) {
            return false;
        }
    }

    if (!ParseIdent(def->name.get())) {
        return false;
    }

    if (!Consume(Token::LPAREN)) {
        return false;
    }
    if (!ParseFuncArgList(def)) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    if (!Consume(Token::COLON)) {
        return false;
    }
    def->return_type.reset(new ASTType());
    if (!ParseType(def->return_type.get())) {
        return false;
    }

    def->block.reset(new ASTStmtBlock());
    if (!ParseBlock(def->block.get())) {
        return false;
    }

    return true;
}

bool Parser::ParseFuncArgList(ASTFunctionDef* def) {
    while (true) {
        if (CurToken().type == Token::RPAREN) {
            break;
        }

        ASTRef<ASTParam> param(new ASTParam());
        param->ident.reset(new ASTIdent());
        if (!ParseIdent(param->ident.get())) {
            return false;
        }
        if (!Consume(Token::COLON)) {
            return false;
        }
        param->type.reset(new ASTType());
        if (!ParseType(param->type.get())) {
            return false;
        }
    }

    return true;
}

bool Parser::ParseBlock(ASTStmtBlock* block) {
    if (!Consume(Token::LBRACE)) {
        return false;
    }

    while (true) {
        if (CurToken().type == Token::RBRACE) {
            break;
        }

        ASTRef<ASTStmt> stmt(new ASTStmt());
        if (!ParseStmt(stmt.get())) {
            return false;
        }
        block->stmts.push_back(move(stmt));
    }

    if (!Consume(Token::RBRACE)) {
        return false;
    }

    return true;
}

bool Parser::ParseTypeDef(ASTTypeDef* def) {
    def->ident.reset(new ASTIdent());
    if (!ParseIdent(def->ident.get())) {
        return false;
    }
    if (!Consume(Token::LBRACE)) {
        return false;
    }
    while (true) {
        if (TryConsume(Token::RBRACE)) {
            break;
        }
        ASTRef<ASTTypeField> field;
        field->ident.reset(new ASTIdent());
        if (!ParseIdent(field->ident.get())) {
            return false;
        }
        if (!Consume(Token::COLON)) {
            return false;
        }
        field->type.reset(new ASTType());
        if (!ParseType(field->type.get())) {
            return false;
        }
        if (!Consume(Token::SEMICOLON)) {
            return false;
        }
        def->fields.push_back(move(field));
    }
    return true;
}

bool Parser::ParseIdent(ASTIdent* id) {
    if (!Expect(Token::IDENT)) {
        return false;
    }
    id->name = CurToken().s;
    Consume();
    return true;
}

bool Parser::ParseType(ASTType* ty) {
    if (!Expect(Token::IDENT)) {
        return false;
    }
    if (CurToken().s == "port") {
        ty->is_port = true;
        Consume();
        if (!Expect(Token::IDENT)) {
            return false;
        }
    }
    ty->ident.reset(new ASTIdent());
    if (!ParseIdent(ty->ident.get())) {
        return false;
    }
    return true;
}

bool Parser::ParseStmt(ASTStmt* st) {
    if (TryExpect(Token::LBRACE)) {
        st->block.reset(new ASTStmtBlock());
        return ParseBlock(st->block.get());
    }

#define HANDLE_STMT_TYPE(str, field, name)                   \
    if (TryExpect(Token::IDENT) && CurToken().s == str) {    \
        Consume();                                           \
        st-> field .reset(new ASTStmt ## name());            \
        return ParseStmt ## name(st-> field .get());         \
    }

    HANDLE_STMT_TYPE("let", let, Let);
    HANDLE_STMT_TYPE("if", if_, If);
    HANDLE_STMT_TYPE("while", while_, While);
    HANDLE_STMT_TYPE("break", break_, Break);
    HANDLE_STMT_TYPE("continue", continue_, Continue);
    HANDLE_STMT_TYPE("write", write, Write);
    HANDLE_STMT_TYPE("spawn", spawn, Spawn);

#undef HANDLE_STMT_TYPE

    // No keywords matched, so we must be seeing the left-hand side identifier
    // in an assignment.
    st->assign.reset(new ASTStmtAssign());
    return ParseStmtAssign(st->assign.get());
}

bool Parser::ParseStmtLet(ASTStmtLet* let) {
    // parent already consumed "def" keyword.
    
    let->lhs.reset(new ASTIdent());
    if (!ParseIdent(let->lhs.get())) {
        return false;
    }

    if (TryConsume(Token::COLON)) {
        let->type.reset(new ASTType());
        if (!ParseType(let->type.get())) {
            return false;
        }
    }

    if (!Consume(Token::EQUALS)) {
        return false;
    }

    let->rhs.reset(new ASTExpr());
    if (!ParseExpr(let->rhs.get())) {
        return false;
    }

    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtAssign(ASTStmtAssign* assign) {
    assign->lhs.reset(new ASTIdent());
    if (!ParseIdent(assign->lhs.get())) {
        return false;
    }

    if (!Consume(Token::EQUALS)) {
        return false;
    }

    assign->rhs.reset(new ASTExpr());
    if (!ParseExpr(assign->rhs.get())) {
        return false;
    }

    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtIf(ASTStmtIf* if_) {
    if (!Consume(Token::LPAREN)) {
        return false;
    }
    if_->condition.reset(new ASTExpr());
    if (!ParseExpr(if_->condition.get())) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    if_->if_body.reset(new ASTStmt());
    if (!ParseStmt(if_->if_body.get())) {
        return false;
    }
    if (TryExpect(Token::IDENT) && CurToken().s == "else") {
        Consume();
        if_->else_body.reset(new ASTStmt());
        if (!ParseStmt(if_->else_body.get())) {
            return false;
        }
    }

    return true;
}

bool Parser::ParseStmtWhile(ASTStmtWhile* while_) {
    if (!Consume(Token::LPAREN)) {
        return false;
    }
    while_->condition.reset(new ASTExpr());
    if (!ParseExpr(while_->condition.get())) {
        return false;
    }
    if (!Consume(Token::RPAREN)) {
        return false;
    }
    while_->body.reset(new ASTStmt());
    if (!ParseStmt(while_->body.get())) {
        return false;
    }
    return true;
}

bool Parser::ParseStmtBreak(ASTStmtBreak* break_) {
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtContinue(ASTStmtContinue* continue_) {
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtWrite(ASTStmtWrite* write) {
    write->port.reset(new ASTIdent());
    if (!ParseIdent(write->port.get())) {
        return false;
    }
    write->rhs.reset(new ASTExpr());
    if (!ParseExpr(write->rhs.get())) {
        return false;
    }
    return Consume(Token::SEMICOLON);
}

bool Parser::ParseStmtSpawn(ASTStmtSpawn* spawn) {
    spawn->body.reset(new ASTStmt());
    return ParseStmt(spawn->body.get());
}

bool Parser::ParseExpr(ASTExpr* expr) {
    // Supported:
    // - Arithmetic infix and prefix operators
    // - Variable references, type field references, array slot references
    // - port reads ('read' expressions)
    // - port type objects ('port' expressions, with or without port name)
    //
    // We use a basic shunting-yard algorithm here:
    // - Keep a stack of subexpressions.
    // - Keep a stack of operators.
    // - When we encounter (i) a unary operator or (b) an atom (left paren,
    //   variable reference, port read, ...) then we push this onto the stack of
    //   subexpressions.
    // - When we encounter an operator:
    //   - If the operator has higher precedence than the top-of-stack operator
    //     (or the operator stack is empty), we push the operator on the stack.
    //   - If the operator has equal or lower precedence than the top-of-stack
    //     operator, pop the top-of-stack operator and two operands from the
    //     subexpression (operand) stack, combine, and push the result on the
    //     subexpression stack.
    // - When we encounter the end of the expression (right paren, any other
    //   unrecognized token), we reduce remaining operators on the operator
    //   stack.
    // - There should be only one expression left on the subexpression stack.
    //   If more or less than one, error. Return that expression if no error.
    
    if (TryExpect(Token::IDENT) && CurToken().s == "read") {
        Consume();
        expr->op = ASTExpr::PORTREAD;
        expr->ident.reset(new ASTIdent());
        if (!ParseIdent(expr->ident.get())) {
            return false;
        }
    }

    return false;
}

}  // namespace frontend
}  // namespace autopiper
