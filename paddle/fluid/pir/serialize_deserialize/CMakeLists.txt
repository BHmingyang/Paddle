file(GLOB_RECURSE SERIALIZE_DESERIALIZE_CPP_SOURCES "*.cc")

include_directories(pir_save_load PRIVATE
                    ${PADDLE_SOURCE_DIR}/third_party/nlohmann_json/include/)
include_directories(pir_save_load PRIVATE
                    ${PADDLE_BINARY_DIR}/third_party/install/yaml-cpp/include/)

link_directories(${PADDLE_BINARY_DIR}/third_party/install/yaml-cpp/lib/)
target_compile_definitions(yaml INTERFACE YAML_CPP_STATIC_DEFINE)
if(LINUX)
  link_libraries(stdc++fs)
endif()

add_definitions(-DPADDLE_ROOT="${PADDLE_SOURCE_DIR}")
add_definitions(
  -DPATCH_PATH="../../../../../python/paddle/pir/serialize_deserialize/patch")

cc_library(
  pir_save_load
  SRCS ${SERIALIZE_DESERIALIZE_CPP_SOURCES}
  DEPS op_dialect phi json yaml)
