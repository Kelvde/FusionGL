LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := fusiongl

LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/../headers

LOCAL_SRC_FILES := \
	fusion_core.c \
	fusion_egl_special.c \
	fusion_gl_stubs.c \
	fusion_egl_stubs.c \
	fusion_egl_ext_stubs.c

LOCAL_CFLAGS := -O2 -Wall -Wno-unused-parameter -std=gnu11 -Wno-visibility
# 注意：桩必须全量导出（LWJGL 通过 dlsym 解析），不能用 -fvisibility=hidden
LOCAL_LDLIBS := -llog -ldl

include $(BUILD_SHARED_LIBRARY)
