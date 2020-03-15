// std
#include <iostream>

// translator
#include <Translator.hpp>

int main() {
  // 0. Create a LLVM and Triton objects
  LLVMContext LLVMCtx;
  API TritonCtx;
  // 1. Setup the Triton context and a Translator object
  TritonCtx.setArchitecture(triton::arch::ARCH_X86_64);
  Translator Tr(LLVMCtx, TritonCtx);
  // 2. Keep a map of translated nodes and variables
  map<ExpKey, shared_ptr<llvm::Module>> Cache;
  map<string, SharedAbstractNode> Variables;
  // 3. Symbolize 2 registers and convert them to AST variables
  auto ASTCtx = TritonCtx.getAstContext();
  auto SymRAX = TritonCtx.symbolizeRegister(TritonCtx.getRegister(triton::arch::register_e::ID_REG_X86_RAX));
  auto SymRBX = TritonCtx.symbolizeRegister(TritonCtx.getRegister(triton::arch::register_e::ID_REG_X86_RBX));
  auto VarRAX = ASTCtx->variable(SymRAX);
  auto VarRBX = ASTCtx->variable(SymRBX);
  Variables[SymRAX->getName()] = VarRAX;
  Variables[SymRBX->getName()] = VarRBX;
  // 4. Build an optimizable Triton AST: (RAX - RBX) + (RAX + RBX) => RAX << 1
  auto InTritonAST = ASTCtx->bvadd(
    ASTCtx->bvsub(VarRAX, VarRBX),
    ASTCtx->bvadd(VarRAX, VarRBX)
    );
  std::cout << "Original Triton AST" << std::endl;
  std::cout << InTritonAST << std::endl;
  // 5. Convert the Triton AST to an optimized LLVM-IR Module
  auto LLVMModule = Tr.TritonAstToLLVMIR(InTritonAST, Cache);
  std::cout << "Optimized LLVM-IR Module" << std::endl;
  LLVMModule->dump();
  // 6. Convert the optimized LLVM-IR Module to a Triton AST
  auto OutTritonAST = Tr.LLVMIRToTritonAst(LLVMModule, Variables);
  std::cout << "Optimized Triton AST" << std::endl;
  std::cout << OutTritonAST << std::endl;
  // 7. Exit
  return 0;
}