# TritonAST to LLVM-IR and back

This code has been developed as a quick test in a weekend to evaluate how feasible it would have been to use LLVM-IR optimizations to simplify a TritonAST.

# Sample output

```
> Original Triton AST

(bvadd (bvsub (bvadd (bvnot (bvneg (bvmul SymVar_0 SymVar_1))) (bvnot (bvneg (bvmul SymVar_0 SymVar_1)))) (bvadd (bvnot (bvneg (bvmul SymVar_0 SymVar_1))) (bvnot (bvneg (bvmul SymVar_0 SymVar_1))))) (bvsub (bvadd (bvnot (bvneg (bvmul SymVar_0 SymVar_1))) (bvnot (bvneg (bvmul SymVar_0 SymVar_1)))) (bvadd (bvnot (bvneg (bvmul SymVar_0 SymVar_1))) (bvnot (bvneg (bvmul SymVar_0 SymVar_1))))))

> Unoptimized LLVM-IR Module

; ModuleID = 'TritonAstModule'
source_filename = "TritonAstModule"

@FakeVar_64_0 = common global i64

; Function Attrs: alwaysinline
define common i64 @TritonAstFunction() #0 {
TritonAstEntry:
  %0 = load i64, i64* @FakeVar_64_0
  %1 = add i64 %0, %0
  %2 = add i64 %0, %0
  %3 = sub i64 %1, %2
  %4 = add i64 %0, %0
  %5 = add i64 %0, %0
  %6 = sub i64 %4, %5
  %7 = add i64 %3, %6
  ret i64 %7
}

attributes #0 = { alwaysinline }

> Optimized LLVM-IR Module

; ModuleID = 'TritonAstModule'
source_filename = "TritonAstModule"

; Function Attrs: alwaysinline
define common i64 @TritonAstFunction() local_unnamed_addr #0 {
TritonAstEntry:
  ret i64 0
}

attributes #0 = { alwaysinline }

> Optimized Triton AST

(_ bv0 64)
```

# Inspiration

It's important to note that this is just an experiment to take the [Triton + Arybo efforts](https://github.com/JonathanSalwan/Tigress_protection/blob/master/solve-vm.py#L618) in converting TritonAST to LLVM-IR a step further. Optimizing an AST is quite useful sometime, especially when attacking obfuscation or opaque predicates.