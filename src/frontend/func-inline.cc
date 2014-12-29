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

#include "frontend/func-inline.h"
#include "common/util.h"
#include "common/exception.h"

using namespace std;

namespace autopiper {
namespace frontend {

FuncInlinePass::FuncInlinePass(autopiper::ErrorCollector* coll)
    : ASTVisitorContext(coll)
{ }

FuncInlinePass::Result
FuncInlinePass::VisitASTFunctionDefPre(const ASTFunctionDef* node) {
    function_defs_.insert(make_pair(node->name->name, node));
    return VISIT_CONTINUE;
}

namespace {
class ArgsReturnReplacer : public ASTVisitorContext {
    public:
        ArgsReturnReplacer(
                const ASTFunctionDef* func,
                ASTVector<ASTExpr>&& args,
                const ASTIdent* return_var,
                const ASTIdent* while_label)
            : func_(func), args_(move(args)),
              return_var_(return_var), while_label_(while_label)
        {}

        virtual Result ModifyASTStmtPost(ASTRef<ASTStmt>& node) {
            if (node->return_) {
                // Replace a "return <expr>" statement with this:
                // {
                //     return_value = <expr>;
                //     break <body-while-wrapper>;
                // }
                ASTRef<ASTStmtBlock> block(new ASTStmtBlock());
                ASTRef<ASTStmtAssign> assign(new ASTStmtAssign());
                ASTRef<ASTStmtBreak> break_(new ASTStmtBreak());
                assign->lhs.reset(new ASTExpr());
                assign->lhs->op = ASTExpr::VAR;
                assign->lhs->ident = CloneAST(return_var_);
                assign->rhs = move(node->return_->value);
                break_->label = CloneAST(while_label_);
 
                ASTRef<ASTStmt> assign_box(new ASTStmt());
                assign_box->assign = move(assign);
                ASTRef<ASTStmt> break_box(new ASTStmt());
                break_box->break_ = move(break_);
                block->stmts.push_back(move(assign_box));
                block->stmts.push_back(move(break_box));

                ASTRef<ASTStmt> block_box(new ASTStmt());
                block_box->block = move(block);
                node = move(block_box);
            }
            return VISIT_CONTINUE;
        }

        void AddArgLets(ASTStmtBlock* parent) {
            ASTVector<ASTStmt> lets;
            int i = 0;
            for (auto& param : func_->params) {
                ASTRef<ASTStmtLet> let_stmt(new ASTStmtLet());
                let_stmt->lhs = CloneAST(param->ident.get());
                let_stmt->rhs = move(args_[i++]);
                let_stmt->type = CloneAST(param->type.get());
                ASTRef<ASTStmt> box(new ASTStmt());
                box->let =  move(let_stmt);
                parent->stmts.push_back(move(box));
            }
        }

    private:
        const ASTFunctionDef* func_;
        ASTVector<ASTExpr> args_;
        const ASTIdent* return_var_;
        const ASTIdent* while_label_;
};

bool InlineFunctionBody(
        AST* ast,
        const ASTFunctionDef* func,
        ASTExpr* call_expr,
        ASTStmtBlock* parent_block,
        const ASTIdent* return_var_ident,
        ASTVector<ASTExpr>&& args,
        ErrorCollector* coll) {
    // Ensure arg count matches.
    if (args.size() != func->params.size()) {
        coll->ReportError(call_expr->loc, autopiper::ErrorCollector::ERROR,
                "Function call arity mismatch");
        return false;
    }

    // Create a 'while' stmt wrapping the function body.
    ASTRef<ASTStmtWhile> while_stmt(new ASTStmtWhile());
    while_stmt->label = ASTGenSym(ast, "func_body");
    while_stmt->condition.reset(new ASTExpr(1));
    while_stmt->condition->op = ASTExpr::CONST;
    while_stmt->condition->constant = 1;
    while_stmt->body.reset(new ASTStmt());
    while_stmt->body->block.reset(new ASTStmtBlock());

    // Start with a sequence of let-statements that define arg values as locals
    // (ordinary scope resolution will then see these when used by the function
    // body).
    ASTVisitor visitor;
    ArgsReturnReplacer replacer(
            func, move(args),
            return_var_ident, while_stmt->label.get());
    replacer.AddArgLets(while_stmt->body->block.get());

    // Insert clones of the function body's statements.
    for (auto& body_stmt : func->block->stmts) {
        while_stmt->body->block->stmts.push_back(CloneAST(body_stmt.get()));
    }

    // Replace all 'return' stmts in function body with assignment to return
    // value variable and a labeled break.
    if (!visitor.ModifyASTStmtBlock(while_stmt->body->block, &replacer)) {
        return false;
    }

    // End with a break statement to exit the enclosing while. This occurs if
    // control falls through the end of the function without an explicit
    // 'return'.
    ASTRef<ASTStmt> break_stmt(new ASTStmt());
    break_stmt->break_.reset(new ASTStmtBreak());
    break_stmt->break_->label = CloneAST(while_stmt->label.get());
    while_stmt->body->block->stmts.push_back(move(break_stmt));

    ASTRef<ASTStmt> while_stmt_box(new ASTStmt());
    while_stmt_box->while_ = move(while_stmt);
    parent_block->stmts.push_back(move(while_stmt_box));

    return true;
}
}  // anonymous namespace

FuncInlinePass::Result
FuncInlinePass::ModifyASTExprPost(ASTRef<ASTExpr>& node) {
    if (node->op == ASTExpr::FUNCCALL) {
        // Look up the function.
        const string& name = node->ident->name;
        auto func = function_defs_[name];
        if (!func) {
            Error(node.get(), strprintf("Unknown function '%s'", name.c_str()));
            return VISIT_END;
        }

        // Create a block that will become part of an expression block.
        ASTRef<ASTStmtBlock> block(new ASTStmtBlock());
        // Create a temporary for the return value.
        ASTRef<ASTExpr> initial_value(new ASTExpr(0));
        auto return_ident_and_expr =
            ASTDefineTemp(ast_, "return_value_",
                          block.get(), move(initial_value),
                          CloneAST(func->return_type.get()));
        auto return_ident = return_ident_and_expr.first;
        auto return_expr = move(return_ident_and_expr.second);

        // Clone the body.
        if (!InlineFunctionBody(
                    ast_, func, node.get(), block.get(),
                    return_ident, move(node->ops), Errors())) {
            return VISIT_END;
        }

        // Add a final expression statement with the value of the return-value
        // temp.
        ASTRef<ASTStmt> return_expr_stmt(new ASTStmt());
        return_expr_stmt->expr.reset(new ASTStmtExpr());
        return_expr_stmt->expr->expr = move(return_expr);
        block->stmts.push_back(move(return_expr_stmt));

        // Replace use of return value with use of the temp we created.
        node.reset(new ASTExpr());
        node->op = ASTExpr::STMTBLOCK;
        node->stmt = move(block);
    }
    return VISIT_CONTINUE;
}

}  // namesapce frontend
}  // namespace autopiper
