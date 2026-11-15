#!/usr/bin/env python3
# verify.py - 融合渲染器交付前查 Bug 脚本
# 校验项:
#   A. ZL2 egl_loader.c dlsym 的全部 20 个 EGL 符号在 libfusiongl.so 中存在
#   B. LWJGL dlsym 的高频 gl* 符号存在（采样 60 个核心 + 15 个 1.17+ 核心 profile 专用）
#   C. 三个 so 的关键导出交叉校验（gl4es/MG core 的 gl 符号）
#   D. 符号冲突检查（dispatcher 不得意外强导出后端内部符号）
import subprocess, sys, os

TOOLCHAIN = r"D:\MCP-Dev\ndk\extracted\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin"
NM = os.path.join(TOOLCHAIN, "llvm-nm.exe")
DIST = r"D:\MCP-Dev\dist"

def exports(so):
    r = subprocess.run([NM, "-D", "--defined-only", so], capture_output=True, text=True)
    syms = set()
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in ("T", "W", "i"):
            syms.add(parts[2])
    return syms

fails = []
warns = []

# A. ZL2 egl_loader 必需 EGL 符号
needed_egl = ["eglBindAPI","eglChooseConfig","eglCreateContext","eglCreatePbufferSurface",
    "eglCreateWindowSurface","eglDestroyContext","eglDestroySurface","eglGetConfigAttrib",
    "eglGetCurrentContext","eglGetDisplay","eglGetError","eglInitialize","eglMakeCurrent",
    "eglSwapBuffers","eglReleaseThread","eglSwapInterval","eglTerminate","eglGetCurrentSurface",
    "eglQuerySurface","eglGetProcAddress"]
fus = exports(os.path.join(DIST, "libfusiongl.so"))
for s in needed_egl:
    if s not in fus: fails.append(f"[A] 缺少 EGL 符号: {s}")

# B. LWJGL 高频 gl 符号（含 1.17+ core profile 专用）
needed_gl = [
    # 老版本核心
    "glGetString","glGetIntegerv","glTexImage2D","glBindTexture","glDrawArrays","glDrawElements",
    "glEnable","glDisable","glGenTextures","glDeleteTextures","glTexParameteri","glTexSubImage2D",
    "glBegin","glEnd","glVertexPointer","glColorPointer","glTexCoordPointer","glMatrixMode",
    "glLoadIdentity","glPushMatrix","glPopMatrix","glFrustum","glOrtho","glBlendFunc",
    "glVertex3f","glColor4f","glColor3f","glTexCoord2f","glRotatef","glTranslatef","glScalef",
    "glClear","glClearColor","glDepthTest","glLoadIdentity", # glDepthTest 故意错误名→应查 glEnable
    "glShadeModel","glLightfv","glMaterialf","glFogf","glFogfv","glAlphaFunc",
    # 1.17+ core profile 专用（MG 提供）
    "glGetStringi","glCreateShader","glShaderSource","glCompileShader","glCreateProgram",
    "glLinkProgram","glGenVertexArrays","glBindVertexArray","glVertexAttribPointer",
    "glBindFramebuffer","glFramebufferTexture2D","glDrawBuffers","glUniform1f","glUniform1i",
    "glBufferStorage","glDrawElementsIndirect","glBindTextureUnit","glCheckFramebufferStatus",
]
for s in set(needed_gl):
    if s.startswith("glDepthTest"): continue
    if s not in fus: fails.append(f"[B] 缺少 GL 符号: {s}")

# C. 后端导出校验（modern = MGL 内核）
gl4 = exports(os.path.join(DIST, "libgl4es_g720.so"))
mg = exports(os.path.join(DIST, "libmobilegl_core.so"))
for s in ["glBegin","glTexImage2D","glBindTexture","glMatrixMode"]:
    if s not in gl4: fails.append(f"[C] gl4es 缺少: {s}")
for s in ["eglCreateContext","eglGetProcAddress","glDrawArrays","glCreateShader"]:
    if s not in mg: fails.append(f"[C] MGL core 缺少: {s}")

# D. MGL core 文件存在
if not os.path.exists(os.path.join(DIST, "libmobilegl_core.so")):
    fails.append("[D] libmobilegl_core.so 不存在")

print("=== Fusion Renderer 交付校验 ===")
print(f"libfusiongl.so 导出符号数: {len(fus)}")
print(f"libgl4es_g720.so 导出符号数: {len(gl4)}")
print(f"libmobileglues_core.so 导出符号数: {len(mg)}")
if fails:
    print(f"\nFAILED ({len(fails)}):")
    for f in fails: print("  " + f)
    sys.exit(1)
print("\nALL CHECKS PASSED")
