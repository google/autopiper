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

#ifndef _AUTOPIPER_RPO_H_
#define _AUTOPIPER_RPO_H_

#include "ir.h"

#include <vector>
#include <set>
#include <map>

namespace autopiper {

template<typename T, typename SuccFunc>
class ReversePostorder {
 public:
  ReversePostorder() {}

  void Compute(const std::vector<const T*>& roots) {
      DFS(roots, &rpo_, &rponums_, &preds_);
  }

  int RPONum(const T* node) const {
      return rponums_.at(node);
  }
  const std::vector<const T*>& RPO() const {
      return rpo_;
  }
  const std::vector<const T*>& Preds(const T* node) const {
      auto it = preds_.find(node);
      if (it != preds_.end()) {
          return it->second;
      } else {
          return empty_pred_vec_;
      }
  }

  template<typename Iter>
  const std::vector<const T*> FindRoots(Iter begin, Iter end) const {
      SuccFunc sf;
      std::vector<const T*> all_nodes(begin, end);
      // Begin with all nodes as candidate roots. Remove all successors.
      std::set<const T*> roots(all_nodes.begin(), all_nodes.end());
      for (const auto* node : all_nodes) {
          for (const auto* succ : sf(node)) {
              roots.erase(succ);
          }
      }
      return std::vector<const T*>(roots.begin(), roots.end());
  }

 private:
  std::vector<const T*> rpo_;
  std::map<const T*, int> rponums_;
  std::map<const T*, std::vector<const T*>> preds_;
  std::vector<const T*> empty_pred_vec_;

  // Compute reverse-postorder, and also collect a map of predecessors.
  void DFS(const std::vector<const T*>& roots,
           std::vector<const T*>* rpo,
           std::map<const T*, int>* rponums,
           std::map<const T*, std::vector<const T*>>* preds) {
      std::set<const T*> visited;
      std::vector<const T*> postorder;
      // Go over roots in reverse order because we reverse the final result --
      // want DFS trees of first roots to come first (user may have good
      // reasons for this).
      std::vector<const T*> reverse_roots(roots);
      std::reverse(reverse_roots.begin(), reverse_roots.end());
      for (auto* root : reverse_roots) {
          DoDFS(root, &visited, &postorder, preds);
      }
      int rpo_num = 0;
      for (auto i = postorder.rbegin(), e = postorder.rend();
           i != e; ++i) {
          rpo->push_back(*i);
          (*rponums)[*i] = rpo_num++;
      }
  }
  void DoDFS(const T* node,
             std::set<const T*>* visited,
             std::vector<const T*>* postorder,
             std::map<const T*, std::vector<const T*>>* preds) {
      SuccFunc sf;
      if (visited->find(node) != visited->end()) return;
      visited->insert(node);
      for (auto* succ : sf(node)) {
          (*preds)[succ].push_back(node);
          DoDFS(succ, visited, postorder, preds);
      }
      postorder->push_back(node);
  }
};

// IRBB-specialized convenience typedef.
typedef ReversePostorder<IRBB, BBSuccFunc> BBReversePostorder;

}  // namespace autopiper

#endif
