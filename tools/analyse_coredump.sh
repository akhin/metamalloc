#!/bin/bash

# Check if a file path is provided as an argument
if [ $# -eq 0 ]; then
    echo "Usage: $0 <core_dump_file>"
    exit 1
fi

core_dump_file=$1

if [ ! -f "$core_dump_file" ]; then
    echo "Error: File '$core_dump_file' does not exist."
    exit 1
fi

# Get the executable name by parsing the output of the 'file' command
executable=$(file "$core_dump_file" | awk -F "execfn: '" '{print $2}' | awk -F "'" '{print $1}')

if [ -z "$executable" ]; then
    echo "Error: Failed to extract the executable name from the core dump file."
    exit 1
fi

# Change directory to the calling directory
current_dir="$PWD"
cd "$current_dir" 

# Run GDB with the core dump file, load debug symbols, and execute "where" command
gdb $executable $core_dump_file -ex "where" -ex "quit"