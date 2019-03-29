#include "Translator.h"

// Default contructor: we need the Triton context to access
// the AstContext and the symbolic variables
Translator::Translator(API& _api) : api(_api) {}

// Default destructor
Translator::~Translator() {}

/*
	Generic utilities
*/

uint64_t Translator::DetermineASTSize(SharedAbstractNode snode, map<SharedAbstractNode, uint64_t>& nodes) {
	// Check if it's a known node -> size
	if (nodes.find(snode) != nodes.end()) {
		return nodes[snode];
	}
	// We always consider the single AST as 1
	uint64_t size = 1;
	// Handle the AST type
	switch (snode->getType()) {
	case ast_e::SX_NODE:
	case ast_e::ZX_NODE:
	case ast_e::LNOT_NODE:
	case ast_e::BVNOT_NODE:
	case ast_e::BVNEG_NODE:
	case ast_e::EXTRACT_NODE: {
		uint64_t sz1 = DetermineASTSize(snode->getChildren()[0], nodes);
		size += sz1;
		break;
	}
	case ast_e::BVSLT_NODE:
	case ast_e::BVUGE_NODE:
	case ast_e::BVUGT_NODE:
	case ast_e::BVULE_NODE:
	case ast_e::BVULT_NODE:
	case ast_e::BVNAND_NODE:
	case ast_e::BVNOR_NODE:
	case ast_e::BVXNOR_NODE:
	case ast_e::BVSLE_NODE:
	case ast_e::BVSGT_NODE:
	case ast_e::BVSGE_NODE:
	case ast_e::DISTINCT_NODE:
	case ast_e::EQUAL_NODE:
	case ast_e::BVSMOD_NODE:
	case ast_e::BVSREM_NODE:
	case ast_e::BVUREM_NODE:
	case ast_e::BVUDIV_NODE:
	case ast_e::BVSDIV_NODE:
	case ast_e::BVROR_NODE:
	case ast_e::BVROL_NODE:
	case ast_e::BVMUL_NODE:
	case ast_e::BVSHL_NODE:
	case ast_e::BVLSHR_NODE:
	case ast_e::BVASHR_NODE:
	case ast_e::LOR_NODE:
	case ast_e::BVOR_NODE:
	case ast_e::LAND_NODE:
	case ast_e::BVAND_NODE:
	case ast_e::BVXOR_NODE:
	case ast_e::BVSUB_NODE:
	case ast_e::BVADD_NODE: {
		uint64_t sz1 = DetermineASTSize(snode->getChildren()[0], nodes);
		uint64_t sz2 = DetermineASTSize(snode->getChildren()[1], nodes);
		size += (sz1 + sz2);
		break;
	}
	case ast_e::ITE_NODE: {
		uint64_t sz1 = DetermineASTSize(snode->getChildren()[0], nodes);
		uint64_t sz2 = DetermineASTSize(snode->getChildren()[1], nodes);
		uint64_t sz3 = DetermineASTSize(snode->getChildren()[2], nodes);
		size += (sz1 + sz2 + sz3);
		break;
	}

	case ast_e::CONCAT_NODE: {
		for (auto child : snode->getChildren()) {
			size += DetermineASTSize(child, nodes);
		}
		break;
	}
	default:
		break;
	}
	// Save the sub-AST size in the dictionary
	nodes[snode] = size;
	// Return the calculated size
	return size;
}

/*
	Converting a Triton AST to a LLVM-IR block.
*/

Value* Translator::GetDecimal(IntegerNode& node, uint64_t bvsz) {
#ifdef VERBOSE_OUTPUT
	cout << "GetDecimal: { bvsz = " << bvsz << ", value = " << hex << node.getInteger().convert_to<uint64_t>() << " }" << endl;
#endif
	return ConstantInt::get(context, APInt(bvsz, node.getInteger().convert_to<uint64_t>()));
}

