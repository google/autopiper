Autopiper: automatic pipeline synthesis
---------------------------------------

Autopiper is a pipeline synthesis tool that understands high-level constructs
and allows for automatic pipelining and automatic inference of low-level
microarchitectural 'plumbing' in RTL (Verilog). It has well-defined semantics
that allow for easier reasoning about digital systems at the level of
functional state, and allow leeway for formal transforms to produce *correct*
high-performance implementations.

Autopiper is a research project -- this may or may not work! -- and is intended
for now more as a developed-in-the-open toy than a serious tool.

Autopiper is developed by Chris Fallin `<cfallin@c1f.net>`. Autopiper's
copyright is assigned to Google Inc., but autopiper is an independent
side-project and is not an official Google product in any way.

Current status
--------------

The pipeline lowering / synthesis backend (IR to Verilog) has implemented the
key transforms, and works with small tests. Currently the frontend is in
development (parsing, typechecking/inference, codegen), after which the macro
layer will be developed alongside some larger test designs.

In other words: it's not actually done yet! You can play with the current
functionality, i.e., lowering IR to pipeline form, by running autopiper on the
IR inputs in tests/.

Building
--------

Autopiper requires CMake, the Boost C++ libraries, and GMP (an
arbitrary-precision numeric library).

On Ubuntu:

    $ sudo apt-get install cmake libboost-dev libgmp-dev

On Arch Linux:

    $ sudo pacman -Syu cmake boost gmp

Then create a build directory and compile the application:

    $ mkdir build/
    $ cd build/
    $ cmake ..
    $ make
    $ src/autopiper --help
