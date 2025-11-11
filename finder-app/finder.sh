#!/bin/bash

FILESDIR=$1
SEARCHSTR=$2

if [ $# -lt 2 ]; then
    echo "Error: 2 parameters are required."
    echo "Usage: $0 DIRECTORY SEARCHSTR
Search for a specific string (SEARCHSTR) in a target directory (DIRECTORY) recursively.
    
Examples:
    ./finder.sh path/to/directory mystring_to_search"
    exit 1
fi

if [ -d "$FILESDIR" ]; then
    files_count=$(find "$FILESDIR" -not -type d | wc -l)
    lines_count=$(grep -r "$SEARCHSTR" "$FILESDIR" 2>/dev/null | wc -l)
    echo "The number of files are $files_count and the number of matching lines are $lines_count"
else
    echo "Error: Directory does not exist!"
    exit 1
fi