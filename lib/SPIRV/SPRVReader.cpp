//===- SPRVReader.cpp � Converts SPIR-V to LLVM -----------------*- C++ -*-===//
//
//                     The LLVM/SPIR-V Translator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright (c) 2014 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal with the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimers.
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimers in the documentation
// and/or other materials provided with the distribution.
// Neither the names of Advanced Micro Devices, Inc., nor the names of its
// contributors may be used to endorse or promote products derived from this
// Software without specific prior written permission.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH
// THE SOFTWARE.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements conversion of SPIR-V binary to LLVM IR.
///
//===----------------------------------------------------------------------===//
#include "SPRVUtil.h"
#include "SPRVType.h"
#include "SPRVValue.h"
#include "SPRVModule.h"
#include "SPRVFunction.h"
#include "SPRVBasicBlock.h"
#include "SPRVInstruction.h"
#include "SPRVExtInst.h"
#include "SPRVInternal.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#define DEBUG_TYPE "spirv"

using namespace std;
using namespace llvm;
using namespace SPRV;

namespace SPRV{

// Prefix for placeholder global variable name.
const char* kPlaceholderPrefix = "placeholder.";

// Save the translated LLVM before validation for debugging purpose.
static bool DbgSaveTmpLLVM = true;
static const char *DbgTmpLLVMFileName = "_tmp_llvmbil.ll";

typedef std::pair < unsigned, AttributeSet > AttributeWithIndex;

static bool
isOpenCLKernel(SPRVFunction *BF) {
  return BF->getModule()->isEntryPoint(SPRVEMDL_Kernel, BF->getId());
}

static void
dumpLLVM(Module *M, const std::string &FName) {
  std::error_code EC;
  raw_fd_ostream FS(FName, EC, sys::fs::F_None);
  if (EC) {
    FS << *M;
    FS.close();
  }
}

static MDNode*
getMDNodeStringIntVec(LLVMContext *Context, const std::string& Str,
    const std::vector<SPRVWord>& IntVals) {
  std::vector<Metadata*> ValueVec;
  ValueVec.push_back(MDString::get(*Context, Str));
  for (auto &I:IntVals)
    ValueVec.push_back(ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(*Context), I)));
  return MDNode::get(*Context, ValueVec);
}

static MDNode*
getMDTwoInt(LLVMContext *Context, unsigned Int1, unsigned Int2) {
  std::vector<Metadata*> ValueVec;
  ValueVec.push_back(ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(*Context), Int1)));
  ValueVec.push_back(ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(*Context), Int2)));
  return MDNode::get(*Context, ValueVec);
}

static MDNode*
getMDString(LLVMContext *Context, const std::string& Str) {
  std::vector<Metadata*> ValueVec;
  if (!Str.empty())
    ValueVec.push_back(MDString::get(*Context, Str));
  return MDNode::get(*Context, ValueVec);
}

static void
addOCLVersionMetadata(LLVMContext *Context, Module *M,
    const std::string &MDName, unsigned Major, unsigned Minor) {
  NamedMDNode *NamedMD = M->getOrInsertNamedMetadata(MDName);
  NamedMD->addOperand(getMDTwoInt(Context, Major, Minor));
}

static void
addNamedMetadataString(LLVMContext *Context, Module *M,
    const std::string &MDName, const std::string &Str) {
  NamedMDNode *NamedMD = M->getOrInsertNamedMetadata(MDName);
  NamedMD->addOperand(getMDString(Context, Str));
}

static void
addOCLKernelArgumentMetadata(LLVMContext *Context,
  std::vector<llvm::Metadata*> &KernelMD, const std::string &MDName,
    SPRVFunction *BF, std::function<Metadata *(SPRVFunctionParameter *)>Func){
  std::vector<Metadata*> ValueVec;
    ValueVec.push_back(MDString::get(*Context, MDName));
  BF->foreachArgument([&](SPRVFunctionParameter *Arg) {
    ValueVec.push_back(Func(Arg));
  });
  KernelMD.push_back(MDNode::get(*Context, ValueVec));
}

class SPRVToLLVM {
public:
  SPRVToLLVM(Module *LLVMModule, SPRVModule *TheSPRVModule)
    :M(LLVMModule), BM(TheSPRVModule){
    if (M)
      Context = &M->getContext();
  }

  std::string getOCLBuiltinName(SPRVInstruction* BI);
  std::string getOCLConvertBuiltinName(SPRVInstruction *BI);

  Type *transType(SPRVType *BT);
  std::string transTypeToOCLTypeName(SPRVType *BT, bool IsSigned = true);
  std::vector<Type *> transTypeVector(const std::vector<SPRVType *>&);
  bool translate();
  bool transAddressingModel();

  Value *transValue(SPRVValue *, Function *F, BasicBlock *,
      bool CreatePlaceHolder = true);
  Value *transValueWithoutDecoration(SPRVValue *, Function *F, BasicBlock *,
      bool CreatePlaceHolder = true);
  bool transDecoration(SPRVValue *, Value *);
  bool transAlign(SPRVValue *, Value *);
  Instruction *transOCLBuiltinFromExtInst(SPRVExtInst *BC, BasicBlock *BB);
  std::vector<Value *> transValue(const std::vector<SPRVValue *>&, Function *F,
      BasicBlock *);
  Function *transFunction(SPRVFunction *F);
  bool transFPContractMetadata();
  bool transKernelMetadata();
  bool transSourceLanguage();
  bool transSourceExtension();
  bool transCompilerOption();
  Value *transConvertInst(SPRVValue* BV, Function* F, BasicBlock* BB);
  Instruction *transOCLBuiltinFromInst(const std::string& FuncName,
      SPRVInstruction* BI, BasicBlock* BB);
  Instruction *transOCLBuiltinFromInst(SPRVInstruction *BI, BasicBlock *BB);
  Instruction *transOCLAtomic(SPRVAtomicOperatorGeneric* BA, BasicBlock *BB);
  Instruction *transOCLBarrierFence(SPRVInstruction* BI, BasicBlock *BB);
  Instruction *transOCLDot(SPRVDot *BD, BasicBlock *BB);
  void transOCLVectorLoadStore(std::string& UnmangledName,
      std::vector<SPRVWord> &BArgs);

  /// Post-process translated LLVM module for OpenCL.
  bool postProcessOCL();

  /// \brief Post-process OpenCL builtin functions returning struct type.
  ///
  /// Some OpenCL builtin functions are translated to SPIR-V instructions with
  /// struct type result, e.g. NDRange creation functions. Such functions
  /// need to be post-processed to return the struct through sret argument.
  bool postProcessOCLBuiltinReturnStruct(Function *F);

  /// \brief Post-process OpenCL builtin functions having block argument.
  ///
  /// These functions are translated to functions with function pointer type
  /// argument first, then post-processed to have block argument.
  bool postProcessOCLBuiltinWithFuncPointer(Function *F,
      Function::arg_iterator I);

  typedef DenseMap<SPRVType *, Type *> SPRVToLLVMTypeMap;
  typedef DenseMap<SPRVValue *, Value *> SPRVToLLVMValueMap;
  typedef DenseMap<SPRVFunction *, Function *> SPRVToLLVMFunctionMap;
  typedef DenseMap<GlobalVariable *, SPRVBuiltinVariableKind> BuiltinVarMap;

  // A SPIRV value may be translated to a load instruction of a placeholder
  // global variable. This map records load instruction of these placeholders
  // which are supposed to be replaced by the real values later.
  typedef std::map<SPRVValue *, LoadInst*> SPRVToLLVMPlaceholderMap;
private:
  Module *M;
  BuiltinVarMap BuiltinGVMap;
  LLVMContext *Context;
  SPRVModule *BM;
  SPRVToLLVMTypeMap TypeMap;
  SPRVToLLVMValueMap ValueMap;
  SPRVToLLVMFunctionMap FuncMap;
  SPRVToLLVMPlaceholderMap PlaceholderMap;

  Type *mapType(SPRVType *BT, Type *T) {
    SPRVDBG(dbgs() << *T << '\n';)
    TypeMap[BT] = T;
    return T;
  }

  // If a value is mapped twice, the existing mapped value is a placeholder,
  // which must be a load instruction of a global variable whose name starts
  // with kPlaceholderPrefix.
  Value *mapValue(SPRVValue *BV, Value *V) {
    auto Loc = ValueMap.find(BV);
    if (Loc != ValueMap.end()) {
      if (Loc->second == V)
        return V;
      auto LD = dyn_cast<LoadInst>(Loc->second);
      auto Placeholder = dyn_cast<GlobalVariable>(LD->getPointerOperand());
      assert (LD && Placeholder &&
          Placeholder->getName().startswith(kPlaceholderPrefix) &&
          "A value is translated twice");
      // Replaces placeholders for PHI nodes
      LD->replaceAllUsesWith(V);
      LD->dropAllReferences();
      LD->removeFromParent();
      Placeholder->dropAllReferences();
      Placeholder->removeFromParent();
    }
    ValueMap[BV] = V;
    return V;
  }

  bool isSPRVBuiltinVariable(GlobalVariable *GV,
      SPRVBuiltinVariableKind *Kind = nullptr) {
    auto Loc = BuiltinGVMap.find(GV);
    if (Loc == BuiltinGVMap.end())
      return false;
    if (Kind)
      *Kind = Loc->second;
    return true;
  }
  // OpenCL function always has NoUnwound attribute.
  // Change this if it is no longer true.
  bool isFuncNoUnwind() const { return true;}
  bool isSPRVCmpInstTransToLLVMInst(SPRVInstruction *BI) const;
  bool transOCLBuiltinsFromVariables();
  bool transOCLBuiltinFromVariable(GlobalVariable *GV,
      SPRVBuiltinVariableKind Kind);
  MDString *transOCLKernelArgTypeName(SPRVFunctionParameter *);

