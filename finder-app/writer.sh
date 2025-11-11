#!/bin/bash

WRITEFILE=$1
WRITESTR=$2

if [ $# -lt 2 ]; then
    echo "Error: 2 parameters are required."
    echo "Usage: $0 WRITEFILE WRITESTR
Write for a specific string (WRITESTR) into a target file (WRITEFILE).
    
Examples:
    ./writer.sh /path/to/file.txt my_string_to_write"
    exit 1
fi

mkdir -p "$(dirname "$WRITEFILE")" && echo "$WRITESTR" > "$WRITEFILE"

pipe_statuses=("${PIPESTATUS[@]}")

for status in "${pipe_statuses[@]}"; do
  if [ "$status" -ne 0 ]; then
    echo "File could not be created! Exit code $status."
    break
  fi
done