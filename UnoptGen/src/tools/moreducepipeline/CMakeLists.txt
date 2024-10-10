file(GLOB_RECURSE MOREDUCEPIPELINE_SRC "lib/*.h"
     "lib/*.cpp" main.cpp)

add_executable(moreducepipeline ${MOREDUCEPIPELINE_SRC})

target_link_libraries(moreducepipeline ${llvm_libs} UnoptGenCore)
