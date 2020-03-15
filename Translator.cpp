#include <Translator.hpp>

/*
  Default contructor:
  - we need the LLVM context to access the cached LLVM-IR Modules
  - we need the Triton context to access the AstContext and the symbolic variables
*/

Translator::Translator(LLVMContext& Context, API& Api) : Context(Context), Api(Api) {}

/*
  Determine the Triton AST size.
*/

uint64_t Translator::DetermineASTSize(SharedAbstractNode Node, map<SharedAbstractNode, uint64_t>& Nodes) {
  // Check if it's a known node -> size
  if (Nodes.find(Node) != Nodes.end()) {
    return Nodes[Node];
  }
  // We always consider the single AST as 1
  uint64_t Size = 1;
  // Handle the AST type
  switch (Node->getType()) {
    case ast_e::SX_NODE:
    case ast_e::ZX_NODE:
    case ast_e::LNOT_NODE:
    case ast_e::BVNOT_NODE:
    case ast_e::BVNEG_NODE:
    case ast_e::EXTRACT_NODE: {
      uint64_t Size1 = DetermineASTSize(Node->getChildren()[0], Nodes);
      Size += Size1;
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
      uint64_t Size1 = DetermineASTSize(Node->getChildren()[0], Nodes);
      uint64_t Size2 = DetermineASTSize(Node->getChildren()[1], Nodes);
      Size += (Size1 + Size2);
      break;
    }
    case ast_e::ITE_NODE: {
      uint64_t Size1 = DetermineASTSize(Node->getChildren()[0], Nodes);
      uint64_t Size2 = DetermineASTSize(Node->getChildren()[1], Nodes);
      uint64_t Size3 = DetermineASTSize(Node->getChildren()[2], Nodes);
      Size += (Size1 + Size2 + Size3);
      break;
    }
    case ast_e::CONCAT_NODE: {
      for (auto Child : Node->getChildren()) {
        Size += DetermineASTSize(Child, Nodes);
      }
      break;
    }
    default:
      break;
  }
  // Save the sub-AST size in the dictionary
  Nodes[Node] = Size;
  // Return the calculated size
  return Size;
}

/*
  Converting a Triton AST to a LLVM-IR block.
*/

ConstantInt* Translator::GetDecimal(IntegerNode& Node, uint64_t BitVectorSize) {
  // Construct a new integer from a string (so we can support arbitrarily long bitvectors)
  stringstream ss;
  ss << dec << Node.getInteger();
  auto NodeValue = APInt(BitVectorSize, ss.str(), 10);
  #ifdef VERBOSE_OUTPUT
    cout << "GetDecimal: { bvsz = " << BitVectorSize << ", value = 0x" << hex << Node.getInteger() << " }" << endl;
  #endif
  return ConstantInt::get(this->Context, NodeValue);
}

/*
  Worklist-based translation of a Triton AST to an LLVM-IR Module.
*/

