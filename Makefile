CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++14 -Wall -Wextra -Wpedantic
CPPFLAGS += -DERA_SGF_VERSION='"$(shell cat VERSION)"'
BUILD ?= build
CORE = src/sgf.cpp src/viewer.cpp src/encoding.cpp src/storage.cpp src/render.cpp src/library.cpp src/logos.cpp
HEADERS = $(wildcard include/erasgf/*.hpp)
DATA = src/codec_tables.inc VERSION

.PHONY: all test test-core clean pocketbook package emulator-setup emulator-build emulator
all: $(BUILD)/core-tests $(BUILD)/era-sgf-inspect $(BUILD)/era-sgf-preview $(BUILD)/era-sgf-logos

$(BUILD):
	mkdir -p $@

$(BUILD)/core-tests: tests/core_tests.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/core_tests.cpp $(CORE) $(LDFLAGS) -lz -o $@

test-core: $(BUILD)/core-tests
	$(BUILD)/core-tests

$(BUILD)/era-sgf-inspect: src/inspect.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/inspect.cpp $(CORE) $(LDFLAGS) -o $@

$(BUILD)/era-sgf-preview: src/preview.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/preview.cpp $(CORE) $(LDFLAGS) -o $@

$(BUILD)/era-sgf-logos: src/logo_preview.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/logo_preview.cpp $(CORE) $(LDFLAGS) -o $@

$(BUILD)/pocketbook-host: src/pocketbook.cpp tests/inkview_stub/inkview.cpp tests/inkview_stub/inkview.h $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) -Itests/inkview_stub $(CPPFLAGS) $(CXXFLAGS) src/pocketbook.cpp tests/inkview_stub/inkview.cpp $(CORE) $(LDFLAGS) -lz -o $@

.PHONY: test-ui
test-ui: $(BUILD)/pocketbook-host $(BUILD)/era-sgf-inspect $(BUILD)/era-sgf-preview
	python3 tests/ui_test.py $(BUILD)/pocketbook-host $(BUILD)/era-sgf-inspect

$(BUILD)/library-tests: tests/library_tests.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/library_tests.cpp $(CORE) $(LDFLAGS) -o $@

.PHONY: test-library
test-library: $(BUILD)/library-tests
	$(BUILD)/library-tests

test: test-core test-ui test-library $(BUILD)/era-sgf-inspect
	python3 tests/integration_test.py $(BUILD)/era-sgf-inspect
	python3 tests/package_test.py
	python3 tests/installer_test.py

pocketbook:
	bash scripts/build-pocketbook.sh

package: pocketbook
	python3 scripts/package.py --binary build/pocketbook/era-sgf.app --output dist

emulator-setup:
	python3 scripts/setup-emulator.py

emulator-build:
	bash scripts/run-emulator.sh --build-only

emulator:
	bash scripts/run-emulator.sh

clean:
	rm -rf $(BUILD)

$(BUILD)/era-sgf-library-benchmark: tools/library_benchmark.cpp $(CORE) $(HEADERS) $(DATA) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DERASGF_BENCH_STATS tools/library_benchmark.cpp $(CORE) $(LDFLAGS) -o $@
