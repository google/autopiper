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

#ifndef _AUTOPIPER_PREDICATE_H_
#define _AUTOPIPER_PREDICATE_H_

#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>

namespace autopiper {

// Represents a logical expression in disjunctive normal form (DNF), i.e., an
// OR of terms, each of which is an AND of factors. Each factor is a thing of
// type |T| (must support equality and ordering) with a polarity (true or
// inverted).
//
// Predicates support two operations: AndWith(T, polarity) and
// OrWith(Predicate). Each returns a new predicate, leaving the original(s)
// unmodified. The terms are accessible via Terms(), each of which provides its
// factors via Factors().
//
// Simplifications performed:
//
// S1. A1 & A2 & ... & B  |  A1 & A2 & ... & ~B  =  A1 & A2 & ...
// S2. A1 & A2 | A1 = A1
// S3. A1 & A1 = A1  (implicitly, by construction of the data structures)
// S4. A1 | A1 = A1
template<typename T>
class Predicate {
 public:
  // Represents an AND of positive and negative factors, e.g. A & ~B & C & ~D.
  struct Term {
   public:
    const std::map<T, bool> Factors() const { return factors; }

    bool operator==(const Term& other) const {
        // N.B.: don't worry about |falsified| here -- will be removed during
        // simplification and so never true at comparison time.
        return (factors == other.factors);
    }
    bool operator<(const Term& other) const {
        return (factors < other.factors);
    }

   protected:
    friend class Predicate;
    std::map<T, bool> factors;
    bool falsified;  // If factors.empty(), is this term true or false?

    Term() : falsified(false) {}

    template<typename StringFunc>
    std::string ToString() const {
        std::ostringstream os;
        StringFunc sf;
        bool first = true;
        if (falsified) {
            os << "false";
            first = false;
        }
        for (auto& p : factors) {
            if (!first) os << " & ";
            first = false;
            if (!p.second)
                os << "~";
            os << sf(p.first);
        }
        if (first) os << "true";
        return os.str();
    }

    // ANDs this term with a factor, updating this term.
    void AndWith(T t, bool polarity) {
        if (falsified) return; // false & A == false -- short-circuit this.
        if (factors.find(t) != factors.end() &&
            factors[t] == !polarity) {
            // Note: we don't need to clear factors here because
            // Predicate::Simplify() will remove this term once |falisified| is
            // set.
            falsified = true;
        } else {
            factors[t] = polarity;
        }
    }

    // ORs this term with another term, updating this term and the other
    // term. This term and the other term may be simplified (have factors
    // removed) or even, in the extreme case, become empty/falsified (in
    // which case it can be removed from the predicate).
    // Returns |true| if either term is modified.
    bool OrWith(Term* other) {
        // false | X = X | false = X. Just let it be and predicate
        // simplification will remove falsified terms (and render the
        // predicate either falsified or tautological).
        if (falsified || other->falsified) return false;

        // factors are stored in T-order, so we can iterate through to find
        // differences in linear time. We implement S1 (AB | A~B = A) and S2
        // (AB | A = A) here by dumping factors that appear on only one side
        // into 'this' and 'other' lists (i.e. factoring out common factors)
        // then handling these cases.
        auto i_this = factors.begin(), e_this = factors.end();
        auto i_other = other->factors.begin(), e_other = other->factors.end();
        std::vector<std::pair<T, bool>> this_factors, other_factors;
        while (i_this != e_this && i_other != e_other) {
            if (i_this->first < i_other->first) {
                this_factors.push_back(*i_this);
                i_this++;
            } else if (i_this->first > i_other->first) {
                other_factors.push_back(*i_other);
                i_other++;
            } else {
                if (i_this->second != i_other->second) {
                    this_factors.push_back(*i_this);
                    other_factors.push_back(*i_other);
                } else {
                    // match.
                }
                i_this++;
                i_other++;
            }
        }
        for (; i_this != e_this; ++i_this) {
            this_factors.push_back(*i_this);
        }
        for (; i_other != e_other; ++i_other) {
            other_factors.push_back(*i_other);
        }

        // Now, if one side strictly covers the other (manifests as either
        // this_factors.empty() or other_factors.empty(), but not both), we
        // can eliminate the more specific term.
        if (this_factors.empty()) {
            // |other| is more specific -- covered by |this|. Clear |other|.
            other->factors.clear();
            other->falsified = true;
            return true;
        }
        else if (other_factors.empty()) {
            this->factors.clear();
            this->falsified = true;
            return true;
        }
        // Otherwise, if each side has exactly one factor not in the other,
        // and they are opposites, then we can cancel them both out (A | ~A
        // reduces to |true|).
        else if (this_factors.size() == 1 && other_factors.size() == 1 &&
                 this_factors[0].first == other_factors[0].first &&
                 this_factors[0].second == ! other_factors[0].second) {
            this->factors.erase(this_factors[0].first);
            other->factors.erase(this_factors[0].first);
            return true;
        }
        return false;
      }
  };

  Predicate() : backedge(false) {}

  static Predicate False() {
      return Predicate();
  }
  static Predicate True() {
      Predicate p;
      p.terms.push_back(Term());
      return p;
  }

 protected:
  struct DefaultStringFunc {
      std::string operator()(T t) { return t->ToString(); }
  };
 public:

