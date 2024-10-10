add_executable(phaser main.cpp Phaser.cpp)

target_link_libraries(phaser ${llvm_libs} UnoptGenCore)