Value* Translator::LiftNodesWBS(const SharedAbstractNode& TopNode, shared_ptr<IRBuilder<>> IR, map<ExpKey, shared_ptr<llvm::Module>>& Cache) {
  // Use a dictionary for the known references
  map<triton::usize, triton::engines::symbolic::SharedSymbolicExpression> References;
  // Use a dictionary for the known AST nodes
  map<SharedAbstractNode, Value*> Nodes;
  // At this point we can translate the AST
  auto Curr = make_shared<AstNode>(TopNode, nullptr);
  while (Curr) {
    // Print the node
    #ifdef VERBOSE_OUTPUT
    cout << "Handling: " << Curr->Node << endl;
    Curr->dump();
    #endif
    // Go to the parent if this node is already lifted
    if (Nodes.find(Curr->Node) != Nodes.end()) {
      #ifdef VERBOSE_OUTPUT
      cout << "Translating: KNOWN_NODE" << endl;
      #endif
      // Get the parent
      Curr = Curr->Parent;
      // Restart the loop
      continue;
    }
    // Craft a constant if possible and continue with the parent
    if (!Curr->Node->isSymbolized() && Curr->Node->getType() != ast_e::INTEGER_NODE) {
      #ifdef VERBOSE_OUTPUT
      cout << "Translating: CONSTANT_NODE (size = " << Curr->Node->getBitvectorSize() << ")" << endl;
      #endif
      // Construct a new integer from a string (so we can support arbitrarily long bitvectors)
      stringstream ss;
      ss << dec << Curr->Node->evaluate();
      auto NodeValue = APInt(Curr->Node->getBitvectorSize(), ss.str(), 10);
      // Dump the constant value
      #ifdef VERBOSE_OUTPUT
      cout << "CONSTANT_NODE: " << ss.str() << endl;
      #endif
      // Get the constant
      Nodes[Curr->Node] = ConstantInt::get(this->Context, NodeValue);
      // Get the parent
      Curr = Curr->Parent;
      // Restart the loop
      continue;
    }
    // Fetch the children of the node
    auto Children = Curr->Node->getChildren();
    // Handle the current node
    if (Curr->Index < Children.size()) {
      // Create the child node
      Curr = make_shared<AstNode>(Children[Curr->Index++], Curr);
    } else {
      #ifdef VERBOSE_OUTPUT
      cout << "Translating: ";
      #endif
      // Get the current node
      auto CNode = Curr->Node;
      // Resolved reference flag
      bool UnresolvedReference = false;
      // Translate the node
      switch (CNode->getType()) {
        // Handle the reference node
        case ast_e::REFERENCE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "REFERENCE_NODE: " << endl;
          #endif
          // Fetch the current "ReferenceNode"
          auto* ReferenceAst = static_cast<ReferenceNode*>(CNode.get());
          // Fetch the referenced expression
          auto ReferencedExpression = ReferenceAst->getSymbolicExpression();
          // Fetch the referenced AST
          auto ReferencedAst = ReferencedExpression->getAst();
          // Check if the referenced expression is in the cache
          if (Cache.find(ReferencedExpression->getId()) != Cache.end()) {
            #ifdef VERBOSE_OUTPUT
            cout << "[!] Found a cached reference, continuing." << endl;
            #endif
            // Clone the Module (we don't want to destroy the original)
            unique_ptr<llvm::Module> Cloned = llvm::CloneModule(*Cache[ReferencedExpression->getId()]);
            // Generate a proper function name
            string FunName = "ref" + to_string(ReferencedExpression->getId());
            // Fetch and rename the referenced function
            auto* RefFun = Cloned->getFunction("TritonAstFunction");
            RefFun->setName(FunName);
            // Link the modules together
            if (Linker::linkModules(*this->Module, std::move(Cloned), llvm::Linker::Flags::OverrideFromSrc)) {
              cout << "Error while linking the modules" << endl;
            }
            // Fetch all the declared global variables
            for (auto& GVar : this->Module->getGlobalList()) {
              // Detect the symbolic variables
              StringRef VarName = GVar.getName();
              if (VarName.startswith("SymVar")) {
                this->VarsValue[VarName.str()] = &GVar;
              }
            }
            // Get the linked copy of the function
            RefFun = this->Module->getFunction(FunName);
            // Call the referenced function
            Nodes[CNode] = IR->CreateCall(RefFun);
          } else if (References.find(ReferencedExpression->getId()) != References.end()) {
            #ifdef VERBOSE_OUTPUT
            cout << "[!] Found a resolved reference, caching it and continuing." << endl;
            cout << "----------- Referenced Expression -----------" << endl;
            cout << ReferencedExpression << endl;
            cout << "---------------------------------------------" << endl;
            cout << "----------- Referenced AST -----------" << endl;
            cout << ReferencedAst << endl;
            cout << "--------------------------------------" << endl;
            #endif
            // Fetch the main function (in the original module)
            auto* MF = this->Module->getFunction("TritonAstFunction");
            // Fetch the entry block
            auto& EB = MF->getEntryBlock();
            // Add a return instruction at the end of the entry block
            auto* RI = ReturnInst::Create(this->Context, Nodes[ReferencedAst], &EB);
            // Optimize the cloned module
            this->OptimizeModule(this->Module.get());
            // Debug print the optimized cloned module
            #ifdef VERBOSE_OUTPUT
            cout << "----------- Referenced Module -----------" << endl;
            this->Module->dump();
            cout << "-----------------------------------------" << endl;
            #endif
            // Cache the optimized cloned module
            Cache[ReferencedExpression->getId()] = this->Module;
            // Mark the reference as fully resolved
            References.erase(ReferencedExpression->getId());
            // Restore the previous exploration state
            if (Curr->Module) {
              this->VarsValue = Curr->VarsValue;
              this->Module = Curr->Module;
              this->Vars = Curr->Vars;
              Nodes = Curr->Nodes;
              IR = Curr->IR;
            }
            // Notify we found an unresolved reference
            UnresolvedReference = true;
          } else {
            #ifdef VERBOSE_OUTPUT
            cout << "[!] Found an unresolved reference, adding it to the references to be solved." << endl;
            cout << "----------- Referenced AST -----------" << endl;
            cout << ReferencedAst << endl;
            cout << "--------------------------------------" << endl;
            #endif
            // Mark the reference as known
            References[ReferencedExpression->getId()] = ReferencedExpression;
            // Backup the current exploration state
            Curr->VarsValue = this->VarsValue;
            Curr->Module = this->Module;
            Curr->Vars = this->Vars;
            Curr->Nodes = Nodes;
            Curr->IR = IR;
            // Craft a new child node
            Curr = make_shared<AstNode>(ReferencedAst, Curr);
            // Save the expression reference when known
            Curr->Expression = ReferencedExpression;
            // Reset the exploration state
            this->VarsValue.clear();
            this->Vars.clear();
            Nodes.clear();
            // Allocate a new module with the proper signature
            this->Module = make_shared<llvm::Module>("NewTritonAstModule", this->Context);
            // Create the function type (consistent with the top node type)
            auto* TritonAstType = FunctionType::get(IntegerType::get(this->Context, ReferencedAst->getBitvectorSize()), false);
            // Create the function (which will contain the basic block)
            auto* TritonAstFunction = Function::Create(TritonAstType, llvm::Function::CommonLinkage, "TritonAstFunction", this->Module.get());
            // Mark the function as always inlineable
            TritonAstFunction->addFnAttr(Attribute::AlwaysInline);
            // Create the only basic block (which will contain the lifted instructions)
            auto* TritonAstBlock = BasicBlock::Create(this->Context, "TritonAstEntry", TritonAstFunction);
            // Initialize the IRBuilder to lift the nodes
            IR = make_shared<IRBuilder<>>(TritonAstBlock);
            // Notify we found an unresolved reference
            UnresolvedReference = true;
          }
        } break;
        // Handle the terminal nodes
        case ast_e::BV_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BV_NODE" << endl;
          #endif
          // Construct a new integer from a string (so we can support arbitrarily long bitvectors)
          stringstream ss;
          ss << dec << CNode->evaluate();
          auto NodeValue = APInt(CNode->getBitvectorSize(), ss.str(), 10);
          Nodes[CNode] = ConstantInt::get(this->Context, NodeValue);
        } break;
        case ast_e::INTEGER_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "INTEGER_NODE" << endl;
          #endif
          Nodes[CNode] = nullptr;
          // Ignoring this node
        } break;
        case ast_e::VARIABLE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "VARIABLE_NODE" << endl;
          #endif
          // Get the VariableNode
          auto* Node = (VariableNode*)(CNode.get());
          // Get the variable name
          string VarName = Node->getSymbolicVariable()->getName();
          // Determine if it's a known variable
          if (this->VarsValue.find(VarName) != this->VarsValue.end()) {
            // It's known, fetch the old Value
            Nodes[CNode] = this->VarsValue[VarName];
            // If it's a GlobalVariable, create a load
            if (auto* gv = dyn_cast<GlobalVariable>(Nodes[CNode])) {
              Nodes[CNode] = IR->CreateLoad(Nodes[CNode]);
            }
          } else {
            // First we create the global variable
            Nodes[CNode] = new GlobalVariable(*this->Module, IntegerType::get(this->Context, CNode->getBitvectorSize()), false, GlobalValue::CommonLinkage, nullptr, VarName);
            // Then we load its value
            Nodes[CNode] = IR->CreateLoad(Nodes[CNode]);
            // At last we save the reference to the variable AstNode
            this->VarsValue[VarName] = Nodes[CNode];
            this->Vars[VarName] = CNode;
          }
        } break;
        // Handle non-terminal nodes
        case ast_e::BVADD_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVADD_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateAdd(LHS, RHS);
        } break;
        case ast_e::BVSUB_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSUB_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateSub(LHS, RHS);
        } break;
        case ast_e::BVXOR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVXOR_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateXor(LHS, RHS);
        } break;
        case ast_e::LAND_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "LAND_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateAnd(LHS, RHS);
          // Convert it to be logical
          auto TrueNode = ConstantInt::get(this->Context, APInt(1, 1));
          Nodes[CNode] = IR->CreateICmpEQ(Nodes[CNode], TrueNode);
        } break;
        case ast_e::BVAND_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVAND_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateAnd(LHS, RHS);
        } break;
        case ast_e::LOR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "LOR_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateOr(LHS, RHS);
          // Convert it to be logical
          auto TrueNode = ConstantInt::get(this->Context, APInt(1, 1));
          Nodes[CNode] = IR->CreateICmpEQ(Nodes[CNode], TrueNode);
        } break;
        case ast_e::BVOR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVOR_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateOr(LHS, RHS);
        } break;
        case ast_e::BVASHR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVASHR_NODE" << endl;
          #endif
          // Fetch the children
          auto c0 = Children[0];
          auto c1 = Children[1];
          // Get the children and handle them first
          auto LHS = Nodes[c0];
          auto RHS = Nodes[c1];
          // BEWARE: we should take into account the sign bit of the first operand
          // https://llvm.org/docs/LangRef.html#ashr-instruction
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            LHS = IR->CreateSExt(LHS, Type::getInt64Ty(this->Context));
            RHS = IR->CreateSExt(RHS, Type::getInt64Ty(this->Context));
          }
          // Lift the current node
          Nodes[CNode] = IR->CreateAShr(LHS, RHS);
          // Truncate the result
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            Nodes[CNode] = IR->CreateTrunc(Nodes[CNode], Type::getIntNTy(this->Context, CNode->getBitvectorSize()));
          }
        } break;
        case ast_e::BVLSHR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVLSHR_NODE" << endl;
          #endif
          // Fetch the children
          auto c0 = Children[0];
          auto c1 = Children[1];
          // Get the known children
          auto LHS = Nodes[c0];
          auto RHS = Nodes[c1];
          // https://llvm.org/docs/LangRef.html#shl-instruction
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            LHS = IR->CreateZExt(LHS, Type::getInt64Ty(this->Context));
            RHS = IR->CreateZExt(RHS, Type::getInt64Ty(this->Context));
          }
          // Lift the current node
          Nodes[CNode] = IR->CreateLShr(LHS, RHS);
          // Trunc the final result to the expected size
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            Nodes[CNode] = IR->CreateTrunc(Nodes[CNode], Type::getIntNTy(this->Context, CNode->getBitvectorSize()));
          }
        } break;
        case ast_e::BVSHL_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSHL_NODE" << endl;
          #endif
          // Fetch the children
          auto c0 = Children[0];
          auto c1 = Children[1];
          // Get the known children
          auto LHS = Nodes[c0];
          auto RHS = Nodes[c1];
          // https://llvm.org/docs/LangRef.html#shl-instruction
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            LHS = IR->CreateZExt(LHS, Type::getInt64Ty(this->Context));
            RHS = IR->CreateZExt(RHS, Type::getInt64Ty(this->Context));
          }
          // Lift the current node
          Nodes[CNode] = IR->CreateShl(LHS, RHS);
          // Trunc the final result to the expected size
          if (c0->getBitvectorSize() <= 16 && c1->getBitvectorSize() <= 16) {
            Nodes[CNode] = IR->CreateTrunc(Nodes[CNode], Type::getIntNTy(this->Context, CNode->getBitvectorSize()));
          }
        } break;
        case ast_e::BVMUL_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVMUL_NODE" << endl;
          #endif
          // Get the known children
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateMul(LHS, RHS);
        } break;
        case ast_e::BVNEG_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVNEG_NODE" << endl;
          #endif
          // Get the known child
          auto LHS = Nodes[Children[0]];
          // Lift the current node
          Nodes[CNode] = IR->CreateNeg(LHS);
        } break;
        case ast_e::LNOT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "LNOT_NODE" << endl;
          #endif
          // Get the known child
          auto LHS = Nodes[Children[0]];
          // Lift the current node
          Nodes[CNode] = IR->CreateNot(LHS);
          // Convert it to be logical
          auto TrueNode = ConstantInt::get(this->Context, APInt(1, 1));
          Nodes[CNode] = IR->CreateICmpEQ(Nodes[CNode], TrueNode);
        } break;
        case ast_e::BVNOT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "LNOT_NODE|BVNOT_NODE" << endl;
          #endif
          // Get the known child
          auto LHS = Nodes[Children[0]];
          // Lift the current node
          Nodes[CNode] = IR->CreateNot(LHS);
        } break;
        case ast_e::BVROL_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVROL_NODE" << endl;
          #endif
          // Get the bitvector to be rotated and the rotation
          auto bv = Children[0];
          auto rot = Children[1];
          // Get the rotation decimal node
          auto rotd = (IntegerNode*)(rot.get());
          // Get the child and handle it first
          auto rotv = GetDecimal(*rotd, bv->getBitvectorSize());
          auto bvv = Nodes[bv];
          // If the rotation value is 0, return the child
          if (rotv->getLimitedValue() == 0) {
            Nodes[CNode] = bvv;
          } else {
            // Size of the bitvector being rotated
            uint64_t sz = bv->getBitvectorSize();
            uint64_t rd = rotd->getInteger().convert_to<uint64_t>();
            // Rotation value
            auto rrot = ConstantInt::get(this->Context, APInt(sz, sz - rd));
            // Emulate the right rotation
            auto shl = IR->CreateShl(bvv, rotv);
            auto shr = IR->CreateLShr(bvv, rrot);
            Nodes[CNode] = IR->CreateXor(shl, shr);
          }
        } break;
        case ast_e::BVROR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVROR_NODE" << endl;
          #endif
          // Get the bitvector to be rotated and the rotation
          auto bv = Children[0];
          auto rot = Children[1];
          // Get the rotation decimal node
          auto rotd = (IntegerNode*)(rot.get());
          // Get the child and handle it first
          auto rotv = GetDecimal(*rotd, bv->getBitvectorSize());
          auto bvv = Nodes[bv];
          // If the rotation value is 0, return the child
          if (rotv->getLimitedValue() == 0) {
            Nodes[CNode] = bvv;
          } else {
            // Size of the bitvector being rotated
            uint64_t sz = bv->getBitvectorSize();
            uint64_t rd = rotd->getInteger().convert_to<uint64_t>();
            // Rotation value
            auto rrot = ConstantInt::get(this->Context, APInt(sz, sz - rd));
            // Emulate the right rotation
            auto shl = IR->CreateShl(bvv, rrot);
            auto shr = IR->CreateLShr(bvv, rotv);
            Nodes[CNode] = IR->CreateXor(shl, shr);
          }
        } break;
        case ast_e::ZX_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "ZX_NODE" << endl;
          #endif
          // Get the child
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateZExt(RHS, IntegerType::get(this->Context, CNode->getBitvectorSize()));
        } break;
        case ast_e::SX_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "SX_NODE" << endl;
          #endif
          // Get the child
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateSExt(RHS, IntegerType::get(this->Context, CNode->getBitvectorSize()));
        } break;
        case ast_e::EXTRACT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "EXTRACT_NODE" << endl;
          #endif
          // Get the children
          auto c0 = Children[0];
          auto c1 = Children[1];
          auto c2 = Children[2];
          // Get the decimal values
          auto c0d = (IntegerNode*)(c0.get());
          auto c1d = (IntegerNode*)(c1.get());
          // Determine the extraction size
          auto sz = 1 + c0d->getInteger().convert_to<uint64_t>() - c1d->getInteger().convert_to<uint64_t>();
          // Get the high and low indexes
          auto lo = GetDecimal(*c1d, c2->getBitvectorSize());
          // Get the value to extract from
          auto bv = Nodes[c2];
          // Proceed with the extraction
          Nodes[CNode] = IR->CreateLShr(bv, lo);
          Nodes[CNode] = IR->CreateTrunc(Nodes[CNode], IntegerType::get(this->Context, sz));
        } break;
        case ast_e::CONCAT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "CONCAT_NODE" << endl;
          #endif
          // Get the final concatenation size
          auto sz = CNode->getBitvectorSize();
          // Get the children
          auto children = Children;
          // Get the last node and extend it to the full size
          Nodes[CNode] = Nodes[children.back()];
          Nodes[CNode] = IR->CreateZExt(Nodes[CNode], IntegerType::get(this->Context, sz));
          // Determine the initial shift value
          uint64_t shift = children.back()->getBitvectorSize();
          // Remove the last child from the children
          children.pop_back();
          // Concatenate all the other children
          for (uint64_t i = children.size(); i-- > 0;) {
            // Get the current child
            auto child = children[i];
            // Get the current child instruction
            auto curr = Nodes[child];
            // Zero extend the value to the full size
            curr = IR->CreateZExt(curr, IntegerType::get(this->Context, sz));
            // Shift it left
            curr = IR->CreateShl(curr, shift);
            // Concatenate it
            Nodes[CNode] = IR->CreateOr(Nodes[CNode], curr);
            // Get the current shift value
            shift += child->getBitvectorSize();
          }
        } break;
        case ast_e::BVSDIV_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSDIV_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateSDiv(LHS, RHS);
        } break;
        case ast_e::BVUDIV_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVUDIV_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateUDiv(LHS, RHS);
        } break;
        case ast_e::BVSMOD_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSMOD_NODE" << endl;
          #endif
          // BEWARE: Proper emulation of SMOD is necessary here
          // https://llvm.org/docs/LangRef.html#srem-instruction
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          auto srem = IR->CreateSRem(LHS, RHS);
          auto add = IR->CreateAdd(srem, RHS);
          Nodes[CNode] = IR->CreateSRem(add, RHS);
        } break;
        case ast_e::BVSREM_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSREM_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateSRem(LHS, RHS);
        } break;
        case ast_e::BVUREM_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVUREM_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateURem(LHS, RHS);
        } break;
        case ast_e::ITE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "ITE_NODE" << endl;
          #endif
          // Get the 'if' node
          auto _if = Nodes[Children[0]];
          // Get the 'then' node
          auto _then = Nodes[Children[1]];
          // Get the 'else' node
          auto _else = Nodes[Children[2]];
          // Lift the 'ite' ast to a 'select'
          Nodes[CNode] = IR->CreateSelect(_if, _then, _else);
        } break;
        case ast_e::EQUAL_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "EQUAL_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'equal' ast to a 'icmp eq'
          Nodes[CNode] = IR->CreateICmpEQ(e0, e1);
        } break;
        case ast_e::DISTINCT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "DISTINCT_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'equal' ast to a 'icmp eq'
          Nodes[CNode] = IR->CreateICmpNE(e0, e1);
        } break;
        case ast_e::BVSGE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSGE_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpSGE(e0, e1);
        } break;
        case ast_e::BVSGT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSGT_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpSGT(e0, e1);
        } break;
        case ast_e::BVSLE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSLE_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpSLE(e0, e1);
        } break;
        case ast_e::BVSLT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVSLT_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpSLT(e0, e1);
        } break;
        case ast_e::BVUGE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVUGE_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpUGE(e0, e1);
        } break;
        case ast_e::BVUGT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVUGT_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpUGT(e0, e1);
        } break;
        case ast_e::BVULE_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVULE_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpULE(e0, e1);
        } break;
        case ast_e::BVULT_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVULT_NODE" << endl;
          #endif
          // Get the 2 expressions
          auto e0 = Nodes[Children[0]];
          auto e1 = Nodes[Children[1]];
          // Lift the 'sge' ast to a 'icmp sge'
          Nodes[CNode] = IR->CreateICmpULT(e0, e1);
        } break;
        case ast_e::BVNAND_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVNAND_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateAnd(LHS, RHS);
          Nodes[CNode] = IR->CreateNot(Nodes[CNode]);
        } break;
        case ast_e::BVNOR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVNOR_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateOr(LHS, RHS);
          Nodes[CNode] = IR->CreateNot(Nodes[CNode]);
        } break;
        case ast_e::BVXNOR_NODE: {
          #ifdef VERBOSE_OUTPUT
          cout << "BVXNOR_NODE" << endl;
          #endif
          // Get the children and handle them first
          auto LHS = Nodes[Children[0]];
          auto RHS = Nodes[Children[1]];
          // Lift the current node
          Nodes[CNode] = IR->CreateXor(LHS, RHS);
          Nodes[CNode] = IR->CreateNot(Nodes[CNode]);
        } break;
        default: {
          // Notify we don't support these nodes
          // COMPOUND, DECLARE, INVALID,
          // ASSERT, STRING, IFF, LEFT
          report_fatal_error("LiftNodesWBS: unsupported node found.");
        } break;
      }
      // Check if we found an unresolved reference
      if (!UnresolvedReference) {
        // Print the translated node
        #ifdef VERBOSE_OUTPUT
        if (Nodes[CNode]) {
          Nodes[CNode]->dump();
        }
        #endif
        // Check the counter of the shared pointers
        Curr->Expression.reset();
        Curr->Module.reset();
        Curr->Nodes.clear();
        Curr->Node.reset();
        Curr->IR = nullptr;
        // Get the parent
        Curr = Curr->Parent;
      }
    }
  }
  // Return the final node
  return Nodes[TopNode];
}