  Value *mapFunction(SPRVFunction *BF, Function *F) {
    SPRVDBG(bildbgs() << "[mapFunction] " << *BF << " -> ";
      dbgs() << *F << '\n';)
    FuncMap[BF] = F;
    return F;
  }

  Value *getTranslatedValue(SPRVValue *BV);
  Type *getTranslatedType(SPRVType *BT);

  SPRVErrorLog &getErrorLog() {
    return BM->getErrorLog();
  }

  void setCallingConv(CallInst *Call) {
    Function *F = Call->getCalledFunction();
    Call->setCallingConv(F->getCallingConv());
  }

  void setAttrByCalledFunc(CallInst *Call);
  Type *transFPType(SPRVType* T);
  BinaryOperator *transShiftLogicalBitwiseInst(SPRVValue* BV, BasicBlock* BB,
      Function* F);
  void transFlags(llvm::Value* V);
  Instruction *transCmpInst(SPRVValue* BV, BasicBlock* BB, Function* F);
  void transOCLBuiltinFromInstPreproc(SPRVInstruction* BI, Type *&RetTy,
      std::vector<Type *> &ArgTys);
  Instruction* transOCLBuiltinFromInstPostproc(SPRVInstruction* BI,
      Instruction* Inst, BasicBlock* BB);
  std::string transOCLImageTypeName(SPRV::SPRVTypeSampler* ST);
  std::string transOCLImageTypeAccessQualifier(SPRV::SPRVTypeSampler* ST);
  Value *oclTransConstantSampler(SPRV::SPRVConstantSampler* BCS);
  template<class Source, class Func>
  bool foreachFuncCtlMask(Source, Func);
};

Type *
SPRVToLLVM::getTranslatedType(SPRVType *BV){
  auto Loc = TypeMap.find(BV);
  if (Loc != TypeMap.end())
    return Loc->second;
  return nullptr;
}

Value *
SPRVToLLVM::getTranslatedValue(SPRVValue *BV){
  auto Loc = ValueMap.find(BV);
  if (Loc != ValueMap.end())
    return Loc->second;
  return nullptr;
}

void
SPRVToLLVM::setAttrByCalledFunc(CallInst *Call) {
  Function *F = Call->getCalledFunction();
  if (F->isIntrinsic()) {
    return;
  }
  Call->setCallingConv(F->getCallingConv());
  Call->setAttributes(F->getAttributes());
}

bool
SPRVToLLVM::transOCLBuiltinsFromVariables(){
  std::vector<GlobalVariable *> WorkList;
  for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
    SPRVBuiltinVariableKind Kind = SPRVBI_Count;
    if (!isSPRVBuiltinVariable(I, &Kind))
      continue;
    if (!transOCLBuiltinFromVariable(I, Kind))
      return false;
    WorkList.push_back(I);
  }
  for (auto &I:WorkList) {
    I->dropAllReferences();
    I->removeFromParent();
  }
  return true;
}

// For integer types shorter than 32 bit, unsigned/signedness can be inferred
// from zext/sext attribute.
MDString *
SPRVToLLVM::transOCLKernelArgTypeName(SPRVFunctionParameter *Arg) {
  auto Ty = Arg->isByVal() ? Arg->getType()->getPointerElementType() :
    Arg->getType();
  return MDString::get(*Context, transTypeToOCLTypeName(Ty, !Arg->isZext()));
}

// Variable like GlobalInvolcationId[x] -> get_global_id(x).
// Variable like WorkDim -> get_work_dim().
bool
SPRVToLLVM::transOCLBuiltinFromVariable(GlobalVariable *GV,
    SPRVBuiltinVariableKind Kind) {
  std::string FuncName = SPIRSPRVBuiltinVariableMap::rmap(Kind);
  std::string MangledName;
  Type *ReturnTy =  GV->getType()->getPointerElementType();
  bool IsVec = ReturnTy->isVectorTy();
  if (IsVec)
    ReturnTy = cast<VectorType>(ReturnTy)->getElementType();
  std::vector<Type*> ArgTy;
  if (IsVec)
    ArgTy.push_back(Type::getInt32Ty(*Context));
  mangle(SPRVBIS_OpenCL20, FuncName, ArgTy, MangledName);
  Function *Func = M->getFunction(MangledName);
  if (!Func) {
    FunctionType *FT = FunctionType::get(ReturnTy, ArgTy, false);
    Func = Function::Create(FT, GlobalValue::ExternalLinkage, MangledName, M);
    Func->setCallingConv(CallingConv::SPIR_FUNC);
    Func->addFnAttr(Attribute::NoUnwind);
    Func->addFnAttr(Attribute::ReadNone);
  }
  std::vector<Instruction *> Deletes;
  std::vector<Instruction *> Uses;
  for (auto UI = GV->user_begin(), UE = GV->user_end(); UI != UE; ++UI) {
    assert (isa<LoadInst>(*UI) && "Unsupported use");
    auto LD = dyn_cast<LoadInst>(*UI);
    if (!IsVec) {
      Uses.push_back(LD);
      Deletes.push_back(LD);
      continue;
    }
    for (auto LDUI = LD->user_begin(), LDUE = LD->user_end(); LDUI != LDUE;
        ++LDUI) {
      assert(isa<ExtractElementInst>(*LDUI) && "Unsupported use");
      auto EEI = dyn_cast<ExtractElementInst>(*LDUI);
      Uses.push_back(EEI);
      Deletes.push_back(EEI);
    }
    Deletes.push_back(LD);
  }
  for (auto &I:Uses) {
    std::vector<Value *> Arg;
    if (auto EEI = dyn_cast<ExtractElementInst>(I))
      Arg.push_back(EEI->getIndexOperand());
    auto Call = CallInst::Create(Func, Arg, "", I);
    Call->takeName(I);
    setAttrByCalledFunc(Call);
    SPRVDBG(dbgs() << "[transOCLBuiltinFromVariable] " << *I << " -> " <<
        *Call << '\n';)
    I->replaceAllUsesWith(Call);
  }
  for (auto &I:Deletes) {
    I->dropAllReferences();
    I->removeFromParent();
  }
  return true;
}

Type *
SPRVToLLVM::transFPType(SPRVType* T) {
  switch(T->getFloatBitWidth()) {
  case 16: return Type::getHalfTy(*Context);
  case 32: return Type::getFloatTy(*Context);
  case 64: return Type::getDoubleTy(*Context);
  default:
    assert(0 && "Invalid type");
    return nullptr;
  }
}

std::string SPRVToLLVM::transOCLImageTypeName(SPRV::SPRVTypeSampler* ST) {
  auto Name = SPIRSPRVImageSamplerTypeMap::rmap(ST->getDescriptor());
  Name = Name + SPIR_TYPE_NAME_DELIMITER +
      SPIRSPRVAccessQualifierMap::rmap(ST->getAccessQualifier());
  return Name;
}

Type *
SPRVToLLVM::transType(SPRVType *T) {
  auto Loc = TypeMap.find(T);
  if (Loc != TypeMap.end())
    return Loc->second;

  SPRVDBG(bildbgs() << "[transType] " << *T << " -> ";)
  T->validate();
  switch(T->getOpCode()) {
  case SPRVOC_OpTypeVoid:
    return mapType(T, Type::getVoidTy(*Context));
  case SPRVOC_OpTypeBool:
    return mapType(T, Type::getInt1Ty(*Context));
  case SPRVOC_OpTypeInt:
    return mapType(T, Type::getIntNTy(*Context, T->getIntegerBitWidth()));
  case SPRVOC_OpTypeFloat:
    return mapType(T, transFPType(T));
  case SPRVOC_OpTypeArray:
    return mapType(T, ArrayType::get(transType(T->getArrayElementType()),
        T->getArrayLength()));
  case SPRVOC_OpTypePointer:
    return mapType(T, PointerType::get(transType(T->getPointerElementType()),
        SPIRSPRVAddrSpaceMap::rmap(T->getPointerStorageClass())));
  case SPRVOC_OpTypeVector:
    return mapType(T, VectorType::get(transType(T->getVectorComponentType()),
        T->getVectorComponentCount()));
  case SPRVOC_OpTypeOpaque:
    return mapType(T, StructType::create(*Context, T->getName()));
  case SPRVOC_OpTypeFunction: {
    auto FT = static_cast<SPRVTypeFunction *>(T);
    auto RT = transType(FT->getReturnType());
    std::vector<Type *> PT;
    for (size_t I = 0, E = FT->getNumParameters(); I != E; ++I)
      PT.push_back(transType(FT->getParameterType(I)));
    return mapType(T, FunctionType::get(RT, PT, false));
    }
  case SPRVOC_OpTypeSampler: {
    auto ST = static_cast<SPRVTypeSampler *>(T);
    if (ST->IsOCLImage())
      return mapType(T, getOrCreateOpaquePtrType(M,
          transOCLImageTypeName(ST)));
    else if (ST->IsOCLSampler())
      return mapType(T, Type::getInt32Ty(*Context));
    else
      assert(0 && "Unsupported sampler type");
    return nullptr;
  }
  case SPRVOC_OpTypeStruct: {
    auto ST = static_cast<SPRVTypeStruct *>(T);
    std::vector<Type *> MT;
    for (size_t I = 0, E = ST->getMemberCount(); I != E; ++I)
      MT.push_back(transType(ST->getMemberType(I)));
    return mapType(T, StructType::create(*Context, MT, ST->getName(),
      ST->isPacked()));
    } 
  case SPRVOC_OpTypePipe: {
    auto PT = static_cast<SPRVTypePipe *>(T);
    std::vector<Type *> MT;
    MT.push_back(transType(PT->getPipeType()));
    return mapType(T, PointerType::get(StructType::create(*Context, MT,
        "opencl.pipe_t"), SPIRAS_Global));
    }
  default: {
    if (isOpaqueGenericTypeOpCode(T->getOpCode())) {
      return mapType(T, PointerType::get(
          llvm::StructType::create(*Context,
              BuiltinOpaqueGenericTypeOpCodeMap::rmap(T->getOpCode())),
          SPIRAS_Private));
    }
    assert(0 && "Not implemented");
    }
  }
  return 0;
}

