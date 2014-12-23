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

#ifndef _AUTOPIPER_FRONTEND_CODEGEN_H_
#define _AUTOPIPER_FRONTEND_CODEGEN_H_

#include "frontend/ast.h"
#include "frontend/visitor.h"
#include "backend/ir.h"

#include <vector>
#include <map>
#include <set>
#include <assert.h>

namespace autopiper {
namespace frontend {

// A CodeGenScope is a set of bindings (indexed by some arbitrary key) that can
// be overridden within nested scopes. This container type is used both for
// let-value slots (where scopes correspond to control-flow paths) and
// primitive generator functions (where overrides can occur due to higher-level
// structures).
template<typename K, typename V>
class CodeGenScope {
    public:
        CodeGenScope() {
            scopes_.push_back({});
        }

        V& operator[](const K& k) {
            for (int i = scopes_.size() - 1; i >= 0; --i) {
                auto m = scopes_[i].bindings;
                auto it = m.find(k);
                if (it != m.end()) {
                    return it->second;
                }
            }
            // If not found, create a new binding implicitly in the
            // deepest-nested scopes.
            assert(!scopes_.empty());
            return scopes_.back().bindings[k];
        }

        bool Has(const K& k) {
            for (int i = scopes_.size() - 1; i >= 0; --i) {
                auto m = scopes_[i].bindings;
                auto it = m.find(k);
                if (it != m.end()) {
                    return true;
                }
            }
            return false;
        }

        void Push() {
            printf("push scope\n");
            scopes_.push_back();
        }

        void Pop() {
            scopes_.pop_back();
            printf("pop scope\n");
            assert(!scopes_.empty());
        }

        std::map<K, V> ReleasePop() {
            auto bindings = std::move(scopes_.back().bindings);
            Pop();
            return bindings;
        }

        // Takes a set of inner-scope overlays (overwritten bindings) over a
        // comon base and collects them together into, for each variable that
        // appears, a (inner1, inner2, ...) set of values. For a given
        // variable, the value corresponding to each scope is either the
        // binding provided by the scope or, if none is provided, the binding
        // present in the baseline scope. Only variables that also appear in
        // the baseline (current) scope are included; i.e., this excludes
        // locals defined only in one inner scope.
        //
        // This function is useful, for example, in generating phi-nodes to
        // join together multiple variable definitions at control flow joins.
        std::map<K, std::vector<V>> JoinOverlays(
                const std::vector<std::map<K, V>>& inner_scopes) {
            // Take the union of all variables overwritten in inner scopes also
            // present in the base scope.
            std::set<K> vars;
            for (auto& s : inner_scopes) {
                for (auto& p : s) {
                    auto k = p.first;
                    if (vars.find(k) == vars.end() && Has(k)) {
                        vars.insert(k);
                    }
                }
            }

            // For each variable, find the binding it takes on for each path
            // (each joining scope).
            std::map<K, std::vector<V>> ret;
            for (auto& k : vars) {
                auto v = ret[k];
                for (auto& s : inner_scopes) {
                    auto it = s.find(k);
                    if (it != s.end()) {
                        v.push_back(it->second);
                    } else {
                        v.push_back((*this)[k]);
                    }
                }
            }

            return ret;
        }

    private:
        struct Scope {
            std::map<K, V> bindings;
        };
        std::vector<Scope> scopes_;
};

class CodeGenContext {
    public:
        CodeGenContext();

        std::unique_ptr<IRProgram> Release() { return std::move(prog_); }

        // Generate a new temporary symbol.
        std::string GenSym();
        // Return a new valnum for IR.
        int Valnum() { return gensym_++; }

        // BB management. The 'current BB' is the control-flow exit point of
        // the code generated up to this point.
        IRBB* CurBB() { return curbb_; }
        void SetCurBB(IRBB* bb) { curbb_ = bb; }
        IRBB* AddBB();

        // The current set of variable bindings is maintained as a 'let ->
        // expr' mapping. (Yes, I know, in a Real Functional Language a let
        // *is* the binding and there is no assignment, only creation of a new
        // binding with another let. This isn't a Real Functional Language.
        // Sorry.)
        CodeGenScope<ASTStmtLet*, const ASTExpr*>& Bindings() {
            return bindings_;
        }