/*
  Function to apply the LLVM optimizations to an LLVM-IR Module.
*/

void Translator::OptimizeModule(llvm::Module* M) {
  auto PassManager = llvm::legacy::PassManager();
  PassManagerBuilder Builder;
  Builder.OptLevel = 3;
  Builder.SizeLevel = 2;
  Builder.Inliner = createAlwaysInlinerLegacyPass();
  Builder.populateModulePassManager(PassManager);
  PassManager.run(*M);
  // Remove all the functions except for TritonAstFunction
  vector<Function*> ToBeRemoved;
  for (auto& F : M->functions()) {
    if (F.getName().startswith("ref")) {
      ToBeRemoved.push_back(&F);
    }
  }
  for (auto& F : ToBeRemoved) {
    F->eraseFromParent();
  }
  // Strip the weird names
  auto* MF = M->getFunction("TritonAstFunction");
  for (inst_iterator I = inst_begin(MF), E = inst_end(MF); I != E; I++) {
    if (I->hasName()) I->setName("");
  }
}

/*
  Function to clone a source LLVM-IR Function inside a destination LLVM-IR Function.
*/

void Translator::CloneFunctionInto(Function* SrcFunc, Function* DstFunc) const {
  // Map the new arguments to the old arguments
  auto NewArgs = DstFunc->arg_begin();
  ValueMap<const Value*, WeakTrackingVH> ArgsMap;
  for (auto& OldArg : SrcFunc->args()) {
    NewArgs->setName(OldArg.getName());
    ArgsMap[&OldArg] = &*NewArgs;
    NewArgs++;
  }
  // Clone the source function into the destination function
  SmallVector<ReturnInst*, 8> Returns;
  llvm::CloneFunctionInto(DstFunc, SrcFunc, ArgsMap, false, Returns);
}

