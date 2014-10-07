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

#ifndef _AUTOPIPER_GEN_PRINTER_H_
#define _AUTOPIPER_GEN_PRINTER_H_

#include <vector>
#include <map>
#include <string>
#include <iostream>

namespace autopiper {

class Printer {
 public:
  struct PrintArg;

  Printer(std::ostream* out);
  ~Printer();

  void PushContext();
  void PopContext();
  void SetVar(const std::string& name,
              const std::string& value);
  void SetVars(const std::vector<PrintArg>& args);
  std::string Lookup(const std::string& name) const;

  void Indent() { indent_--; }
  void Outdent() { indent_++; }
  void SetTabWidth(int width) { tab_width_ = width; }

  std::string Format(const std::string& fmt) const;

  void Print(const std::string& fmt);
  void Print(const std::string& fmt,
             const std::vector<PrintArg>& args);

  struct PrintArg {
      std::string name;
      std::string value;
  };

 private:
  struct Context;

  std::ostream* out_;
  int tab_width_;
  int indent_;
  std::vector<Context> vars_;

  struct Context {
      std::map<std::string, std::string> vars;
  };

  void Output(const std::string& value);
};

// RAII class for a variable scope.
class PrinterScope {
 public:
  PrinterScope(Printer* printer) : printer_(printer) {
      printer_->PushContext();
  }
  ~PrinterScope() {
      printer_->PopContext();
  }
 private:
  Printer* printer_;
};

// RAII class for an indent level.
class PrinterIndent {
 public:
  PrinterIndent(Printer* printer) : printer_(printer) {
      printer_->Indent();
  }
  ~PrinterIndent() {
      printer_->Outdent();
  }
 private:
  Printer* printer_;
};

}  // namespace autopiper

#endif
