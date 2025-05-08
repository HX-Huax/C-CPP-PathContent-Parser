#!/usr/bin/env bash

SOURCE_DIR=/home/hx/Dev/Project/CAM/ProProcess/examples/final
DATASET_NAME=scpcdb

DATA_ROOT=${SOURCE_DIR}/data
RAW_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.raw.txt
SHUFFED_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.shuffed.raw.txt
TRAIN_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.train.raw.txt
VAL_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.val.raw.txt
TEST_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.test.raw.txt
SMALL_TEST_DATA_FILE=${DATA_ROOT}/${DATASET_NAME}.small_test.raw.txt # 新增小型测试集

# MEM_PERCENT - for configurable commands like sort limit increase memory percentage to use
MEM_PERCENT=75
# NUM_PROCESSORS - for configurable commands like sort raise the number of processors to use
NUM_PROCESSORS=6
# DOWNSAMPLE - Reduce the size of the dataset.  Change to a percentage (ex. DOWNSAMPLE=.8)
# to only use a portion of the available features.
DOWNSAMPLE=1

# sort -u -R -S ${MEM_PERCENT}% --parallel ${NUM_PROCESSORS} --output=${SHUFFED_DATA_FILE} ${RAW_DATA_FILE}
shuf ${RAW_DATA_FILE} --output=${SHUFFED_DATA_FILE} # 仅随机化（保留重复行）

echo "Calculating lines"
n=$(wc -l <${SHUFFED_DATA_FILE})
[[ ! "$n" -gt "0" ]] && echo "Extracted no lines. (missing source files?)" && exit 1

# 设置小型测试集大小（例如总数据的1%或固定1000行）
SMALL_TEST_SIZE=$((n / 100)) # 1% 的例子
# SMALL_TEST_SIZE=1000  # 固定行数
if [ $SMALL_TEST_SIZE -ge $n ]; then
  SMALL_TEST_SIZE=$((n / 10)) # 避免超过总行数
fi

# 提取小型测试集
head -n ${SMALL_TEST_SIZE} ${SHUFFED_DATA_FILE} >${SMALL_TEST_DATA_FILE}
REMAINING_LINES=$((n - SMALL_TEST_SIZE))

# 剩余数据划分（原比例85%训练，剩余15%中测试和验证各半）
TRAIN_REMAINING=$(echo "x=${REMAINING_LINES} * 0.85; scale=0; x/1" | bc -l)
TEST_VAL_REMAINING=$((REMAINING_LINES - TRAIN_REMAINING))
TEST_REMAINING=$((TEST_VAL_REMAINING / 2))
VAL_REMAINING=$((TEST_VAL_REMAINING - TEST_REMAINING))

# 分割剩余数据到训练、测试、验证
tail -n +$((SMALL_TEST_SIZE + 1)) ${SHUFFED_DATA_FILE} | head -n ${TRAIN_REMAINING} >${TRAIN_DATA_FILE}
tail -n +$((SMALL_TEST_SIZE + TRAIN_REMAINING + 1)) ${SHUFFED_DATA_FILE} | head -n ${TEST_REMAINING} >${TEST_DATA_FILE}
tail -n +$((SMALL_TEST_SIZE + TRAIN_REMAINING + TEST_REMAINING + 1)) ${SHUFFED_DATA_FILE} >${VAL_DATA_FILE}
