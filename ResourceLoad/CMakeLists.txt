# CMake ��Ͱ汾��Ҫ��
cmake_minimum_required (VERSION 2.8)

# ��Ŀ��Ϣ
project (EasyLoader)

# ���ҵ�ǰĿ¼�µ�����Դ�ļ�
# �������Ʊ��浽 DIR_SRCS ����
aux_source_directory(. DIR_SRCS)

# ���� math ��Ŀ¼
add_subdirectory(Code)

# ָ������Ŀ�� 
add_executable(EasyLoader ./ResourceLoad/main.cpp)

# include directories
INCLUDE_DIRECTORIES(
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/Code
)

# lib directories
LINK_DIRECTORIES(
  ${PROJECT_BINARY_DIR}/lib
)
TARGET_LINK_LIBRARIES(EasyLoader EasyLoaderLibrary)
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)