# Lumen – Build mit MinGW-w64 (Linux-Cross-Compile oder MSYS2 unter Windows)
#   make            -> build/Lumen.exe
#   make DEBUG=1    -> build/debug/Lumen.exe (mit Symbolen)
#   make test       -> Selbsttest bauen und ausführen (nur unter Windows)
#   make clean

# Unter MSYS2/Windows: make CROSS=
CROSS   ?= x86_64-w64-mingw32-
CXX     := $(CROSS)g++
WINDRES := $(CROSS)windres

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -DUNICODE -D_UNICODE -municode -ffunction-sections -fdata-sections
LDFLAGS  = -municode -mwindows -static -s -Wl,--gc-sections
LIBS     = -ld3d11 -ldxgi -lmfuuid -lole32 -loleaut32 -luuid -lwindowscodecs \
           -lshell32 -lcomdlg32 -ldwmapi -lwtsapi32 -lurlmon -lwininet -lpropsys -luser32 -lgdi32 -ladvapi32

# Debug-Build mit Symbolen für gdb: make DEBUG=1 -> build/debug/Lumen.exe
OUT = build
ifeq ($(DEBUG),1)
  OUT       = build/debug
  CXXFLAGS += -O0 -g
  LDFLAGS  := $(filter-out -s,$(LDFLAGS))
endif

SRC  = $(wildcard src/*.cpp)
OBJ  = $(patsubst src/%.cpp,$(OUT)/obj/%.o,$(SRC)) $(OUT)/obj/lumen.res.o

all: $(OUT)/Lumen.exe

$(OUT)/obj/%.o: src/%.cpp src/*.h | $(OUT)/obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/obj/lumen.res.o: res/lumen.rc res/lumen.ico res/app.manifest | $(OUT)/obj
	$(WINDRES) -I res res/lumen.rc -O coff -o $@

$(OUT)/Lumen.exe: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OUT)/obj:
	mkdir -p $(OUT)/obj

# Selbsttest (Konsolenprogramm, nur unter Windows ausführbar)
TEST_SRC = tests/selftest.cpp $(filter-out src/main.cpp,$(SRC))

test: $(OUT)/selftest.exe
	./$(OUT)/selftest.exe

$(OUT)/selftest.exe: $(TEST_SRC) src/*.h | $(OUT)/obj
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $(TEST_SRC) -static $(LIBS)

clean:
	rm -rf build

.PHONY: all clean test