std::string
SPRVToLLVM::transTypeToOCLTypeName(SPRVType *T, bool IsSigned) {
  switch(T->getOpCode()) {
  case SPRVOC_OpTypeVoid:
    return "void";
  case SPRVOC_OpTypeBool:
    return "bool";
  case SPRVOC_OpTypeInt: {
    std::string Prefix = IsSigned ? "" : "u";
    switch(T->getIntegerBitWidth()) {
    case 8:
      return Prefix + "char";
    case 16:
      return Prefix + "short";
    case 32:
      return Prefix + "int";
    case 64:
      return Prefix + "long";
    default:
      assert(0 && "invalid integer size");
      return Prefix + std::string("int") + T->getIntegerBitWidth() + "_t";
    }
  }
  break;
  case SPRVOC_OpTypeFloat:
    switch(T->getFloatBitWidth()){
    case 16:
      return "half";
    case 32:
      return "float";
    case 64:
      return "double";
    default:
      assert(0 && "invalid floating pointer bitwidth");
      return std::string("float") + T->getFloatBitWidth() + "_t";
    }
    break;
  case SPRVOC_OpTypeArray:
    return "array";
  case SPRVOC_OpTypePointer:
    return transTypeToOCLTypeName(T->getPointerElementType()) + "*";
  case SPRVOC_OpTypeVector:
    return transTypeToOCLTypeName(T->getVectorComponentType()) +
        T->getVectorComponentCount();
  case SPRVOC_OpTypeOpaque:
      return T->getName();
  case SPRVOC_OpTypeFunction:
    assert(0 && "Unsupported");
    return "function";
  case SPRVOC_OpTypeStruct: {
    auto Name = T->getName();
    if (Name.find("struct.") == 0)
      Name[6] = ' ';
    else if (Name.find("union.") == 0)
      Name[5] = ' ';
    return Name;
  }
  case SPRVOC_OpTypePipe:
    return "pipe_t";
  case SPRVOC_OpTypeSampler:
    return SPIRSPRVImageSamplerTypeMap::rmap(static_cast<SPRVTypeSampler *>(T)
        ->getDescriptor()).substr(7);
  default:
      if (isOpaqueGenericTypeOpCode(T->getOpCode())) {
        return BuiltinOpaqueGenericTypeOpCodeMap::rmap(T->getOpCode());
      }
      assert(0 && "Not implemented");
      return "unknown";
  }
}

std::vector<Type *>
SPRVToLLVM::transTypeVector(const std::vector<SPRVType *> &BT) {
  std::vector<Type *> T;
  for (auto I: BT)
    T.push_back(transType(I));
  return T;
}

std::vector<Value *>
SPRVToLLVM::transValue(const std::vector<SPRVValue *> &BV, Function *F,
    BasicBlock *BB) {
  std::vector<Value *> V;
  for (auto I: BV)
    V.push_back(transValue(I, F, BB));
  return V;
}

bool
SPRVToLLVM::isSPRVCmpInstTransToLLVMInst(SPRVInstruction* BI) const {
  auto OC = BI->getOpCode();
  return isCmpOpCode(OC) &&
      !(OC >= SPRVOC_OpLessOrGreater && OC <= SPRVOC_OpUnordered);
}

void
SPRVToLLVM::transFlags(llvm::Value* V) {
  if(!isa<Instruction>(V))
    return;
  auto OC = cast<Instruction>(V)->getOpcode();
  if (OC == Instruction::AShr || OC == Instruction::LShr) {
    cast<BinaryOperator>(V)->setIsExact();
    return;
  }
}

Value *
SPRVToLLVM::transValue(SPRVValue *BV, Function *F, BasicBlock *BB,
    bool CreatePlaceHolder){
  SPRVToLLVMValueMap::iterator Loc = ValueMap.find(BV);
  if (Loc != ValueMap.end() && (!PlaceholderMap.count(BV) || CreatePlaceHolder))
    return Loc->second;

  SPRVDBG(bildbgs() << "[transValue] " << *BV << " -> ";)
  BV->validate();

  auto V = transValueWithoutDecoration(BV, F, BB, CreatePlaceHolder);
  if (!V) {
    SPRVDBG(dbgs() << " Warning ! nullptr\n";)
    return nullptr;
  }
  V->setName(BV->getName());
  if (!transDecoration(BV, V)) {
    assert (0 && "trans decoration fail");
    return nullptr;
  }
  transFlags(V);

  SPRVDBG(dbgs() << *V << '\n';)

  return V;
}

Value *
SPRVToLLVM::transConvertInst(SPRVValue* BV, Function* F, BasicBlock* BB) {
  SPRVUnary* BC = static_cast<SPRVUnary*>(BV);
  auto Src = transValue(BC->getOperand(), F, BB, BB ? true : false);
  auto Dst = transType(BC->getType());
  CastInst::CastOps CO = Instruction::BitCast;
  bool IsExt = Dst->getScalarSizeInBits()
      > Src->getType()->getScalarSizeInBits();
  switch (BC->getOpCode()) {
  case SPRVOC_OpPtrCastToGeneric:
  case SPRVOC_OpGenericCastToPtr:
    CO = Instruction::AddrSpaceCast;
    break;
  case SPRVOC_OpSConvert:
    CO = IsExt ? Instruction::SExt : Instruction::Trunc;
    break;
  case SPRVOC_OpUConvert:
    CO = IsExt ? Instruction::ZExt : Instruction::Trunc;
    break;
  case SPRVOC_OpFConvert:
    CO = IsExt ? Instruction::FPExt : Instruction::FPTrunc;
    break;
  default:
    CO = static_cast<CastInst::CastOps>(OpCodeMap::rmap(BC->getOpCode()));
  }
  assert(CastInst::isCast(CO) && "Invalid cast op code");
  SPRVDBG(if (!CastInst::castIsValid(CO, Src, Dst)) {
    bildbgs() << "Invalid cast: " << *BV << " -> ";
    dbgs() << "Op = " << CO << ", Src = " << *Src << " Dst = " << *Dst << '\n';
  })
  if (BB)
    return CastInst::Create(CO, Src, Dst, BV->getName(), BB);
  return ConstantExpr::getCast(CO, dyn_cast<Constant>(Src), Dst);
}

BinaryOperator *SPRVToLLVM::transShiftLogicalBitwiseInst(SPRVValue* BV,
    BasicBlock* BB,Function* F) {
  SPRVBinary* BBN = static_cast<SPRVBinary*>(BV);
  assert(BB && "Invalid BB");
  unsigned ThellvmOpCode;
  SPRVOpCode TheBilOpCode = BBN->getOpCode();
  switch (TheBilOpCode) {
  case SPRVOC_OpBitwiseOr:
  case SPRVOC_OpLogicalOr:
    ThellvmOpCode = Instruction::Or;
    break;
  case SPRVOC_OpBitwiseAnd:
  case SPRVOC_OpLogicalAnd:
    ThellvmOpCode = Instruction::And;
    break;
  case SPRVOC_OpBitwiseXor:
  case SPRVOC_OpLogicalXor:
    ThellvmOpCode = Instruction::Xor;
    break;
  default:
    ThellvmOpCode = OpCodeMap::rmap(BBN->getOpCode());
  }
  Instruction::BinaryOps BO = static_cast<Instruction::BinaryOps>(
      ThellvmOpCode);
  auto Inst = BinaryOperator::Create(BO, transValue(
      BBN->getOperand(0), F, BB),
      transValue(BBN->getOperand(1), F, BB), BV->getName(), BB);
  return Inst;
}

Instruction *
SPRVToLLVM::transCmpInst(SPRVValue* BV, BasicBlock* BB, Function* F) {
  SPRVCompare* BC = static_cast<SPRVCompare*>(BV);
  assert(BB && "Invalid BB");
  SPRVType* BT = BC->getOperand(0)->getType();
  Instruction* Inst = nullptr;
  if (BT->isTypeVectorOrScalarInt() || BT->isTypePointer())
    Inst = new ICmpInst(*BB, CmpMap::rmap(BC->getOpCode()),
        transValue(BC->getOperand(0), F, BB),
        transValue(BC->getOperand(1), F, BB));
  else if (BT->isTypeVectorOrScalarFloat())
    Inst = new FCmpInst(*BB, CmpMap::rmap(BC->getOpCode()),
        transValue(BC->getOperand(0), F, BB),
        transValue(BC->getOperand(1), F, BB));
  assert(Inst && "not implemented");
  return Inst;
}

bool
SPRVToLLVM::postProcessOCL() {
  for (auto I = M->begin(), E = M->end(); I != E;) {
    auto F = I++;
    if (F->hasName() && F->isDeclaration()) {
      DEBUG(dbgs() << "[postProcessOCL] " << *F << '\n');
      SPRVWord SrcLangVer = 0;
      BM->getSourceLanguage(&SrcLangVer);
      if (F->getReturnType()->isStructTy() && oclIsBuiltin(F->getName(), SrcLangVer)) {
        if (!postProcessOCLBuiltinReturnStruct(F))
          return false;
      }

      auto AI = F->arg_begin();
      if (hasFunctionPointerArg(F, AI) && isSPRVFunction(F))
        if (!postProcessOCLBuiltinWithFuncPointer(F, AI))
          return false;
    }
  }
  return true;
}

