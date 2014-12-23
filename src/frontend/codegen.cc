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
    ctx_->Bindings().Set(node.get(), node->rhs.get());
    return true;
}

bool CodeGenPass::ModifyASTStmtAssignPost(ASTRef<ASTStmtAssign>& node) {
    if (node->lhs->op == ASTExpr::VAR) {
        // Simple variable assignment.  Associate the binding (let) with the
        // new ASTExpr (the RHS).
        ctx_->Bindings().Set(node->lhs->def, node->rhs.get());
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
    IRStmt* val = ctx_->GetIRStmt(node->rhs.get());
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
            IRStmt* op_stmt = ctx_->GetIRStmt(op.get());
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
    // We need a subpass to do manual codegen for our subordinate parts. It
    // shares our context.
    CodeGenPass subpass(Errors(), ctx_);
    ASTVisitor visitor;

    // Create two BBs for the if- and else-bodies respectively.
    IRBB* if_body = ctx_->AddBB();
    IRBB* else_body = ctx_->AddBB();

    // Generate the conditional explicitly -- since we're a pre-hook (so that
    // we can control if-/else-body generation), we don't get this for free as
    // the post-hooks do.
    if (!visitor.ModifyASTExpr(node->condition, &subpass)) {
        return false;
    }
    IRStmt* conditional = ctx_->GetIRStmt(node->condition.get());

    // Terminate CurBB() with the conditional branch to the if- or else-path.
    unique_ptr<IRStmt> cond_br(new IRStmt());
    cond_br->valnum = ctx_->Valnum();
    cond_br->type = IRStmtIf;
    cond_br->args.push_back(conditional);
    cond_br->arg_nums.push_back(conditional->valnum);
    cond_br->targets.push_back(if_body);
    cond_br->target_names.push_back(if_body->label);
    cond_br->targets.push_back(else_body);
    cond_br->target_names.push_back(else_body->label);
    ctx_->AddIRStmt(ctx_->CurBB(), move(cond_br));

    // Generate each of the if- and else-bodies, pushing a control-flow-path
    // binding context and saving the overwritten bindings so that we can merge
    // with phis at the join point.
    
    ctx_->Bindings().Push();
    ctx_->SetCurBB(if_body);
    if (!visitor.ModifyASTStmt(node->if_body, &subpass)) {
        return false;
    }
    auto if_bindings = ctx_->Bindings().ReleasePop();
    // current BB at end of if-body may be different (if body was not
    // straightline code) -- save to build the merge below.
    IRBB* if_end = ctx_->CurBB();

    ctx_->Bindings().Push();
    ctx_->SetCurBB(else_body);
    if (node->else_body) {
        if (!visitor.ModifyASTStmt(node->else_body, &subpass)) {
            return false;
        }
    }
    auto else_bindings = ctx_->Bindings().ReleasePop();
    IRBB* else_end = ctx_->CurBB();

    // Generate the merge point. We (i) create the new BB and set it as the
    // current BB (so that at exit, we leave the current BB set to our single
    // exit point), (ii) generate the jumps from each side of the diamond to
    // the merge point, and (iii) insert phis for all overwritten bindings.
    IRBB* merge_bb = ctx_->AddBB();
    ctx_->SetCurBB(merge_bb);

    unique_ptr<IRStmt> if_end_jmp(new IRStmt());
    if_end_jmp->valnum = ctx_->Valnum();
    if_end_jmp->type = IRStmtJmp;
    if_end_jmp->targets.push_back(merge_bb);
    if_end_jmp->target_names.push_back(merge_bb->label);
    ctx_->AddIRStmt(if_end, move(if_end_jmp));

    unique_ptr<IRStmt> else_end_jmp(new IRStmt());
    else_end_jmp->valnum = ctx_->Valnum();
    else_end_jmp->type = IRStmtJmp;
    else_end_jmp->targets.push_back(merge_bb);
    else_end_jmp->target_names.push_back(merge_bb->label);
    ctx_->AddIRStmt(else_end, move(else_end_jmp));

    auto phi_map = ctx_->Bindings().JoinOverlays(
            vector<map<ASTStmtLet*, const ASTExpr*>> {
                if_bindings, else_bindings });

    for (auto& p : phi_map) {
        ASTStmtLet* let = p.first;
        vector<const ASTExpr*>& sub_bindings = p.second;
        IRStmt* if_val = ctx_->GetIRStmt(sub_bindings[0]);
        IRStmt* else_val = ctx_->GetIRStmt(sub_bindings[1]);

        unique_ptr<IRStmt> phi_node(new IRStmt());
        phi_node->type = IRStmtPhi;
        phi_node->valnum = ctx_->Valnum();
        phi_node->width = let->inferred_type.width;
        phi_node->args.push_back(if_val);
        phi_node->arg_nums.push_back(if_val->valnum);
        phi_node->args.push_back(else_val);
        phi_node->arg_nums.push_back(else_val->valnum);
        phi_node->targets.push_back(if_end);
        phi_node->target_names.push_back(if_end->label);
        phi_node->targets.push_back(else_end);
        phi_node->target_names.push_back(else_end->label);

        IRStmt* new_node = ctx_->AddIRStmt(merge_bb, move(phi_node));

        // This is a bit of a hack: to set the current binding to the phi node,
        // we overwrite the expr -> IR value mapping for the last binding in
        // this context. We have to do this because there is no ASTExpr
        // corresponding to the "merged value" -- the AST doesn't have phi
        // nodes (it's not SSA). Ideally bindings would be directly to IR
        // stmts, but there are cases where that becomes a little ugly too.
        // Eh, this is just a prototype. TODO: Make it cleaner.
        const ASTExpr* expr_binding = ctx_->Bindings()[let];
        ctx_->AddIRStmt(new_node, expr_binding);
    }

    // Now clear the condition and bodies so that the visit-pass driver does
    // not recurse into them.
    node->condition.reset(nullptr);
    node->if_body.reset(nullptr);
    node->else_body.reset(nullptr);

    return true;
}

// While-loop support: while, break, continue

bool CodeGenPass::ModifyASTStmtWhilePre(ASTRef<ASTStmtWhile>& node) {
    // TODO
    assert(false);
    return false;
}

bool CodeGenPass::ModifyASTStmtBreakPost(ASTRef<ASTStmtBreak>& node) {
    // TODO
    assert(false);
    return false;
}

bool CodeGenPass::ModifyASTStmtContinuePost(ASTRef<ASTStmtContinue>& node) {
    // TODO
    assert(false);
    return false;
}

// Spawn support.

bool CodeGenPass::ModifyASTStmtSpawnPre(ASTRef<ASTStmtSpawn>& node) {
    // TODO
    assert(false);
    return false;
}

}  // namespace frontend
}  // namespace autopiper
