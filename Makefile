# 工具链配置
TOOLCHAIN_DIR := $(HOME)/k230_linux_sdk/output/k230_canmv_lckfb_defconfig/host
CROSS_COMPILE := $(TOOLCHAIN_DIR)/bin/riscv64-unknown-linux-gnu-
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
STRIP := $(CROSS_COMPILE)strip

# SDK 路径
STAGING_DIR := $(HOME)/k230_linux_sdk/output/k230_canmv_lckfb_defconfig/staging

# 目标定义
TARGET := camera

# 目录结构 
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj

# 源文件处理 
SRCS := $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.c.o,$(filter %.c,$(SRCS))) \
        $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.cpp.o,$(filter %.cpp,$(SRCS)))
DEPS := $(OBJS:.o=.d)

# 编译选项 
COMMON_FLAGS := -Os -march=rv64imafdc -mabi=lp64d
COMMON_FLAGS += -I$(INC_DIR)
COMMON_FLAGS += --sysroot=$(STAGING_DIR)
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include/opencv4
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include/libdrm
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include/drm
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include/ai_demo_common
COMMON_FLAGS += -I$(STAGING_DIR)/usr/include/nncase
COMMON_FLAGS += -MMD -MP

CFLAGS := $(COMMON_FLAGS) -std=gnu11
CXXFLAGS := $(COMMON_FLAGS) -std=c++17

#链接选项
LDFLAGS := -L$(STAGING_DIR)/usr/lib -Wl,-rpath-link=$(STAGING_DIR)/usr/lib
LIBS := -Wl,--start-group \
        -lnncase.rt_modules.k230 \
        -lNncase.Runtime.Native \
        -lfunctional_k230 \
        -lopencv_imgcodecs -lopencv_imgproc -lopencv_core \
        -lsharpyuv -ljpeg -lwebp -lpng -lz \
		-lai_demo_common -lmmz \
        -ldrm -lv4l2 -lpthread -lavformat -lavcodec -lswscale -lavutil -ldisplay\
		-lstdc++ -ldl -lm \
        -lavdevice -lswresample \
        -lavfilter \
        -Wl,--end-group \
        
# 目录创建
$(shell mkdir -p $(OBJ_DIR))

# 构建规则
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@
	$(STRIP) $@
	@echo "Build complete: $@"

$(OBJ_DIR)/%.c.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

$(OBJ_DIR)/%.cpp.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "CXX $<"

clean:
	rm -rf $(TARGET) $(OBJ_DIR)
	@echo "Clean complete"

-include $(DEPS)

.PHONY: all clean