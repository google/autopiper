# Autopiper: automatic pipeline synthesis

Welcome! This document describes Autopiper, a new hardware description
language. Autopiper describes hardware at a higher level than the usual
hardware description languages (Verilog or VHDL), but generates Verilog.
Autopiper aims to capture standard microarchitectural building blocks, such as
pipelines (with the usual bypass, stall, and clearing logic), queues and more,
so that the language user can describe their hardware at a higher semantic
level and be assured that the implementation is correct by construction. This
allows higher productivity and produces better hardware.

In brief, Autopiper builds on a straight-line pipeline semantics (the user
writes code following computation down a pipeline), includes primitives that
allow for expressing bypass, pipeline clearing and stalling, and includes a
powerful macro facility that allows higher-level constructs (queues, register
renaming, etc.) to be built out of these primitives. A milestone and design
target for Autopiper is to be able to express an out-of-order core
microarchitecture without 'breaking out' of the language to specify any logic
by hand.

More about the language semantics can be found in [Semantics](#semantics).

## Overview: Main Ideas

Autopiper is built on three main ideas: transactional pipeline semantics,
low-level primitives that enable efficient and idiomatic pipeline design within
the transactional semantics, and a macro processor that enables composition of
these primitives into high-level structures such as queues and (eventually!)
out-of-order renamed superscalar backends.

### Transactional Pipeline Semantics

The language is built around a *transactional pipeline semantics*. The basic
building block is the synchronous pipeline; a design is composed of many such
pipelines that communicate (via queues or ports). For each pipeline, the user
writes out computations to be performed, and the tool will lay out the
computations into stages and generate all of the glue logic required.  The
compiler understands stalls, pipeline clears, and more, and converts control
flow to pipeline control (stalls and restarts) with well-defined transforms. An
example:

    # A simple pipeline. This is manually timed with 'stage barriers',
    # but an automatic pipe-timing engine exists too.

    stage 0;
    let x : int32 = read port_in;

    stage 1;
    let y : int32 = (x + 42) << 1;
    if (y == 0) y = 1;
    while (read port_ready == 0);  # spin

    stage 2;
    write port_out, y;

The abstraction is that this computation runs all the way through for each
transaction (work cycle) of the pipeline. Pipelining is then an optimization
transform that overlaps transactions.

This shift of perspective -- from conventional hardware design, which involves
"spatial thinking" (pipeline-stage-centric), to imagining the flow of items
(transactions) and describing computations from their point of view -- is what
enables Autopiper's succinctness and simplicity. We will provide a more precise
description of these semantics, including its "parallel program interpretation"
and "pipelined interpretation", below.

In order to allow this optimization to occur while transactions communicate
(e.g. via state), we must expose several primitives.

### Pipelining Primitives

The language contains *primitives that capture common pipeline tools* in a way
that's agnostic to the underlying pipelining.

For a simple example, consider a bypass network. Pipeline bypass is a means
for later pipeline transactions to receive values from earlier pipeline
transactions when the values "match" (e.g., the same register or memory
address in a CPU core is being written then immediately read). Bypass could
be implemented by hand, but this requires intricate knowledge of how
computation is split into pipeline stages. Instead, Autopiper exposes a
primitive that is integrated with its pipelining transform:

* A transaction can "provide" a value, identified by some bypass object ID
  or name and an "index" (e.g., register or memory address) and later
  "unprovide" it. This represents the lifetime that the value is 'live' in
  the pipeline.
* At some point during that lifetime, the producer "publishes" the result.
* Any consumer may "ask" for the value. If the value is provided but not
  published, then the consumer stalls.

For example:

    stage N;
    let inst : Instruction = Decode(bytes);

    stage N+1;
    let byp : bypass int32 = bypass;
    provide byp, inst.register_dest;

    while (!avail byp, inst.register_srcA);  # stall
    while (!avail byp, inst.register_srcB);  # stall
    let srcA : int32 =
        ask byp, inst.register_srcA ?
        askvalue byp, inst.register_srcA : RF[inst.register_srcA];
    let srcB : int32 =
        ask byp, inst.register_srcB ?
        askvalue byp, inst.register_srcB : RF[inst.register_srcB];

    stage N+2;
    let result = Compute(inst, srcA, srcB);
    publish byp, inst.register_dest, result;

    stage N+3;
    RF[inst.register_dest] = result;
    unprovide byp, inst.register_dest;

As another example, pipeline clearing is abstracted away with several
primitives: "killyounger", which clears all in-flight transactions younger
than the current one, "kill\_if", which kills the current transaction if some
condition becomes true, and "onkill", "ondone" and "onkillyounger", which
enable macros to hook clearing behavior.

These primitives are described in more detail in [Semantics](#semantics) below.

The set of primitives included in the core language spec is enough to build
transactional state elements (which wrap a simple latch or array with
bypass/stall as above, commit on transaction end, and restore on early
kill), queues (with read/write pointers as transactional elements), and
others.

### Macros

The language includes a powerful *macro processor* which allows primitives to
be composed. Composability is at the heart of Autopiper's design: the
pipelining transform and low-level primitives were designed to capture the
essential semantics of what is going on (e.g., bypass or pipeline clear)
without being overly prescriptive.

To provide some philosophy: earlier designs attempted to capture a formal
semantics of "out-of-order core design" in the compiler itself, but too much
magic in the compiler turns a fundamental, precise transform into simply a
generator for a particular human-designed microarchitecture. Autopiper's goal
is to build correct-by-construction designs, and the way to do this is to
*factor* the microarchitecture into *composable fundamental concepts*. Macros
are the means by which the concepts are composed.

Macros operate at the tokenizer level, prior to the parser, so they are quite
flexible. (They are also heavily inspired by [Rust
macros](http://doc.rust-lang.org/0.12.0/guide-macros.html), for readers who are
familiar with that language.) An example might suffice best -- here is a
definition for a transactional state element (`$_temp` is a temporary
identifier, like `(gensym)` in a Lisp macro, and `$$` concatenates identifiers,
like `##` in the C preprocessor):

    # Define the element, e.g.: txn!(my_reg, int32, 0);
    macro! txn {
        (name, type, initial) = (
            let $name $$ _reg : reg $type = reg;
            let $name $$ _value = $name $$ _value;
            let $name $$ _bypass : bypass $type = bypass;
        )
    }
    macro! txnwrite {
        (name, value) = (
            $name $$ _value = $value;
            publish $name $$ _bypass, 0, $value;
        )
    }
    # Commit. If desired, 'ondone { txncommit!(myvar); }' to ensure a commit
    # happens at the end of the main control-flow spine.
    macro! txncommit {
        (name) = (
            $name $$ _reg = $name $$ _value;
            unprovide $name $$ _bypass, 0;
        )
    }
    # Blocking read.
    macro! txnread {
        (name) = (
            # expression statement block -- value is last expression.
            expr {
                # ensure that we start a 'provide' scope as soon as we
                # are liable to read in the pipeline.
                provide $name $$ _bypass;

                # wait if provided but not published.
                while (unavail $name $$ _bypass);

                let $_byp = ask $name $$ _bypass;
                let $_val = 0;
                if ($_byp) {
                    $_val = askvalue $name $$ _bypass;
                } else {
                    $_val = $name $$ _value;
                }
                $_val;
            }
        )
    }

Similar macros can be defined for other, more complex structures. For example,
it is possible to define a macro that separates a synchronous pipe into two
synchronous halves with a queue in the middle. The macro defines read and write
pointers (these are transactional state, restored by early kills), ports to
send and receive queue-space credits, and sets up a linkage using 'onkill',
'onkillyounger', and 'valid\_if' so that (i) if the backend kills all younger
transactions, the frontend is flushed, and (ii) if the frontend kills all
younger transactions at a point after the queue write, the backend is CAM'd and
cleared as appropriate.

This is the beauty of composability: the user only needs to think about
stateful computations, and the underlying macros and compiler will generate all
of the program-order-maintenance, branch-predict-mispredict-clear-restore
boilerplate. It falls out of the proper use of the primitives and the layers of
abstraction. Assuming it is all used properly (and run through a synthesis tool
that performs constant propagation, dead-code elimination, and the like!),
there is zero overhead relative to a hand-rolled Verilog implementation.
Assuming the primitives are implemented correctly (relatively easier to ensure
in isolation), the final product will be correct as well.

## Semantics

This section describes the semantics of the Autopiper language in detail.

### Continuous-Time and Pipelined Interpretations

An Autopiper program is a parallel program in which multiple "transactions", or
invocations of top-level *entry point* functions, are active simultaneously.

Autopiper has a continuous-time semantics, which interprets the program as a
parallel software program in which transactions make forward progress at some
arbitrary rate, and a well-specified *pipelined semantics*, which "lowers" the
program onto a hardware pipeline by cutting its operations into stages.

This split interpretation (i) gives well-defined semantics to all operations
while (ii) allowing leeway for a timing model to time the pipeline in the most
appropriate way. (There are currently two timing models, one heuristic-based
and one simple single-forward-pass "earliest ready time"-based; both may be
constrained with timing stage barriers.)

Below, we will describe the meaning of each primitive or transform in the
general "continuous-time parallel program" interpretation first, then specify
how the compiler may lower the primitive to pipeline form. Note that even
nominally pipeline-specific primitives, such as value bypass, have meaning in
the continuous-time interpretation.

### Structure

An Autopiper program consists of a set of functions and various other
definitions (structure type definitions, constant values, possibly others
later). Functions may be marked with `entry`, in which case they are *entry
points*, or may be called from other functions. A program may have multiple
entry points.

Each entry point function is "elaborated" (in the EDA-software sense) by
inlining all called functions to produce a single function body. This entry
point then becomes a *process*.

Each process body is invoked continuously. New invocations may occur before old
invocations are complete, but new invocations will never proceed past old
invocations (precisely: for any program point P, all invocations that pass
through P will pass through P in invocation order.) In the pipeline
interpretation, each process body is invoked once per cycle.

Thus, a *process* corresponds to a *synchronous pipeline*. A general digital
design is built by creating many processes and connecting them via ports and
storage elements (below).

Example:

    # results in a pipeline, invoked once per cycle
    func entry main() : void {
        func1();   # inlined at this point
        func1();   # *another copy* inlined at this point
    }

    func func1() : void {
        # ...
    }

    # separate synchronous pipeline, also invoked every cycle
    func entry aux_pipe() : void {
        # ...
    }

### Control Flow and Value Computation

Autopiper code in a process body consists of a sequence of operations along
control-flow paths. Operations include:

* Computing a value (with the usual operators: arithmetic, bit operations,
  etc.)
* Assigning a value to a binding created by a `let` statement
* Any other primitive (below), such as:
  * Reading or writing a port or chan
  * Reading or writing a storage location (a reg or an array)
  * Interacting with younger or older invocations of the same process: bypass
    or kills/clears
  * Timing barriers

Operations that are not side-effect-free expressions (e.g. `add`) have a
well-defined order and cannot be reordered. Side-effect-free expression nodes
float freely across each other and side-effectful operations, except that they
are constrained by timing barriers.

The "control-flow spine" threaded through this semantic ordering can be split
and joined by control-flow operations:

* If/else statements
* While loops (possibly containing break/continue statements)
* Spawn statements: these "fork" control flow, with one fork the main spine and
  the other the spawned spine.
* 'Kill' statements, which end the current spine early.

A process body can thus be visualized as a set of directed edges through
operation nodes, with forks and joins:

                                .______________________________________________
      (entry)                   | Legend:                                     |
         |                      |    -->    directed arrow: control-flow edge |
         v                      |    o      node: a program operation         |
         o  (portwrite)         |_____________________________________________|
         |
         v
         o  (if/else)
        / \
       /   \______
      /           \
     o (assign)   v
     |            o (spawn)
     v            |\
     o (regread)  | \_________
     |            |           \
     v            |           v
     o            v           o (portwrite)
     \            o           |
      \          /            x (done)
       \        /
        \      /
         \    /
          \  /
           o  (join)
           |
           x  (done)


Any function results in a single-in, single-out control-flow graph. An if/else
statement, a while loop, and a spawn statement also result in such a graph,
constituted of 'body' sub-graphs joined in a particular way. (This is the
"combinator" view of control flow.) Note that the spawn statement/combinator
yields its left (main) spine as its single out; the body always ends with a
'done' and does not re-join the output flow.

In the continuous interpretation, multiple invocations may be active on this
control-flow graph at any one time, as long as one never passes another (as
defined precisely above).

In the pipelined interpretation, the graph is *cut* into pipeline stages.
Pipeline registers carry all live values across these cuts.  The control-flow
spine corresponds to an actual set of "valid" signals wired through the
hardware implementation of the process body; a single invocation of the process
corresponds to a single valid pulse flowing through the stages.

    ------------------------(stage 0)--------------------------------


      (entry)
         |
         v
         o  (portwrite)
    -----+-------------------(stage 1)-------------------------------
         v
         o  (if/else)
        / \   .--------------(stage 2)-------------------------------
       /   \__|___
      /       |   \
     o (assign|   v
    -+--------'   o (spawn)
     v            |\
     o (regread)  | \_________
    -+------------------------+---------(stage 3)---------------------
     v            |           v
     o            v           o (portwrite)
     \            o           |
      \          /            x (done)
       \        /
    ----\------/-------------(stage 4)--------------------------------
         \    /
          \  /
           o  (join)
           |
           x  (done)

An invariant of such a cut is that within a single stage, no valid pulse
dominates another (can flow into another). This allows for some nice properties
during code generation.

### Control-Flow Backedges: Stalls and Restarts

The model above assumes a DAG (directed acyclic graph) of control flow: in
other words, does not account for loops. Autopiper supports loops and maps them
to two well-defined pipelined behaviors in the pipeline interpretation:
*stalls* and (if combined with a killyounger, described below) *restarts*.

In other words, in Autopiper, it is idiomatic to write `while (!ready_signal);`
to stall at that point on a ready signal, or to wrap a pipeline frontend in a
while-loop in order to allow it to restart a unit of work.

If the following structure exists in user code:

    (entry)
      |
      v
      o (op)
      | ____________
      |/            ^
      o (join)      |
      |             |
      v             |
      o (body)      |
      |             |
      v             |
      o (if/else)   ^
      |\____________|
      |
      |
      x (done)

Then Autopiper will break the back-edge, inserting special restart logic, and
ensuring that the computation prior to the loop stalls if the loop back-edge is
taken. It also ensures that the entire loop body is in one pipeline stage (a
simple stall)  *unless* the backedge is dominated by (cannot be reached without
executing) a `killyounger`. This is because a backedge restart spanning
multiple stages would flush the invocations currently in those intermediate
stages, unless the user intended to flush them, but restarting the current
stage in the next cycle for a one-stage backedge results in no lost work
(assuming upstream is stalled).

    (entry)
     -+-------------------(stage 0)--------------------
      v
      o (op)
     -+-------------------(stage 1)--------------------
      |        (restart)
      |        /
      | ______/
      |/
      o (join)
      |
      v
      o (body)
      |
      v
      o (if/else)
      |\________
      |         \
      |         (invoke restart, stall stage 0, send bubble to stage 2)
     -+--------------------(stage 2)-------------------
      x (done)

### Examples of Basic Computation

A few code examples follow:

    func entry adder_pipeline() : void {

        # Ports will be explained below. These are simply inputs to the
        # pipeline.
        let x_in : port int32 = port "x_in";
        let y_in : port int32 = port "y_in";
        let sum_out : port int32 = port "sum_out";
        let request : port bool = port "request";
        let done : port bool = port "done";

        while (!request); # stall the pipe until a request comes.

        # no timing barriers/annotations here. The timing model will lay out
        # the computation across stages as necessary.
        let x = read x_in;
        let y = read y_in;
        let sum = x + y;
        write sum_out, sum;
        done = 1;

    }

    const CmdRead = 1;
    const CmdWrite = 2;
    func entry memory() : void {
        let addr : port int32 = port "addr";
        let cmd : port int8 = port "read";
        let data_in : port int32 = port "data_in";
        let data_out : port int32 = port "data_out";

        let arr : int32[256] = array;

        # timing annotation. 'stage' statements below are relative to the start
        # of this block.
        timing {
            stage 0;
            let c = read cmd;
            let a = (read addr) [7:0];
            if (c == CmdRead) {
                let res = arr[a];
                stage 1;
                write data_out, res;
            } else {
                arr[a] = read data_in;
            }
        }
    }

    func entry MIPS() : void {
        timing {
            stage 0;

            # unhandled here: bypass, transactional arch state. See below --
            # this is only a simple sketch example!
            let inst : int32 = Fetch(PC);
            PC = PC + 4;

            stage 1;

            # Instruction is an aggregate (struct) type.
            let decoded : Instruction = Decode(inst);

            stage 2;

            let rs = RF[decoded.rs];
            let rt = RF[decoded.rt];

            stage 3;

            let result = Execute(decoded, rs, rt);

            stage 4;

            if (decoded.write_mem)
                Store(result, rt);

            stage 5;

            RF[decoded.rd] = result;
        }
    }

### Communication Primitives: Ports and Chans

To build effective systems, processes must communicate with each other, and
separate control-flow spines within a process must communicate as well. These
needs are served by ports (inter-process) and chans (intra-process),
respectively.

Ports and chans are nonblocking single-duplex connections for communication. A
writer may write a value to a port or chan, and any reader will see whatever
value was present, or an undefined value if none was written, at the
corresponding write-cycle at the write point.

A port sees across invocations and between processes: a reader will see
whatever value was written at some undefined time offset at the write site.
Because processes are invoked at the same rate at the top level, a process that
writes a port every invocation and another process that reads that port every
invocation will properly communicate every value. 

In the pipelined interpretation, a port *does not stage/buffer its values in
pipeline latches*: it is a direct connection from the write site in some
pipeline stage in one synchronous pipe to some pipeline stage at some other
read site. If there are multiple read sites in one process, each site may see a
different value as it is assigned to a different stage.

In contrast, a chan logically lives within a *single process* and is tied to a
*single invocation*: it sees whatever value was written by the write site as
executed in the same invocation as the read site. In the pipelined
interpretation, a chan *does stage/buffer its values in pipeline latches*.
Hence, if a chan is written in stage 1, and read in stages 2, 3, and 4, each
read sees the value written in stage 1 (1, 2 or 3 cycles ago respectively).

If multiple writes occur to a port or chan, all wites must be in the same
process. Furthermore, all writes must occur in the same pipeline stage (use
timing barriers to enforce this). At most one write must occur per invocation.
If multiple writes occur, the resulting value is undefined.

Ports and chans are defined and used as so:

    func use_ports() : void {
        # defines a port with external name "asdf". This will appear in the
        # Verilog as a port on the generated module.
        let p1 : port int32 = port "asdf";

        # defines an anonymous port. Unusable outside the generated Verilog
        # module; pass between functions.
        let p2 : port int32 = port;

        write p1, 42;
        let x = read p1;

        let c1 : chan bool = chan;
        write c1, 1;
        spawn {
            let val = read c1;
        }
    }

### Storage Primitives: Reg and Array

Autopiper provides access to stateful storage via two primitives: `reg` and
`array`. A `reg` (register) is a single storage slot of a specified size, and
an array is a one-dimensional storage array (e.g., SRAM).

A register may have multiple write sites, as long as they are in the same cycle
when lowered to pipeline form. The rules for resolving this case are the same
as for ports and chans: the writes must be mutually exclusive.

Register and array contents are not associated with invocations of a process,
as chans are; rather, they "see through" the pipelining transform, such that a
write from a later invocation may be seen by an earlier invocation's read if
the read happens sufficiently far down the pipeline. Thus, these primitives are
building blocks but are likely not suitable for use by regular users *unless*
all reads and writes happen in the same stage.

The standard library will contain macros that wrap regs and arrays into
transactional storage elements (txn!, txnread!, txnwrite!) with bypass and
stall on non-ready reads.

Regs and arrays are declared and used as follows:

    func entry main() : void {
        let r : reg int32 = reg;
        let a : int32[64] = array;

        # 'reg' keyword is necessary to disambiguate this vs. a regular
        # variable assignment, which would assign the reg object, not its
        # contents.
        reg r = 1;

        # reg read
        let x = reg r;

        a[0] = x;
        x = a[x[5:0]];  # slice index down to 6-bit width
    }

### Kill Primitives: Pipeline Clears

Pipeline clearing and restarting (with proper state fixup) is one of the most
significant sources of implementation complexity in microarchitecture -- many
corner cases lurk and many details must be managed.

Autopiper provides a very simple set of abstractions -- an invocation can:

* Kill the current invocation (`kill`).
* Kill younger invocations of the same process (`killyounger`).

And two that are less commonly used by user code, but very useful in macros
that may split code into multiple processes and need to "attach" kill behavior
between different processes, e.g. ensuring that a clear in a backend clears a
frontend on the other side of a queue as well:

* Kill the current invocation if a given condition is true presently or becomes
  true at any later stage (`killif`) -- usually used with a port read.
* Perform a specified set of actions when the current invocation is done
  (`ondone`), is self-killed (`onkill`), or kills younger invocations
  (`onkillyounger`).

### Bypass Primitives: Ask/Provide

Autopiper abstracts communication between invocations into an `ask`/`provide`
set of primitives. This abstraction is sufficient to capture bypass networks
and similar structures (e.g., the CAM in a load/store buffer in a CPU core).

There are six fundamental operations:

* `provide`: Given a bypass object and an index that uniquely identifies a
  value, this begins a "provide scope" during which a writer of the given value
  is logically active in the calling invocation. Note that multiple `provide`s
  may be present, each with a different index, corresponding to different
  bypass write channels.

* `unprovide`: Given a bypass object and an index, ends the "provide scope" for
  that index. Provide and unprovide must be matched.

* `publish`: Furnish the computed value to the bypass network. Must be within a
  provide scope. The provide scope thus has two phases: "waiting to be
  computed" (prior to the publish) and "computed" (after the publish).

* `unavail`: Given a bypass object and an index, returns true if the given
  index has an open provide scope in an earlier (older) invocation *without* a
  published value, i.e., is still waiting to be published. This is usually used
  to stall the pipeline.

* `ask`: Given a bypass object and an index, returns true if the given index
  has an open provide scope with published value.

* `askvalue`: Given a bypass object and an index, returns the published value,
  if `ask` returned true.

See the txnwrite!/txnread! macros above for examples of use.

### Timing: Barriers and Timing Algorithms

Autopiper maps operations to pipeline stages, as described above. By default,
each operation is mapped according to various constraints:

* Operations with side-effects (storage/port accesses, clears and bypass
  primitives, control flow) must occur in the order specified in the program.
* All other operations (e.g., computations such as arithmetic and
  bit-manipulation) must occur after the values they depend on have been
  produced.
* All operations (both of the above categories) are constrained by explicit
  timing barriers.

Aside from those constraints, the compiler is free to place operations in
whatever stage it believes is best. The particular choices are guided by a set
of *heuristics*. Autopiper has two heuristics available, selectable via global
pragma:

* The 'null' timing model, selected with `pragma timing_model = "null";` at the
  top of the source, assigns zero delay-cost to every operation. This has the
  result that computations are placed in later stages only as a result of
  explicit timing barriers.
* The 'standard' timing model, selected with `pragma timing_model =
  "standard";` at the top of the source, has a built-in model of logic
  complexity (in terms of gate delays) and attempts to place operations within
  stages intelligently based on these delays.

The 'null' timing model is default: this is consistent with Autopiper's
general philosophy of "no magic" / "explicit semantics".

To force operations into particular stages, Autopiper provides the `timing`
block, in which `stage` statements are valid. Within the timing block, each
`stage` statement acts as a timing barrier that constrains all statements up to
the next `stage` statement (or the end of the block) to that particular offset
from the stage at the start of the timing block. For example:

    timing {
        stage 0;
        let x = Stage0();
        stage 1;
        let y = Stage1(x);
        stage 2;
        let z = Stage2(y);
        write output_port, z;
    }

### Full list of supported operators

Autopiper supports the following operators:

* +, -, \*, /, %: standard arithmetic
* &, |, ^: bitwise and, or, xor
* ~, -, +: unary complement, negative, plus
* <<, >>: left shift, right shift
* x[a:b]: bitslice from bit a down to bit b, inclusive (as in Verilog)
* { a, b, c }: bitwise concatenation of a, b, c
* integer literals in decimal and hexadecimal (0xFF -- C syntax)
* expr { stmt1; stmt2; value; }: a statement-block expression, whose value is
  equal to the last value in the block (which must be a value statement).
* port: an anonymous port (only in let-statement initializers)
* port "portname": an exported port with the given Verilog namem
* chan: initializer for chans in let-statements
* array: initializer for arrays in let-statements
* reg: initializer for registers in let-statements
* read port-or-chan: read from a port or chan
* array[index]: read from an array
* reg reg-object: read from a reg
* function(arg1, arg2, arg3)

### Full list of statement types

Autopiper supports the following statements:

* { stmt; stmt; } -- blocks
* if (condition) if-body \[ else else-body \]
* while (condition) while-body
* break (inside a while)
* continue (inside a while)
* spawn { spawn-body }
* return (inside a non-entry function)
* let variable : type = initial-value;
* variable = value;
* array[index] = value;
* reg register = value;
* write port-or-chan, value;
* kill;
* killyounger;
* killif condition; (condition must consist only of port reads and pure
  computation)
* onkill { body }
* ondone { body }
* onkillyounger { body }
* bypass-related operations (TODO)

### Macros

Autopiper supports a macro system to allow for easy composability: the user
should have a library of building blocks that, under the hood, are built from
language built-in primitives or other lower-level macros.

Macros work at the tokenizer level, before the language parser sees the input.
Thus, they may appear at any place in the input, and may produce any output.
The only limitation is that argument values, and the produced bodies, must have
matching paren/bracket/brace nesting so that the macro expander can properly
delimit things.

Macro invocations always occur as identifiers with a trailing '!'; this is how
the tokenizer recognizes them.

A macro definition looks like the following:

    macro! macroname {
        ( arg1, arg2 ) = ( body; write $arg1, 1; write $arg2, 2; )
    }

Each `( args ) = ( body )` clause in the definition is called a *match arm* and
provides an expansion for a particular argument pattern.

When the macro expander recognizes a macro invocation (e.g., `macroname!(x,
y)`), it reads in the arguments as strings of tokens delimited by commas at the
top nesting level (so, e.g., a comma inside a parenthesized argument value does
not delimit arguments but the comma after that argument's closing parenthesis
does), then takes the first match arm that matches. A match arm matches if it
has the same number of argument names as arguments in the list.

Match arm argument patterns support lists as follows:

    macro! macroname {
        ([x, *others]) = ( let $x = 1; macroname!([$others]) )
        ([]) = ()
    }

A list of argument names in brackets matches a list of arguments in brackets.
An argument name prefixed by an asterisk (`*`) matches the remainder of the
list, and expands to that list including separating commas.

Within the body, arguments' values may be substituted by prepending a `$` to
argument names. The expander also supports concatenating identifiers with the
`$$` operator, so, e.g., `$x $$ \_suffix` will expand to `var\_suffix` if `x`
bound to the identifier `var`.

### Nested Functions

Entry points (toplevel functions, or "processes") may be defined inside the
body of another function. Such definitions may be useful, for example, in the
expansion of macros that define new processes -- e.g., a
queue-with-separate-backend-subpipe macro.

Such nested functions to *not* have names nor return types.

These definitions may occur even in non-entry functions and will properly
instantiate a new process for each call site.

These anonymous functions *do* bind values in-scope at their
definition sites. This allows for sharing of ports and arrays (however, it is
an error to use values from the parent context). For example:

    func entry main() : void {

        let backend_port : port int32 = port;

        func entry {
            Backend(backend_port);
        }
    }

    func Backend(p : port int32) : void {
    }

### Implementation Details: Transforms/Algorithms

TODO: expand this section a bit more.

Autopiper operates by performing the following transforms:

* Lexer
* Macro expander
* Parser
* Function inliner
* Variable scope resolution
* Type inference engine
* IR code generator
* IR typecheck
* Pipeline lowering:
  * Process extraction
  * Dominance tree computation
  * Backedge conversion (restart-point insertion)
  * If-conversion (predication) and valid-spine insertion
  * Pipe-timing-DAG construction
  * Pipe flattening (control flow removal)
  * Timing system solve / stage separation
  * Per-stage hooks:
    * Insert kill\_if checks
    * Generate stall signals
    * Generate stage kill signals
* Generate Verilog

## Current Status

A prototype compiler exists on [GitHub](https://github.com/google/autopiper/),
having been developed since July 2014 (currently at ~14K lines of C++). It
implements the pipeline transform (key idea #1) and most of the primitives (key
idea #2). It is, as of today (Jan 2, 2015), still missing (i) bypass generation
and (ii) the macro transformer.

I plan to finish these pieces, but will eventually build a new implementation,
likely as a Haskell DSL (the Autopiper monad!), because the language semantics
have undergone significant evolution and the compiler is not as clean and
malleable as it could be.

The final goal is to produce a language in which I can write an out-of-order
superscalar CPU core (of, say, MIPS or OpenRISC) with the same amount of
complexity (or nearly so) as a functional (non-pipelined) description of the
ISA. This will hopefully be achieved sometime in Spring 2015.
