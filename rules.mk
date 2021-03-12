# rules:

CXXFLAGS += -I../external_libs/$(BUILD_DIR)/include -I../external_libs/pugixml-1.11.4/src
LDLIBS += -L../external_libs/$(BUILD_DIR)/lib

build: build/.directory

%/.directory:
	mkdir -p $*
	touch $@

unit-tests: execute-unit-tests $(patsubst %,%-subdir-unit-tests,$(SUBDIRS))

$(patsubst %,%-subdir-unit-tests,$(SUBDIRS)):
	$(MAKE) -C $(@:-subdir-unit-tests=) unit-tests

execute-unit-tests: $(BUILD_DIR)/unit-test-runner
	if [ -x $< ]; then LD_LIBRARY_PATH=./build:./libtascar/build $<; fi

unit_tests_test_files = $(wildcard $(SOURCE_DIR)/*_unit_tests.cc)

$(BUILD_DIR)/unit-test-runner: CXXFLAGS += -I../libtascar/src -I../libtascar/build
$(BUILD_DIR)/unit-test-runner: CPPFLAGS += -I../libtascar/src -I../libtascar/build
$(BUILD_DIR)/unit-test-runner: LDFLAGS += -L../libtascar/build 

$(BUILD_DIR)/unit-test-runner: $(BUILD_DIR)/.directory $(unit_tests_test_files) $(patsubst %_unit_tests.cpp, %.cpp , $(unit_tests_test_files))
	if test -n "$(unit_tests_test_files)"; then $(CXX) $(CXXFLAGS) --coverage -o $@ $(wordlist 2, $(words $^), $^) $(LDFLAGS) ../libtascar/$(BUILD_DIR)/libtascar.so $(LDLIBS) -lgmock_main -lpthread; fi

