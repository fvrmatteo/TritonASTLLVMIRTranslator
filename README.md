# TritonAST to LLVM-IR and back

This code has been developed as a quick test in a weekend to evaluate how feasible it would have been to use LLVM-IR optimizations to simplify a TritonAST.

# Sample output

```
Original Triton AST
(bvadd (bvsub SymVar_0 SymVar_1) (bvadd SymVar_0 SymVar_1))

Optimized LLVM-IR Module
; ModuleID = 'TritonAstModule'
source_filename = "TritonAstModule"

@SymVar_0 = common local_unnamed_addr global i64

; Function Attrs: alwaysinline
define common i64 @TritonAstFunction() local_unnamed_addr #0 {
TritonAstEntry:
  %0 = load i64, i64* @SymVar_0, align 4
  %1 = shl i64 %0, 1
  ret i64 %1
}

attributes #0 = { alwaysinline }

Optimized Triton AST
(bvshl SymVar_0 (_ bv1 64))
```

# Inspiration

It's important to note that this is just an experiment to take the [Triton + Arybo efforts](https://github.com/JonathanSalwan/Tigress_protection/blob/master/solve-vm.py#L618) in converting TritonAST to LLVM-IR a step further. Optimizing an AST is quite useful sometime, especially when attacking obfuscation or opaque predicates.