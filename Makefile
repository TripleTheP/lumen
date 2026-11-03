# Lumen – Build mit MinGW-w64 (Linux-Cross-Compile oder MSYS2 unter Windows)
#   make            -> build/Lumen.exe
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

SRC  = $(wildcard src/*.cpp)
OBJ  = $(patsubst src/%.cpp,build/obj/%.o,$(SRC)) build/obj/lumen.res.o

all: build/Lumen.exe

build/obj/%.o: src/%.cpp src/*.h | build/obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/lumen.res.o: res/lumen.rc res/lumen.ico res/app.manifest | build/obj
	$(WINDRES) -I res res/lumen.rc -O coff -o $@

build/Lumen.exe: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

build/obj:
	mkdir -p build/obj

clean:
	rm -rf build

.PHONY: all clean
