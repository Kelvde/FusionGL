#!/usr/bin/env python3
# FusionGL v3.0 打包脚本（独立新项目）
# 骨架 = MGL 插件 APK；顶名 = 蹦床层 libng_gl4es.so；两个内核 = G720 定制编译版。
# 全部字符串替换均为等长，binary XML / arsc 结构不变。
import zipfile, os

SRC       = r"D:\MCP-Dev\packaging\MGL.apk"
OUT       = r"D:\MGL-Legacy-Research\dist\FusionGL_v3_unsigned.apk"
DISPATCH  = r"D:\MGL-Legacy-Research\dist\libng_gl4es.so"
HIGH      = r"D:\MGL-Legacy-Research\dist\libMobileGL_g720.so"
LOW       = r"D:\MGL-Legacy-Research\dist\libng_gl4es_g720.so"
CXXRT     = r"D:\MGL-Legacy-Research\dist\libc++_shared.so"
ABI       = "lib/arm64-v8a/"
TOP_LIB   = ABI + "libng_gl4es.so"
OLD_LIB   = ABI + "libMobileGL.so"


def patch_strings(b):
    """等长替换：显示名、库文件名。UTF-8 与 UTF-16LE 两种编码都处理。"""
    pairs = [
        ("MobileGL", "FusionGL"),                    # 8 -> 8
        ("libFusionGL.so", "libng_gl4es.so"),        # 15 -> 15（承接上一行的结果）
        ("MobileGL Legacy", "FusionGL Legacy"),      # 15 -> 15（若存在）
    ]
    for old, new in pairs:
        assert len(old) == len(new), (old, new)
        b = b.replace(old.encode(), new.encode())
        b = b.replace(old.encode("utf-16-le"), new.encode("utf-16-le"))
    return b


def patch_package(b):
    """只替换独立的 package 属性串；组件类名 top.mobilegl.plugin.Xxx 保留，指向原 dex 类。"""
    old = "top.mobilegl.plugin".encode("utf-16-le") + b"\x00\x00"
    new = "top.fusiongl.plugin".encode("utf-16-le") + b"\x00\x00"
    assert len(old) == len(new)
    n = b.count(old)
    b = b.replace(old, new)
    print(f"  package rename x{n}")
    assert n >= 1, "未找到独立包名串"
    return b


def main():
    for p in (SRC, DISPATCH, HIGH, LOW, CXXRT):
        assert os.path.exists(p), "缺少: " + p
    zin = zipfile.ZipFile(SRC)
    manifest = patch_package(patch_strings(zin.read("AndroidManifest.xml")))
    arsc = patch_package(patch_strings(zin.read("resources.arsc")))

    dispatch = open(DISPATCH, "rb").read()
    high = open(HIGH, "rb").read()
    low = open(LOW, "rb").read()

    zout = zipfile.ZipFile(OUT, "w")
    for name in zin.namelist():
        if name.startswith("META-INF/") and name.endswith((".SF", ".RSA", ".DSA", ".EC", ".MF")):
            continue
        info = zin.getinfo(name)
        # Android 11+ 要求 arsc 未压缩 + 4 字节对齐
        comp = zipfile.ZIP_STORED if name == "resources.arsc" else info.compress_type
        zi = zipfile.ZipInfo(name, date_time=info.date_time)
        zi.compress_type = comp
        zi.external_attr = info.external_attr
        if name == "resources.arsc":
            zout.writestr(zi, arsc)
        elif name == "AndroidManifest.xml":
            zout.writestr(zi, manifest)
        elif name == OLD_LIB:
            # 一个条目拆成两个：
            #  - 顶名 libng_gl4es.so = 蹦床层（rendererGLPath 指向它，触发 PojavRendererInit 的 gl4es 链）
            #  - 原名 libMobileGL.so = 我们编译的 G720 内核（插件 dex 的自检/Bench 硬编码 dlopen 这个名字）
            zi2 = zipfile.ZipInfo(TOP_LIB, date_time=info.date_time)
            zi2.compress_type = comp
            zi2.external_attr = info.external_attr
            zout.writestr(zi2, dispatch)
            zi3 = zipfile.ZipInfo(OLD_LIB, date_time=info.date_time)
            zi3.compress_type = comp
            zi3.external_attr = info.external_attr
            zout.writestr(zi3, high)
        else:
            zout.writestr(zi, zin.read(name))

    cxxrt = open(CXXRT, "rb").read()
    for fname, blob in ((ABI + "libng_gl4es_g720.so", low),
                        (ABI + "libc++_shared.so", cxxrt)):
        zi = zipfile.ZipInfo(fname, date_time=(2026, 11, 15, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zout.writestr(zi, blob)
    zout.close()
    print("written", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