/*
  Public function to execute the Triton AST to LLVM-IR Module translation.
*/

shared_ptr<Module> Translator::TritonAstToLLVMIR(const SharedAbstractNode& node, map<ExpKey, shared_ptr<llvm::Module>>& cache) {
  // Allocate a new Module (the old one is deallocated only if not referenced anymore)
  this->Module = make_shared<llvm::Module>("TritonAstModule", this->Context);
  if (Module == nullptr) {
    report_fatal_error("TritonAstToLLVMIR: failed to allocate Module");
  }
  // Create the function type (consistent with the top node type)
  auto* TritonAstType = FunctionType::get(IntegerType::get(this->Context, node->getBitvectorSize()), false);
  // Create the function (which will contain the basic block)
  auto* TritonAstFunction = Function::Create(TritonAstType, llvm::Function::CommonLinkage, "TritonAstFunction", this->Module.get());
  // Mark the function as always inlineable
  TritonAstFunction->addFnAttr(Attribute::AlwaysInline);
  // Create the only basic block (which will contain the lifted instructions)
  auto* TritonAstBlock = BasicBlock::Create(this->Context, "TritonAstEntry", TritonAstFunction);
  // Clear the old variable Value(s)
  this->VarsValue.clear();
  this->Vars.clear();
  // Map for the AST nodes
  map<SharedAbstractNode, Value*> nodes;
  // Initialize the IRBuilder to lift the nodes
  shared_ptr<IRBuilder<>> IR = make_shared<IRBuilder<>>(TritonAstBlock);
  // Traverse the AST in a WBS way (and lift the AST nodes)
  auto* Value = this->LiftNodesWBS(node, IR, cache);
  // Add the return statement
  IR->CreateRet(Value);
#ifdef DEBUG_OUTPUT
  // DEBUG: show the original ast
  cout << "\nOriginal Triton AST: " << node << endl;
  // DEBUG: dump the Module
  cout << "\nLifted Triton AST" << endl;
  // Dump the function
  Module->dump();
#endif
  // Optimize with LLVM
  this->OptimizeModule(this->Module.get());
  // DEBUG: dump the optimized Module
#ifdef DEBUG_OUTPUT
  cout << "\nOptimized Lifted Triton AST" << endl;
  Module->dump();
#endif
  // Return the generated Module
  return Module;
}

