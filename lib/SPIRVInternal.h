//===- LLVMSPIRVInternal.h -  SPIR-V internal header file --------*- C++ -*-===//
//
//                     The LLVM/SPIRV Translator
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
/// This file declares classes and functions shared by SPIR-V reader/writer.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMSPIRVINTERNAL_HPP_
#define LLVMSPIRVINTERNAL_HPP_

#include "libSPIRV/SPIRVUtil.h"
#include "libSPIRV/SPIRVEnum.h"
#include "libSPIRV/SPIRVNameMapEnum.h"
#include "libSPIRV/SPIRVError.h"
#include "libSPIRV/SPIRVType.h"
#include "Mangler/NameMangleAPI.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "SPIRV.h"

#include <utility>
#include <functional>

using namespace SPIRV;
using namespace llvm;

namespace SPIRV{

  /// The LLVM/SPIR-V translator version used to fill the lower 16 bits of the
  /// generator's magic number in the generated SPIR-V module.
  /// This number should be bumped up whenever the generated SPIR-V changes.
  const static unsigned short kTranslatorVer = 14;

#define SPCV_TARGET_LLVM_IMAGE_TYPE_ENCODE_ACCESS_QUAL 0
// Workaround for SPIR 2 producer bug about kernel function calling convention.
// This workaround checks metadata to determine if a function is kernel.
#define SPCV_RELAX_KERNEL_CALLING_CONV 1

class SPIRVOpaqueType;
typedef SPIRVMap<std::string, Op, SPIRVOpaqueType>
  SPIRVOpaqueTypeOpCodeMap;

// Ad hoc function used by LLVM/SPIRV converter for type casting
#define SPCV_CAST "spcv.cast"
#define LLVM_MEMCPY "llvm.memcpy"

namespace kOCLTypeQualifierName {
  const static char *Const      = "const";
  const static char *Volatile   = "volatile";
  const static char *Restrict   = "restrict";
  const static char *Pipe       = "pipe";
}

template<> inline void
SPIRVMap<unsigned, Op>::init() {
#define _SPIRV_OP(x,y) add(Instruction::x, Op##y);
  /* Casts */
    _SPIRV_OP(ZExt, UConvert)
    _SPIRV_OP(SExt, SConvert)
    _SPIRV_OP(Trunc, UConvert)
    _SPIRV_OP(FPToUI, ConvertFToU)
    _SPIRV_OP(FPToSI, ConvertFToS)
    _SPIRV_OP(UIToFP, ConvertUToF)
    _SPIRV_OP(SIToFP, ConvertSToF)
    _SPIRV_OP(FPTrunc, FConvert)
    _SPIRV_OP(FPExt, FConvert)
    _SPIRV_OP(PtrToInt, ConvertPtrToU)
    _SPIRV_OP(IntToPtr, ConvertUToPtr)
    _SPIRV_OP(BitCast, Bitcast)
    _SPIRV_OP(AddrSpaceCast, GenericCastToPtr)
    _SPIRV_OP(GetElementPtr, AccessChain)
  /*Binary*/
    _SPIRV_OP(And, BitwiseAnd)
    _SPIRV_OP(Or, BitwiseOr)
    _SPIRV_OP(Xor, BitwiseXor)
    _SPIRV_OP(Add, IAdd)
    _SPIRV_OP(FAdd, FAdd)
    _SPIRV_OP(Sub, ISub)
    _SPIRV_OP(FSub, FSub)
    _SPIRV_OP(Mul, IMul)
    _SPIRV_OP(FMul, FMul)
    _SPIRV_OP(UDiv, UDiv)
    _SPIRV_OP(SDiv, SDiv)
    _SPIRV_OP(FDiv, FDiv)
    _SPIRV_OP(SRem, SRem)
    _SPIRV_OP(FRem, FRem)
    _SPIRV_OP(URem, UMod)
    _SPIRV_OP(Shl, ShiftLeftLogical)
    _SPIRV_OP(LShr, ShiftRightLogical)
    _SPIRV_OP(AShr, ShiftRightArithmetic)
#undef _SPIRV_OP
}
typedef SPIRVMap<unsigned, Op> OpCodeMap;

template<> inline void
SPIRVMap<CmpInst::Predicate, Op>::init() {
#define _SPIRV_OP(x,y) add(CmpInst::x, Op##y);
    _SPIRV_OP(FCMP_OEQ, FOrdEqual)
    _SPIRV_OP(FCMP_OGT, FOrdGreaterThan)
    _SPIRV_OP(FCMP_OGE, FOrdGreaterThanEqual)
    _SPIRV_OP(FCMP_OLT, FOrdLessThan)
    _SPIRV_OP(FCMP_OLE, FOrdLessThanEqual)
    _SPIRV_OP(FCMP_ONE, FOrdNotEqual)
    _SPIRV_OP(FCMP_ORD, Ordered)
    _SPIRV_OP(FCMP_UNO, Unordered)
    _SPIRV_OP(FCMP_UEQ, FUnordEqual)
    _SPIRV_OP(FCMP_UGT, FUnordGreaterThan)
    _SPIRV_OP(FCMP_UGE, FUnordGreaterThanEqual)
    _SPIRV_OP(FCMP_ULT, FUnordLessThan)
    _SPIRV_OP(FCMP_ULE, FUnordLessThanEqual)
    _SPIRV_OP(FCMP_UNE, FUnordNotEqual)
    _SPIRV_OP(ICMP_EQ, IEqual)
    _SPIRV_OP(ICMP_NE, INotEqual)
    _SPIRV_OP(ICMP_UGT, UGreaterThan)
    _SPIRV_OP(ICMP_UGE, UGreaterThanEqual)
    _SPIRV_OP(ICMP_ULT, ULessThan)
    _SPIRV_OP(ICMP_ULE, ULessThanEqual)
    _SPIRV_OP(ICMP_SGT, SGreaterThan)
    _SPIRV_OP(ICMP_SGE, SGreaterThanEqual)
    _SPIRV_OP(ICMP_SLT, SLessThan)
    _SPIRV_OP(ICMP_SLE, SLessThanEqual)
#undef _SPIRV_OP
}
typedef SPIRVMap<CmpInst::Predicate, Op> CmpMap;

class IntBoolOpMapId;
template<> inline void
SPIRVMap<Op, Op, IntBoolOpMapId>::init() {
  add(OpNot,         OpLogicalNot);
  add(OpBitwiseAnd,  OpLogicalAnd);
  add(OpBitwiseOr,   OpLogicalOr);
  add(OpBitwiseXor,  OpLogicalNotEqual);
  add(OpIEqual,      OpLogicalEqual);
  add(OpINotEqual,   OpLogicalNotEqual);
}
typedef SPIRVMap<Op, Op, IntBoolOpMapId> IntBoolOpMap;

#define SPIR_TARGETTRIPLE32 "spir-unknown-unknown"
#define SPIR_TARGETTRIPLE64 "spir64-unknown-unknown"
#define SPIR_DATALAYOUT32 "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32"\
                          "-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32"\
                          "-v32:32:32-v48:64:64-v64:64:64-v96:128:128"\
                          "-v128:128:128-v192:256:256-v256:256:256"\
                          "-v512:512:512-v1024:1024:1024"
#define SPIR_DATALAYOUT64 "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32"\
                          "-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32"\
                          "-v32:32:32-v48:64:64-v64:64:64-v96:128:128"\
                          "-v128:128:128-v192:256:256-v256:256:256"\
                          "-v512:512:512-v1024:1024:1024"

enum SPIRAddressSpace {
  SPIRAS_Private,
  SPIRAS_Global,
  SPIRAS_Constant,
  SPIRAS_Local,
  SPIRAS_Generic,
  SPIRAS_Count,
};

template<>inline void
SPIRVMap<SPIRAddressSpace, std::string>::init() {
  add(SPIRAS_Private, "Private");
  add(SPIRAS_Global, "Global");
  add(SPIRAS_Constant, "Constant");
  add(SPIRAS_Local, "Local");
  add(SPIRAS_Generic, "Generic");
}
typedef SPIRVMap<SPIRAddressSpace, SPIRVStorageClassKind>
  SPIRAddrSpaceCapitalizedNameMap;

template<> inline void
SPIRVMap<SPIRAddressSpace, SPIRVStorageClassKind>::init() {
  add(SPIRAS_Private, StorageClassFunction);
  add(SPIRAS_Global, StorageClassCrossWorkgroup);
  add(SPIRAS_Constant, StorageClassUniformConstant);
  add(SPIRAS_Local, StorageClassWorkgroup);
  add(SPIRAS_Generic, StorageClassGeneric);
}
typedef SPIRVMap<SPIRAddressSpace, SPIRVStorageClassKind> SPIRSPIRVAddrSpaceMap;

// Maps OCL builtin function to SPIRV builtin variable.
template<> inline void
SPIRVMap<std::string, SPIRVAccessQualifierKind>::init() {
  add("read_only", AccessQualifierReadOnly);
  add("write_only", AccessQualifierWriteOnly);
  add("read_write", AccessQualifierReadWrite);
}
typedef SPIRVMap<std::string, SPIRVAccessQualifierKind> SPIRSPIRVAccessQualifierMap;

template<> inline void
SPIRVMap<Attribute::AttrKind, SPIRVFuncParamAttrKind>::init() {
  add(Attribute::ZExt, FunctionParameterAttributeZext);
  add(Attribute::SExt, FunctionParameterAttributeSext);
  add(Attribute::ByVal, FunctionParameterAttributeByVal);
  add(Attribute::StructRet, FunctionParameterAttributeSret);
  add(Attribute::NoAlias, FunctionParameterAttributeNoAlias);
  add(Attribute::NoCapture, FunctionParameterAttributeNoCapture);
}
typedef SPIRVMap<Attribute::AttrKind, SPIRVFuncParamAttrKind>
  SPIRSPIRVFuncParamAttrMap;

template<> inline void
SPIRVMap<Attribute::AttrKind, SPIRVFunctionControlMaskKind>::init() {
  add(Attribute::ReadNone, FunctionControlPureMask);
  add(Attribute::ReadOnly, FunctionControlConstMask);
  add(Attribute::AlwaysInline, FunctionControlInlineMask);
  add(Attribute::NoInline, FunctionControlDontInlineMask);
}
typedef SPIRVMap<Attribute::AttrKind, SPIRVFunctionControlMaskKind>
  SPIRSPIRVFuncCtlMaskMap;

class SPIRVExtSetShortName;
template<> inline void
SPIRVMap<SPIRVExtInstSetKind, std::string, SPIRVExtSetShortName>::init() {
  add(SPIRVEIS_OpenCL, "ocl");
}
typedef SPIRVMap<SPIRVExtInstSetKind, std::string, SPIRVExtSetShortName>
  SPIRVExtSetShortNameMap;

#define SPIR_MD_KERNELS                     "opencl.kernels"
#define SPIR_MD_COMPILER_OPTIONS            "opencl.compiler.options"
#define SPIR_MD_KERNEL_ARG_ADDR_SPACE       "kernel_arg_addr_space"
#define SPIR_MD_KERNEL_ARG_ACCESS_QUAL      "kernel_arg_access_qual"
#define SPIR_MD_KERNEL_ARG_TYPE             "kernel_arg_type"
#define SPIR_MD_KERNEL_ARG_BASE_TYPE        "kernel_arg_base_type"
#define SPIR_MD_KERNEL_ARG_TYPE_QUAL        "kernel_arg_type_qual"
#define SPIR_MD_KERNEL_ARG_NAME             "kernel_arg_name"

#define OCL_TYPE_NAME_SAMPLER_T             "sampler_t"
#define SPIR_TYPE_NAME_EVENT_T              "opencl.event_t"
#define SPIR_TYPE_NAME_CLK_EVENT_T          "opencl.clk_event_t"
#define SPIR_TYPE_NAME_BLOCK_T              "opencl.block"
#define SPIR_INTRINSIC_BLOCK_BIND           "spir_block_bind"
#define SPIR_INTRINSIC_GET_BLOCK_INVOKE     "spir_get_block_invoke"
#define SPIR_INTRINSIC_GET_BLOCK_CONTEXT    "spir_get_block_context"
#define SPIR_TEMP_NAME_PREFIX_BLOCK         "block"
#define SPIR_TEMP_NAME_PREFIX_CALL          "call"

namespace kLLVMTypeName {
  const static char StructPrefix[] = "struct.";
}

namespace kSPIRVImageSampledTypeName {
  const static char Float[] = "float";
  const static char Half[]  = "half";
  const static char Int[]   = "int";
  const static char UInt[]  = "uint";
  const static char Void[]  = "void";
}

namespace kSPIRVTypeName {
  const static char Delimiter             = '.';
  const static char DeviceEvent[]         = "DeviceEvent";
  const static char Event[]               = "Event";
  const static char Image[]               = "Image";
  const static char Pipe[]                = "Pipe";
  const static char PostfixDelim          = '_';
  const static char Prefix[]              = "spirv";
  const static char PrefixAndDelim[]      = "spirv.";
  const static char Queue[]               = "Queue";
  const static char ReserveId[]           = "ReserveId";
  const static char SampledImg[]          = "SampledImage";
  const static char Sampler[]             = "Sampler";
  const static char ConstantSampler[]     = "ConstantSampler";
  const static char PipeStorage[]         = "PipeStorage";
  const static char ConstantPipeStorage[] = "ConstantPipeStorage";
}

namespace kSPR2TypeName {
  const static char Delimiter   = '.';
  const static char OCLPrefix[]   = "opencl.";
  const static char ImagePrefix[] = "opencl.image";
  const static char Pipe[]        = "opencl.pipe_t";
  const static char Sampler[]     = "opencl.sampler_t";
  const static char Event[]       = "opencl.event_t";
}

namespace kAccessQualName {
  const static char ReadOnly[]    = "read_only";
  const static char WriteOnly[]   = "write_only";
  const static char ReadWrite[]   = "read_write";
}

namespace kMangledName {
  const static char Sampler[]               = "11ocl_sampler";
  const static char AtomicPrefixIncoming[]  = "U7_Atomic";
  const static char AtomicPrefixInternal[]  = "atomic_";
}

namespace kSPIRVName {
  const static char GroupPrefix[]            = "group_";
  const static char Prefix[]                 = "__spirv_";
  const static char Postfix[]                = "__";
  const static char ImageQuerySize[]         = "ImageQuerySize";
  const static char ImageQuerySizeLod[]      = "ImageQuerySizeLod";
  const static char ImageSampleExplicitLod[] = "ImageSampleExplicitLod";
  const static char ReservedPrefix[]         = "reserved_";
  const static char SampledImage[]           = "SampledImage";
  const static char TempSampledImage[]       = "TempSampledImage";
}

namespace kSPIRVPostfix {
  const static char Sat[]       = "sat";
  const static char Rtz[]       = "rtz";
  const static char Rte[]       = "rte";
  const static char Rtp[]       = "rtp";
  const static char Rtn[]       = "rtn";
  const static char Rt[]        = "rt";
  const static char Return[]    = "R";
  const static char Divider[]   = "_";
  /// Divider between extended instruction name and postfix
  const static char ExtDivider[] = "__";
}

namespace kSPIRVMD {
  const static char Capability[]        = "spirv.Capability";
  const static char EntryPoint[]        = "spirv.EntryPoint";
  const static char ExecutionMode[]     = "spirv.ExecutionMode";
  const static char Extension[]         = "spirv.Extension";
  const static char Generator[]         = "spirv.Generator";
  const static char Source[]            = "spirv.Source";
  const static char SourceExtension[]   = "spirv.SourceExtension";
  const static char MemoryModel[]       = "spirv.MemoryModel";
}

namespace kSPIR2MD {
  const static char Extensions[]        = "opencl.used.extensions";
  const static char FPContract[]        = "opencl.enable.FP_CONTRACT";
  const static char OCLVer[]            = "opencl.ocl.version";
  const static char OptFeatures[]       = "opencl.used.optional.core.features";
  const static char SPIRVer[]           = "opencl.spir.version";
  const static char VecTyHint[]         = "vec_type_hint";
  const static char WGSize[]            = "reqd_work_group_size";
  const static char WGSizeHint[]        = "work_group_size_hint";
}

enum Spir2SamplerKind {
  CLK_ADDRESS_NONE            = 0x0000,
  CLK_ADDRESS_CLAMP           = 0x0004,
  CLK_ADDRESS_CLAMP_TO_EDGE   = 0x0002,
  CLK_ADDRESS_REPEAT          = 0x0006,
  CLK_ADDRESS_MIRRORED_REPEAT = 0x0008,
  CLK_NORMALIZED_COORDS_FALSE = 0x0000,
  CLK_NORMALIZED_COORDS_TRUE  = 0x0001,
  CLK_FILTER_NEAREST          = 0x0010,
  CLK_FILTER_LINEAR           = 0x0020,
};


/// Additional information for mangling a function argument type.
struct BuiltinArgTypeMangleInfo {
  bool IsSigned;
  bool IsVoidPtr;
  bool IsEnum;
  bool IsSampler;
  bool IsAtomic;
  bool IsLocalArgBlock;
  SPIR::TypePrimitiveEnum Enum;
  unsigned Attr;
  BuiltinArgTypeMangleInfo():IsSigned(true), IsVoidPtr(false), IsEnum(false),
                             IsSampler(false), IsAtomic(false), IsLocalArgBlock(false),
                             Enum(SPIR::PRIMITIVE_NONE), Attr(0)
  {}
};

/// Information for mangling builtin function.
class BuiltinFuncMangleInfo {
public:
  /// Translate builtin function name and set
  /// argument attributes and unsigned args.
  BuiltinFuncMangleInfo(const std::string &UniqName = "") : LocalArgBlockIdx(-1),
                                                            VarArgIdx(-1) {
    if (!UniqName.empty())
      init(UniqName);
  }
  virtual ~BuiltinFuncMangleInfo(){}
  const std::string &getUnmangledName() const { return UnmangledName;}
  void addUnsignedArg(int Ndx) { UnsignedArgs.insert(Ndx);}
  void addVoidPtrArg(int Ndx) { VoidPtrArgs.insert(Ndx);}
  void addSamplerArg(int Ndx) { SamplerArgs.insert(Ndx);}
  void addAtomicArg(int Ndx) { AtomicArgs.insert(Ndx);}
  void setLocalArgBlock(int Ndx) {
    assert(0 <= Ndx && "it is not allowed to set less than zero index");
    LocalArgBlockIdx = Ndx;
  }
  void setEnumArg(int Ndx, SPIR::TypePrimitiveEnum Enum) {
    EnumArgs[Ndx] = Enum;}
  void setArgAttr(int Ndx, unsigned Attr) {
    Attrs[Ndx] = Attr;}
  void setVarArg(int Ndx) {
    assert(0 <= Ndx && "it is not allowed to set less than zero index");
    VarArgIdx = Ndx;
  }
  bool isArgUnsigned(int Ndx) {
    return UnsignedArgs.count(-1) || UnsignedArgs.count(Ndx);}
  bool isArgVoidPtr(int Ndx) {
    return VoidPtrArgs.count(-1) || VoidPtrArgs.count(Ndx);}
  bool isArgSampler(int Ndx) {
    return SamplerArgs.count(Ndx);}
  bool isArgAtomic(int Ndx) {
    return AtomicArgs.count(Ndx);}
  bool isLocalArgBlock(int Ndx) {
    return LocalArgBlockIdx == Ndx;}
  bool isArgEnum(int Ndx, SPIR::TypePrimitiveEnum *Enum = nullptr) {
    auto Loc = EnumArgs.find(Ndx);
    if (Loc == EnumArgs.end())
      Loc = EnumArgs.find(-1);
    if (Loc == EnumArgs.end())
      return false;
    if (Enum)
      *Enum = Loc->second;
    return true;
  }
  unsigned getArgAttr(int Ndx) {
    auto Loc = Attrs.find(Ndx);
    if (Loc == Attrs.end())
      Loc = Attrs.find(-1);
    if (Loc == Attrs.end())
      return 0;
    return Loc->second;
  }
  // get ellipsis index, single ellipsis at the end of the function is possible only
  // return value < 0 if none
  int getVarArg() const {
    return VarArgIdx;
  }
  BuiltinArgTypeMangleInfo getTypeMangleInfo(int Ndx) {
    BuiltinArgTypeMangleInfo Info;
    Info.IsSigned = !isArgUnsigned(Ndx);
    Info.IsVoidPtr = isArgVoidPtr(Ndx);
    Info.IsEnum = isArgEnum(Ndx, &Info.Enum);
    Info.IsSampler = isArgSampler(Ndx);
    Info.IsAtomic = isArgAtomic(Ndx);
    Info.IsLocalArgBlock = isLocalArgBlock(Ndx);
    Info.Attr = getArgAttr(Ndx);
    return Info;
  }
  virtual void init(const std::string &UniqUnmangledName){
    UnmangledName = UniqUnmangledName;
  }
protected:
  std::string UnmangledName;
  std::set<int> UnsignedArgs; // unsigned arguments, or -1 if all are unsigned
  std::set<int> VoidPtrArgs;  // void pointer arguments, or -1 if all are void
                              // pointer
  std::set<int> SamplerArgs;  // sampler arguments
  std::set<int> AtomicArgs;   // atomic arguments
  std::map<int, SPIR::TypePrimitiveEnum> EnumArgs; // enum arguments
  std::map<int, unsigned> Attrs;                   // argument attributes
  int LocalArgBlockIdx; // index of a block with local arguments, idx < 0 if none
  int VarArgIdx;        // index of ellipsis argument, idx < 0 if none
};

/// \returns a vector of types for a collection of values.
template<class T>
std::vector<Type *>
getTypes(T V) {
  std::vector<Type *> Tys;
  for (auto &I:V)
    Tys.push_back(I->getType());
  return Tys;
}

/// Move elements of std::vector from [begin, end) to target.
template <typename T>
void move(std::vector<T>& V, size_t begin, size_t end, size_t target) {
  assert(begin < end && end <= V.size() && target <= V.size() &&
      !(begin < target && target < end));
  if (begin <= target && target <= end)
    return;
  auto B = V.begin() + begin, E = V.begin() + end;
  if (target > V.size())
    target = V.size();
  if (target > end)
    target -= (end - begin);
  std::vector<T> Segment(B, E);
  V.erase(B, E);
  V.insert(V.begin() + target, Segment.begin(), Segment.end());
}

/// Find position of first pointer type value in a vector.
inline size_t findFirstPtr(const std::vector<Value *> &Args) {
  auto PtArg = std::find_if(Args.begin(), Args.end(), [](Value *V){
    return V->getType()->isPointerTy();
  });
  return PtArg - Args.begin();
}

void removeFnAttr(LLVMContext *Context, CallInst *Call,
    Attribute::AttrKind Attr);
void addFnAttr(LLVMContext *Context, CallInst *Call,
    Attribute::AttrKind Attr);
void saveLLVMModule(Module *M, const std::string &OutputFile);
std::string mapSPIRVTypeToOCLType(SPIRVType* Ty, bool Signed);
std::string mapLLVMTypeToOCLType(const Type* Ty, bool Signed);
SPIRVDecorate *mapPostfixToDecorate(StringRef Postfix, SPIRVEntry *Target);

/// Add decorations to a SPIR-V entry.
/// \param Decs Each string is a postfix without _ at the beginning.
SPIRVValue *addDecorations(SPIRVValue *Target,
    const SmallVectorImpl<std::string>& Decs);

PointerType *getOrCreateOpaquePtrType(Module *M, const std::string &Name,
    unsigned AddrSpace = SPIRAS_Global);
PointerType* getSamplerType(Module *M);
PointerType* getPipeStorageType(Module *M);
void getFunctionTypeParameterTypes(llvm::FunctionType* FT,
    std::vector<Type*>& ArgTys);
Function *getOrCreateFunction(Module *M, Type *RetTy,
    ArrayRef<Type *> ArgTypes, StringRef Name,
    BuiltinFuncMangleInfo *Mangle = nullptr,
    AttributeList *Attrs = nullptr, bool takeName = true);

/// Get function call arguments.
/// \param Start Starting index.
/// \param End Ending index.
std::vector<Value *> getArguments(CallInst* CI, unsigned Start = 0,
    unsigned End = 0);

/// Get constant function call argument as an integer.
/// \param I argument index.
uint64_t getArgAsInt(CallInst *CI, unsigned I);

/// Get constant function call argument as type \param T.
/// \param I argument index.
template<typename T>
T getArgAs(CallInst *CI, unsigned I){
  return static_cast<T>(getArgAsInt(CI, I));
}

/// Get constant function call argument as a Scope enum.
/// \param I argument index.
Scope getArgAsScope(CallInst *CI, unsigned I);

/// Get constant function call argument as a Decoration enum.
/// \param I argument index.
Decoration getArgAsDecoration(CallInst *CI, unsigned I);

bool isPointerToOpaqueStructType(llvm::Type* Ty);
bool isPointerToOpaqueStructType(llvm::Type* Ty, const std::string &Name);

/// Check if a type is OCL image type.
/// \return type name without "opencl." prefix.
bool isOCLImageType(llvm::Type* Ty, StringRef *Name = nullptr);

/// \param BaseTyName is the type name as in spirv.BaseTyName.Postfixes
/// \param Postfix contains postfixes extracted from the SPIR-V image
///   type name as spirv.BaseTyName.Postfixes.
bool
isSPIRVType(llvm::Type* Ty, StringRef BaseTyName, StringRef *Postfix = 0);

/// Decorate a function name as __spirv_{Name}_
std::string decorateSPIRVFunction(const std::string &S);

/// Remove prefix/postfix from __spirv_{Name}_
std::string undecorateSPIRVFunction(const std::string &S);

/// Check if a function has decorated name as __spirv_{Name}_
/// and get the original name.
bool isDecoratedSPIRVFunc(const Function *F, std::string *UndecName = nullptr);

/// Get a canonical function name for a SPIR-V op code.
std::string getSPIRVFuncName(Op OC, StringRef PostFix = "");

std::string getSPIRVFuncName(Op OC, const Type *pRetTy, bool IsSigned = false);

/// Get a canonical function name for a SPIR-V extended instruction
std::string getSPIRVExtFuncName(SPIRVExtInstSetKind Set, unsigned ExtOp,
    StringRef PostFix = "");

/// Get SPIR-V op code given the canonical function name.
/// Assume \param Name is either IA64 mangled or unmangled, and the unmangled
/// name takes the __spirv_{OpName}_{Postfixes} format.
/// \return op code if the unmangled function name is a valid op code name,
///   otherwise return OpNop.
/// \param Dec contains decorations decoded from function name if it is
///   not nullptr.
Op getSPIRVFuncOC(const std::string& Name,
    SmallVectorImpl<std::string> *Dec = nullptr);

/// Get SPIR-V builtin variable enum given the canonical builtin name
/// Assume \param Name is in format __spirv_BuiltIn{Name}
/// \return false if \param Name is not a valid builtin name.
bool getSPIRVBuiltin(const std::string &Name, spv::BuiltIn &Builtin);

/// \param Name LLVM function name
/// \param DemangledName demanged name of the OpenCL built-in function
/// \returns true if Name is the name of the OpenCL built-in function,
/// false for other functions
bool oclIsBuiltin(const StringRef &Name, std::string *DemangledName = nullptr,
                  bool isCPP = false);

/// Check if a function type is void(void).
bool isVoidFuncTy(FunctionType *FT);

/// \returns true if \p T is a function pointer type.
bool isFunctionPointerType(Type *T);

/// \returns true if function \p F has function pointer type argument.
/// \param AI points to the function pointer type argument if returns true.
bool hasFunctionPointerArg(Function *F, Function::arg_iterator& AI);

/// \returns true if function \p F has array type argument.
bool hasArrayArg(Function *F);

/// Mutates function call instruction by changing the arguments.
/// \param ArgMutate mutates the function arguments.
/// \return mutated call instruction.
CallInst *mutateCallInst(Module *M, CallInst *CI,
    std::function<std::string (CallInst *, std::vector<Value *> &)>ArgMutate,
    BuiltinFuncMangleInfo *Mangle = nullptr, AttributeList *Attrs = nullptr,
    bool takeName = false);

/// Mutates function call instruction by changing the arguments and return
/// value.
/// \param ArgMutate mutates the function arguments.
/// \param RetMutate mutates the return value.
/// \return mutated instruction.
Instruction *mutateCallInst(Module *M, CallInst *CI,
    std::function<std::string (CallInst *, std::vector<Value *> &,
        Type *&RetTy)> ArgMutate,
    std::function<Instruction *(CallInst *)> RetMutate,
    BuiltinFuncMangleInfo *Mangle = nullptr, AttributeList *Attrs = nullptr,
    bool takeName = false);

/// Mutate call instruction to call SPIR-V builtin function.
CallInst *
mutateCallInstSPIRV(Module *M, CallInst *CI,
    std::function<std::string (CallInst *, std::vector<Value *> &)>ArgMutate,
    AttributeList *Attrs = nullptr);

/// Mutate call instruction to call SPIR-V builtin function.
Instruction *
mutateCallInstSPIRV(Module *M, CallInst *CI,
    std::function<std::string (CallInst *, std::vector<Value *> &,
        Type *&RetTy)> ArgMutate,
    std::function<Instruction *(CallInst *)> RetMutate,
    AttributeList *Attrs = nullptr);

/// Mutate function by change the arguments.
/// \param ArgMutate mutates the function arguments.
/// \param TakeName Take the original function's name if a new function with
///   different type needs to be created.
void mutateFunction(Function *F,
    std::function<std::string (CallInst *, std::vector<Value *> &)>ArgMutate,
    BuiltinFuncMangleInfo *Mangle = nullptr, AttributeList *Attrs = nullptr,
    bool TakeName = true);

/// Add a call instruction at \p Pos.
CallInst *addCallInst(Module *M, StringRef FuncName, Type *RetTy,
    ArrayRef<Value *> Args, AttributeList *Attrs, Instruction *Pos,
    BuiltinFuncMangleInfo *Mangle = nullptr,
    StringRef InstName = SPIR_TEMP_NAME_PREFIX_CALL,
    bool TakeFuncName = true);

/// Add a call instruction for SPIR-V builtin function.
CallInst *
addCallInstSPIRV(Module *M, StringRef FuncName, Type *RetTy,
    ArrayRef<Value *> Args,
    AttributeList *Attrs, Instruction *Pos, StringRef InstName);

/// Add a call of spir_block_bind function.
CallInst *
addBlockBind(Module *M, Function *InvokeFunc, Value *BlkCtx, Value *CtxLen,
    Value *CtxAlign, Instruction *InsPos,
    StringRef InstName = SPIR_TEMP_NAME_PREFIX_BLOCK);

typedef std::pair<std::vector<Value *>::iterator,
    std::vector<Value *>::iterator> ValueVecRange;

/// Add a vector at \param InsPos.
Value *
addVector(Instruction *InsPos, ValueVecRange Range);

/// Replace scalar values with a vector created at \param InsPos.
void
makeVector(Instruction *InsPos, std::vector<Value *> &Ops,
    ValueVecRange Range);

/// Expand a vector type value in \param Ops at index \param VecPos.
/// Generate extract element instructions at \param InsPos and replace
/// the vector type value with scalar type values.
/// If the value to be expanded is not vector type, do nothing.
void
expandVector(Instruction *InsPos, std::vector<Value *> &Ops, size_t VecPos);

/// Get size_t type.
IntegerType *getSizetType(Module *M);

/// Get void(void) function type.
Type *getVoidFuncType(Module *M);

/// Get void(void) function pointer type.
Type *getVoidFuncPtrType(Module *M, unsigned AddrSpace = 0);

/// Get a 64 bit integer constant.
ConstantInt *getInt64(Module *M, int64_t value);

/// Get a 32 bit integer constant.
ConstantInt *getInt32(Module *M, int value);

/// Get a 32 bit unsigned integer constant.
ConstantInt *getUInt32(Module *M, unsigned value);

/// Get a 16 bit unsigned integer constant.
ConstantInt *getUInt16(Module *M, unsigned short value);

// Get a 32 bit floating point constant.
Constant *getFloat32(Module *M, float value);

/// Get a 32 bit integer constant vector.
std::vector<Value *> getInt32(Module *M, const std::vector<int> &value);

/// Get a size_t type constant.
ConstantInt *getSizet(Module *M, uint64_t value);

/// Get metadata operand as int.
int getMDOperandAsInt(MDNode* N, unsigned I);

/// Get metadata operand as string.
std::string getMDOperandAsString(MDNode* N, unsigned I);

/// Get metadata operand as type.
Type* getMDOperandAsType(MDNode* N, unsigned I);

/// Get a named metadata as a set of string.
/// Assume the named metadata has one or more operands each of which might
/// contain set of strings. For instance:
/// !opencl.used.optional.core.features = !{!0}
/// !0 = !{!"cl_doubles", !"cl_images"}
/// or if we linked two modules we may have
/// !opencl.used.optional.core.features = !{!0, !1}
/// !0 = !{!"cl_doubles"}
/// !1 = !{!"cl_images"}
std::set<std::string> getNamedMDAsStringSet(Module *M,
                                            const std::string &MDName);

/// Get SPIR-V language by SPIR-V metadata spirv.Source
std::tuple<unsigned, unsigned, std::string>
getSPIRVSource(Module *M);

/// Map an unsigned integer constant by applying a function.
ConstantInt *mapUInt(Module *M, ConstantInt *I,
    std::function<unsigned(unsigned)> F);

/// Map a signed integer constant by applying a function.
ConstantInt *mapSInt(Module *M, ConstantInt *I,
    std::function<int(int)> F);

/// Get postfix for given decoration.
/// The returned postfix does not include "_" at the beginning.
std::string getPostfix(Decoration Dec, unsigned Value = 0);

/// Get postfix _R{ReturnType} for return type
/// The returned postfix does not includ "_" at the beginning
std::string getPostfixForReturnType(CallInst *CI, bool IsSigned = false);
std::string getPostfixForReturnType(const Type *pRetTy, bool IsSigned = false);

Constant *
getScalarOrVectorConstantInt(Type *T, uint64_t V, bool isSigned = false);

/// Get a constant int or a constant int array.
/// \param T is the type of the constant. It should be an integer type or
//  an integer pointer type.
/// \param Len is the length of the array.
/// \param V is the value to fill the array.
Value *
getScalarOrArrayConstantInt(Instruction *P, Type *T, unsigned Len, uint64_t V,
    bool isSigned = false);

/// Get the array from GEP.
/// \param V is a GEP whose pointer operand is a pointer to an array of size
/// \param Size.
Value *
getScalarOrArray(Value *V, unsigned Size, Instruction *Pos);

void
dumpUsers(Value* V, StringRef Prompt = "");

/// Get SPIR-V type name as spirv.BaseTyName.Postfixes.
std::string
getSPIRVTypeName(StringRef BaseTyName, StringRef Postfixes = "");

/// Checks if given type name is either ConstantSampler or ConsantPipeStorage.
bool
isSPIRVConstantName(StringRef TyName);

/// Get SPIR-V type by changing the type name from spirv.OldName.Postfixes
/// to spirv.NewName.Postfixes.
Type *
getSPIRVTypeByChangeBaseTypeName(Module *M, Type *T, StringRef OldName,
    StringRef NewName);

/// Get the postfixes of SPIR-V image type name as in spirv.Image.postfixes.
std::string
getSPIRVImageTypePostfixes(StringRef SampledType,
    SPIRVTypeImageDescriptor Desc,
    SPIRVAccessQualifierKind Acc);

/// Get the sampled type name used in postfix of image type in SPIR-V
/// friendly LLVM IR.
std::string
getSPIRVImageSampledTypeName(SPIRVType *Ty);

/// Translates OpenCL image type names to SPIR-V.
/// E.g. %opencl.image1d_rw_t -> %spirv.Image._void_0_0_0_0_0_0_2
Type *getSPIRVImageTypeFromOCL(Module *M, Type *T);

/// Get LLVM type for sampled type of SPIR-V image type by postfix.
Type*
getLLVMTypeForSPIRVImageSampledTypePostfix(StringRef Postfix,
  LLVMContext &Ctx);

/// Map OpenCL opaque type name to SPIR-V type name.
std::string
mapOCLTypeNameToSPIRV(StringRef Name, StringRef Acc = "");

/// Check if access qualifier is encoded in the type name.
bool hasAccessQualifiedName(StringRef TyName);

/// Get access qualifier from the type name.
StringRef getAccessQualifier(StringRef TyName);

bool
eraseUselessFunctions(Module *M);

/// Erase a function if it is declaration, has internal linkage and has no use.
bool
eraseIfNoUse(Function *F);

void
eraseIfNoUse(Value *V);

// Check if a mangled type name is unsigned
bool
isMangledTypeUnsigned(char Mangled);

// Check if a mangled type name is signed
bool
isMangledTypeSigned(char Mangled);

// Check if a mangled type name is floating point (except half)
bool
isMangledTypeFP(char Mangled);

// Check if a mangled type name is half
bool
isMangledTypeHalf(std::string Mangled);

// Check if \param I is valid vector size: 2, 3, 4, 8, 16.
bool
isValidVectorSize(unsigned I);

enum class ParamType
{
    FLOAT    = 0,
    SIGNED   = 1,
    UNSIGNED = 2,
    UNKNOWN  = 3
};

ParamType LastFuncParamType(const std::string& MangledName);

// Check if the last function parameter is signed
bool
isLastFuncParamSigned(const std::string& MangledName);

// Check if a mangled function name contains unsigned atomic type
bool
containsUnsignedAtomicType(StringRef Name);

/// Mangle builtin function name.
/// \return \param UniqName if \param BtnInfo is null pointer, otherwise
///    return IA64 mangled name.
std::string
mangleBuiltin(const std::string &UniqName,
    ArrayRef<Type*> ArgTypes, BuiltinFuncMangleInfo* BtnInfo);

/// Remove cast from a value.
Value *
removeCast(Value *V);

/// Cast a function to a void(void) funtion pointer.
Constant *
castToVoidFuncPtr(Function *F);

/// Get i8* with the same address space.
PointerType *getInt8PtrTy(PointerType *T);

/// Cast a value to a i8* by inserting a cast instruction.
Value *
castToInt8Ptr(Value *V, Instruction *Pos);

template<> inline void
SPIRVMap<std::string, Op, SPIRVOpaqueType>::init() {
  add(kSPIRVTypeName::DeviceEvent, OpTypeDeviceEvent);
  add(kSPIRVTypeName::Event, OpTypeEvent);
  add(kSPIRVTypeName::Image, OpTypeImage);
  add(kSPIRVTypeName::Pipe, OpTypePipe);
  add(kSPIRVTypeName::Queue, OpTypeQueue);
  add(kSPIRVTypeName::ReserveId, OpTypeReserveId);
  add(kSPIRVTypeName::Sampler, OpTypeSampler);
  add(kSPIRVTypeName::SampledImg, OpTypeSampledImage);
}

}

#endif
