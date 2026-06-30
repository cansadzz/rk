CC = aarch64-linux-gnu-gcc

# 目标文件
TARGET = main

# 源文件
SRC := deepseektest.c 
SRC += ./cJSON/cJSON.c ./cJSON/cJSON_Utils.c

#指定头文件目录
CFLAGS := -I./cJSON

# 默认目标
all: $(TARGET)

# 编译规则
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

# 清理目标
clean:
	rm -f $(TARGET)

# phony targets
.PHONY: all clean