/*
  Converting a LLVM-IR basic block to a Triton AST.
*/

SharedAbstractNode Translator::LiftInstructionsDFS(Value* value, map<Value*, SharedAbstractNode>& values, map<string, SharedAbstractNode>& variables) {
  // DEBUG: show input value
#ifdef VERBOSE_OUTPUT
  cout << "Input value: " << endl;
  cout << "------------" << endl;
  value->dump();
  cout << "------------" << endl;
#endif
  // Check if we already lifted this value
  if (values.find(value) != values.end()) {
    return values[value];
  }
  // Get a reference to the ast context
  auto Ctx = this->Api.getAstContext();
  // We need to create a new SharedAbstractNode
  SharedAbstractNode node = nullptr;
  // Check if we are dealing with an undefined value
  if (auto UndefinedValue = dyn_cast<UndefValue>(value)) {
    // Return a 0 value by default, although making it symbolic would be better
    outs() << "[!] Found undefined value, returning a null bitvector (a new symbolic value would be better)\n";
    // Create a new zero bitvector
    node = Ctx->bv(0, value->getType()->getIntegerBitWidth());
  } else if (auto* CI = dyn_cast<ConstantInt>(value)) {
    // Check if we are dealing with a constant value
    const auto& Val = CI->getValue();
    // Convert the value into a string
    stringstream ss;
    ss << dec << Val.toString(10, false);
    triton::uint512 IntVal{ss.str()};
    // Create a new bitvector
    node = Ctx->bv(IntVal, Val.getBitWidth());
  } else if (auto Inst = dyn_cast<llvm::Instruction>(value)) {
    // Lift the instruction into an ast node
    switch (Inst->getOpcode()) {
        // Handle terminal instructions
      case llvm::Instruction::Load: {
        auto* GlobalVar = Inst->getOperand(0);
        #ifdef VERBOSE_OUTPUT
        outs() << "Found global variable loading:\n";
        GlobalVar->dump();
        #endif
        auto var = this->Api.getSymbolicVariable(GlobalVar->getName().str());
        #ifdef VERBOSE_OUTPUT
        outs() << "Triton symbolic variable:\n";
        cout << var << endl;
        #endif
        node = variables[GlobalVar->getName().str()];
        #ifdef VERBOSE_OUTPUT
        cout << "Triton variable node:\n";
        cout << node << endl;
        #endif
      } break;
        // Handle non-terminal instructions
      case llvm::Instruction::Ret: {
        auto* ReturnValue = Inst->getOperand(0);
        node = LiftInstructionsDFS(ReturnValue, values, variables);
      } break;
      case llvm::Instruction::Add: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvadd(n0, n1);
      } break;
      case llvm::Instruction::Sub: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvsub(n0, n1);
      } break;
      case llvm::Instruction::Xor: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvxor(n0, n1);
      } break;
      case llvm::Instruction::Or: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvor(n0, n1);
      } break;
      case llvm::Instruction::And: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvand(n0, n1);
      } break;
      case llvm::Instruction::Mul: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvmul(n0, n1);
      } break;
      case llvm::Instruction::UDiv: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvudiv(n0, n1);
      } break;
      case llvm::Instruction::SDiv: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvsdiv(n0, n1);
      } break;
      case llvm::Instruction::URem: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvurem(n0, n1);
      } break;
      case llvm::Instruction::SRem: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvsrem(n0, n1);
      } break;
      case llvm::Instruction::Shl: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvshl(n0, n1);
      } break;
      case llvm::Instruction::AShr: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvashr(n0, n1);
      } break;
      case llvm::Instruction::LShr: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Create the node
        node = Ctx->bvlshr(n0, n1);
      } break;
      case llvm::Instruction::ZExt: {
        // Get the destination type size
        auto dty = Inst->getType();
        auto dsz = dty->getIntegerBitWidth();
        // Fetch the operand
        auto o0 = Inst->getOperand(0);
        // Lift the operand
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        // Get the source type size
        auto sty = o0->getType();
        auto ssz = sty->getIntegerBitWidth();
        // Create the node
        node = Ctx->zx(dsz - ssz, n0);
      } break;
      case llvm::Instruction::SExt: {
        // Get the destination type size
        auto dty = Inst->getType();
        auto dsz = dty->getIntegerBitWidth();
        // Fetch the operand
        auto o0 = Inst->getOperand(0);
        // Lift the operand
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        // Get the source type size
        auto sty = o0->getType();
        auto ssz = sty->getIntegerBitWidth();
        // Create the node
        node = Ctx->sx(dsz - ssz, n0);
      } break;
      case llvm::Instruction::Trunc: {
        // Get the destination type size
        auto dty = Inst->getType();
        auto dsz = dty->getIntegerBitWidth();
        // Fetch the operand
        auto o0 = Inst->getOperand(0);
        // Lift the operand
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        // Create the node
        node = Ctx->extract(dsz - 1, 0, n0);
      } break;
      case llvm::Instruction::ICmp: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        // Detect the comparison type
        if (auto* ICmp = dyn_cast<ICmpInst>(Inst)) {
          switch (ICmp->getPredicate()) {
              // Equality comparisons
            case ICmpInst::ICMP_EQ: {
              node = Ctx->equal(n0, n1);
            } break;
            case ICmpInst::ICMP_NE: {
              node = Ctx->distinct(n0, n1);
            } break;
              // Unsigned comparisons
            case ::ICmpInst::ICMP_UGE: {
              node = Ctx->bvuge(n0, n1);
            } break;
            case ::ICmpInst::ICMP_UGT: {
              node = Ctx->bvugt(n0, n1);
            } break;
            case ::ICmpInst::ICMP_ULE: {
              node = Ctx->bvule(n0, n1);
            } break;
            case ::ICmpInst::ICMP_ULT: {
              node = Ctx->bvult(n0, n1);
            } break;
              // Signed comparisons
            case ::ICmpInst::ICMP_SGE: {
              node = Ctx->bvsge(n0, n1);
            } break;
            case ::ICmpInst::ICMP_SGT: {
              node = Ctx->bvsgt(n0, n1);
            } break;
            case ::ICmpInst::ICMP_SLE: {
              node = Ctx->bvsle(n0, n1);
            } break;
            case ::ICmpInst::ICMP_SLT: {
              node = Ctx->bvslt(n0, n1);
            } break;
            default: {
              cout << "Unsupported ICmpInst: ";
              ICmp->dump();
            } break;
          }
        }
        // Add the ITE node
        node = this->FixICmpBehavior(node);
      } break;
      case llvm::Instruction::Select: {
        // Fetch the operands
        auto o0 = Inst->getOperand(0);
        auto o1 = Inst->getOperand(1);
        auto o2 = Inst->getOperand(2);
        // Lift the operands
        auto n0 = LiftInstructionsDFS(o0, values, variables);
        auto n1 = LiftInstructionsDFS(o1, values, variables);
        auto n2 = LiftInstructionsDFS(o2, values, variables);
        // Undo the ICmp fix if it was applied to a logical node
        n0 = this->UndoICmpBehavior(n0);
        // If the condition is not logical, we must fix it
        n0 = this->ConvertToLogical(n0);
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
        node = Ctx->ite(n0, n1, n2);
      } break;
      default: {
        cout << "Unsupported instruction type: ";
        value->dump();
      } break;
    }
  } else {
    cout << "Unexpected Value: ";
    value->dump();
  }
  // DEBUG: dump the value