  template<typename StringFunc = DefaultStringFunc>
  std::string ToString() const {
      std::ostringstream os;
      bool first = true;
      for (auto& term : terms) {
          if (!first) os << " | ";
          first = false;
          os << term.template ToString<StringFunc>();
      }
      if (first) os << "false";
      return os.str();
  }

  // Predicates can be used as keys so must support == and < operators.
  // These work because we keep terms in canonical order, i.e., sorted, and
  // factors are sorted (implicitly by std::map) within terms, so we can use
  // the builtin == and < on the vector of terms.
  bool operator==(const Predicate<T>& other) const {
      return terms == other.terms;
  }
  bool operator<(const Predicate<T>& other) const {
      return terms < other.terms;
  }

  // ANDs the whole predicate with a new factor, returning a new predicate.
  // This adds the factor to each term (i.e., distributes it) and simplifies
  // where possible.
  Predicate AndWith(T t, bool polarity) const {
      Predicate p(*this);
      for (auto& term : p.terms) {
          term.AndWith(t, polarity);
      }
      p.Simplify();
      p.backedge = false;
      return p;
  }

  // ORs the whole predicate with another predicate, returning a new predicate.
  Predicate OrWith(const Predicate& other) const {
      // Build a new termlist as the concatenation of our terms and other's
      // terms, after running them all past each other to perform possible
      // simplifications.
      Predicate p(*this);
      p.terms.insert(p.terms.end(), other.terms.begin(), other.terms.end());

      // We want to run all terms against each other (i.e., (N+M)^2/2), not
      // just old-against new (N*M). This is because as new terms cause old
      // terms to be simplified (drop factors), old terms may then be able to
      // simplify against other old terms.
      bool changed = true;
      while (changed) {
          changed = false;
          for (unsigned i = 0; i < p.terms.size(); i++) {
              for (unsigned j = i + 1; j < p.terms.size(); j++) {
                  changed |= p.terms[i].OrWith(&p.terms[j]);
              }
          }
          p.Simplify();
      }
      p.backedge = false;
      return p;
  }

  // A predicate is always true if (in its canonical form, after Simplify() has
  // run) it contains exactly one term, and that term has no factors and is not
  // falsified.
  bool IsTrue() const {
      for (auto& term : terms) {
          if (term.falsified || !term.factors.empty()) return false;
      }
      return true;
  }
  bool IsFalse() const {
      return terms.empty();
  }

  const std::vector<Term> Terms() const { return terms; }

  bool IsBackedge() const {
      return backedge;
  }
  void SetBackedge() {
      backedge = true;
  }

 private:
  std::vector<Term> terms;
  bool backedge;

  // Simplification pass: (i) remove falsified terms; (ii) if any tautological
  // terms are present, remove all terms except this one.
  void Simplify() {
      // is_tautological indicates, if |new_terms| is empty, whether the
      // predicate is constant true or constant false.
      bool is_tautological = false;
      std::vector<Term> new_terms;
      for (auto& term : terms) {
          if (term.falsified) continue;
          if (term.factors.empty()) {
              // Once we OR in a constant-true term, the predicate must be
              // constant true.
              is_tautological = true;
              break;
          }
          new_terms.push_back(term);
      }
      if (is_tautological) {
          // Constant true is represented by a single empty term.
          terms.clear();
          terms.push_back(Term());
      } else {
          terms = new_terms;
      }
      // Now sort the terms to obtain canonical form.
      sort(terms.begin(), terms.end());
  }
};

}  // namespace autopiper

#ifdef PREDICATE_TEST
#include <iostream>

using namespace std;
using namespace autopiper;

typedef Predicate<string> P;

struct IdentityStringFunc {
    string operator()(string s) { return s; }
};

string str(const P& p) {
    return p.ToString<IdentityStringFunc>();
}

int main() {
    P p1 = P::True();
    cout << "original: " << str(p1) << endl;
    P p2 = p1.AndWith("A", true);
    cout << "AndWith A: " << str(p2) << endl;
    P p3 = p2.AndWith("A", false);
    cout << "AndWith ~A: " << str(p3) << endl;

    P p4 = P::True().AndWith("B", true);
    P p5 = p4.OrWith(p2);
    cout << "A | B = " << str(p5) << endl;
    P p6 = p4.AndWith("A", true);
    cout << "(A | B) & A = " << str(p6) << endl;
    P p7 = P::True().AndWith("A", false);
    P p8 = p5.OrWith(p7);
    cout << "A | B | ~A = " << str(p8) << endl;
    P p9  = P::True().AndWith("A", true).AndWith("B", false);
    P p10 = P::True().AndWith("A", true).AndWith("B", true);
    P p11 = p9.OrWith(p10);
    cout << str(p9) << " | " << str(p10) << " = " << str(p11) << endl;
    P p12 = P::True().AndWith("A", true).AndWith("B", false).AndWith("C", true);
    P p13 = P::True().AndWith("A", true);
    cout << str(p12) << " | " << str(p13) << " = " << str(p12.OrWith(p13)) << endl;

    P p14 = P::False().OrWith(P::True());
    cout << str(P::False()) << " | " << str(P::True()) << " = " << str(p14) << endl;
}
#endif

#endif
