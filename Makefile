CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++14 -Wall -Wextra -Wpedantic
CPPFLAGS += -DERA_SGF_VERSION='"$(shell cat VERSION)"'
BUILD ?= build
CORE = src/sgf.cpp src/viewer.cpp src/encoding.cpp src/storage.cpp src/render.cpp
HEADERS = $(wildcard include/erasgf/*.hpp)
DATA = src/codec_tables.inc VERSION

.PHONY: all test test-core clean pocketbook package
all: $(BUILD)/core-tests $(BUILD)/era-sgf-inspect $(BUILD)/era-sgf-preview

$(BUILD):
	mkdir -p $@

$(BUILD)/core-tests: tests/core_tests.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/core_tests.cpp $(CORE) $(LDFLAGS) -o $@

test-core: $(BUILD)/core-tests
	$(BUILD)/core-tests

$(BUILD)/era-sgf-inspect: src/inspect.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/inspect.cpp $(CORE) $(LDFLAGS) -o $@

$(BUILD)/era-sgf-preview: src/preview.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/preview.cpp $(CORE) $(LDFLAGS) -o $@

$(BUILD)/pocketbook-host: src/pocketbook.cpp tests/inkview_stub/inkview.cpp tests/inkview_stub/inkview.h $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) -Itests/inkview_stub $(CPPFLAGS) $(CXXFLAGS) src/pocketbook.cpp tests/inkview_stub/inkview.cpp $(CORE) $(LDFLAGS) -o $@

.PHONY: test-ui
test-ui: $(BUILD)/pocketbook-host $(BUILD)/era-sgf-inspect
	python3 tests/ui_test.py $(BUILD)/pocketbook-host $(BUILD)/era-sgf-inspect

test: test-core test-ui $(BUILD)/era-sgf-inspect
	python3 tests/integration_test.py $(BUILD)/era-sgf-inspect
	python3 tests/package_test.py

pocketbook:
	bash scripts/build-pocketbook.sh

package: pocketbook
	python3 scripts/package.py --binary build/pocketbook/era-sgf.app --output dist

clean:
	rm -rf $(BUILD)