llvm::Value* Translator::LiftNodesDFS(const SharedAbstractNode& snode,
	map<SharedAbstractNode, Value *>& nodes, IRBuilder<> &ir) {
	// DEBUG: show input value
#ifdef VERBOSE_OUTPUT
	cout << "Input: " << snode << endl;
#endif
	// Check if it's a known node
	if (nodes.find(snode) != nodes.end()) {
		return nodes[snode];
	}
	// We need to create a new Value
	Value* value = nullptr;
	// Handle the AST type
	switch (snode->getType()) {
		// Handle terminal nodes
	case ast_e::BV_NODE: {
		value = ConstantInt::get(context, APInt(snode->getBitvectorSize(), snode->evaluate().convert_to<uint64_t>()));
		break;
	}
	case ast_e::VARIABLE_NODE: {
		// Get the VariableNode
		VariableNode* node = (VariableNode*)snode.get();
		// Get the variable name
		string varName = node->getSymbolicVariable()->getName();
#ifdef VERBOSE_OUTPUT
		cout << "Variable name: " << varName << endl;
#endif
		// Determine if it's a known variable
		if (varsValue.find(varName) != varsValue.end()) {
			// It's known, fetch the old Value
			value = varsValue[varName];
		} else {
			// First we create the global variable
			value = new GlobalVariable(*module, IntegerType::get(context, snode->getBitvectorSize()),
				false, GlobalValue::CommonLinkage, nullptr, varName);
			// Then we load its value
			value = ir.CreateLoad(value);
			// At last we save the reference to the variable AstNode
			varsValue[varName] = value;
			vars[varName] = snode;
		}
		break;
	}
		// Handle non-terminal nodes
	case ast_e::BVADD_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateAdd(lhs, rhs);
		break;
	}
	case ast_e::BVSUB_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateSub(lhs, rhs);
		break;
	}
	case ast_e::BVXOR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateXor(lhs, rhs);
		break;
	}
	case ast_e::LAND_NODE:
	case ast_e::BVAND_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateAnd(lhs, rhs);
		break;
	}
	case ast_e::LOR_NODE:
	case ast_e::BVOR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateOr(lhs, rhs);
		break;
	}
	case ast_e::BVASHR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateAShr(lhs, rhs);
		break;
	}
	case ast_e::BVLSHR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateLShr(lhs, rhs);
		break;
	}
	case ast_e::BVSHL_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateShl(lhs, rhs);
		break;
	}
	case ast_e::BVMUL_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateMul(lhs, rhs);
		break;
	}
	case ast_e::BVNEG_NODE: {
		// Get the child and handle it first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		// Lift the current node
		value = ir.CreateNeg(lhs);
		break;
	}
	case ast_e::LNOT_NODE:
	case ast_e::BVNOT_NODE: {
		// Get the child and handle it first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		// Lift the current node
		value = ir.CreateNot(lhs);
		break;
	}
	case ast_e::BVROL_NODE: {
		// Get the bitvector to be rotated and the rotation
		auto bv = snode->getChildren()[0];
		auto rot = snode->getChildren()[1];
		// Get the rotation decimal node
		auto rotd = (IntegerNode*)rot.get();
		// Get the child and handle it first
		auto rotv = GetDecimal(*rotd, bv->getBitvectorSize());
		auto bvv = LiftNodesDFS(bv, nodes, ir);
		// Size of the bitvector being rotated
		uint64_t sz = bv->getBitvectorSize();
		uint64_t rd = rotd->getInteger().convert_to<uint64_t>();
		// Rotation value
		auto rrot = ConstantInt::get(context, APInt(sz, sz - rd));
		// Emulate the right rotation
		auto shl = ir.CreateShl(bvv, rotv);
		auto shr = ir.CreateLShr(bvv, rrot);
		value = ir.CreateXor(shl, shr);
		break;
	}
	case ast_e::BVROR_NODE: {
		// Get the bitvector to be rotated and the rotation
		auto bv = snode->getChildren()[0];
		auto rot = snode->getChildren()[1];
		// Get the rotation decimal node
		auto rotd = (IntegerNode*)rot.get();
		// Get the child and handle it first
		auto rotv = GetDecimal(*rotd, bv->getBitvectorSize());
		auto bvv = LiftNodesDFS(bv, nodes, ir);
		// Size of the bitvector being rotated
		uint64_t sz = bv->getBitvectorSize();
		uint64_t rd = rotd->getInteger().convert_to<uint64_t>();
		// Rotation value
		auto rrot = ConstantInt::get(context, APInt(sz, sz - rd));
		// Emulate the right rotation
		auto shl = ir.CreateShl(bvv, rrot);
		auto shr = ir.CreateLShr(bvv, rotv);
		value = ir.CreateXor(shl, shr);
		break;
	}
	case ast_e::ZX_NODE: {
		// Get the child
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateZExt(rhs, IntegerType::get(context, snode->getBitvectorSize()));
		break;
	}
	case ast_e::SX_NODE: {
		// Get the child
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateSExt(rhs, IntegerType::get(context, snode->getBitvectorSize()));
		break;
	}
	case ast_e::EXTRACT_NODE: {
		// Get the children
		auto c0 = snode->getChildren()[0];
		auto c1 = snode->getChildren()[1];
		auto c2 = snode->getChildren()[2];
		// Get the decimal values
		auto c0d = (IntegerNode*)c0.get();
		auto c1d = (IntegerNode*)c1.get();
		// Determine the extraction size
		auto sz = 1 + c0d->getInteger().convert_to<uint64_t>() - c1d->getInteger().convert_to<uint64_t>();
		// Get the high and low indexes
		auto lo = GetDecimal(*c1d, c2->getBitvectorSize());
		// Get the value to extract from
		auto bv = LiftNodesDFS(c2, nodes, ir);
		// Proceed with the extraction
		value = ir.CreateLShr(bv, lo);
		value = ir.CreateTrunc(value, IntegerType::get(context, sz));
		break;
	}
	case ast_e::CONCAT_NODE: {
		// Get the final concatenation size
		auto sz = snode->getBitvectorSize();
		// Get the children
		auto children = snode->getChildren();
		// Get the last node and extend it to the full size
		value = LiftNodesDFS(children.back(), nodes, ir);
		value = ir.CreateZExt(value, IntegerType::get(context, sz));
		// Determine the initial shift value
		uint64_t shift = children.back()->getBitvectorSize();
		// Remove the last child from the children
		children.pop_back();
		// Concatenate all the other children
		for (uint64_t i = children.size(); i-- > 0;) {
			// Get the current child
			auto child = children[i];
			// Get the current child instruction
			auto curr = LiftNodesDFS(child, nodes, ir);
			// Zero extend the value to the full size
			curr = ir.CreateZExt(curr, IntegerType::get(context, sz));
			// Shift it left
			curr = ir.CreateShl(curr, shift);
			// Concatenate it
			value = ir.CreateOr(value, curr);
			// Get the current shift value
			shift += child->getBitvectorSize();
		}
		break;
	}
	case ast_e::BVSDIV_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateSDiv(lhs, rhs);
		break;
	}
	case ast_e::BVUDIV_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateUDiv(lhs, rhs);
		break;
	}
	case ast_e::BVSMOD_NODE:
		// Proper emulation of SMOD would be necessary
		// https://llvm.org/docs/LangRef.html#srem-instruction
	case ast_e::BVSREM_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateSRem(lhs, rhs);
		break;
	}
	case ast_e::BVUREM_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateURem(lhs, rhs);
		break;
	}
	case ast_e::ITE_NODE: {
		// Get the 'if' node
		auto _if = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		// Get the 'then' node
		auto _then = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Get the 'else' node
		auto _else = LiftNodesDFS(snode->getChildren()[2], nodes, ir);
		// Lift the 'ite' ast to a 'select'
		value = ir.CreateSelect(_if, _then, _else);
		break;
	}
	case ast_e::EQUAL_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'equal' ast to a 'icmp eq'
		value = ir.CreateICmpEQ(e0, e1);
		break;
	}
	case ast_e::DISTINCT_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'distinct' ast to a 'icmp ne'
		value = ir.CreateICmpNE(e0, e1);
		break;
	}
	case ast_e::BVSGE_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'sge' ast to a 'icmp sge'
		value = ir.CreateICmpSGE(e0, e1);
		break;
	}
	case ast_e::BVSGT_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'sgt' ast to a 'icmp sgt'
		value = ir.CreateICmpSGT(e0, e1);
		break;
	}
	case ast_e::BVSLE_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'sle' ast to a 'icmp sle'
		value = ir.CreateICmpSLE(e0, e1);
		break;
	}
	case ast_e::BVSLT_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'slt' ast to a 'icmp slt'
		value = ir.CreateICmpSLT(e0, e1);
		break;
	}
	case ast_e::BVUGE_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'uge' ast to a 'icmp uge'
		value = ir.CreateICmpUGE(e0, e1);
		break;
	}
	case ast_e::BVUGT_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'ugt' ast to a 'icmp ugt'
		value = ir.CreateICmpUGT(e0, e1);
		break;
	}
	case ast_e::BVULE_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'ule' ast to a 'icmp ule'
		value = ir.CreateICmpULE(e0, e1);
		break;
	}
	case ast_e::BVULT_NODE: {
		// Get the 2 expressions
		auto e0 = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto e1 = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the 'ult' ast to a 'icmp ult'
		value = ir.CreateICmpULT(e0, e1);
		break;
	}
	case ast_e::BVNAND_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateAnd(lhs, rhs);
		value = ir.CreateNot(value);
		break;
	}
	case ast_e::BVNOR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateOr(lhs, rhs);
		value = ir.CreateNot(value);
		break;
	}
	case ast_e::BVXNOR_NODE: {
		// Get the children and handle them first
		auto lhs = LiftNodesDFS(snode->getChildren()[0], nodes, ir);
		auto rhs = LiftNodesDFS(snode->getChildren()[1], nodes, ir);
		// Lift the current node
		value = ir.CreateXor(lhs, rhs);
		value = ir.CreateNot(value);
		break;
	}
	default: {
		cout << "Unsupported node type: " << snode << endl;
		/*
			List of unsupported nodes:

				AST_NODE.ASSERT
				AST_NODE.COMPOUND
				AST_NODE.DECLARE
				AST_NODE.INVALID
				AST_NODE.IFF
				AST_NODE.LET
				AST_NODE.REFERENCE
				AST_NODE.STRING
		*/
		break;
	}
	}
	// Save the lifted node in the dictionary
	nodes[snode] = value;
	// DEBUG: dump the value
