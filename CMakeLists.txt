cmake_minimum_required(VERSION 3.16)
project(FileSystem)

set(CMAKE_CXX_STANDARD 17)

add_executable(FileSystem main.cpp src/DiskDriver.cpp src/DiskDriver.h src/FileSystem.cpp src/FileSystem.h src/UserInterface.cpp src/UserInterface.h src/Shell.cpp src/Shell.h src/Constraints.h src/entity/Directory.cpp src/entity/Directory.h src/entity/DirectoryItem.cpp src/entity/DirectoryItem.h src/entity/FileSystemInfo.cpp src/entity/FileSystemInfo.h src/entity/FileIndex.cpp src/entity/FileIndex.h src/entity/INode.cpp src/entity/INode.h src/entity/FreeBlockStack.cpp src/entity/FreeBlockStack.h src/Tools.h src/entity/User.cpp src/entity/User.h src/entity/FileOpenItem.cpp src/entity/FileOpenItem.h)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
