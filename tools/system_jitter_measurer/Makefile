#Compiler
CXX=g++
#Source Directories
SOURCE_DIR=.
SOURCES = $(SOURCE_DIR)/system_jitter_measurer.cpp
#Include Directories
INCLUDE_DIRS = -I./
#Objects
OBJECTS = $(SOURCES:.cpp=.o)
#Executable
EXECUTABLE = ./system_jitter_measurer
#Compiler flags
CFLAGS= $(INCLUDE_DIRS) -std=c++2a -c
#Linker flags
LFLAGS= -lstdc++ -pthread

#Release mode
release: CFLAGS +=  -O3
release: all

all: $(OBJECTS) $(EXECUTABLE)

$(EXECUTABLE) : $(OBJECTS)
		$(CXX) $(LFLAGS) $(OBJECTS) -o $@ 

.cpp.o: *.h
	$(CXX) $(CFLAGS) $< -o $@

clean:
	@echo Cleaning
	-rm -f $(OBJECTS) $(EXECUTABLE)
	@echo Cleaning done
	
.PHONY: all clean
