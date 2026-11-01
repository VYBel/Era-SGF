CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
BUILD ?= build
CORE = src/sgf.cpp src/viewer.cpp src/encoding.cpp src/storage.cpp
HEADERS = $(wildcard include/erasgf/*.hpp)

.PHONY: all test test-core clean
all: $(BUILD)/core-tests $(BUILD)/era-sgf-inspect

$(BUILD):
	mkdir -p $@

$(BUILD)/core-tests: tests/core_tests.cpp $(CORE) $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/core_tests.cpp $(CORE) $(LDFLAGS) -o $@

test-core: $(BUILD)/core-tests
	$(BUILD)/core-tests

$(BUILD)/era-sgf-inspect: src/inspect.cpp $(CORE) $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/inspect.cpp $(CORE) $(LDFLAGS) -o $@

test: test-core $(BUILD)/era-sgf-inspect
	python3 tests/integration_test.py $(BUILD)/era-sgf-inspect

clean:
	rm -rf $(BUILD)
