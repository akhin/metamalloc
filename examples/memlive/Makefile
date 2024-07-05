#Compiler
CXX=g++
#Source Directories
SOURCE_DIR=.
SOURCES = $(SOURCE_DIR)/memlive_example.cpp
#Include Directories
INCLUDE_DIRS = -I../../src -I../..
#Objects
OBJECTS = $(SOURCES:.cpp=.o)
#Executable
EXECUTABLE = ./memlive_example
MISSED_REPORT = ./missed.all
#Compiler flags
CFLAGS= $(INCLUDE_DIRS) -std=c++2a -c 
#Linker flags
LFLAGS= -lstdc++ -pthread

#Add DEBUG macro , symbol generation and show all warnings
debug: CFLAGS += -DDEBUG -g -Wall -fno-omit-frame-pointer
debug: all
#unresolved-symbols=ignore-in-shared-libs is for sanitizers
#as sanitizers cause additional code to be added
#Debug mode + compile and link with GCC address sanitizer 
debug_with_asan: CFLAGS += -DDEBUG -g -Wall -fno-omit-frame-pointer
debug_with_asan: LFLAGS += -Wall -Wl,-z,defs -fsanitize=address -unresolved-symbols=ignore-in-shared-libs
debug_with_asan: all
#Debug mode + compile and link with GCC leak sanitizer
debug_with_lsan: CFLAGS += -DDEBUG -g -Wall -fno-omit-frame-pointer
debug_with_lsan: LFLAGS += -Wall -Wl,-z,defs -fsanitize=leak -unresolved-symbols=ignore-in-shared-libs
debug_with_lsan: all
#Debug mode + compile and link with GCC thread sanitizer 
debug_with_tsan: CFLAGS += -DDEBUG -g -Wall -fno-omit-frame-pointer
debug_with_tsan: LFLAGS += -Wall -Wl,-z,defs -fsanitize=thread -unresolved-symbols=ignore-in-shared-libs
debug_with_tsan: all
#Debug mode + compile and link with GCC undefined behaviour sanitizer 
debug_with_ubsan: CFLAGS += -DDEBUG -g -Wall -fno-omit-frame-pointer
debug_with_ubsan: LFLAGS += -Wall -Wl,-z,defs -fsanitize=undefined -unresolved-symbols=ignore-in-shared-libs
debug_with_ubsan: all

#Release mode
release: CFLAGS += -DNDEBUG -O3 -fopt-info-missed=missed.all -fno-rtti
release: all
all: $(OBJECTS) $(EXECUTABLE)

$(EXECUTABLE) : $(OBJECTS)
		$(CXX) $(OBJECTS) $(LFLAGS) -o $@ 
	
.cpp.o: *.h
	$(CXX) $(CFLAGS) $< -o $@

clean:
	@echo Cleaning
	-rm -f $(OBJECTS) $(EXECUTABLE) $(MISSED_REPORT)
	@echo Cleaning done
	
.PHONY: all clean
