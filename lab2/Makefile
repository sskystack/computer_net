# Makefile for Lab2 - Reliable Data Transfer Protocol

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I./include
LDFLAGS = -lws2_32 -pthread

# 目录定义
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# 源文件
COMMON_SRCS = $(SRC_DIR)/common.cpp $(SRC_DIR)/checksum.cpp $(SRC_DIR)/packet.cpp
TEST_SRCS = $(COMMON_SRCS)

# 目标文件
TEST_OBJS = $(BUILD_DIR)/common.o $(BUILD_DIR)/checksum.o $(BUILD_DIR)/packet.o $(BUILD_DIR)/test.o

# 默认目标
all: test_basic

# 创建build目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译目标文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/test.o: test.cpp $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c test.cpp -o $@

# 编译测试程序
test_basic: $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) $(LDFLAGS) -o test_basic.exe
	@echo "Build successful! Run: test_basic.exe"

# 清理编译文件
clean:
	rm -rf $(BUILD_DIR)
	rm -f *.exe

.PHONY: all clean test_basic
