#!/bin/bash

JOBS=8
TARGET_DIR="${1:-.}"

if [ ! -d "$TARGET_DIR" ]; then
  echo "指定的路径不存在或不是目录：$TARGET_DIR"
  exit 1
fi

echo "开始处理目录：$TARGET_DIR"

find "$TARGET_DIR" -type f \( -name "*.c" -o -name "*.cpp" \) |
  parallel --bar -j "$JOBS" '
    perl -0777 -pe "s{//[^\n\r]*|/\\*.*?\\*/}{}gs" {} |
    awk "NF" |
    clang-format --style=LLVM |
    sed "s/[ \t]*$//" > {}.tmp &&
    mv {}.tmp {}
  '

echo "处理完成！"
