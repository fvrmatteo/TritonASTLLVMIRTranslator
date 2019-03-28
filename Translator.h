#ifndef TRANSLATOR
#define TRANSLATOR

// std
#include <map>
#include <string>

// llvm
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

// triton
#include <triton/api.hpp>

// llvm namespaces
using namespace std;
using namespace llvm;
using namespace llvm::legacy;

// triton namespaces
using namespace triton;
using namespace triton::ast;

/*
	The idea is to use the "visitor pattern" to implement the lifting of a Triton AST to LLVM-IR
	to provide the capability to optimize it and get back to have a simplified Triton AST.
*/

//#define DEBUG_OUTPUT
//#define VERBOSE_OUTPUT

class Translator {

private:

	// Fields needed for the Triton 2 LLVM conversion
	LLVMContext context;
	shared_ptr<Module> module;
	
	// Fields needed for the LLVM 2 Triton conversion
	API& api;
	map<string, SharedAbstractNode> vars;
	map<string, Value *> varsValue;

	// Get a properly sized decimal node
	Value* GetDecimal(IntegerNode& val, uint64_t bvsz);

	// Lift the nodes in an AST in a DFS way
	Value* LiftNodesDFS(const SharedAbstractNode& node, map<SharedAbstractNode, Value *>& nodes, IRBuilder<>& ir);

	// Lift the instructions in a block in a DFS way
	SharedAbstractNode LiftInstructionsDFS(Value* value, map<Value *, SharedAbstractNode>& values);

	// Optimize our LLVM module
	void OptimizeModule();

	// Fix the ICmp behaviour using an 'ite' node
	SharedAbstractNode FixICmpBehavior(SharedAbstractNode node);

	// Undo the ICmp behavior fix removing the 'ite' node
	SharedAbstractNode UndoICmpBehavior(SharedAbstractNode node);

	// Convert a not-logical node in a logical one
	SharedAbstractNode ConverToLogical(SharedAbstractNode node);

public:

	// Default constructor
	Translator(API& _api);

	// Default destructor
	~Translator();

	// Lift a Triton AST to a LLVM-IR block
	shared_ptr<Module> TritonAstToLLVMIR(const SharedAbstractNode& node);

	// Lift a LLVM-IR block to a Triton AST
	SharedAbstractNode LLVMIRToTritonAst(const shared_ptr<Module> module);

	// Determine AST size
	uint64_t DetermineASTSize(SharedAbstractNode snode, map<SharedAbstractNode, uint64_t>& nodes);

};

#endif
