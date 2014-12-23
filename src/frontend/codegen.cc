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

#include "frontend/codegen.h"
#include "common/util.h"

#include <sstream>

using namespace std;

namespace autopiper {
namespace frontend {

// -------------------------- CodeGenContext ----------------------------------

CodeGenContext::CodeGenContext() {
    prog_.reset(new IRProgram());
    gensym_ = 1;
    curbb_ = nullptr;
}

std::string CodeGenContext::GenSym() {
    ostringstream os;
    os << "__codegen_gensym__" << gensym_;
    gensym_++;
    return os.str();
}

IRBB* CodeGenContext::AddBB() {
    unique_ptr<IRBB> bb(new IRBB());
    bb->label = GenSym();
    IRBB* ret = bb.get();
    prog_->bbs.push_back(move(bb));
    return ret;
}

IRStmt* CodeGenContext::AddIRStmt(
        IRBB* bb, std::unique_ptr<IRStmt> stmt,
        const ASTExpr* expr) {
    if (expr) {
        expr_to_ir_map_[expr] = stmt.get();
    }
    IRStmt* ret = stmt.get();
    bb->stmts.push_back(move(stmt));
    return ret;
}

// -------------------------- CodeGenPass ----------------------------------

bool CodeGenPass::ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node) {
    // Skip non-entry functions completely.
    if (!node->is_entry) {
        // Remove the stmt block so that codegen traversal doesn't see the
        // stmts.
        node->block.reset(nullptr);
        return true;
    }

    // Start a new BB and mark it as an etry point. Name it after the function.
    IRBB* bb = ctx_->AddBB();
    bb->label = node->name->name;
    bb->is_entry = true;
    ctx_->SetCurBB(bb);

    return true;
}

bool CodeGenPass::ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node) {
    ctx_->Bindings()[node.get()] = node->rhs.get();
    return true;
}

bool CodeGenPass::ModifyASTStmtAssignPost(ASTRef<ASTStmtAssign>& node) {
    if (node->lhs->op == ASTExpr::VAR) {
        // Simple variable assignment.  Associate the binding (let) with the
        // new ASTExpr (the RHS).
        ctx_->Bindings()[node->lhs->def] = node->rhs.get();
    } else if (node->lhs->op == ASTExpr::ARRAY_REF) {
        // Generate an array-write IR stmt.
        // TODO.
        assert(false);
    } else if (node->lhs->op == ASTExpr::FIELD_REF) {
        // This should have been desugared by the type-lower pass to (i)
        // constructing the new aggregate value in a temporary and (ii)
        // assigning to the whole aggregate.
        assert(false);
    } else {
        Error(node.get(),
                "Cannot assign to non-variable, non-array-slot, "
                "non-field-slot lvalue.");
        return false;
    }

    return true;
}

bool CodeGenPass::ModifyASTStmtWritePost(ASTRef<ASTStmtWrite>& node) {
    unique_ptr<IRStmt> write_stmt(new IRStmt());
    write_stmt->valnum = ctx_->Valnum();
    write_stmt->type = IRStmtPortWrite;
    const ASTExpr* portdef = FindPortDef(node->port.get(), node->port.get());
    if (!portdef) {
        return false;
    }
    write_stmt->port_name = portdef->ident->name;
    IRStmt* val = const_cast<IRStmt*>(
            ctx_->GetIRStmt(node->rhs.get()));
    write_stmt->args.push_back(val);
    write_stmt->arg_nums.push_back(val->valnum);
    write_stmt->width = node->rhs->inferred_type.width;
    ctx_->AddIRStmt(ctx_->CurBB(), move(write_stmt));
    return true;
}

static IRStmtOp ExprTypeToOpType(ASTExpr::Op op) {
    switch (op) {
#define T(ast, ir) \
        case ASTExpr:: ast : return IRStmtOp ## ir
        T(ADD, Add);
        T(SUB, Sub);
        T(MUL, Mul);
        T(DIV, Div);
        T(REM, Rem);
        T(AND, And);
        T(OR, Or);
        T(NOT, Not);
        T(XOR, Xor);
        T(LSH, Lsh);
        T(RSH, Rsh);
        T(SEL, Select);
        T(BITSLICE, Bitslice);
        T(CONCAT,Concat);
        T(EQ, CmpEQ);
        T(NE, CmpNE);
        T(LE, CmpLE);
        T(LT, CmpLT);
        T(GE, CmpGE);
        T(GT, CmpGT);
#undef T
        default:
            return IRStmtOpNone;
    }
}

const ASTExpr* CodeGenPass::FindPortDef(
        const ASTExpr* node,
        const ASTExpr* orig) {
    if (node->op == ASTExpr::PORTDEF) {
        return node;
    } else if (node->op == ASTExpr::VAR) {
        const ASTExpr* binding = ctx_->Bindings()[node->def];
        return FindPortDef(binding, orig);
    } else {
        Error(orig,
                "Port value expected but cannot trace back to a port "
                "def statically.");
        assert(false);
        return nullptr;
    }
}

