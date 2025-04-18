# RyuJIT: Porting to different platforms

First, understand the JIT architecture by reading [RyuJIT overview](ryujit-overview.md).

# What is a Platform?

* Target instruction set
* Target pointer size
* Target operating system
* Target calling convention and ABI (Application Binary Interface)
* Runtime data structures (not really covered here)
* GC encoding
  * All targets use the same GC encoding scheme and APIs, except for Windows x86, which uses JIT32_GCENCODER.
* Debug information (mostly the same for all targets)
* EH (exception handling) information (not really covered here)

One advantage of the CLR is that the VM (mostly) hides the (non-ABI) OS differences.

# The Very High Level View

The following components need to be updated, or target-specific versions created, for a new platform.

* The basics
  * target.h
* Instruction set architecture:
  * registerXXX.h - defines all registers used by the architecture and any aliases for them
  * emitXXX.h - defines signatures for public instruction emission methods (e.g. "emit an instruction which takes a single integer argument") and private architecture-specific helpers
  * emitXXX.cpp - implementation for emitXXX.h
  * emitfmtXXX.h - optionally defines validity rules for how instructions should be formatted (e.g. RISC-V has no rules defined)
  * instrsXXX.h - defines per-architecture instructions in assembly
  * targetXXX.h - defines architectural constraints used elsewhere, such as "bitmask for all integer registers where callee is saved" or "size in bytes of a floating point register"
  * targetXXX.cpp - implements ABI classifier for this architecture
  * lowerXXX.cpp - implements [Lowering](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/ryujit-overview.md#lowering) for this architecture
  * lsraXXX.cpp - implements register requirement setting based on [GenTree Nodes](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/ryujit-overview.md#gentree-nodes)
  * codegenXXX.cpp - implements main codegen for this architecture (i.e. generating per-architecture instructions based on [GenTree Nodes](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/ryujit-overview.md#gentree-nodes))
  * hwintrinsic\*XXX.\* and simdashwintrinsic\*XXX.h - defines and implements hardware intrinsic features, e.g. vector instructions
  * unwindXXX.cpp - implements public unwinding API and unwind info dumping for debug use
* Calling Convention and ABI: all over the place
* 32 vs. 64 bits
  * Also all over the place. Some pointer size-specific data is centralized in target.h, but probably not 100%.

# Porting stages and steps

There are several steps to follow to port the JIT (some of which can be be done in parallel), described below.

## Initial bring-up

* Create the new platform-specific files
* Create the platform-specific build instructions (in CMakeLists.txt). This probably will require
  new platform-specific build instructions at the root level, as well as the JIT level of the source tree.
* Focus on MinOpts; disable the optimization phases, or always test with `DOTNET_JITMinOpts=1`.
* Disable optional features, such as:
  * `FEATURE_EH` -- if 0, all exception handling blocks are removed. Of course, tests with exception handling
    that depend on exceptions being thrown and caught won't run correctly.
  * `FEATURE_STRUCTPROMOTE`
  * `FEATURE_FASTTAILCALL`
  * `FEATURE_TAILCALL_OPT`
  * `FEATURE_SIMD`
* Build the new JIT as an altjit. In this mode, a "base" JIT is invoked to compile all functions except
  the one(s) specified by the `DOTNET_AltJit` variable. For example, setting `DOTNET_AltJit=Add` and running
  a test will use the "base" JIT (say, the Windows x64 targeting JIT) to compile all functions *except*
  `Add`, which will be first compiled by the new altjit, and if it fails, fall back to the "base" JIT. In this
  way, only very limited JIT functionality need to work, as the "base" JIT takes care of most functions.
* Implement the basic instruction encodings. Test them using a method like `CodeGen::genArm64EmitterUnitTests()`.
* Implement the bare minimum to get the compiler building and generating code for very simple operations, like addition.
* Focus on the CodeGenBringUpTests (src\tests\JIT\CodeGenBringUpTests), starting with the simple ones.
  * These are designed such that for a test `XXX.cs`, there is a single interesting function named `XXX` to compile
    (that is, the name of the source file is the same as the name of the interesting function. This was done to make
    the scripts to invoke these tests very simple.). Set `DOTNET_AltJit=XXX` so the new JIT only attempts to
    compile that one function.
  * Merged test groups interfere with the simplicity of these tests by removing the entry point from each individual
    test and creating a single wrapper that calls all of the tests in a single process. To restore the
    old behavior, build the tests with the environment variable `BuildAsStandalone` set to `true`.
* Use `DOTNET_JitDisasm` to see the generated code for functions, even if the code isn't run.

## Expand test coverage

* Get more and more tests to run successfully:
  * Run more of the `JIT` directory of tests
  * Run all of the Pri-0 "innerloop" tests
  * Run all of the Pri-1 "outerloop" tests
* It is helpful to collect data on asserts generated by the JIT across the entire test base, and fix the asserts in
  order of frequency. That is, fix the most frequently occurring asserts first.
* Track the number of asserts, and number of tests with/without asserts, to help determine progress.

## Bring the optimizer phases on-line

* Run tests with and without `DOTNET_JITMinOpts=1`.
* It probably makes sense to set `DOTNET_TieredCompilation=0` (or disable it for the platform entirely) until much later.

## Improve quality

* When the tests pass with the basic modes, start running with `JitStress` and `JitStressRegs` stress modes.
* Bring `GCStress` on-line. This also requires VM work.
* Work on `DOTNET_GCStress=4` quality. When crossgen/ngen is brought on-line, test with `DOTNET_GCStress=8`
  and `DOTNET_GCStress=C` as well.

## Work on performance

* Determine a strategy for measuring and improving performance, both throughput (compile time) and generated code
  quality (CQ).

## Work on platform parity

* Implement features that were intentionally disabled, or for which implementation was delayed.
* Implement SIMD (`Vector<T>`) and hardware intrinsics support.

# Front-end changes

* Calling Convention
  * Struct args and returns seem to be the most complex differences
    * Importer and morph are highly aware of these
      * E.g. `fgMorphArgs()`, `fgFixupStructReturn()`, `fgMorphCall()`, `fgPromoteStructs()` and the various struct assignment morphing methods
  * HFAs on ARM
* Tail calls are target-dependent, but probably should be less so
* Intrinsics: each platform recognizes different methods as intrinsics (e.g. `Sin` only for x86, `Round` everywhere BUT amd64)
* Target-specific morphs such as for mul, mod and div

# Backend Changes

* Lowering: fully expose control flow and register requirements
* Code Generation: traverse blocks in layout order, generating code (InstrDescs) based on register assignments on nodes
  * Then, generate prolog & epilog, as well as GC, EH and scope tables
* ABI changes:
  * Calling convention register requirements
    * Lowering of calls and returns
    * Code sequences for prologs & epilogs
  * Allocation & layout of frame

# Target ISA "Configuration"

* Conditional compilation (set in jit.h, based on incoming define, e.g. #ifdef X86)
```C++
_TARGET_64_BIT_ (32 bit target is just ! _TARGET_64BIT_)
_TARGET_XARCH_, _TARGET_ARMARCH_
_TARGET_AMD64_, _TARGET_X86_, _TARGET_ARM64_, _TARGET_ARM_
```
* Target.h
* InstrsXXX.h

# Instruction Encoding

* The `insGroup` and `instrDesc` data structures are used for encoding
  * `instrDesc` is initialized with the opcode bits, and has fields for immediates and register numbers.
  * `instrDesc`s are collected into `insGroup` groups
  * A label may only occur at the beginning of a group
* The emitter is called to:
  * Create new instructions (`instrDesc`s), during CodeGen
  * Emit the bits from the `instrDesc`s after CodeGen is complete
  * Update Gcinfo (live GC vars & safe points)

# Adding Encodings

* The instruction encodings are captured in instrsXXX.h. These are the opcode bits for each instruction
* The structure of each instruction set's encoding is target-dependent
* An "instruction" is just the representation of the opcode
* An instance of `instrDesc` represents the instruction to be emitted
* For each "type" of instruction, emit methods need to be implemented. These follow a pattern but a target may have unique ones, e.g.
```C++
emitter::emitInsMov(instruction ins, emitAttr attr, GenTree* node)
emitter::emitIns_R_I(instruction ins, emitAttr attr, regNumber reg, ssize_t val)
emitter::emitInsTernary(instruction ins, emitAttr attr, GenTree* dst, GenTree* src1, GenTree* src2) (currently Arm64 only)
```

# Lowering

* Lowering ensures that all register requirements are exposed for the register allocator
  * Use count, def count, "internal" reg count, and any special register requirements
  * Does half the work of code generation, since all computation is made explicit
    * But it is NOT necessarily a 1:1 mapping from lowered tree nodes to target instructions
  * Its first pass does a tree walk, transforming the instructions. Some of this is target-independent. Notable exceptions:
    * Calls and arguments
    * Switch lowering
    * LEA transformation
  * Its second pass walks the nodes in execution order
    * Sets register requirements
      * sometimes changes the register requirements children (which have already been traversed)
    * Sets the block order and node locations for LSRA
      * `LinearScan::startBlockSequence()` and `LinearScan::moveToNextBlock()`

# Register Allocation

* Register allocation is largely target-independent
  * The second phase of Lowering does nearly all the target-dependent work
* Register candidates are determined in the front-end
  * Local variables or temps, or fields of local variables or temps
  * Not address-taken, plus a few other restrictions
  * Sorted by `lvaSortByRefCount()`, and determined by `lvIsRegCandidate()`

# Addressing Modes

* The code to find and capture addressing modes is particularly poorly abstracted
* `genCreateAddrMode()`, in CodeGenCommon.cpp traverses the tree looking for an addressing mode, then captures its constituent elements (base, index, scale & offset) in "out parameters"
  * It never generates code, and is only used by `gtSetEvalOrder`, and by Lowering

# Code Generation

* For the most part, the code generation method structure is the same for all architectures
  * Most code generation methods start with "gen"
* Theoretically, CodeGenCommon.cpp contains code "mostly" common to all targets (this factoring is imperfect)
  * Method prolog, epilog,
* `genCodeForBBList()`
  * Walks the trees in execution order, calling `genCodeForTreeNode()`, which needs to handle all nodes that are not "contained"
  * Generates control flow code (branches, EH) for the block
