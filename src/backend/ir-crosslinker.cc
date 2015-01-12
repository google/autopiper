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

#include "backend/ir.h"
#include "backend/compiler.h"
#include "common/util.h"

#include <map>
#include <vector>
#include <string>

using namespace std;
using namespace autopiper;

namespace {

void SetBBBackpointers(IRProgram* program) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            stmt->bb = bb.get();
        }
    }
}

typedef map<string, IRBB*> BBMap;

bool GetBBMap(IRProgram* program,
              BBMap* m,
              ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        if (m->find(bb->label) != m->end()) {
            collector->ReportError(bb->location, ErrorCollector::ERROR,
                    string("Duplicate basic-block label '") + bb->label +
                    string("': previous was at ") +
                    (*m)[bb->label]->location.ToString());
            return false;
        }
        m->insert(make_pair(bb->label, bb.get()));
    }
    return true;
}

typedef map<int, IRStmt*> ValnumMap;

bool GetValnumMap(IRProgram* program,
                  ValnumMap* m,
                  ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            if (m->find(stmt->valnum) != m->end()) {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        string("Value number duplicated: original use at ") +
                        (*m)[stmt->valnum]->location.ToString());
                return false;
            }
            m->insert(make_pair(stmt->valnum, stmt.get()));
        }
    }
    return true;
}

typedef map<string, vector<IRStmt*>> PortMap;

bool GetPortMap(IRProgram* program,
                PortMap* m,
                ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            if (!IRReadsPort(stmt->type) &&
                !IRWritesPort(stmt->type) &&
                stmt->type != IRStmtPortExport) continue;
            if (stmt->port_name == "") {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        "Empty port name");
                return false;
            }
            (*m)[stmt->port_name].push_back(stmt.get());
        }
    }
    return true;
}

typedef map<string, vector<IRStmt*>> StorageMap;

bool GetStorageMap(IRProgram* program,
                   StorageMap* m,
                   ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            if (!IRReadsStorage(stmt->type) &&
                !IRWritesStorage(stmt->type) &&
                stmt->type != IRStmtArraySize) continue;
            if (stmt->port_name == "") {
                collector->ReportError(stmt->location, ErrorCollector::ERROR,
                        "Empty storage name");
                return false;
            }
            (*m)[stmt->port_name].push_back(stmt.get());
        }
    }
    return true;
}

typedef map<string, vector<IRStmt*>> BypassMap;

bool GetBypassMap(IRProgram* program,
                  BypassMap* m,
                  ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            switch (stmt->type) {
                case IRStmtBypassStart:
                case IRStmtBypassEnd:
                case IRStmtBypassPresent:
                case IRStmtBypassReady:
                case IRStmtBypassRead:
                case IRStmtBypassWrite:
                    if (stmt->port_name == "") {
                        collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                "Empty bypass-network name");
                        return false;
                    }
                    (*m)[stmt->port_name].push_back(stmt.get());
                    break;
                default:
                    break;
            }
        }
    }
    return true;
}

bool LinkStmts(IRProgram* program,
               BBMap& targets,
               ValnumMap& valnums,
               ErrorCollector* collector) {
    for (auto& bb : program->bbs) {
        for (auto& stmt : bb->stmts) {
            for (auto& targ : stmt->target_names) {
                if (targets.find(targ) == targets.end()) {
                    collector->ReportError(stmt->location, ErrorCollector::ERROR,
                            string("Unknown target label '") + targ + string("'"));
                    return false;
                }
                stmt->targets.push_back(targets[targ]);
            }

            for (auto valnum : stmt->arg_nums) {
                if (valnums.find(valnum) == valnums.end()) {
                    collector->ReportError(stmt->location, ErrorCollector::ERROR,
                            "Unknown argument value number");
                    return false;
                }
                stmt->args.push_back(valnums[valnum]);
            }
        }
    }
    return true;
}