#ifdef VERBOSE_OUTPUT
  cout << "Original value: " << endl;
  value->dump();
  cout << "Dumping the value: " << node << endl;
#endif
  // Save the lifted value
  values[value] = node;
  // Return the lifted node
  return node;
}

/*
  Function to convert the BSWAP intrinsic into a sequence of Triton AST nodes.
*/

BasicBlock* Translator::FixBSWAPIntrinsic(BasicBlock* BB) {
  bool Fixed = false;
  do {
    // Reset the loop flag
    Fixed = false;
    // Search for bswap intrinsics
    for (auto& I : *BB) {
      // Check if we are dealing with an intrinsic call
      if (auto* C = dyn_cast<CallInst>(&I)) {
        auto* CF = C->getCalledFunction();
        auto CFN = CF->getName();
        if (CFN == "llvm.bswap.i64") {
          // Get the bswapped value
          auto V = C->getOperand(0);
          // Get the operations type
          auto i64 = Type::getInt64Ty(this->Context);
          // Lower it to standard instructions
          IRBuilder<> IR(C);
          auto i1 = IR.CreateShl(V, ConstantInt::get(i64, 8));
          auto i2 = IR.CreateAnd(i1, ConstantInt::get(i64, 0xFF00FF00FF00FF00));
          auto i3 = IR.CreateLShr(V, ConstantInt::get(i64, 8));
          auto i4 = IR.CreateAnd(i3, ConstantInt::get(i64, 0x00FF00FF00FF00FF));
          auto i5 = IR.CreateOr(i2, i4);
          auto i6 = IR.CreateShl(i5, ConstantInt::get(i64, 16));
          auto i7 = IR.CreateAnd(i6, ConstantInt::get(i64, 0xFFFF0000FFFF0000));
          auto i8 = IR.CreateLShr(i5, ConstantInt::get(i64, 16));
          auto i9 = IR.CreateAnd(i8, ConstantInt::get(i64, 0x0000FFFF0000FFFF));
          auto i10 = IR.CreateOr(i7, i9);
          auto i11 = IR.CreateShl(i10, ConstantInt::get(i64, 32));
          auto i12 = IR.CreateLShr(i10, ConstantInt::get(i64, 32));
          auto i13 = IR.CreateOr(i11, i12);
          // Replace the bswap with 'i13'
          C->replaceAllUsesWith(i13);
          // Remove the call to bswap
          C->eraseFromParent();
          // Notify we fixed the bswap
          Fixed = true;
          // Stop the search
          break;
        } else if (CFN == "llvm.bswap.i32") {
          // Get the bswapped value
          auto V = C->getOperand(0);
          // Get the operations type
          auto i32 = Type::getInt32Ty(this->Context);
          // Lower it to standard instructions
          IRBuilder<> IR(C);
          auto i1 = IR.CreateShl(V, ConstantInt::get(i32, 8));
          auto i2 = IR.CreateAnd(i1, ConstantInt::get(i32, 0xFF00FF00));
          auto i3 = IR.CreateLShr(V, ConstantInt::get(i32, 8));
          auto i4 = IR.CreateAnd(i3, ConstantInt::get(i32, 0x00FF00FF));
          auto i5 = IR.CreateOr(i2, i4);
          auto i6 = IR.CreateShl(i5, ConstantInt::get(i32, 16));
          auto i7 = IR.CreateLShr(i5, ConstantInt::get(i32, 16));
          auto i8 = IR.CreateOr(i6, i7);
          // Replace the bswap with 'i8'
          C->replaceAllUsesWith(i8);
          // Remove the call to bswap
          C->eraseFromParent();
          // Notify we fixed the bswap
          Fixed = true;
          // Stop the search
          break;
        } else if (CFN == "llvm.bswap.i16") {
          // Get the bswapped value
          auto V = C->getOperand(0);
          // Get the operations type
          auto i16 = Type::getInt16Ty(this->Context);
          // Lower it to standard instructions
          IRBuilder<> IR(C);
          auto i1 = IR.CreateShl(V, ConstantInt::get(i16, 8));
          auto i2 = IR.CreateLShr(V, ConstantInt::get(i16, 8));
          auto i3 = IR.CreateOr(i1, i2);
          // Replace the bswap with 'i8'
          C->replaceAllUsesWith(i3);
          // Remove the call to bswap
          C->eraseFromParent();
          // Notify we fixed the bswap
          Fixed = true;
          // Stop the search
          break;
        }
      }
    }
  } while (Fixed);
  return BB;
}

