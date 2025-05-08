#!/bin/bash

# 检查是否提供了文件路径
if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <file_path>" >&2
  exit 1
fi

file_path="$1"

# 检查文件是否存在
if [ ! -f "$file_path" ]; then
  echo "Error: File '$file_path' does not exist." >&2
  exit 1
fi

# 检查文件是否可读
if [ ! -r "$file_path" ]; then
  echo "Error: File '$file_path' is not readable." >&2
  exit 1
fi

# 查找并输出不满足要求的行
invalid_lines=$(awk -F'[ ,]' '$1 !~ /\.cpp$|\.c$/ {print $0}' "$file_path")

if [ -z "$invalid_lines" ]; then
  echo "All lines start with *.cpp or *.c"
  exit 0
else
  echo "The following lines do not start with *.cpp or *.c:"
  # echo "$invalid_lines"
  exit 1
fi
