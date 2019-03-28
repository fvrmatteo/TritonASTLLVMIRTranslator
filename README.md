# TritonAST to LLVM-IR and back

This code has been developed as a quick test in a weekend to evaluate how feasible it would have been to use LLVM-IR optimizations to simplify a TritonAST.

The steps are:

1. getting an **unrolled** TritonAST;
2. convert it to LLVM-IR via the **TritonAstToLLVMIR** function;
3. get the **simplified** TritonAST as output with the **LLVMIRToTritonAst** function.

# Inspiration

It's important to note that this is just an experiment to take the [Triton + Arybo efforts](https://github.com/JonathanSalwan/Tigress_protection/blob/master/solve-vm.py#L618) in converting TritonAST to LLVM-IR a step further. Optimizing an AST is quite useful sometime, especially when attacking obfuscation or opaque predicates.

# TODO

1. Provide a C++ example showing the described steps.
2. Verify the consistency of all the translations.