/*
  Function to convert a comparison node to be logical.
*/

SharedAbstractNode Translator::FixICmpBehavior(SharedAbstractNode Node) {
  // Fetch the AST context
  SharedAstContext Ctx = this->Api.getAstContext();
  // Convert the node to be logical (if possible)
  Node = this->ConvertToLogical(Node);
  // Handling of the rest of the comparisons
  switch (Node->getType()) {
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
      Node = Ctx->ite(Node, Ctx->bvtrue(), Ctx->bvfalse());
      break;
    }
    default: break;
  }
  return Node;
}

/*
  Function to undo the conversion of a comparison node to logical.
*/

SharedAbstractNode Translator::UndoICmpBehavior(SharedAbstractNode Node) {
  // Fetch the AST context
  auto Ctx = this->Api.getAstContext();
  if (Node->getType() == ast_e::ITE_NODE) {
    auto C1 = Node->getChildren()[0];
    auto C2 = Node->getChildren()[1];
    auto C3 = Node->getChildren()[2];
    if (C1->isLogical() && C2->equalTo(Ctx->bvtrue()) && C3->equalTo(Ctx->bvfalse())) {
      Node = Node->getChildren()[0];
    }
  }
  return Node;
}

/*
  Function to convert a bv1 node to be logical.
*/