#ifdef VERBOSE_OUTPUT
	cout << "Dumping the value: ";
	value->dump();
#endif
	// Return the lifted instruction
	return value;
}

void Translator::OptimizeModule() {
	PassManager *pm = new PassManager();
	int optLevel = 3;
	int sizeLevel = 2;
	PassManagerBuilder builder;
	builder.OptLevel = optLevel;
	builder.SizeLevel = sizeLevel;
	builder.populateModulePassManager(*pm);
	pm->run(*module);
}

shared_ptr<Module> Translator::TritonAstToLLVMIR(const SharedAbstractNode& node) {
	// Allocate a new module (the old one is deallocated only if not referenced anymore)
	module = make_shared<Module>("TritonAstModule", context);
	// Create the function type (consistent with the top node type)
	FunctionType *TritonAstType = FunctionType::get(IntegerType::get(context,
		node->getBitvectorSize()), false);
	// Create the function (which will contain the basic block)
	Function *TritonAstFunction = Function::Create(
		TritonAstType, llvm::Function::CommonLinkage, "TritonAstFunction", module.get());
	// Create the only basic block (which will contain the lifted instructions)
	BasicBlock* TritonAstBlock = BasicBlock::Create(context, "TritonAstEntry",
		TritonAstFunction);
	map<SharedAbstractNode, Value *> nodes;
	// Clear the old variable Value(s)
	varsValue.clear();
	// Initialize the IRBuilder to lift the nodes
	IRBuilder<> ir(TritonAstBlock);
	// Traverse the AST in a DFS way (and lift the ast nodes)
	auto value = LiftNodesDFS(node, nodes, ir);
	// Add the return statement
	ir.CreateRet(value);
#ifdef DEBUG_OUTPUT
	// DEBUG: show the original ast
	cout << "\nOriginal Triton AST: " << node << endl;
	// DEBUG: dump the module
	cout << "\tLifted Triton AST" << endl;
	module->dump();
#endif
	// Optimize the module
	OptimizeModule();
	// DEBUG: dump the optimized module
#ifdef DEBUG_OUTPUT
	cout << "\nOptimized Lifted Triton AST" << endl;
	module->dump();
#endif
	// Return the generated module
	return module;
}

