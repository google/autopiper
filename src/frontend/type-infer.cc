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

#include "frontend/type-infer.h"
#include "common/util.h"

#include <vector>
#include <functional>

using namespace std;

namespace autopiper {
namespace frontend {

// --------------- main program AST type inference -------------

bool InferenceNode::Update() {
    // Starts at \top (unresolved).
    InferredType new_value;

    // Compute the new value by joining all linked type slots in the AST and
    // all input edges via the meet function.
    for (auto* type_slot : nodes_) {
        new_value = new_value.Meet(*type_slot);
    }
    for (auto& input_edge : inputs_) {
        auto& xfer = input_edge.first;
        vector<InferredType> input_types;
        bool all_resolved = true;
        for (auto* input_node : input_edge.second) {
            input_types.push_back(input_node->type_);
            if (input_node->type_.type == InferredType::UNKNOWN) {
                all_resolved = false;
            } else if (input_node->type_.type == InferredType::CONFLICT) {
                new_value = new_value.Meet(input_node->type_);
                all_resolved = false;
            }
        }
        if (all_resolved) {
            InferredType edge_type = xfer(input_types);
            new_value = new_value.Meet(edge_type);
        }
    }

    // Did the value of this node change?
    bool changed = type_ != new_value;
    type_ = new_value;
    
    // Propagate the new value to the AST if so.
    if (changed) {
        for (auto* type_slot : nodes_) {
            *type_slot = new_value;
        }
    }

    return changed;
}

bool InferenceNode::Validate(ErrorCollector* coll) const {
    for (auto& validator : validators_) {
        if (!validator(type_, coll)) {
            return false;
        }
    }
    return true;
}

// ------ type-inference graph building: modify pass so we can take refs -------

TypeInferPass::TypeInferPass(ErrorCollector* coll)
    : ASTVisitorContext(coll) {}

TypeInferPass::~TypeInferPass() {}

InferenceNode* TypeInferPass::AddNode() {
    nodes_.emplace_back(new InferenceNode());
    return nodes_.back().get();
}

InferenceNode* TypeInferPass::NodeForAST(const void* key) {
    auto it = nodes_by_value_.find(key);
    if (it == nodes_by_value_.end()) {
        InferenceNode* node = AddNode();
        nodes_by_value_[key] = node;
        return node;
    }
    return it->second;
}

void TypeInferPass::ConveyType(InferenceNode* n1, InferenceNode* n2) {
    n1->inputs_.push_back(
            make_pair(
                [](const vector<InferredType>& args) {
                    return args[0];
                },
                vector<InferenceNode*> { n2 }));
}

void TypeInferPass::ConveyConstType(InferenceNode* n, InferredType type) {
    n->inputs_.push_back(
            make_pair(
                [type](const vector<InferredType>& args) {
                    return type;
                }, vector<InferenceNode*> {}));
}

void TypeInferPass::SumWidths(
        InferenceNode* sum,
        vector<InferenceNode*> nodes) {

    EnsureSimple(sum);
    sum->inputs_.push_back(
            make_pair(
                [](const vector<InferredType>& args) {
                    int bits = 0;
                    for (auto& arg : args) {
                        bits += arg.width;
                        if (arg.type == InferredType::EXPANDING_CONST) {
                            InferredType unknown;
                            return unknown;
                        }
                    }
                    return InferredType(bits);
                }, nodes));

    for (int i = 0; i < nodes.size(); i++) {
        EnsureSimple(nodes[i]);
        vector<InferenceNode*> other_terms;
        other_terms.push_back(sum);
        for (int j = 0; j < nodes.size(); j++) {
            if (j == i) continue;
            other_terms.push_back(nodes[j]);
        }
        nodes[i]->inputs_.push_back(
                make_pair(
                    [](const vector<InferredType>& args) {
                        int bits = args[0].width;
                        for (int i = 1; i < args.size(); i++) {
                            if (args[i].type == InferredType::EXPANDING_CONST) {
                                InferredType unknown;
                                return unknown;
                            }
                            bits -= args[i].width;
                        }
                        return InferredType(bits);
                    }, other_terms));
    }
}

void TypeInferPass::ConveyPort(InferenceNode* port_node, InferenceNode* value_node) {
    value_node->inputs_.push_back(
        make_pair(
            [](const vector<InferredType>& args) {
                if (args[0].is_port || args[0].is_chan) {
                    InferredType ret = args[0];
                    ret.is_port = false;
                    ret.is_chan = false;
                    return ret;
                } else {
                    InferredType conflict;
                    conflict.type = InferredType::CONFLICT;
                    conflict.conflict_msg =
                        "Port type mismatch";
                    return conflict;
                }
            }, vector<InferenceNode*> { port_node }));
}

void TypeInferPass::EnsureSimple(InferenceNode* n) {
    Location loc = n->loc;
    n->validators_.push_back(
            [loc](InferredType type, ErrorCollector* coll) {
                if (type.is_port || type.is_chan || type.is_array) {
                    coll->ReportError(loc, ErrorCollector::ERROR,
                            "Type cannot be an array, port, or chan");
                    return false;
                }
                return true;
            });
}

static int Log2(int n) {
    int bits = 1;
    n--;
    while (n > 1) {
        bits++;
        n >>= 1;
    }
    return bits;
}

void TypeInferPass::ConveyArrayRef(
        InferenceNode* n,
        InferenceNode* array,
        InferenceNode* index) {

    n->inputs_.push_back(
            make_pair(
                [](const vector<InferredType>& args) {
                    if (!args[0].is_array) {
                        InferredType conflict;
                        conflict.type = InferredType::CONFLICT;
                        conflict.conflict_msg =
                            "Array reference on a non-array value.";
                        return conflict;
                    } else {
                        InferredType ret = args[0];
                        ret.is_array = false;
                        return ret;
                    }
               }, vector<InferenceNode*> { array }));

    EnsureSimple(index);
    index->inputs_.push_back(
            make_pair(
                [](const vector<InferredType>& args) {
                    if (!args[0].is_array) {
                        InferredType conflict;
                        conflict.type = InferredType::CONFLICT;
                        conflict.conflict_msg =
                            "Array reference on a non-array value.";
                        return conflict;
                    } else {
                        int bits = Log2(args[0].array_size);
                        InferredType ret(bits);
                        return ret;
                    }
                }, vector<InferenceNode*> { array }));
}

void TypeInferPass::EnsureArray(InferenceNode* n) {
    Location loc = n->loc;
    n->validators_.push_back([loc](InferredType type, ErrorCollector* coll) {
        if (!type.is_array || type.array_size <= 0) {
            coll->ReportError(loc, ErrorCollector::ERROR,
                    "Expected array (with nonzero size).");
            return false;
        }
        return true;
    });
}

TypeInferPass::Result
TypeInferPass::ModifyASTPre(ASTRef<AST>& node) {
    aggs_.reset(new AggTypeResolver(node.get()));
    return aggs_->Compute(Errors()) ? VISIT_CONTINUE : VISIT_END;
}

static int BignumLog2(ASTBignum num) {
    int bits = 0;
    if (num == 0) {
        return 1;
    }
    while (num != 0) {
        bits++;
        num >>= 1;
    }
    return bits;
}

TypeInferPass::Result
TypeInferPass::ModifyASTExprPost(ASTRef<ASTExpr>& node) {
    // Always create an inference node for an ASTExpr.
    InferenceNode* n = NodeForAST(node.get());
    n->nodes_.push_back(&node->inferred_type);
    n->loc = node->loc;

    // Collect nodes for args' types.
    vector<InferenceNode*> arg_types;
    for (auto& op : node->ops) {
        arg_types.push_back(NodeForAST(op.get()));
    }

    switch (node->op) {
        case ASTExpr::ADD:
        case ASTExpr::SUB:
        case ASTExpr::AND:
        case ASTExpr::OR:
        case ASTExpr::NOT:
        case ASTExpr::XOR:
            // Link input types to output types and vice versa.
            ConveyType(n, arg_types[0]);
            ConveyType(n, arg_types[1]);
            ConveyType(arg_types[0], n);
            ConveyType(arg_types[1], n);
            EnsureSimple(n);
            break;

        case ASTExpr::LE:
        case ASTExpr::LT:
        case ASTExpr::GE:
        case ASTExpr::GT:
        case ASTExpr::EQ:
        case ASTExpr::NE:
            // Link args to each other.
            ConveyType(arg_types[0], arg_types[1]);
            ConveyType(arg_types[1], arg_types[0]);

            // Link output to a constant 1-bit type.
            ConveyConstType(n, InferredType(1));
            break;


        case ASTExpr::MUL:
            // Result's width is sum of two args' widths.
            SumWidths(n, vector<InferenceNode*> { arg_types[0], arg_types[1] });
            break;

        case ASTExpr::DIV:
        case ASTExpr::REM:
            // Result's width is first arg's width minus second arg's width.
            SumWidths(arg_types[0], vector<InferenceNode*> { n, arg_types[1] });
            break;

        case ASTExpr::LSH:
        case ASTExpr::RSH:
            // Link first arg to result.
            ConveyType(arg_types[0], n);
            ConveyType(n, arg_types[0]);
            break;

        case ASTExpr::BITSLICE: {
            // This is technically an instance of 'dependent types' -- we look
            // to be sure that the second and third args are constants, then
            // force the result type based on that.
            if (node->ops[1]->op != ASTExpr::CONST ||
                node->ops[2]->op != ASTExpr::CONST) {
                Error(node.get(),
                        "Index operands to bitslice expr must be constants");
                return VISIT_END;
            }

            ASTBignum diff = node->ops[1]->constant - node->ops[2]->constant;
            int width = static_cast<int>(diff) + 1;
            if (width < 0) width = -width;
            ConveyConstType(n, InferredType(width));

            EnsureSimple(arg_types[0]);

            break;
        }
            
        case ASTExpr::CONCAT:
            SumWidths(n, arg_types);
            break;

        case ASTExpr::VAR: {
            InferenceNode* var_node = NodeForAST(node->def);
            ConveyType(n, var_node);
            ConveyType(var_node, n);
            break;
        }

        case ASTExpr::CONST: {
            InferredType const_type;
            const_type.type = InferredType::EXPANDING_CONST;
            const_type.width = BignumLog2(node->constant);
            ConveyConstType(n, const_type);
            EnsureSimple(n);
            break;
        }

        case ASTExpr::PORTREAD:
            ConveyPort(arg_types[0], n);
            break;

        case ASTExpr::PORTDEF:
            break;

        case ASTExpr::STMTBLOCK: {
            // Find the last stmt, if any -- should be a StmtExpr.
            if (node->stmt->stmts.size()) {
                ASTStmt* last = node->stmt->stmts.back().get();
                if (last->expr) {
                    ASTExpr* ret_expr = last->expr->expr.get();
                    InferenceNode* expr_node = NodeForAST(ret_expr);
                    ConveyType(n, expr_node);
                    ConveyType(expr_node, n);
                }
            }
            break;
        } 

        case ASTExpr::ARG:
            break;

        case ASTExpr::ARRAY_REF:
            ConveyArrayRef(n, arg_types[0], arg_types[1]);
            break;

        case ASTExpr::ARRAY_INIT:
            EnsureArray(n);
            break;

        case ASTExpr::AGGLITERAL:
            assert(false);
            break;

        case ASTExpr::FIELD_REF:
            // TODO
            assert(false);
            break;

        default:
            assert(false);
            break;
    }

    return VISIT_CONTINUE;
}

TypeInferPass::Result
TypeInferPass::ModifyASTStmtLetPost(ASTRef<ASTStmtLet>& node) {
    InferenceNode* n = NodeForAST(node.get());
    if (node->type) {
        InferredType t = aggs_->ResolveType(node->type.get());
        ConveyConstType(n, t);
    }
    InferenceNode* rhs_node = NodeForAST(node->rhs.get());
    ConveyType(n, rhs_node);
    ConveyType(rhs_node, n);
    return VISIT_CONTINUE;
}

TypeInferPass::Result
TypeInferPass::ModifyASTStmtAssignPost(ASTRef<ASTStmtAssign>& node) {
    InferenceNode* lhs = NodeForAST(node->lhs.get());
    InferenceNode* rhs = NodeForAST(node->rhs.get());
    ConveyType(lhs, rhs);
    ConveyType(rhs, lhs);
    return VISIT_CONTINUE;
}

TypeInferPass::Result
TypeInferPass::ModifyASTStmtWritePost(ASTRef<ASTStmtWrite>& node) {
    InferenceNode* port = NodeForAST(node->port.get());
    InferenceNode* rhs = NodeForAST(node->rhs.get());
    ConveyPort(port, rhs);
    return VISIT_CONTINUE;
}

TypeInferPass::Result
TypeInferPass::ModifyASTStmtIfPost(ASTRef<ASTStmtIf>& node) {
    InferenceNode* condition = NodeForAST(node->condition.get());
    ConveyConstType(condition, InferredType(1));
    return VISIT_CONTINUE;
}

TypeInferPass::Result
TypeInferPass::ModifyASTStmtWhilePost(ASTRef<ASTStmtWhile>& node) {
    InferenceNode* condition = NodeForAST(node->condition.get());
    ConveyConstType(condition, InferredType(1));
    return VISIT_CONTINUE;
}

static void AddNodeAndDeps(const InferenceNode* node,
        set<const InferenceNode*>* s) {
    if (s->find(node) != s->end()) {
        return;
    }
    s->insert(node);
    for (auto& p : node->inputs_) {
        for (auto& n : p.second) {
            AddNodeAndDeps(n, s);
        }
    }
}

bool TypeInferPass::Infer(ErrorCollector* coll) {

    // Maximum iterations to converge, per graph node.
    static const int kMaxTypeItersPerNode = 10;

    // For each node, perform an update. Keep going as long as any change is
    // observed. (Node values take from a lattice with finite height, so this
    // process cannot continue forever.)
    bool changed = true;
    unsigned iters = 0;
    bool failed = false;
    while (changed) {
        changed = false;
        for (auto& node : nodes_) {
            if (node->Update()) {
                changed = true;
                if (node->type_.type == InferredType::CONFLICT) {
                    failed = true;
                    break;
                }
            }
        }
        if (iters > kMaxTypeItersPerNode * nodes_.size()) {
            failed = true;
            break;
        }
        iters++;
    }

    // Now, for any expanding constants remaining, fix at the current width.
    for (auto& node : nodes_) {
        if (node->type_.type == InferredType::EXPANDING_CONST) {
            node->type_.type = InferredType::RESOLVED;
        }
    }

    // Now validate the result.
    for (auto& node : nodes_) {
        if (!node->Validate(coll)) {
            failed = true;
        }
    }

    // If there were any failures, add the error messages from conflicts.
    set<const InferenceNode*> already_added;
    if (failed) {
        for (auto& node : nodes_) {
            if (node->type_.type == InferredType::CONFLICT &&
                already_added.find(node.get()) == already_added.end()) {
                coll->ReportError(node->loc, ErrorCollector::ERROR,
                        node->type_.conflict_msg);
                AddNodeAndDeps(node.get(), &already_added);
            }
        }
    }

    return !failed;
}

}  // namesapce frontend
}  // namespace autopiper