SharedAbstractNode Translator::ConvertToLogical(SharedAbstractNode Node) {
  // Fetch the AST context
  auto Ctx = this->Api.getAstContext();
  if (!Node->isLogical() && Node->getBitvectorSize() == 1) {
    Node = Ctx->equal(Node, Ctx->bvtrue());
  }
  return Node;
}

/*
  Public function to execute the LLVM-IR Module to Triton AST translation.
*/

SharedAbstractNode Translator::LLVMIRToTritonAst(const shared_ptr<llvm::Module>& Module, map<string, SharedAbstractNode>& Variables, bool IsITE, bool IsLogical) {
  // Get our lovely function out of the Module
  auto* TritonAstFunction = Module->getFunction("TritonAstFunction");
  if (TritonAstFunction == nullptr) {
    cout << "Sorry but the provided llvm::Module doesn't contain a function named 'TritonAstFunction'" << endl;
    return nullptr;
  }
  // Get our lovely basic block out of the function
  auto& TritonAstBlock = TritonAstFunction->getEntryBlock();
  // Fix the bswap intrinsics
  auto* TritonAstBB = this->FixBSWAPIntrinsic(&TritonAstBlock);
  // Get the return value of the function
  auto* ReturnValue = TritonAstBB->getTerminator();
  // Explore the function in a bottom-up fashion
  map<Value*, SharedAbstractNode> Values;
  auto Ast = this->LiftInstructionsDFS(ReturnValue, Values, Variables);
  // Fix the ICmp behavior if needed
  if (IsITE) {
    Ast = this->FixICmpBehavior(Ast);
  }
  // Undo the ICmp behavior if needed
  if (IsLogical) {
    Ast = this->UndoICmpBehavior(Ast);
  }
  // DEBUG: dump the lifted AST
#ifdef DEBUG_OUTPUT
  cout << "\nRecovered Triton AST: " << Ast << endl;
#endif
  // Return the generated AST
  return Ast;
}