/*
	Converting a LLVM-IR basic block to a Triton AST.
*/

SharedAbstractNode Translator::LiftInstructionsDFS(Value* value, map<Value *, SharedAbstractNode>& values) {
	// DEBUG: show input value
#ifdef VERBOSE_OUTPUT
	cout << "Input value: ";
	value->dump();
#endif
	// Check if we already lifted this value
	if (values.find(value) != values.end()) {
		return values[value];
	}
	// Get a reference to the ast context
	AstContext& ctx = api.getAstContext();
	// We need to create a new SharedAbstractNode
	SharedAbstractNode node = nullptr;
	// Check if we are dealing with a constant
	if (ConstantInt* ci = dyn_cast<ConstantInt>(value)) {
		const APInt& val = ci->getValue();
		return ctx.bv(val.getLimitedValue(), val.getBitWidth());
	}
	else if (Instruction* inst = dyn_cast<Instruction>(value)) {
		// Lift the instruction into an ast node
		switch (inst->getOpcode()) {
			// Handle terminal instructions
		case Instruction::Load: {
			Value* gv = inst->getOperand(0);
			auto var = api.getSymbolicVariableFromName(gv->getName().str());
			node = vars[var->getName()];
			break;
		}
			// Handle non-terminal instructions
		case Instruction::Ret: {
			Value* rv = inst->getOperand(0);
			node = LiftInstructionsDFS(rv, values);
			break;
		}
		case Instruction::Add: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvadd(n0, n1);
			break;
		}
		case Instruction::Sub: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvsub(n0, n1);
			break;
		}
		case Instruction::Xor: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvxor(n0, n1);
			break;
		}
		case Instruction::Or: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvor(n0, n1);
			break;
		}
		case Instruction::And: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvand(n0, n1);
			break;
		}
		case Instruction::Mul: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvmul(n0, n1);
			break;
		}
		case Instruction::Shl: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvshl(n0, n1);
			break;
		}
		case Instruction::AShr: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvashr(n0, n1);
			break;
		}
		case Instruction::LShr: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Create the node
			node = ctx.bvlshr(n0, n1);
			break;
		}
		case Instruction::ZExt: {
			// Get the destination type size
			auto dty = inst->getType();
			auto dsz = dty->getIntegerBitWidth();
			// Fetch the operand
			auto o0 = inst->getOperand(0);
			// Lift the operand
			auto n0 = LiftInstructionsDFS(o0, values);
			// Get the source type size
			auto sty = o0->getType();
			auto ssz = sty->getIntegerBitWidth();
			// Create the node
			node = ctx.zx(dsz - ssz, n0);
			break;
		}
		case Instruction::SExt: {
			// Get the destination type size
			auto dty = inst->getType();
			auto dsz = dty->getIntegerBitWidth();
			// Fetch the operand
			auto o0 = inst->getOperand(0);
			// Lift the operand
			auto n0 = LiftInstructionsDFS(o0, values);
			// Get the source type size
			auto sty = o0->getType();
			auto ssz = sty->getIntegerBitWidth();
			// Create the node
			node = ctx.sx(dsz - ssz, n0);
			break;
		}
		case Instruction::Trunc: {
			// Get the destination type size
			auto dty = inst->getType();
			auto dsz = dty->getIntegerBitWidth();
			// Fetch the operand
			auto o0 = inst->getOperand(0);
			// Lift the operand
			auto n0 = LiftInstructionsDFS(o0, values);
			// Create the node
			node = ctx.extract(dsz - 1, 0, n0);
			break;
		}
		case Instruction::ICmp: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			// Detect the comparison type
			if (ICmpInst* icmp = dyn_cast<ICmpInst>(inst)) {
				switch (icmp->getPredicate()) {
					// Equality comparisons
				case ICmpInst::ICMP_EQ: {
					node = ctx.equal(n0, n1);
					break;
				}
				case ICmpInst::ICMP_NE: {
					node = ctx.distinct(n0, n1);
					break;
				}
					// Unsigned comparisons
				case::ICmpInst::ICMP_UGE: {
					node = ctx.bvuge(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_UGT: {
					node = ctx.bvugt(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_ULE: {
					node = ctx.bvule(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_ULT: {
					node = ctx.bvult(n0, n1);
					break;
				}
					// Signed comparisons
				case::ICmpInst::ICMP_SGE: {
					node = ctx.bvsge(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_SGT: {
					node = ctx.bvsgt(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_SLE: {
					node = ctx.bvsle(n0, n1);
					break;
				}
				case::ICmpInst::ICMP_SLT: {
					node = ctx.bvslt(n0, n1);
					break;
				}
				default:
					cout << "Unsupported icmp instruction: ";
					icmp->dump();
					break;
				}
			}
			// Add the ITE node
			node = FixICmpBehavior(node);
			break;
		}
		case Instruction::Select: {
			// Fetch the operands
			auto o0 = inst->getOperand(0);
			auto o1 = inst->getOperand(1);
			auto o2 = inst->getOperand(2);
			// Lift the operands
			auto n0 = LiftInstructionsDFS(o0, values);
			auto n1 = LiftInstructionsDFS(o1, values);
			auto n2 = LiftInstructionsDFS(o2, values);
			// Undo the ICmp fix if it was applied to a logical node
			n0 = UndoICmpBehavior(n0);
			// If the condition is not logical, we must fix it
			n0 = ConverToLogical(n0);
			// DEBUG
			if (n0->isLogical() == false) {
				cout << "n0 isn't logical: " << n0 << endl;
			}
			if (n1->getBitvectorSize() != n2->getBitvectorSize()) {
				cout << "size(n1) != size(n2)" << endl;
				cout << "n1: " << n1 << endl;
				cout << "n2: " << n2 << endl;
			}
			// Create the node
			node = ctx.ite(n0, n1, n2);
			break;
		}
		default: {
			cout << "Unsupported instruction type: ";
			value->dump();
			break;
		}
		}
	}
	else {
		cout << "Unexpected Value: ";
		value->dump();
	}
	// DEBUG: dump the value
#ifdef VERBOSE_OUTPUT
	cout << "Dumping the value: " << node << endl;
#endif
	// Save the lifted value
	values[value] = node;
	// Return the lifted node
	return node;
}

SharedAbstractNode Translator::FixICmpBehavior(SharedAbstractNode node) {
	AstContext& ctx = api.getAstContext();
	// Handling of the rest of the comparisons
	switch (node->getType()) {
	case ast_e::DISTINCT_NODE:
	case ast_e::EQUAL_NODE:
	case ast_e::BVUGT_NODE:
	case ast_e::BVUGE_NODE:
	case ast_e::BVSGT_NODE:
	case ast_e::BVSGE_NODE:
	case ast_e::BVULT_NODE:
	case ast_e::BVULE_NODE:
	case ast_e::BVSLT_NODE:
	case ast_e::BVSLE_NODE: {
		node = ctx.ite(node, ctx.bvtrue(), ctx.bvfalse());
		break;
	}
	default:
		break;
	}
	return node;
}

SharedAbstractNode Translator::UndoICmpBehavior(SharedAbstractNode node) {
	AstContext& ctx = api.getAstContext();
	if (node->getType() == ast_e::ITE_NODE) {
		auto c1 = node->getChildren()[0];
		auto c2 = node->getChildren()[1];
		auto c3 = node->getChildren()[2];
		if (c1->isLogical() && c2->equalTo(ctx.bvtrue()) && c3->equalTo(ctx.bvfalse())) {
			node = node->getChildren()[0];
		}
	}
	return node;
}

SharedAbstractNode Translator::ConverToLogical(SharedAbstractNode node) {
	AstContext& ctx = api.getAstContext();
	if (!node->isLogical() && node->getBitvectorSize() == 1) {
		node = ctx.equal(node, ctx.bvtrue());
	}
	return node;
}

SharedAbstractNode Translator::LLVMIRToTritonAst(const shared_ptr<Module> module) {
	// Get our lovely function out of the module
	Function* TritonAstFunction = module->getFunction("TritonAstFunction");
	if (TritonAstFunction == nullptr) {
		cout << "Sorry but the provided llvm::Module doesn't contain a"
			"function named 'TritonAstFunction'" << endl;
		return nullptr;
	}
	// Get our lovely basic block out of the function
	BasicBlock& TritonAstBlock = TritonAstFunction->getEntryBlock();
	// Get the return value of the function
	Instruction* retval = TritonAstBlock.getTerminator();
	// Explore the function in a bottom-up fashion
	map<Value *, SharedAbstractNode> values;
	auto ast = LiftInstructionsDFS(retval, values);
	// Fix the ICmp behavior if it's returned
	ast = FixICmpBehavior(ast);
	// DEBUG: dump the lifted ast
#ifdef DEBUG_OUTPUT
	cout << "\nRecovered Triton AST: " << ast << endl;
#endif
	// Return the generated AST
	return ast;
}