CXX ?= g++
WINDRES ?= windres

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 \
            -Iinclude \
            -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE

LDFLAGS := -shared -static -static-libgcc -static-libstdc++

LIBS := -lwinhttp -lcrypt32 -lole32 -lshell32 -luser32 -lgdi32 -lcomctl32

SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,build/obj/%.o,$(SRCS))
RES_SRCS := $(wildcard src/*.rc)
RES_OBJ := $(patsubst src/%.rc,build/obj/%.o,$(RES_SRCS))
TARGET := build/DeepSeekUsage.dll

TEST_CXXFLAGS := $(CXXFLAGS) -DDEEPSEEK_USAGE_TEST_BASE_URLS
TEST_OBJS := $(patsubst src/%.cpp,build/test_obj/%.o,$(SRCS))
TEST_TARGET := build/DeepSeekUsage_test.dll

all: $(TARGET)

build/obj/%.o: src/%.cpp include/PluginInterface.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/test_obj/%.o: src/%.cpp include/PluginInterface.h
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@

ifneq ($(RES_SRCS),)
$(RES_OBJ): build/obj/%.o: src/%.rc src/resource.h
	$(WINDRES) -Iinclude -Isrc -DUNICODE -D_UNICODE --codepage=65001 $< $@
endif

$(TARGET): $(OBJS) $(RES_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(RES_OBJ) $(LIBS)

$(TEST_TARGET): $(TEST_OBJS) $(RES_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_OBJS) $(RES_OBJ) $(LIBS)

test-dll: $(TEST_TARGET)

clean:
	rm -rf build/obj build/test_obj build/DeepSeekUsage.dll build/DeepSeekUsage_test.dll

.PHONY: all clean test-dll