bool
SPRVToLLVM::postProcessOCLBuiltinReturnStruct(Function *F) {
  std::string Name = F->getName();
  F->setName(Name + ".old");
  for (auto I = F->user_begin(), E = F->user_end(); I != E; ++I) {
    if (auto CI = dyn_cast<CallInst>(*I)) {
      auto ST = dyn_cast<StoreInst>(*(CI->user_begin()));
      std::vector<Type *> ArgTys;
      getFunctionTypeParameterTypes(F->getFunctionType(), ArgTys);
      ArgTys.insert(ArgTys.begin(), PointerType::get(F->getReturnType(),
          SPIRAS_Private));
      auto newF = getOrCreateFunction(M, Type::getVoidTy(*Context),
          ArgTys, Name);
      auto Args = getArguments(CI);
      Args.insert(Args.begin(), ST->getPointerOperand());
      CallInst::Create(newF, Args, CI->getName(), CI);
      ST->dropAllReferences();
      ST->removeFromParent();
      CI->dropAllReferences();
      CI->removeFromParent();
    }
  }
  F->dropAllReferences();
  F->removeFromParent();
  return true;
}

bool
SPRVToLLVM::postProcessOCLBuiltinWithFuncPointer(Function* F,
    Function::arg_iterator I) {
  auto Name = undecorateSPRVFunction(F->getName());
  mutateFunction (F, [=](CallInst *CI, std::vector<Value *> &Args) {
    auto ALoc = Args.begin();
    for (auto E = Args.end(); ALoc != E; ++ALoc) {
      if (isa<Function>(*ALoc)) {
        break;
      }
    }
    assert (ALoc != Args.end());
    Value *Ctx = nullptr;
    Value *CtxLen = nullptr;
    Value *CtxAlign = nullptr;
    if (Name == OCL_BUILTIN_ENQUEUE_KERNEL) {
      assert(Args.end() - ALoc > 3);
      Ctx = ALoc[1];
      CtxLen = ALoc[2];
      CtxAlign = ALoc[3];
      Args.erase(ALoc + 1, ALoc + 4);
    }
    *ALoc = addBlockBind(M, dyn_cast<Function>(*ALoc), Ctx, CtxLen, CtxAlign,
        CI);

    return Name;
  }, true);
  return true;
}

Value *
SPRVToLLVM::oclTransConstantSampler(SPRV::SPRVConstantSampler* BCS) {
  auto Lit = (BCS->getAddrMode() << 1) |
      BCS->getNormalized() |
      ((BCS->getFilterMode() + 1) << 4);
  auto Ty = IntegerType::getInt32Ty(*Context);
  return ConstantInt::get(Ty, Lit);
}

