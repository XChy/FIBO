add_executable(unoptgen main.cpp engine/Mutator.cpp)

target_link_libraries(unoptgen ${llvm_libs} UnoptGenCore)
