# TritonAST to LLVM-IR and back

This code has been developed as a quick test in a weekend to evaluate how feasible it would have been to use LLVM-IR optimizations to simplify a TritonAST.

The steps are:

1. getting an **unrolled** TritonAST;
2. convert it to LLVM-IR via the **TritonAstToLLVMIR** function;
3. get the **simplified** TritonAST as output with the **LLVMIRToTritonAst** function.

# TODO

1. Provide a C++ example showing the described steps.
2. Verify the consistency of all the translations.