/// For instructions, this function assumes they are created in order
/// and appended to the given basic block. An instruction may use a
/// instruction from another BB which has not been translated. Such
/// instructions should be translated to place holders at the point
/// of first use, then replaced by real instructions when they are
/// created.
///
/// When CreatePlaceHolder is true, create a load instruction of a
/// global variable as placeholder for SPIRV instruction. Otherwise,
/// create instruction and replace placeholder if there is one.
Value *
SPRVToLLVM::transValueWithoutDecoration(SPRVValue *BV, Function *F,
    BasicBlock *BB, bool CreatePlaceHolder){
  // Translation of non-instruction values
  switch(BV->getOpCode()) {
  case SPRVOC_OpConstant: {
    SPRVConstant *BConst = static_cast<SPRVConstant *>(BV);
    SPRVType *BT = BV->getType();
    Type *LT = transType(BT);
    switch(BT->getOpCode()) {
    case SPRVOC_OpTypeBool:
    case SPRVOC_OpTypeInt:
      return mapValue(BV, ConstantInt::get(LT, BConst->getZExtIntValue(),
          static_cast<SPRVTypeInt*>(BT)->isSigned()));
    case SPRVOC_OpTypeFloat: {
      const llvm::fltSemantics *FS = nullptr;
      switch (BT->getFloatBitWidth()) {
      case 16:
        FS = &APFloat::IEEEhalf;
        break;
      case 32:
        FS = &APFloat::IEEEsingle;
        break;
      case 64:
        FS = &APFloat::IEEEdouble;
        break;
      default:
        assert (0 && "invalid float type");
      }
      return mapValue(BV, ConstantFP::get(*Context, APFloat(*FS,
          APInt(BT->getFloatBitWidth(), BConst->getZExtIntValue()))));
    }
    default:
      assert(0 && "Not implemented");
      return NULL;
    }
  }
  break;

  case SPRVOC_OpConstantTrue:
    return mapValue(BV, ConstantInt::getTrue(*Context));

  case SPRVOC_OpConstantFalse:
    return mapValue(BV, ConstantInt::getFalse(*Context));

  case SPRVOC_OpConstantNull: {
    auto LT = transType(BV->getType());
    if (auto PT = dyn_cast<PointerType>(LT))
      return mapValue(BV, ConstantPointerNull::get(PT));
    return mapValue(BV, ConstantAggregateZero::get(LT));
  }

  case SPRVOC_OpConstantComposite: {
    auto BCC = static_cast<SPRVConstantComposite*>(BV);
    std::vector<Constant *> CV;
    for (auto &I:BCC->getElements())
      CV.push_back(dyn_cast<Constant>(transValue(I, F, BB)));
    switch(BV->getType()->getOpCode()) {
    case SPRVOC_OpTypeVector:
      return mapValue(BV, ConstantVector::get(CV));
    case SPRVOC_OpTypeArray:
      return mapValue(BV, ConstantArray::get(
          dyn_cast<ArrayType>(transType(BCC->getType())), CV));
    default:
      assert(0 && "not implemented");
      return nullptr;
    }
  }
  break;

  case SPRVOC_OpConstantSampler: {
    auto BCS = static_cast<SPRVConstantSampler*>(BV);
    return mapValue(BV, oclTransConstantSampler(BCS));
  }

  case SPRVOC_OpUndef:
    return mapValue(BV, UndefValue::get(transType(BV->getType())));

  case SPRVOC_OpVariable: {
    auto BVar = static_cast<SPRVVariable *>(BV);
    auto Initializer = BVar->getInitializer();
    SPRVStorageClassKind BS = BVar->getStorageClass();
    auto Ty = transType(BVar->getType()->getPointerElementType());

    if (BS == SPRVSC_Function) {
        assert (BB && "Invalid BB");
        return mapValue(BV, new AllocaInst(Ty, BV->getName(), BB));
    }
    auto AddrSpace = SPIRSPRVAddrSpaceMap::rmap(BS);
    bool IsConst = BVar->isConstant();
    auto LVar = new GlobalVariable(*M, Ty, IsConst,
        SPIRSPRVLinkageTypeMap::rmap(BVar->getLinkageType()),
        Initializer?dyn_cast<Constant>(transValue(Initializer, F, BB, false)):
            nullptr,
        BV->getName(), 0, GlobalVariable::NotThreadLocal, AddrSpace);
    LVar->setUnnamedAddr(IsConst && Ty->isArrayTy() &&
        Ty->getArrayElementType()->isIntegerTy(8));
    SPRVBuiltinVariableKind BVKind = SPRVBI_Count;
    if (BVar->isBuiltin(&BVKind))
      BuiltinGVMap[LVar] = BVKind;
    return mapValue(BV, LVar);
  }
  break;

  case SPRVOC_OpVariableArray: {
    auto BVA = static_cast<SPRVVariableArray*>(BV);
    assert(BVA->getStorageClass() == SPRVSC_Function &&
        "Invalid Storage Class");
    return mapValue(BV, new AllocaInst(transType(
      BVA->getType()->getPointerElementType()),
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(
      *Context), BVA->getArraySize()),
      BVA->getName(), BB));
  }
  break;

  case SPRVOC_OpFunctionParameter: {
    auto BA = static_cast<SPRVFunctionParameter*>(BV);
    assert (F && "Invalid function");
    unsigned ArgNo = 0;
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E;
        ++I, ++ArgNo) {
      if (ArgNo == BA->getArgNo())
        return mapValue(BV, I);
    }
    assert (0 && "Invalid argument");
    return NULL;
  }
  break;

  case SPRVOC_OpFunction:
    return mapValue(BV, transFunction(static_cast<SPRVFunction *>(BV)));

  case SPRVOC_OpLabel:
    return mapValue(BV, BasicBlock::Create(*Context, BV->getName(), F));
    break;
  default:
    // do nothing
    break;
  }

  // Creation of place holder
  if (CreatePlaceHolder) {
    auto GV = new GlobalVariable(*M,
        transType(BV->getType()),
        false,
        GlobalValue::PrivateLinkage,
        nullptr,
        std::string(kPlaceholderPrefix) + BV->getName(),
        0, GlobalVariable::NotThreadLocal, 0);
    auto LD = new LoadInst(GV, BV->getName(), BB);
    PlaceholderMap[BV] = LD;
    return mapValue(BV, LD);
  }

  // Translation of instructions
  switch (BV->getOpCode()) {
  case SPRVOC_OpBranch: {
    auto BR = static_cast<SPRVBranch *>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, BranchInst::Create(
      dyn_cast<BasicBlock>(transValue(BR->getTargetLabel(), F, BB)), BB));
    }
    break;

  case SPRVOC_OpBranchConditional: {
    auto BR = static_cast<SPRVBranchConditional *>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, BranchInst::Create(
      dyn_cast<BasicBlock>(transValue(BR->getTrueLabel(), F, BB)),
      dyn_cast<BasicBlock>(transValue(BR->getFalseLabel(), F, BB)),
      transValue(BR->getCondition(), F, BB),
      BB));
    }
    break;

  case SPRVOC_OpPhi: {
    auto Phi = static_cast<SPRVPhi *>(BV);
    assert(BB && "Invalid BB");
    auto LPhi = dyn_cast<PHINode>(mapValue(BV, PHINode::Create(
      transType(Phi->getType()),
      Phi->getPairs().size() / 2,
      Phi->getName(),
      BB)));
    Phi->foreachPair([&](SPRVValue *IncomingV, SPRVBasicBlock *IncomingBB,
      size_t Index){
      auto Translated = transValue(IncomingV, F, BB);
      LPhi->addIncoming(Translated,
        dyn_cast<BasicBlock>(transValue(IncomingBB, F, BB)));
    });
    return LPhi;
    }
    break;

  case SPRVOC_OpReturn:
    assert(BB && "Invalid BB");
    return mapValue(BV, ReturnInst::Create(*Context, BB));
    break;

  case SPRVOC_OpReturnValue: {
    auto RV = static_cast<SPRVReturnValue *>(BV);
    return mapValue(BV, ReturnInst::Create(*Context,
      transValue(RV->getReturnValue(), F, BB), BB));
    }
    break;

  case SPRVOC_OpStore: {
    SPRVStore *BS = static_cast<SPRVStore*>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, new StoreInst(
      transValue(BS->getSrc(), F, BB),
      transValue(BS->getDst(), F, BB),
      BS->SPRVMemoryAccess::isVolatile(),
      BS->SPRVMemoryAccess::getAlignment(),
      BB));
    }
    break;

  case SPRVOC_OpLoad: {
    SPRVLoad *BL = static_cast<SPRVLoad*>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, new LoadInst(
      transValue(BL->getSrc(), F, BB),
      BV->getName(),
      BL->SPRVMemoryAccess::isVolatile(),
      BL->SPRVMemoryAccess::getAlignment(),
      BB));
    }
    break;

  case SPRVOC_OpCopyMemorySized: {
    SPRVCopyMemorySized *BC = static_cast<SPRVCopyMemorySized *>(BV);
    assert(BB && "Invalid BB");
    std::string FuncName = "llvm.memcpy";
    SPRVType* BS = BC->getSource()->getType();
    SPRVType* BT = BC->getTarget()->getType();
    Type *Int1Ty = Type::getInt1Ty(*Context);
    Type* Int32Ty = Type::getInt32Ty(*Context);
    Type* VoidTy = Type::getVoidTy(*Context);
    Type* SrcTy = transType(BS);
    Type* TrgTy = transType(BT);
    Type* SizeTy = transType(BC->getSize()->getType());
    Type* ArgTy[] = { TrgTy, SrcTy, SizeTy, Int32Ty, Int1Ty };

    if (BT->getPointerStorageClass() == SPRVSC_Private)
      FuncName += ".p0i8";
    else
      FuncName += ".p1i8";
    if (BS->getPointerStorageClass() == SPRVSC_Private)
      FuncName += ".p0i8";
    else
      FuncName += ".p1i8";
    if (BC->getSize()->getType()->getBitWidth() == 32)
      FuncName += ".i32";
    else
      FuncName += ".i64";

    FunctionType *FT = FunctionType::get(VoidTy, ArgTy, false);
    Function *Func = Function::Create(FT, GlobalValue::ExternalLinkage,
        FuncName, M);

    if (isFuncNoUnwind())
      Func->addFnAttr(Attribute::NoUnwind);

    Value *Arg[] = { transValue(BC->getTarget(), Func, BB),
                     transValue(BC->getSource(), Func, BB),
                     dyn_cast<llvm::ConstantInt>(transValue(BC->getSize(),
                         Func, BB)),
                     ConstantInt::get(Int32Ty,
                         BC->SPRVMemoryAccess::getAlignment()),
                     ConstantInt::get(Int1Ty,
                         BC->SPRVMemoryAccess::isVolatile())};
    return mapValue( BV, CallInst::Create(Func, Arg, "", BB));
  }
  break;
  case SPRVOC_OpSelect: {
    SPRVSelect *BS = static_cast<SPRVSelect*>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, SelectInst::Create(
      transValue(BS->getCondition(), F, BB),
      transValue(BS->getTrueValue(), F, BB),
      transValue(BS->getFalseValue(), F, BB),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpSwitch: {
    auto BS = static_cast<SPRVSwitch *>(BV);
    assert(BB && "Invalid BB");
    auto Select = transValue(BS->getSelect(), F, BB);
    auto LS = SwitchInst::Create(Select,
      dyn_cast<BasicBlock>(transValue(BS->getDefault(), F, BB)),
      BS->getNumPairs(), BB);
    BS->foreachPair([&](SPRVWord Literal, SPRVBasicBlock *Label, size_t Index){
      LS->addCase(ConstantInt::get(dyn_cast<IntegerType>(Select->getType()),
        Literal), dyn_cast<BasicBlock>(transValue(Label, F, BB)));
    });
    return mapValue(BV, LS);
    }
    break;

  case SPRVOC_OpAccessChain:
  case SPRVOC_OpInBoundsAccessChain: {
    SPRVAccessChain *AC = static_cast<SPRVAccessChain *>(BV);
    auto Base = transValue(AC->getBase(), F, BB);
    auto Index = transValue(AC->getIndices(), F, BB);
    auto IsInbound = BV->getOpCode() == SPRVOC_OpInBoundsAccessChain;
    Value *V = nullptr;
    if (BB) {
      auto GEP = GetElementPtrInst::Create(Base, Index, BV->getName(), BB);
      GEP->setIsInBounds(IsInbound);
      V = GEP;
    } else {
      V = ConstantExpr::getGetElementPtr(dyn_cast<Constant>(Base), Index,
          IsInbound);
    }
    return mapValue(BV, V);
    }
    break;

  case SPRVOC_OpCompositeExtract: {
    SPRVCompositeExtract *CE = static_cast<SPRVCompositeExtract *>(BV);
    assert(BB && "Invalid BB");
    assert(CE->getComposite()->getType()->isTypeVector() && "Invalid type");
    assert(CE->getIndices().size() == 1 && "Invalid index");
    return mapValue(BV, ExtractElementInst::Create(
      transValue(CE->getComposite(), F, BB),
      ConstantInt::get(*Context, APInt(32, CE->getIndices()[0])),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpVectorExtractDynamic: {
    auto CE = static_cast<SPRVVectorExtractDynamic *>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, ExtractElementInst::Create(
      transValue(CE->getVector(), F, BB),
      transValue(CE->getIndex(), F, BB),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpCompositeInsert: {
    auto CI = static_cast<SPRVCompositeInsert *>(BV);
    assert(BB && "Invalid BB");
    assert(CI->getComposite()->getType()->isTypeVector() && "Invalid type");
    assert(CI->getIndices().size() == 1 && "Invalid index");
    return mapValue(BV, InsertElementInst::Create(
      transValue(CI->getComposite(), F, BB),
      transValue(CI->getObject(), F, BB),
      ConstantInt::get(*Context, APInt(32, CI->getIndices()[0])),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpVectorInsertDynamic: {
    auto CI = static_cast<SPRVVectorInsertDynamic *>(BV);
    assert(BB && "Invalid BB");
    return mapValue(BV, InsertElementInst::Create(
      transValue(CI->getVector(), F, BB),
      transValue(CI->getComponent(), F, BB),
      transValue(CI->getIndex(), F, BB),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpVectorShuffle: {
    auto VS = static_cast<SPRVVectorShuffle *>(BV);
    assert(BB && "Invalid BB");
    std::vector<Constant *> Components;
    IntegerType *Int32Ty = IntegerType::get(*Context, 32);
    for (auto I : VS->getComponents()) {
      if (I == static_cast<SPRVWord>(-1))
        Components.push_back(UndefValue::get(Int32Ty));
      else
        Components.push_back(ConstantInt::get(Int32Ty, I));
    }
    return mapValue(BV, new ShuffleVectorInst(
      transValue(VS->getVector1(), F, BB),
      transValue(VS->getVector2(), F, BB),
      ConstantVector::get(Components),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpFunctionCall: {
    SPRVFunctionCall *BC = static_cast<SPRVFunctionCall *>(BV);
    assert(BB && "Invalid BB");
    auto Call = CallInst::Create(
      transFunction(BC->getFunction()),
      transValue(BC->getArgumentValues(), F, BB),
      BC->getName(),
      BB);
    setCallingConv(Call);
    setAttrByCalledFunc(Call);
    return mapValue(BV, Call);
    }
    break;

  case SPRVOC_OpExtInst:
    return mapValue(BV, transOCLBuiltinFromExtInst(
      static_cast<SPRVExtInst *>(BV), BB));
    break;

  case SPRVOC_OpControlBarrier:
  case SPRVOC_OpMemoryBarrier:
    return mapValue(BV, transOCLBarrierFence(
        static_cast<SPRVInstruction *>(BV), BB));

  case SPRVOC_OpSNegate: {
    SPRVUnary *BC = static_cast<SPRVUnary*>(BV);
    return mapValue(BV, BinaryOperator::CreateNSWNeg(
      transValue(BC->getOperand(), F, BB),
      BV->getName(), BB));
    }

  case SPRVOC_OpFNegate: {
    SPRVUnary *BC = static_cast<SPRVUnary*>(BV);
    return mapValue(BV, BinaryOperator::CreateFNeg(
      transValue(BC->getOperand(), F, BB),
      BV->getName(), BB));
    }
    break;

  case SPRVOC_OpNot: {
    SPRVUnary *BC = static_cast<SPRVUnary*>(BV);
    return mapValue(BV, BinaryOperator::CreateNot(
      transValue(BC->getOperand(), F, BB),
      BV->getName(), BB));
    }
    break;

  default:
  if (BV->isAtomic()) {
    return mapValue(BV, transOCLAtomic(
      static_cast<SPRVAtomicOperatorGeneric *>(BV), BB));
  } else if (isSPRVCmpInstTransToLLVMInst(static_cast<SPRVInstruction*>(BV))) {
    return mapValue(BV, transCmpInst(BV, BB, F));
  } else if (SPIRSPRVBuiltinInstMap::rfind(BV->getOpCode(), nullptr)) {
    return mapValue(BV, transOCLBuiltinFromInst(
        static_cast<SPRVInstruction *>(BV), BB));
  } else if (isBinaryShiftLogicalBitwiseOpCode(BV->getOpCode())) {
        return mapValue(BV, transShiftLogicalBitwiseInst(BV, BB, F));
  } else if (isCvtOpCode(BV->getOpCode())) {
      auto BI = static_cast<SPRVInstruction *>(BV);
      Value *Inst = nullptr;
      if (BI->hasFPRoundingMode() || BI->isSaturatedConversion())
        Inst = transOCLBuiltinFromInst(BI, BB);
      else
        Inst = transConvertInst(BV, F, BB);
      return mapValue(BV, Inst);
  }

  SPRVDBG(bildbgs() << "Cannot translate " << *BV << '\n';)
  assert(0 && "Translation of SPIRV instruction not implemented");
  return NULL;
  }
}

template<class SourceTy, class FuncTy>
bool
SPRVToLLVM::foreachFuncCtlMask(SourceTy Source, FuncTy Func) {
  SPRVWord FCM = Source->getFuncCtlMask();
  SPIRSPRVFuncCtlMaskMap::foreach([&](Attribute::AttrKind Attr,
      SPRVFunctionControlMaskKind Mask){
    if (FCM & Mask)
      Func(Attr);
  });
  return true;
}

Function *
SPRVToLLVM::transFunction(SPRVFunction *BF) {
  auto Loc = FuncMap.find(BF);
  if (Loc != FuncMap.end())
    return Loc->second;

  auto IsKernel = BM->isEntryPoint(SPRVEMDL_Kernel, BF->getId());
  auto Linkage = IsKernel ? GlobalValue::ExternalLinkage :
      SPIRSPRVLinkageTypeMap::rmap(BF->getLinkageType());
  FunctionType *FT = dyn_cast<FunctionType>(transType(BF->getFunctionType()));
  Function *F = dyn_cast<Function>(mapValue(BF, Function::Create(FT, Linkage,
      BF->getName(), M)));
  mapFunction(BF, F);
  if (!F->isIntrinsic()) {
    F->setCallingConv(IsKernel ? CallingConv::SPIR_KERNEL :
        CallingConv::SPIR_FUNC);
    if (isFuncNoUnwind())
      F->addFnAttr(Attribute::NoUnwind);
    foreachFuncCtlMask(BF, [&](Attribute::AttrKind Attr){
      F->addFnAttr(Attr);
    });
  }

  for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E;
      ++I) {
    auto BA = BF->getArgument(I->getArgNo());
    mapValue(BA, I);
    const std::string &ArgName = BA->getName();
    if (ArgName.empty())
      continue;
    I->setName(ArgName);
    BA->foreachAttr([&](SPRVFuncParamAttrKind Kind){
      if (Kind == SPRVFPA_Const)
        return;
      F->addAttribute(I->getArgNo() + 1, SPIRSPRVFuncParamAttrMap::rmap(Kind));
    });
  }
  BF->foreachReturnValueAttr([&](SPRVFuncParamAttrKind Kind){
    if (Kind == SPRVFPA_Const)
      return;
    F->addAttribute(AttributeSet::ReturnIndex,
        SPIRSPRVFuncParamAttrMap::rmap(Kind));
  });

  // Creating all basic blocks before creating instructions.
  for (size_t I = 0, E = BF->getNumBasicBlock(); I != E; ++I) {
    transValue(BF->getBasicBlock(I), F, nullptr);
  }

  for (size_t I = 0, E = BF->getNumBasicBlock(); I != E; ++I) {
    SPRVBasicBlock *BBB = BF->getBasicBlock(I);
    BasicBlock *BB = dyn_cast<BasicBlock>(transValue(BBB, F, nullptr));
    for (size_t BI = 0, BE = BBB->getNumInst(); BI != BE; ++BI) {
      SPRVInstruction *BInst = BBB->getInst(BI);
      transValue(BInst, F, BB, false);
    }
  }
  return F;
}

/// LLVM convert builtin functions is translated to two instructions:
/// y = i32 islessgreater(float x, float z) ->
///     y = i32 ZExt(bool LessGreater(float x, float z))
/// When translating back, for simplicity, a trunc instruction is inserted
/// w = bool LessGreater(float x, float z) ->
///     w = bool Trunc(i32 islessgreater(float x, float z))
/// Optimizer should be able to remove the redundant trunc/zext
void
SPRVToLLVM::transOCLBuiltinFromInstPreproc(SPRVInstruction* BI, Type *&RetTy,
    std::vector<Type *> &ArgTys) {
  if (!BI->hasType())
    return;
  auto BT = BI->getType();
  if (isCmpOpCode(BI->getOpCode())) {
    if (BT->isTypeBool())
      RetTy = IntegerType::getInt32Ty(*Context);
    else if (BT->isTypeVectorBool())
      RetTy = VectorType::get(IntegerType::getInt32Ty(*Context),
          BT->getVectorComponentCount());
    else
      assert(0 && "invalid compare instruction");
  }
}

Instruction*
SPRVToLLVM::transOCLBuiltinFromInstPostproc(SPRVInstruction* BI,
    Instruction* Inst, BasicBlock* BB) {
  if (isCmpOpCode(BI->getOpCode()) &&
      BI->getType()->isTypeVectorOrScalarBool()) {
    Inst = CastInst::Create(Instruction::Trunc, Inst, transType(BI->getType()),
        "cvt", BB);
  }
  return Inst;
}

Instruction *
SPRVToLLVM::transOCLBuiltinFromInst(const std::string& FuncName,
    SPRVInstruction* BI, BasicBlock* BB) {
  std::string MangledName;
  std::vector<Type*> ArgTys = transTypeVector(BI->getOperandTypes());
  bool HasFuncPtrArg = false;
  for (auto& I:ArgTys) {
    if (isa<FunctionType>(I)) {
      I = PointerType::get(I, SPIRAS_Private);
      HasFuncPtrArg = true;
    }
  }
  Type* RetTy = BI->hasType() ? transType(BI->getType()) :
      Type::getVoidTy(*Context);
  transOCLBuiltinFromInstPreproc(BI, RetTy, ArgTys);
  if (!HasFuncPtrArg)
  mangle(SPRVBIS_OpenCL20, FuncName, ArgTys, MangledName);
  else
    MangledName = decorateSPRVFunction(FuncName);
  Function* Func = M->getFunction(MangledName);
  FunctionType* FT = FunctionType::get(RetTy, ArgTys, false);
  if (!Func || Func->getFunctionType() != FT) {
    DEBUG(for (auto& I:ArgTys) {
      dbgs() << *I << '\n';
    });
    Func = Function::Create(FT, GlobalValue::ExternalLinkage, MangledName, M);
    Func->setCallingConv(CallingConv::SPIR_FUNC);
    if (isFuncNoUnwind())
      Func->addFnAttr(Attribute::NoUnwind);
  }
  auto Call = CallInst::Create(Func,
      transValue(BI->getOperands(), BB->getParent(), BB), "", BB);
  Call->setName(BI->getName());
  setAttrByCalledFunc(Call);
  SPRVDBG(bildbgs() << "[transInstToBuiltinCall] " << *BI << " -> "; dbgs() <<
      *Call << '\n';)
  Instruction *Inst = Call;
  Inst = transOCLBuiltinFromInstPostproc(BI, Inst, BB);
  return Inst;
}

std::string
SPRVToLLVM::getOCLBuiltinName(SPRVInstruction* BI) {
  auto OC = BI->getOpCode();
  if (isCvtOpCode(OC))
    return getOCLConvertBuiltinName(BI);
  if (OC == SPRVOC_OpBuildNDRange) {
    auto NDRangeInst = static_cast<SPRVBuildNDRange *>(BI);
    auto EleTy = ((NDRangeInst->getOperands())[0])->getType();
    int Dim = EleTy->isTypeVector() ? EleTy->getVectorComponentCount() : 1;
    // cygwin does not have std::to_string
    ostringstream OS;
    OS << Dim;
    return std::string(OCL_BUILTIN_NDRANGE_PREFIX) + OS.str() + "D";
  }
  return SPIRSPRVBuiltinInstMap::rmap(OC);
}

Instruction *
SPRVToLLVM::transOCLBuiltinFromInst(SPRVInstruction *BI, BasicBlock *BB) {
  assert(BB && "Invalid BB");
  auto FuncName = getOCLBuiltinName(BI);
  return transOCLBuiltinFromInst(FuncName, BI, BB);
}

bool
SPRVToLLVM::translate() {
  if (!transAddressingModel())
    return false;

  for (unsigned I = 0, E = BM->getNumVariables(); I != E; ++I) {
    auto BV = BM->getVariable(I);
    if (BV->getStorageClass() != SPRVSC_Function)
      transValue(BV, nullptr, nullptr);
  }

  for (unsigned I = 0, E = BM->getNumFunctions(); I != E; ++I) {
    transFunction(BM->getFunction(I));
  }
  if (!transKernelMetadata())
    return false;
  if (!transFPContractMetadata())
    return false;
  if (!transSourceLanguage())
    return false;
  if (!transSourceExtension())
    return false;
  if (!transCompilerOption())
    return false;
  if (!transOCLBuiltinsFromVariables())
    return false;
  if (!postProcessOCL())
    return false;
  return true;
}

bool
SPRVToLLVM::transAddressingModel() {
  switch (BM->getAddressingModel()) {
  case SPRVAM_Physical64:
    M->setTargetTriple(SPIR_TARGETTRIPLE64);
    M->setDataLayout(SPIR_DATALAYOUT64);
    break;
  case SPRVAM_Physical32:
    M->setTargetTriple(SPIR_TARGETTRIPLE32);
    M->setDataLayout(SPIR_DATALAYOUT32);
    break;
  case SPRVAM_Logical:
    // Do not set target triple and data layout
    break;
  default:
    SPRVCKRT(0, InvalidAddressingModel, "Actual addressing mode is " +
        (unsigned)BM->getAddressingModel());
  }
  return true;
}

bool
SPRVToLLVM::transDecoration(SPRVValue *BV, Value *V) {
  if (!transAlign(BV, V))
    return false;
  return true;
}

bool
SPRVToLLVM::transFPContractMetadata() {
  bool ContractOff = false;
  for (unsigned I = 0, E = BM->getNumFunctions(); I != E; ++I) {
    SPRVFunction *BF = BM->getFunction(I);
    if (!isOpenCLKernel(BF))
      continue;
    if (BF->getExecutionMode(SPRVEM_ContractionOff)) {
      ContractOff = true;
      break;
    }
  }
  if (!ContractOff)
    M->getOrInsertNamedMetadata(SPIR_MD_ENABLE_FP_CONTRACT);
  return true;
}

std::string SPRVToLLVM::transOCLImageTypeAccessQualifier(
    SPRV::SPRVTypeSampler* ST) {
  return SPIRSPRVAccessQualifierMap::rmap(ST->getAccessQualifier());
}

bool
SPRVToLLVM::transKernelMetadata() {
  NamedMDNode *KernelMDs = M->getOrInsertNamedMetadata(SPIR_MD_KERNELS);
  for (unsigned I = 0, E = BM->getNumFunctions(); I != E; ++I) {
    SPRVFunction *BF = BM->getFunction(I);
    Function *F = static_cast<Function *>(getTranslatedValue(BF));
    assert(F && "Invalid translated function");
    if (F->getCallingConv() != CallingConv::SPIR_KERNEL)
      continue;
    std::vector<llvm::Metadata*> KernelMD;
    KernelMD.push_back(ValueAsMetadata::get(F));
    // Generate metadata for kernel_arg_address_spaces
    addOCLKernelArgumentMetadata(Context, KernelMD,
        SPIR_MD_KERNEL_ARG_ADDR_SPACE, BF,
        [=](SPRVFunctionParameter *Arg){
      SPRVType *ArgTy = Arg->getType();
      SPIRAddressSpace AS = SPIRAS_Private;
      if (ArgTy->isTypePointer())
        AS = SPIRSPRVAddrSpaceMap::rmap(ArgTy->getPointerStorageClass());
      else if (ArgTy->isTypeOCLImage() || ArgTy->isTypePipe())
        AS = SPIRAS_Global;
      return ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(*Context), AS));
    });
    // Generate metadata for kernel_arg_access_qual
    addOCLKernelArgumentMetadata(Context, KernelMD,
        SPIR_MD_KERNEL_ARG_ACCESS_QUAL, BF,
        [=](SPRVFunctionParameter *Arg){
      std::string Qual;
      if (!Arg->getType()->isTypeOCLImage())
        Qual = "none";
      else {
        auto ST = static_cast<SPRVTypeSampler *>(Arg->getType());
        Qual = transOCLImageTypeAccessQualifier(ST);
      }
      return MDString::get(*Context, Qual);
    });
    // Generate metadata for kernel_arg_type
    addOCLKernelArgumentMetadata(Context, KernelMD,
        SPIR_MD_KERNEL_ARG_TYPE, BF,
        [=](SPRVFunctionParameter *Arg){
      return transOCLKernelArgTypeName(Arg);
    });
    // Generate metadata for kernel_arg_type_qual
    addOCLKernelArgumentMetadata(Context, KernelMD,
        SPIR_MD_KERNEL_ARG_TYPE_QUAL, BF,
        [=](SPRVFunctionParameter *Arg){
      std::string Qual;
      if (Arg->hasDecorate(SPRVDEC_Volatile))
        Qual = "volatile";
      Arg->foreachAttr([&](SPRVFuncParamAttrKind Kind){
        Qual += Qual.empty() ? "" : " ";
        switch(Kind){
        case SPRVFPA_NoAlias:
          Qual += "restrict";
          break;
        case SPRVFPA_Const:
          Qual += "const";
          break;
        default:
          // do nothing.
          break;
        }
      });
      return MDString::get(*Context, Qual);
    });
    // Generate metadata for kernel_arg_base_type
    addOCLKernelArgumentMetadata(Context, KernelMD,
        SPIR_MD_KERNEL_ARG_BASE_TYPE, BF,
        [=](SPRVFunctionParameter *Arg){
      return transOCLKernelArgTypeName(Arg);
    });
    // Generate metadata for kernel_arg_name
    if (BM->getCompileFlag().find("-cl-kernel-arg-info") !=
        std::string::npos) {
      bool ArgHasName = true;
      BF->foreachArgument([&](SPRVFunctionParameter *Arg){
        ArgHasName &= !Arg->getName().empty();
      });
      if (ArgHasName)
        addOCLKernelArgumentMetadata(Context, KernelMD,
            SPIR_MD_KERNEL_ARG_NAME, BF,
            [=](SPRVFunctionParameter *Arg){
          return MDString::get(*Context, Arg->getName());
        });
    }
    // Generate metadata for reqd_work_group_size
    if (auto EM = BF->getExecutionMode(SPRVEM_LocalSize)) {
      KernelMD.push_back(getMDNodeStringIntVec(Context,
          SPIR_MD_REQD_WORK_GROUP_SIZE, EM->getLiterals()));
    }
    // Generate metadata for work_group_size_hint
    if (auto EM = BF->getExecutionMode(SPRVEM_LocalSizeHint)) {
      KernelMD.push_back(getMDNodeStringIntVec(Context,
          SPIR_MD_WORK_GROUP_SIZE_HINT, EM->getLiterals()));
    }
    // Generate metadata for vec_type_hint
    if (auto EM = BF->getExecutionMode(SPRVEM_VecTypeHint)) {
      std::vector<Metadata*> ValueVec;
      ValueVec.push_back(MDString::get(*Context, SPIR_MD_VEC_TYPE_HINT));
      Type *VecHintTy = transType(BM->get<SPRVType>(EM->getLiterals()[0]));
      ValueVec.push_back(ValueAsMetadata::get(UndefValue::get(VecHintTy)));
      ValueVec.push_back(ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(*Context),
          VecHintTy->isIntegerTy() && EM->getStringLiteral()[0] != 'u'?1:0)));
      KernelMD.push_back(MDNode::get(*Context, ValueVec));
    }

    llvm::MDNode *Node = MDNode::get(*Context, KernelMD);
    KernelMDs->addOperand(Node);
  }
  return true;
}

bool
SPRVToLLVM::transAlign(SPRVValue *BV, Value *V) {
  if (auto AL = dyn_cast<AllocaInst>(V)) {
    SPRVWord Align = 0;
    if (BV->hasAlignment(&Align))
      AL->setAlignment(Align);
    return true;
  }
  if (auto GV = dyn_cast<GlobalVariable>(V)) {
    SPRVWord Align = 0;
    if (BV->hasAlignment(&Align))
      GV->setAlignment(Align);
    return true;
  }
  return true;
}

Instruction *
SPRVToLLVM::transOCLAtomic(SPRVAtomicOperatorGeneric *BA, BasicBlock *BB) {
  assert(BB && "Invalid BB");
  std::string FuncName = SPIRSPRVBuiltinInstMap::rmap(BA->getOpCode());
  std::string MangledName;
  std::vector<Type *> ArgTys = transTypeVector(BA->getOperandTypes());
  Type * RetTy = ArgTys[0]->getPointerElementType();
  mangle(SPRVBIS_OpenCL20, FuncName, ArgTys, MangledName);
  Function *Func = M->getFunction(MangledName);
  if (!Func) {
    FunctionType *FT = FunctionType::get(RetTy, ArgTys, false);
    Func = Function::Create(FT, GlobalValue::ExternalLinkage, MangledName, M);
    Func->setCallingConv(CallingConv::SPIR_FUNC);
    if (isFuncNoUnwind())
      Func->addFnAttr(Attribute::NoUnwind);
  }
  auto Call = CallInst::Create(Func, transValue(BA->getOperands(),
      BB->getParent(), BB), "", BB);
  Call->setName(BA->getName());
  setAttrByCalledFunc(Call);
  SPRVDBG(bildbgs() << "[transAtomic] " << *BA << " -> ";
    dbgs() << *Call << '\n';)
  return Call;
}

void
SPRVToLLVM::transOCLVectorLoadStore(std::string& UnmangledName,
    std::vector<SPRVWord> &BArgs) {
  if (UnmangledName.find("vload") == 0 &&
      UnmangledName.find("n") != std::string::npos) {
    if (BArgs.back() != 1) {
      std::stringstream SS;
      SS << BArgs.back();
      UnmangledName.replace(UnmangledName.find("n"), 1, SS.str());
    } else {
      UnmangledName.erase(UnmangledName.find("n"), 1);
    }
    BArgs.pop_back();
  } else if (UnmangledName.find("vstore") == 0) {
    if (UnmangledName.find("n") != std::string::npos) {
      auto T = BM->getValueType(BArgs[0]);
      if (T->isTypeVector()) {
        auto W = T->getVectorComponentCount();
        std::stringstream SS;
        SS << W;
        UnmangledName.replace(UnmangledName.find("n"), 1, SS.str());
      } else {
        UnmangledName.erase(UnmangledName.find("n"), 1);
      }
    }
    if (UnmangledName.find("_r") != std::string::npos) {
      UnmangledName.replace(UnmangledName.find("_r"), 2, std::string("_") +
          SPIRSPRVFPRoundingModeMap::rmap(static_cast<SPRVFPRoundingModeKind>(
              BArgs.back())));
      BArgs.pop_back();
    }
   }
}

// printf is not mangled. The function type should have just one argument.
// read_image*: the second argument should be mangled as sampler.
Instruction *
SPRVToLLVM::transOCLBuiltinFromExtInst(SPRVExtInst *BC, BasicBlock *BB) {
  assert(BB && "Invalid BB");
  std::string MangledName;
  SPRVWord EntryPoint = BC->getEntryPoint();
  SPRVExtInstSetKind Set = BM->getBuiltinSet(BC->getBuiltinSet());
  bool IsVarArg = false;
  bool IsPrintf = false;
  std::string UnmangledName;
  auto BArgs = BC->getArguments();

  if (Set == SPRVBIS_OpenCL12) {
    if (EntryPoint == OCL12_Printf)
      IsPrintf = true;
    else {
      UnmangledName = OCL12Map::map(static_cast<SPRVBuiltinOCL12Kind>(
          EntryPoint));
    }
  } else if (Set == SPRVBIS_OpenCL20) {
    if (EntryPoint == OCL20_Printf)
      IsPrintf = true;
    else {
      UnmangledName = OCL20Map::map(static_cast<SPRVBuiltinOCL20Kind>(
          EntryPoint));
    }
  } else if (Set == SPRVBIS_OpenCL21) {
    if (EntryPoint == OCL21_Printf)
      IsPrintf = true;
    else {
      UnmangledName =
          OCL21Map::map(static_cast<SPRVBuiltinOCL21Kind>(EntryPoint));
    }
  }

  SPRVDBG(bildbgs() << "[transOCLBuiltinFromExtInst] OrigUnmangledName: " <<
      UnmangledName << '\n');
  transOCLVectorLoadStore(UnmangledName, BArgs);

  std::vector<Type *> ArgTypes = transTypeVector(BC->getValueTypes(BArgs));

  if (IsPrintf) {
    MangledName = "printf";
    IsVarArg = true;
    ArgTypes.resize(1);
  } else if (UnmangledName.find("read_image") == 0) {
    auto ModifiedArgTypes = ArgTypes;
    ModifiedArgTypes[1] = getOrCreateOpaquePtrType(M, "opencl.sampler_t");
    mangle(Set, UnmangledName, ModifiedArgTypes, MangledName);
  } else {
    mangle(Set, UnmangledName, ArgTypes, MangledName);
  }
  SPRVDBG(bildbgs() << "[transOCLBuiltinFromExtInst] ModifiedUnmangledName: " <<
      UnmangledName << " MangledName: " << MangledName << '\n');

  FunctionType *FT = FunctionType::get(
      transType(BC->getType()),
      ArgTypes,
      IsVarArg);
  Function *F = M->getFunction(MangledName);
  if (!F) {
    F = Function::Create(FT,
      GlobalValue::ExternalLinkage,
      MangledName,
      M);
    F->setCallingConv(CallingConv::SPIR_FUNC);
    if (isFuncNoUnwind())
      F->addFnAttr(Attribute::NoUnwind);
  }
  auto Args = transValue(BC->getValues(BArgs), F, BB);
  SPRVDBG(dbgs() << "[transOCLBuiltinFromExtInst] Function: " << *F <<
      ", Args: ";
    for (auto &I:Args) dbgs() << *I << ", "; dbgs() << '\n');
  CallInst *Call = CallInst::Create(F,
      Args,
      BC->getName(),
      BB);
  setCallingConv(Call);
  addFnAttr(Context, Call, Attribute::NoUnwind);
  return Call;
}

Instruction *
SPRVToLLVM::transOCLBarrierFence(SPRVInstruction* MB, BasicBlock *BB) {
  assert(BB && "Invalid BB");
  std::string FuncName;
  SPRVWord MemSema = 0;
  if (MB->getOpCode() == SPRVOC_OpMemoryBarrier) {
    auto MemB = static_cast<SPRVMemoryBarrier*>(MB);
    FuncName = "mem_fence";
    MemSema = MemB->getMemSemantic();
  } else if (MB->getOpCode() == SPRVOC_OpControlBarrier) {
    auto CtlB = static_cast<SPRVControlBarrier*>(MB);
    FuncName = "barrier";
    MemSema = CtlB->getMemSemantic();
  } else {
    assert(0 && "Invalid instruction");
  }
  std::string MangledName;
  Type* Int32Ty = Type::getInt32Ty(*Context);
  Type* VoidTy = Type::getVoidTy(*Context);
  Type* ArgTy[] = {Int32Ty};
  mangle(SPRVBIS_OpenCL20, FuncName, ArgTy, MangledName);
  Function *Func = M->getFunction(MangledName);
  if (!Func) {
    FunctionType *FT = FunctionType::get(VoidTy, ArgTy, false);
    Func = Function::Create(FT, GlobalValue::ExternalLinkage, MangledName, M);
    Func->setCallingConv(CallingConv::SPIR_FUNC);
    if (isFuncNoUnwind())
      Func->addFnAttr(Attribute::NoUnwind);
  }
  Value *Arg[] = {ConstantInt::get(Int32Ty,
      rmapBitMask<SPIRSPRVMemFenceFlagMap>(MemSema))};
  auto Call = CallInst::Create(Func, Arg, "", BB);
  Call->setName(MB->getName());
  setAttrByCalledFunc(Call);
  SPRVDBG(bildbgs() << "[transBarrier] " << *MB << " -> ";
    dbgs() << *Call << '\n';)
  return Call;
}

// SPIR-V only contains language version. Use OpenCL language version as
// SPIR version.
bool
SPRVToLLVM::transSourceLanguage() {
  SPRVWord Ver = 0;
  SPRVSourceLanguageKind Lang = BM->getSourceLanguage(&Ver);
  assert(Lang == SPRVSL_OpenCL && "Unsupported source language");
  unsigned Major = Ver/10;
  unsigned Minor = (Ver%10);
  addOCLVersionMetadata(Context, M, SPIR_MD_SPIR_VERSION, Major, Minor);
  addOCLVersionMetadata(Context, M, SPIR_MD_OCL_VERSION, Major, Minor);
  return true;
}

bool
SPRVToLLVM::transSourceExtension() {
  std::string OCLExtensions = BM->getSourceExtension();
  std::string OCLOptionalCoreFeatures;
  bool First = true;
  static const char *OCLOptCoreFeatureNames[] = {
      "cl_images",
      "cl_doubles",
  };
  for (auto &I:OCLOptCoreFeatureNames) {
    size_t Loc = OCLExtensions.find(I);
    if (Loc != std::string::npos) {
      OCLExtensions.erase(Loc, strlen(I));
      if (First)
        First = false;
      else
        OCLOptionalCoreFeatures += ' ';
      OCLOptionalCoreFeatures += I;
    }
  }
  addNamedMetadataString(Context, M, SPIR_MD_USED_EXTENSIONS, OCLExtensions);
  addNamedMetadataString(Context, M, SPIR_MD_USED_OPTIONAL_CORE_FEATURES,
      OCLOptionalCoreFeatures);
  return true;
}

bool
SPRVToLLVM::transCompilerOption() {
  addNamedMetadataString(Context, M, SPIR_MD_COMPILER_OPTIONS,
      BM->getCompileFlag());
  return true;
}

// If the argument is unsigned return uconvert*, otherwise return convert*.
std::string
SPRVToLLVM::getOCLConvertBuiltinName(SPRVInstruction* BI) {
  auto OC = BI->getOpCode();
  assert(isCvtOpCode(OC) && "Not convert instruction");
  auto U = static_cast<SPRVUnary *>(BI);
  std::string Name;
  if (isCvtFromUnsignedOpCode(OC))
    Name = "u";
  Name += "convert_";
  Name += mapSPRVTypeToOpenCLType(U->getType(),
      !isCvtToUnsignedOpCode(OC));
  SPRVFPRoundingModeKind Rounding = SPRVFRM_Count;
  if (U->isSaturatedConversion())
    Name += "_sat";
  if (U->hasFPRoundingMode(&Rounding)) {
    Name += "_";
    Name += SPIRSPRVFPRoundingModeMap::rmap(Rounding);
  }
  return Name;
}

}

bool
llvm::ReadSPRV(LLVMContext &C, std::istream &IS, Module *&M,
    std::string &ErrMsg) {
  M = new Module("", C);
  std::unique_ptr<SPRVModule> BM(SPRVModule::createSPRVModule());

  IS >> *BM;

  SPRVToLLVM BTL(M, BM.get());
  bool Succeed = true;
  if (!BTL.translate()) {
    BM->getError(ErrMsg);
    Succeed = false;
  }
  if (DbgSaveTmpLLVM)
    dumpLLVM(M, DbgTmpLLVMFileName);
  if (!Succeed) {
    delete M;
    M = nullptr;
  }
  return Succeed;
}

