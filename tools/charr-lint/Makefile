LLVM_CONFIG ?= llvm-config
CXX := clang++

ROOT := ../..
BUILD_DIR := $(ROOT)/local/charr-lint
TARGET := $(BUILD_DIR)/charr-lint

LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs)

.PHONY: all fixtures clean

all: $(TARGET)

$(TARGET): charr_lint.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(LLVM_CXXFLAGS) -std=c++17 -fno-exceptions -fno-rtti \
		$< -o $@ -lclang-cpp $(LLVM_LDFLAGS)

fixtures: $(TARGET)
	bash run-fixtures.sh $(TARGET)

clean:
	rm -f $(TARGET)