bool CreatePorts(IRProgram* program,
                 const PortMap& m,
                 ErrorCollector* collector) {
    for (auto& pair : m) {
        auto portname = pair.first;
        auto stmts = pair.second;
        unique_ptr<IRPort> port(new IRPort());
        port->name = portname;
        for (auto* stmt : stmts) {
            stmt->port = port.get();
            if (IRWritesPort(stmt->type)) {
                port->defs.push_back(stmt);
                if (stmt->type == IRStmtPortWrite) {
                    port->type = IRPort::PORT;
                } else if (stmt->type == IRStmtChanWrite) {
                    port->type = IRPort::CHAN;
                } else {
                    assert(false);
                }
            } else if (IRReadsPort(stmt->type)) {
                port->uses.push_back(stmt);
                if (stmt->type == IRStmtPortRead) {
                    port->type = IRPort::PORT;
                } else if (stmt->type == IRStmtChanRead) {
                    port->type = IRPort::CHAN;
                } else {
                    assert(false);
                }
            } else if (stmt->type == IRStmtPortExport) {
                port->exported = true;
                port->exports.push_back(stmt);
            }
        }
        program->ports.push_back(move(port));
    }
    return true;
}

bool CreateStorage(IRProgram* program,
                   const StorageMap& m,
                   ErrorCollector* collector) {
    for (auto& pair : m) {
        auto storagename = pair.first;
        auto stmts = pair.second;
        unique_ptr<IRStorage> storage(new IRStorage());
        storage->name = storagename;
        for (auto* stmt : stmts) {
            stmt->storage = storage.get();
            if (IRWritesStorage(stmt->type)) {
                storage->writers.push_back(stmt);
            }
            if (IRReadsStorage(stmt->type)) {
                storage->readers.push_back(stmt);
            }
            if (stmt->type == IRStmtArraySize) {
                storage->elements = static_cast<int>(stmt->constant);
            }
        }
        program->storage.push_back(move(storage));
    }
    return true;
}

bool CreateBypass(IRProgram* program,
                  const BypassMap& m,
                  ErrorCollector* collector) {
    for (auto& pair : m ) {
        auto bypassname = pair.first;
        auto stmts = pair.second;
        unique_ptr<IRBypass> bypass(new IRBypass());
        bypass->name = bypassname;
        for (auto* stmt : stmts) {
            switch (stmt->type) {
                case IRStmtBypassStart:
                    if (bypass->start) {
                        collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                strprintf(
                                    "More than one 'start' statement on bypass network '%s'",
                                    bypassname.c_str()));
                        return false;
                    }
                    bypass->start = stmt;
                    stmt->bypass = bypass.get();
                    break;
                case IRStmtBypassEnd:
                    if (bypass->end) {
                        collector->ReportError(stmt->location, ErrorCollector::ERROR,
                                strprintf(
                                    "More than one 'end' statement on bypass network '%s'",
                                    bypassname.c_str()));
                        return false;
                    }
                    bypass->end = stmt;
                    stmt->bypass = bypass.get();
                    break;
                case IRStmtBypassWrite:
                    bypass->writes.push_back(stmt);
                    stmt->bypass = bypass.get();
                    break;
                case IRStmtBypassPresent:
                case IRStmtBypassReady:
                case IRStmtBypassRead:
                    bypass->reads.push_back(stmt);
                    stmt->bypass = bypass.get();
                    break;
                default:
                    break;
            }
        }
        program->bypasses.push_back(move(bypass));
    }
    return true;
}

}  // anonymous namespace

bool IRProgram::Crosslink(ErrorCollector* collector) {
    SetBBBackpointers(this);
    if (!crosslinked_args_bbs) {
        BBMap bbmap;
        ValnumMap valnummap;
        if (!GetBBMap(this, &bbmap, collector)) return false;
        if (!GetValnumMap(this, &valnummap, collector)) return false;
        if (!LinkStmts(this, bbmap, valnummap, collector)) return false;
        crosslinked_args_bbs = true;
    }

    PortMap portmap;
    if (!GetPortMap(this, &portmap, collector)) return false;
    if (!CreatePorts(this, portmap, collector)) return false;

    StorageMap storagemap;
    if (!GetStorageMap(this, &storagemap, collector)) return false;
    if (!CreateStorage(this, storagemap, collector)) return false;

    BypassMap bypassmap;
    if (!GetBypassMap(this, &bypassmap, collector)) return false;
    if (!CreateBypass(this, bypassmap, collector)) return false;

    return true;
}
