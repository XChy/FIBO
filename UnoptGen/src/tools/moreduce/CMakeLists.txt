file(GLOB_RECURSE OPTREDUCER_SRC "lib/*.h"
     "lib/*.cpp")

add_executable(moreduce ${OPTREDUCER_SRC} main.cpp)

target_link_libraries(moreduce ${llvm_libs} UnoptGenCore)
