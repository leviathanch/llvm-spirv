option(SPIRV_USE_LLVM_API "Enable usage of LLVM API for libSPIRV." ON)
if ( SPIRV_USE_LLVM_API )
  add_definitions(-D_SPIRV_LLVM_API)
endif()

add_library(llvm_spirv SHARED
  libSPIRV/SPIRVBasicBlock.cpp
  libSPIRV/SPIRVDebug.cpp
  libSPIRV/SPIRVDecorate.cpp
  libSPIRV/SPIRVEntry.cpp
  libSPIRV/SPIRVFunction.cpp
  libSPIRV/SPIRVInstruction.cpp
  libSPIRV/SPIRVModule.cpp
  libSPIRV/SPIRVStream.cpp
  libSPIRV/SPIRVType.cpp
  libSPIRV/SPIRVValue.cpp
  libSPIRV/SPIRVUtil.cpp
  Mangler/FunctionDescriptor.cpp
  Mangler/Mangler.cpp
  Mangler/ManglingUtils.cpp
  Mangler/ParameterType.cpp
  OCL20To12.cpp
  OCL20ToSPIRV.cpp
  OCL21ToSPIRV.cpp
  OCLTypeToSPIRV.cpp
  OCLUtil.cpp
  SPIRVLowerBool.cpp
  SPIRVLowerConstExpr.cpp
  SPIRVLowerOCLBlocks.cpp
  SPIRVReader.cpp
  SPIRVRegularizeLLVM.cpp
  SPIRVToOCL20.cpp
  SPIRVWriter.cpp
  SPIRVWriterPass.cpp
  TransOCLMD.cpp
)

target_include_directories(llvm_spirv PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(llvm_spirv PRIVATE ${LLVM_SPIRV_INCLUDE_DIRS})
target_include_directories(llvm_spirv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(llvm_spirv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libSPIRV)
target_include_directories(llvm_spirv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Mangler)

target_link_libraries(llvm_spirv LLVM)

