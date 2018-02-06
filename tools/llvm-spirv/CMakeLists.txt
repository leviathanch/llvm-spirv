add_executable(llvm-spirv llvm-spirv.cpp)

target_include_directories(llvm-spirv PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(llvm-spirv PRIVATE ${LLVM_SPIRV_INCLUDE_DIRS})

target_link_libraries(llvm-spirv llvm_spirv LLVM)

install(
  TARGETS
    llvm-spirv
  DESTINATION
    ${CMAKE_INSTALL_PREFIX}/bin)
