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

using namespace std;

namespace autopiper {
namespace frontend {

FuncInlinePass::FuncInlinePass(autopiper::ErrorCollector* coll)
    : ASTVisitorContext(coll), visit_in_stmt_(nullptr)
{ }

bool FuncInlinePass::VisitASTFunctionDefPre(const ASTFunctionDef* node) {
    function_defs_.insert(make_pair(node->name->name, node));
    return true;
}

bool FuncInlinePass::VisitASTStmtPre(const ASTStmt* node) {
    visit_in_stmt_ = node;
    return true;
}

bool FuncInlinePass::VisitASTExprPre(const ASTExpr* node) {
    if (node->op == ASTExpr::FUNCCALL) {
        const string& name = node->ident->name;
        auto it = function_defs_.find(name);
        if (it == function_defs_.end()) {
            Error(node, strprintf("No such function: %s", name.c_str()));
            return false;
        }
        function_calls_.insert(make_pair(node, it->second));
        stmts_containing_func_calls_.insert(visit_in_stmt_);
    }
    return true;
}

ASTRef<ASTStmt> FuncInlinePass::ModifyASTStmtPre(ASTRef<ASTStmt> node) {
    modify_in_stmt_ = node.get();
    return node;
}

ASTRef<ASTExpr> FuncInlinePass::ModifyASTExprPre(ASTRef<ASTExpr> node) {
    expr_parent_[node.get()] = modify_in_stmt_;
    return node;
}

ASTRef<ASTStmt> FuncInlinePass::ModifyASTStmtPost(ASTRef<ASTStmt> node) {
    // If this statement contains a function call, ensure it's wrapped in a
    // block into which we can inline the function body.
    if (stmts_containing_func_calls_.find(node.get()) !=
        stmts_containing_func_calls_.end()) {
        ASTRef<ASTStmtBlock> block(new ASTStmtBlock());
        parent_block_map_.insert(make_pair(node.get(), block.get()));
        block->stmts.push_back(move(node));

        ASTRef<ASTStmt> box(new ASTStmt());
        box->block = move(block);
        return box;
    } else {
        return node;
    }
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

        virtual ASTRef<ASTStmt> ModifyASTStmtPost(ASTRef<ASTStmt> node) {
            if (node->return_) {
                ASTRef<ASTStmtBlock> block(new ASTStmtBlock());
                ASTRef<ASTStmtAssign> assign(new ASTStmtAssign());
                ASTRef<ASTStmtBreak> break_(new ASTStmtBreak());
                assign->lhs = CloneAST(return_var_);
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
                return block_box;
            } else {
                return node;
            }
        }

        void AddArgLets(ASTStmtBlock* parent) {
            ASTVector<ASTStmt> lets;
            int i = 0;
            for (auto& param : func_->params) {
                ASTRef<ASTStmtLet> let_stmt(new ASTStmtLet());
                let_stmt->lhs = CloneAST(param->ident.get());
                let_stmt->rhs = move(args_[i++]);
                ASTRef<ASTStmt> box(new ASTStmt());
                box->let =  move(let_stmt);
                lets.push_back(move(box));
            }

            ASTInsertStmts(parent,
                    // insert at beginning
                    parent->stmts.empty() ? nullptr : parent->stmts[0].get(),
                    move(lets));
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
        ASTStmtBlock* parent_block,
        ASTStmt* before,
        const ASTIdent* return_var_ident,
        ASTVector<ASTExpr>&& args,
        ErrorCollector* coll) {
    // Ensure arg count matches.
    if (args.size() != func->params.size()) {
        coll->ReportError(before->loc, autopiper::ErrorCollector::ERROR,
                "Function call arity mismatch");
        return false;
    }

    // Create a 'while' stmt wrapping the function body.
    ASTRef<ASTStmtWhile> while_stmt(new ASTStmtWhile());
    while_stmt->label = ASTGenSym(ast);
    while_stmt->condition.reset(new ASTExpr(1));
    while_stmt->body.reset(new ASTStmt());
    while_stmt->body->block = CloneAST(func->block.get());
    ASTRef<ASTStmt> break_stmt;
    break_stmt->break_.reset(new ASTStmtBreak());
    break_stmt->break_->label = CloneAST(while_stmt->label.get());
    while_stmt->body->block->stmts.push_back(move(break_stmt));

    // Replace all 'return' stmts in function body with assignment to return
    // value variable and a labeled break. Insert lets for all args in the
    // function scope.
    ArgsReturnReplacer replacer(
            func, move(args),
            return_var_ident, while_stmt->label.get());
    ASTVisitor visitor;
    while_stmt->body->block = visitor.ModifyASTStmtBlock(
            move(while_stmt->body->block), &replacer);
    replacer.AddArgLets(while_stmt->body->block.get());
    assert(while_stmt->body->block);

    // Insert the 'while' body immediately before the stmt that contains the call.
    ASTVector<ASTStmt> stmts;
    stmts.emplace_back(new ASTStmt());
    stmts[0]->while_ = move(while_stmt);
    ASTInsertStmts(parent_block, before, move(stmts));

    return true;
}
}  // anonymous namespace

ASTRef<ASTExpr> FuncInlinePass::ModifyASTExprPost(ASTRef<ASTExpr> node) {
    if (node->op == ASTExpr::FUNCCALL) {
        auto stmt = expr_parent_[node.get()];
        auto parent_block = parent_block_map_[stmt];
        // Create a temporary for the return value.
        ASTRef<ASTExpr> initial_value(new ASTExpr(0));
        auto return_ident_and_expr =
            ASTDefineTemp(ast_, parent_block, stmt, move(initial_value));
        auto return_ident = return_ident_and_expr.first;
        auto return_expr = move(return_ident_and_expr.second);
        // Clone the body.
        auto func = function_calls_[node.get()];
        if (!InlineFunctionBody(
                    ast_, func, parent_block,
                    stmt, return_ident, move(node->ops), Errors())) {
            return ASTRef<ASTExpr>(nullptr);
        }
        // Replace use of return value with use of the temp we created.
        return return_expr;
    } else {
        return node;
    }
}

}  // namesapce frontend
}  // namespace autopiper
