// std
#include <iostream>
#include <sstream>
#include <vector>

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
  // 4. Allocate some fake symbolic variables
  for (size_t Size = 1; Size <= 128; Size++) {
    for (size_t i = 0; i < 20; i++) {
      std::stringstream ss;
      ss << "FakeVar_";
      ss << dec << Size;
      ss << "_";
      ss << dec << i;
      auto VarName = ss.str();
      auto SymVar = TritonCtx.newSymbolicVariable(Size, VarName);
      Variables[SymVar->getAlias()] = ASTCtx->variable(SymVar);
    }
  }
  // 5. Build an optimizable Triton AST
  auto InnerAST = ASTCtx->bvnot(
    ASTCtx->bvneg(
      ASTCtx->bvmul(
        VarRAX,
        VarRBX
        )
      )
    );
  auto InTritonAST = ASTCtx->bvadd(
    ASTCtx->bvsub(
      ASTCtx->bvadd(
        InnerAST,
        InnerAST
        ),
      ASTCtx->bvadd(
        InnerAST,
        InnerAST
      )
    ),
    ASTCtx->bvsub(
      ASTCtx->bvadd(
        InnerAST,
        InnerAST
        ),
      ASTCtx->bvadd(
        InnerAST,
        InnerAST
        )
      )
    );
  std::cout << "> Original Triton AST\n" << std::endl;
  std::cout << InTritonAST << std::endl;
  // 5. Convert the Triton AST to an optimized LLVM-IR Module
  auto LLVMModule = Tr.TritonAstToLLVMIR(InTritonAST, Cache, 3);
  std::cout << "\n> Optimized LLVM-IR Module\n" << std::endl;
  LLVMModule->dump();
  // 6. Convert the optimized LLVM-IR Module to a Triton AST
  auto OutTritonAST = Tr.LLVMIRToTritonAst(LLVMModule, Variables);
  std::cout << "\n> Optimized Triton AST\n" << std::endl;
  std::cout << OutTritonAST << std::endl;
  // 7. Exit
  return 0;
}