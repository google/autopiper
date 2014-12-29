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

#include "frontend/var-scope.h"
#include "common/util.h"

using namespace std;

// TODO: Run this resolution prior to inlining, then do the inlining and patch
// everything up afterward. Right now we actually have dynamic scope, not
// lexical scope, which is really ugly!

namespace autopiper {
namespace frontend {

ArgLetPass::Result
ArgLetPass::ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node) {
    ASTVector<ASTStmt> stmts;
    int idx = 0;
    for (auto& param : node->params) {
        ASTRef<ASTStmt> stmt(new ASTStmt());
        stmt->let.reset(new ASTStmtLet());
        stmt->let->lhs = CloneAST(param->ident.get());
        stmt->let->type = CloneAST(param->type.get());
        stmt->let->rhs.reset(new ASTExpr());
        stmt->let->rhs->op = ASTExpr::ARG;
        stmt->let->rhs->constant = idx++;
        stmts.push_back(move(stmt));
    }
    for (auto& stmt : node->block->stmts) {
        stmts.push_back(move(stmt));
    }
    node->block->stmts.swap(stmts);
    stmts.clear();

    return VISIT_CONTINUE;
}

VarScopePass::Result
VarScopePass::ModifyASTStmtBlockPre(ASTRef<ASTStmtBlock>& node) {
    OpenScope();
    return VISIT_CONTINUE;
}

VarScopePass::Result
VarScopePass::ModifyASTStmtBlockPost(ASTRef<ASTStmtBlock>& node) {
    CloseScope();
    return VISIT_CONTINUE;
}

VarScopePass::Result
VarScopePass::ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node) {
    const std::string& name = node->lhs->name;
    if (!AddDef(name, node.get())) {
        Error(node.get(), strprintf(
                    "Multiple definition for binding '%s'",
                    name.c_str()));
        return VISIT_END;
    }
    return VISIT_CONTINUE;
}

VarScopePass::Result
VarScopePass::ModifyASTExprPre(ASTRef<ASTExpr>& node) {
    if (node->op == ASTExpr::VAR) {
        const std::string& name = node->ident->name;
        node->def = GetDef(name);
        if (!node->def) {
            Error(node.get(), strprintf(
                        "Unknown binding '%s'", name.c_str()));
            return VISIT_END;
        }
    }
    return VISIT_CONTINUE;
}

bool VarScopePass::AddDef(const std::string& name, ASTStmtLet* def) {
    assert(!scopes_.empty());
    auto& defs = scopes_.back().defs;
    if (defs.find(name) != defs.end()) {
        return false;
    }
    defs.insert(make_pair(name, def));
    return true;
}

ASTStmtLet* VarScopePass::GetDef(const std::string& name) {
    for (int i = scopes_.size() - 1; i >= 0; --i) {
        auto& defs = scopes_[i].defs;
        auto it = defs.find(name);
        if (it != defs.end()) {
            return it->second;
        }
    }
    return nullptr;
}

}  // namesapce frontend
}  // namespace autopiper
