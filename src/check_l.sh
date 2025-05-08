#!/bin/bash

# 带颜色输出的错误消息
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# 检查文件参数
if [ $# -ne 1 ]; then
  echo -e "${RED}用法: $0 <数据文件路径>${NC}"
  exit 2
fi

file="$1"
tmp_errors=$(mktemp)

# 获取总行数（包含最后一行无换行的情况）
total_lines=$(awk 'END {print NR}' "$file")
echo -e "校验文件: ${GREEN}$file${NC} (共 $total_lines 行)"

# 行处理函数
process_line() {
  local line_num="$1"
  local line="$2"

  # 分割行内容
  read -ra parts <<<"$line"

  # 错误收集
  {
    # 检查文件名是否存在
    [ ${#parts[@]} -lt 1 ] &&
      echo -e "${RED}错误${NC}: 行 $line_num 缺少文件名"

    # 检查三元组格式
    for ((i = 1; i < ${#parts[@]}; i++)); do
      [[ ! "${parts[$i]}" =~ ^[0-9]+,[0-9]+,[0-9]+$ ]] &&
        echo -e "${RED}错误${NC}: 行 $line_num 无效三元组: '${parts[$i]}'"
    done
  } >>"$tmp_errors" # 原子写入
}

export -f process_line
export RED GREEN NC tmp_errors

# 并行处理主流程
awk '{print NR "\t" $0}' "$file" |
  parallel --bar --progress --jobs 80% --keep-order --colsep '\t' \
    'process_line {1} {2}' 2>/dev/null

# 错误汇总
errors=$(grep -c '错误' "$tmp_errors")
if [ $errors -eq 0 ]; then
  echo -e "\n${GREEN}校验通过: 文件格式合法${NC}"
  rm "$tmp_errors"
  exit 0
else
  echo -e "\n发现 ${RED}$errors 个错误${NC}"
  cat "$tmp_errors"
  rm "$tmp_errors"
  exit 1
fi
