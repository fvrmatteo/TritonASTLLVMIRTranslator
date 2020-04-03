#ifndef TRANSLATOR_HPP
#define TRANSLATOR_HPP

// std
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <string>
#include <map>

// llvm
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/IR/Module.h>

// triton
#include <triton/api.hpp>

// llvm namespaces
using namespace std;
using namespace llvm;
using namespace llvm::legacy;

// triton namespaces
using namespace triton;
using namespace triton::ast;

// typedefs
using ExpKey = triton::usize;

// strutures
typedef struct AstNode {
  // Special values for the reference handling
  triton::engines::symbolic::SharedSymbolicExpression Expression;
  // We need to keep track of the current state
  map<SharedAbstractNode, Value*> Nodes;
  map<string, SharedAbstractNode> Vars;
  map<string, Value*> VarsValue;
  shared_ptr<llvm::Module> Module;
  shared_ptr<IRBuilder<>> IR = nullptr;
  // Normal values for the standard handling
  shared_ptr<AstNode> Parent;
  SharedAbstractNode Node;
  uint64_t Index;
  // Depth of the AST
  size_t Depth;
  // Default constructor
  AstNode(SharedAbstractNode Node, shared_ptr<AstNode> Parent) :
    Node(Node), Parent(Parent), Expression(nullptr), Index(0),
    Depth(0) {}
  // Print the node
  void dump() {
    // Convert the expression to a string
    stringstream ss;
    if (this->Expression) {
      ss << this->Expression;
    } else {
      ss << "";
    }
    string ExpressionString = ss.str();
    // Dump the node
    cout << "{ Index = " << dec << this->Index
         << ", Depth = " << dec << this->Depth
         << ", Node = " << this->Node
         << ", Expression = " << ExpressionString
         << ", Children = " << dec << this->Node->getChildren().size()
         << ", Type = " << dec << this->Node->getType()
         << " }" << endl;
  }
} AstNode;

/*
  The idea is to use the "visitor pattern" to implement the lifting of a Triton
  AST to LLVM-IR to provide the capability to optimize it and get back to have a
  simplified Triton AST.
*/

// #define DEBUG_OUTPUT
// #define VERBOSE_OUTPUT
// #define RECURSIVE

class Translator {
private:

  // Fields needed for the Triton 2 LLVM conversion
  LLVMContext& Context;
  shared_ptr<Module> Module;

  // Fields needed for the LLVM 2 Triton conversion
  API& Api;
  map<string, SharedAbstractNode> Vars;
  map<string, Value*> VarsValue;

  // Get a properly sized decimal node
  ConstantInt* GetDecimal(IntegerNode& Value, uint64_t BitVectorSize);

  // Lift the nodes in an AST in a worklist-based way
  Value* LiftNodesWBS(const SharedAbstractNode& TopNode, shared_ptr<IRBuilder<>> IR, map<ExpKey, shared_ptr<llvm::Module>>& Cache, ssize_t MaxDepth);

  // Lift the instructions in a block in a DFS way
  SharedAbstractNode LiftInstructionsDFS(Value* value, map<Value*, SharedAbstractNode>& Values, map<string, SharedAbstractNode>& Variables);

  // Optimize our LLVM Module
  void OptimizeModule(llvm::Module* M);

  // Lower thr BSWAP intrinsic
  BasicBlock* FixBSWAPIntrinsic(BasicBlock* BB);

  // Fix the ICmp behaviour using an 'ite' node
  SharedAbstractNode FixICmpBehavior(SharedAbstractNode Node);

  // Undo the ICmp behavior fix removing the 'ite' node
  SharedAbstractNode UndoICmpBehavior(SharedAbstractNode Node);

  // Convert a not-logical node in a logical one
  SharedAbstractNode ConvertToLogical(SharedAbstractNode Node);

  // Clone the body of a function inside another function (with the same parameters)
  void CloneFunctionInto(Function* SrcFunc, Function* DstFunc) const;

  // Determine AST size
  uint64_t DetermineASTSize(SharedAbstractNode Node, map<SharedAbstractNode, uint64_t>& Nodes);

public:
  // Default constructor
  Translator(LLVMContext& Context, API& Api);

  // Default destructor
  ~Translator() {};

  // Lift a Triton AST to a LLVM-IR block
  shared_ptr<llvm::Module> TritonAstToLLVMIR(const SharedAbstractNode& Node, map<ExpKey, shared_ptr<llvm::Module>>& Cache, ssize_t MaxDepth = -1);

  // Lift a LLVM-IR block to a Triton AST
  SharedAbstractNode LLVMIRToTritonAst(const shared_ptr<llvm::Module>& Module, map<string, SharedAbstractNode>& Variables, bool IsITE = false, bool IsLogical = false);
  
};

#endif