        // The expr -> IRStmt mapping. Each IRStmt is one value (in the SSA
        // sense).
        const IRStmt* GetIRStmt(const ASTExpr* expr) {
            return expr_to_ir_map_[expr];
        }

        // Add an IRStmt to the given BB, also recording that it computes the
        // value of |expr| (may be null if this stmt doesn't compute any expr's
        // value).
        IRStmt* AddIRStmt(IRBB* bb, std::unique_ptr<IRStmt> stmt,
                          const ASTExpr* expr = nullptr);

        // Associate an IRStmt created in some other way (e.g., already added)
        // as the value of |expr|.
        void AddIRStmt(const IRStmt* stmt, const ASTExpr* expr) {
            expr_to_ir_map_[expr] = stmt;
        }

    private:
        std::unique_ptr<IRProgram> prog_;
        int gensym_;
        IRBB* curbb_;
        std::map<const ASTExpr*, const IRStmt*> expr_to_ir_map_;

        CodeGenScope<ASTStmtLet*, const ASTExpr*> bindings_;
};

// A CodeGenPass traverses some subtree (or possibly the whole tree) of the
// AST, emitting code to a given CodeGenContext.
//
// This pass is destructive to the AST: codegen of some statements (e.g., 'if'
// and 'while') needs fine-grained control of codegen order, and so explicitly
// visits subtrees then removes them so that the visitor driver will not see
// them.
//
// TODO -- do this in a cleaner way by (i) recursively creating a CodeGenPass
// and invoking it over the if-body and else-body (for example) and (ii)
// supporting a return-value from the visit hooks that means "don't recurse
// into the subtree".
class CodeGenPass : public ASTVisitorContext {
    public:
        CodeGenPass(ErrorCollector* coll, CodeGenContext* ctx)
            : ASTVisitorContext(coll), ctx_(ctx) {}

    protected:
        // pre-hook on function: determine whether we'll do codegen or not, and
        // remove the function body if not.
        virtual bool ModifyASTFunctionDefPre(ASTRef<ASTFunctionDef>& node);

        // straight-line-code statement codegen hooks. We do codegen after
        // sub-stmts because exprs in the stmt must be gen'd first.
        virtual bool ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node);
        virtual bool ModifyASTStmtAssignPost(ASTRef<ASTStmtAssign>& node);
        virtual bool ModifyASTStmtBreakPost(ASTRef<ASTStmtBreak>& node);
        virtual bool ModifyASTStmtContinuePost(ASTRef<ASTStmtContinue>& node);
        virtual bool ModifyASTStmtWritePost(ASTRef<ASTStmtWrite>& node);
        virtual bool ModifyASTStmtExprPost(ASTRef<ASTStmtExpr>& node);

        // Expr codegen. Post-hook so that ops are already materialized.
        virtual bool ModifyASTExprPost(ASTRef<ASTExpr>& node);

        // 'if' codegen hook. This explicitly calls our visit functions on if-
        // and else-bodies after setting up the BBs appropriately, then removes
        // the bodies so the ordinary visit driver doesn't traverse into the
        // subtrees again.
        virtual bool ModifyASTStmtIfPre(ASTRef<ASTStmtIf>& node);

        // 'while' codegen hook. This also explicitly calls our visit functions
        // and then removes the subtree, for the same reasons as given above
        // for the 'if' hook.
        virtual bool ModifyASTStmtWhilePre(ASTRef<ASTStmtWhile>& node);

        virtual bool ModifyASTStmtSpawnPre(ASTRef<ASTStmtSpawn>& node);

        virtual bool ModifyASTStmtReturnPre(ASTRef<ASTStmtReturn>& node) {
            // Should have been removed by func-inlining pass.
            Error(node.get(),
                  "Return statement present at codegen: likely because "
                  "a return was attempted from an entry func.");
            return false;
        }

    private:
        CodeGenContext* ctx_;

        const ASTExpr* FindPortDef(const ASTExpr* node, const ASTExpr* orig);
};

}  // namespace frontend
}  // namespace autopiper

#endif  // _AUTOPIPER_FRONTEND_CODEGEN_H_