bool CodeGenPass::ModifyASTExprPost(ASTRef<ASTExpr>& node) {
    IRStmtOp op = ExprTypeToOpType(node->op);
    // Is this a 'normal' case where we have a 1-to-1 mapping with an IR stmt?
    if (op != IRStmtOpNone) {
        unique_ptr<IRStmt> stmt(new IRStmt());
        stmt->valnum = ctx_->Valnum();
        stmt->type = IRStmtExpr;
        stmt->op = op;
        stmt->width = node->inferred_type.width;
        for (auto& op : node->ops) {
            IRStmt* op_stmt = const_cast<IRStmt*>(
                    ctx_->GetIRStmt(op.get()));
            stmt->args.push_back(op_stmt);
            stmt->arg_nums.push_back(op_stmt->valnum);
        }
        ctx_->AddIRStmt(ctx_->CurBB(), move(stmt), node.get());
    } else {
        // Handle a few other special cases.
        switch (node->op) {
            case ASTExpr::CONST: {
                unique_ptr<IRStmt> stmt(new IRStmt());
                stmt->type = IRStmtExpr;
                stmt->op = IRStmtOpConst;
                stmt->valnum = ctx_->Valnum();
                stmt->constant = node->constant;
                stmt->has_constant = true;
                stmt->width = node->inferred_type.width;
                ctx_->AddIRStmt(ctx_->CurBB(), move(stmt), node.get());
                break;
            }
            case ASTExpr::VAR: {
                // Simply pass through the current binding.
                const ASTExpr* expr = ctx_->Bindings()[node->def];
                ctx_->AddIRStmt(ctx_->GetIRStmt(expr), node.get());
                break;
            }
            case ASTExpr::PORTDEF: {
                // If the portdef has a user-specified name, it's exported.
                // Else we come up with a name at this point.
                if (!node->ident->name.empty()) {
                    unique_ptr<IRStmt> export_stmt(new IRStmt());
                    export_stmt->valnum = ctx_->Valnum();
                    export_stmt->type = IRStmtPortExport;
                    export_stmt->port_name = node->ident->name;
                    ctx_->AddIRStmt(ctx_->CurBB(), move(export_stmt));
                } else {
                    // Anonymous port: give it a name, but don't mark it as
                    // exported.
                    node->ident->name = ctx_->GenSym();
                }
                break;
            }
            case ASTExpr::PORTREAD: {
                // Get the eventual port def. We trace through VAR->lets but
                // don't support any other static analysis here.
                const ASTExpr* portdef = FindPortDef(
                        node->ops[0].get(), node.get());
                if (!portdef) {
                    return false;
                }
                unique_ptr<IRStmt> read_stmt(new IRStmt());
                read_stmt->valnum = ctx_->Valnum();
                // TODO: come up with a way to mark ports as chans.
                read_stmt->type = IRStmtPortRead;
                read_stmt->port_name = portdef->ident->name;
                read_stmt->width = portdef->inferred_type.width;
                ctx_->AddIRStmt(ctx_->CurBB(), move(read_stmt), node.get());
                break;
            }
            case ASTExpr::STMTBLOCK: {
                // Do codegen for the block -- must be a single-entrance,
                // single-exit region, so we'll be left with a valid CurBB
                // after this -- finding and extracting the value of its last
                // stmt (which must be an Expr stmt).
                const ASTStmtExpr* expr_value = nullptr;
                if (node->stmt->stmts.size() < 1 ||
                    !node->stmt->stmts.back()->expr) {
                    Error(node.get(),
                            "Statement-block expr where last stmt is "
                            "not an expression statement.");
                    return false;
                }
                expr_value = node->stmt->stmts.back()->expr.get();

                CodeGenPass subpass(Errors(), ctx_);
                ASTVisitor subvisitor;
                if (!subvisitor.ModifyASTStmtBlock(node->stmt, &subpass)) {
                    return false;
                }
                node->stmt.reset(nullptr);

                // Associate the final expression statement's value with this
                // whole expression's value.
                ctx_->AddIRStmt(
                        ctx_->GetIRStmt(expr_value->expr.get()),
                        node.get());
                break;
            }
            default:
                Error(node.get(),
                        strprintf("Unsupported node type: %d", node->op));
                assert(false);
                return false;
        }
    }
    return true;
}

bool CodeGenPass::ModifyASTStmtExprPost(ASTRef<ASTStmtExpr>& node) {
    // No further codegen necessary -- we will have already gen'd the IR to
    // compute the underlying expression.
    return true;
}

// If-statement support.

bool CodeGenPass::ModifyASTStmtIfPre(ASTRef<ASTStmtIf>& node) {
    // TODO
    assert(false);
    return false;
}

// While-loop support: while, break, continue

bool CodeGenPass::ModifyASTStmtWhilePre(ASTRef<ASTStmtWhile>& node) {
    assert(false);
    return false;
}

bool CodeGenPass::ModifyASTStmtBreakPost(ASTRef<ASTStmtBreak>& node) {
    assert(false);
    return false;
}

bool CodeGenPass::ModifyASTStmtContinuePost(ASTRef<ASTStmtContinue>& node) {
    assert(false);
    return false;
}

// Spawn support.

bool CodeGenPass::ModifyASTStmtSpawnPre(ASTRef<ASTStmtSpawn>& node) {
    assert(false);
    return false;
}

}  // namespace frontend
}  // namespace autopiper
