cmake_minimum_required(VERSION 3.20)
project(EnhancedAnimalShelter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(EnhancedAnimalShelter
  main.cpp
)

find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)

target_link_libraries(EnhancedAnimalShelter PRIVATE mongo::mongocxx_shared mongo::bsoncxx_shared)
