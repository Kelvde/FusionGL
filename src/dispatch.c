// FusionGL v3.0 dispatcher - 独立新项目
// 双内核自动分派 + AArch64 蹦床填表，全程零 JNI。
// 内核优先级：G720 优化编译版 -> 官方原版（仅故障安全网，不作交付路径）。
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <android/log.h>

#define LOG_TAG "FusionGL"
#define LOG_DIR1  "/sdcard/FusionGL"
#define LOG_DIR2  "/sdcard/Android/data/com.movtery.zalithlauncher.v2/files/FusionGL"
#define LOG_FILE1 LOG_DIR1 "/latest.log"
#define LOG_FILE2 LOG_DIR2 "/latest.log"

extern void *g_slots[];
extern const char * const g_sym_names[];
extern const int g_slot_count;
extern void fus_null_stub(void);

static int g_log_fd[2] = {-1, -1};
static int g_log_tried = 0;
static void *g_gl = NULL;
static void *g_egl = NULL;
static char g_self_dir[512];

static void open_log(void) {
    static const char *dirs[2]  = {LOG_DIR1, LOG_DIR2};
    static const char *files[2] = {LOG_FILE1, LOG_FILE2};
    g_log_tried = 1;
    for (int i = 0; i < 2; i++) {
        mkdir(dirs[i], 0777);
        int fd = open(files[i], O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) g_log_fd[i] = fd;
    }
}

static void fus_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, ap);
    va_end(ap);
    if (!g_log_tried) open_log();
    char ts[32], msg[1024];
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    int n = snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ",
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_start(ap, fmt);
    int m = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (m < 0) return;
    if (m > (int)sizeof(msg) - 1) m = (int)sizeof(msg) - 1;
    for (int i = 0; i < 2; i++) {
        int fd = g_log_fd[i];
        if (fd < 0) continue;
        if (write(fd, ts, (size_t)n) < 0) {}
        if (write(fd, msg, (size_t)m) < 0) {}
        if (write(fd, "\n", 1) < 0) {}
    }
}

// ---------- 版本判定：只有明确 >=1.17 才走 modern ----------
struct det { int pathL, pathM, kwL, kwM; char ver[64], src[32]; };

static void note(struct det *d, int a, int b, int c, int from_path, const char *src) {
    int legacy = (a < 1) || (a == 1 && b < 17);
    snprintf(d->ver, sizeof(d->ver), "%d.%d.%d", a, b, c);
    snprintf(d->src, sizeof(d->src), "%s", src);
    if (from_path) { if (legacy) d->pathL++; else d->pathM++; }
    else           { if (legacy) d->kwL++;   else d->kwM++;   }
}

static int contains_ci(const char *h, const char *n) {
    if (!h || !n) return 0;
    size_t ln = strlen(n);
    if (!ln) return 0;
    for (; *h; h++) {
        size_t i = 0;
        while (i < ln && h[i] &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++;
        if (i == ln) return 1;
    }
    return 0;
}

static void scan_text(struct det *d, const char *s) {
    if (!s || !*s) return;
    const char *p = s;
    while ((p = strstr(p, "versions/")) != NULL) {
        const char *id = p + 9;
        int a = 0, b = 0, c = 0;
        int n = sscanf(id, "%d.%d.%d", &a, &b, &c);
        if (n >= 2 && a == 1 && b >= 0 && b <= 40) {
            if (n < 3) c = 0;
            note(d, a, b, c, 1, "versions/");
        }
        p += 9;
    }
    static const char *pats[] = {
        "forge-1.", "neoforge-1.", "minecraft-1.", "mc-1.", "fabric-1.", NULL
    };
    for (int i = 0; pats[i]; i++) {
        p = s;
        const char *pat = pats[i];
        const char *dot = strchr(pat, '1');
        while ((p = strstr(p, pat)) != NULL) {
            const char *num = p + (dot - pat);
            int a = 0, b = 0, c = 0;
            int n = sscanf(num, "%d.%d.%d", &a, &b, &c);
            if (n >= 2 && a == 1 && b >= 0 && b <= 40) {
                if (n < 3) c = 0;
                note(d, a, b, c, 1, pat);
            }
            p += strlen(pat);
        }
    }
    if (contains_ci(s, "gtnh") || contains_ci(s, "1.7.10-forge") ||
        contains_ci(s, "forge-1.7.10") || contains_ci(s, "forge/1.7"))
        note(d, 1, 7, 10, 0, "kw-gtnh");
    if (contains_ci(s, "sodium") || contains_ci(s, "embeddium") ||
        contains_ci(s, "iris") || contains_ci(s, "oculus"))
        note(d, 1, 20, 1, 0, "kw-modern");
}

static void scan_file(struct det *d, const char *path, int nul_to_space) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
        buf[n] = 0;
        if (nul_to_space)
            for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
        scan_text(d, buf);
    }
    fclose(f);
}

static void scan_fds(struct det *d) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return;
    struct dirent *e;
    char link[256], target[1024];
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(link, sizeof(link), "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = 0;
        scan_text(d, target);
    }
    closedir(dir);
}

static void scan_versions_dir(struct det *d) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[640];
    snprintf(path, sizeof(path), "%s/.minecraft/versions", home);
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *e;
    int a = 0, b = 0, c = 0, count = 0;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        int x = 0, y = 0, z = 0;
        int n = sscanf(e->d_name, "%d.%d.%d", &x, &y, &z);
        if (n >= 2 && x == 1 && y >= 0 && y <= 40) {
            a = x; b = y; c = (n >= 3) ? z : 0; count++;
        }
    }
    closedir(dir);
    if (count == 1) note(d, a, b, c, 1, "versions-dir");
}

// GTNH / lwjgl3ify 整合包：MC 版本仍是 1.7.10，但 Angelica 要求 OpenGL 3.3
// core profile。所有 GL4ES 系内核都报 "GL 3.1 / core profile: false"，
// 会被 Angelica 在 Display.create 处直接拒绝（真机 4 次崩溃同一错误）。
// 因此只要检测到 lwjgl3ify / Angelica 这类现代 GL 栈，就强制走 core-profile
// 的 DirectVulkan 内核，哪怕 MC 版本号是 1.7.10。
static int has_modern_gl_stack(const char *root, const char *name) {
    // 不能只看 config/lwjgl3ify.cfg：GTNH 官方 Java 8 包里也带着这个配置残留。
    // 唯一可靠信号是 mods/ 里确实存在 lwjgl3ify 的 jar。
    char mods[1024];
    snprintf(mods, sizeof(mods), "%s/versions/%s/mods", root, name);
    DIR *dir = opendir(mods);
    if (!dir) return 0;
    struct dirent *e;
    int hit = 0;
    // 只认 lwjgl3ify：Angelica/Sodium/Embeddium 在 Java 8 版 GTNH 里同样存在，
    // 但 LWJGL2 下不要求 core profile；只有 lwjgl3ify(Java 17+) 才需要 modern 内核。
    while ((e = readdir(dir)) != NULL && !hit) {
        if (contains_ci(e->d_name, "lwjgl3ify")) hit = 1;
    }
    closedir(dir);
    return hit;
}

// ZL2 每次启动都把「当前版本名」写进 .minecraft/zalith-game.cfg。
// 版本目录里的 <name>.json，第一个 "version" 值就是 MC 版本（实测：
//   GregTech Leisure -> 1.20.1，GTNH 2.9.0 -> 1.7.10）。
// 分派层的 constructor 在 dlopen 时运行，那时 /proc/self/{cmdline,maps} 里
// 往往还没有游戏 classpath（版本目录多是中文/自定义名，不含数字），
// 旧版判定会全部落空而兜底 legacy，把 1.20.1 交给 gl4es ->
// LDLib 建 stencil FBO 时 GL_FRAMEBUFFER_UNSUPPORTED 崩溃。
// 这条配置文件链路与进程状态无关，最可靠，因此优先采用。
// 返回：1=modern, 0=legacy, -1=未判定（交给后续扫描）。
static int scan_zalith_cfg(struct det *d) {
    static const char *fixed[] = {
        "/sdcard/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft",
        "/storage/emulated/0/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft",
        NULL
    };
    const char *cands[3];
    char homeRoot[512];
    int n = 0;
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(homeRoot, sizeof(homeRoot), "%s/.minecraft", home);
        cands[n++] = homeRoot;
    }
    for (int i = 0; fixed[i] && n < 3; i++) cands[n++] = fixed[i];

    for (int r = 0; r < n; r++) {
        char cfg[640], name[256], vjson[1024];
        snprintf(cfg, sizeof(cfg), "%s/zalith-game.cfg", cands[r]);
        FILE *f = fopen(cfg, "rb");
        if (!f) continue;
        char buf[4096];
        size_t bn = fread(buf, 1, sizeof(buf) - 1, f);
        buf[bn] = 0;
        fclose(f);
        const char *p = strstr(buf, "\"version\"");
        if (!p) continue;
        p += 9;
        while (*p && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '"')) p++;
        size_t k = 0;
        while (p[k] && p[k] != '"' && p[k] != '\n' && k < sizeof(name) - 1) {
            name[k] = p[k]; k++;
        }
        name[k] = 0;
        while (k > 0 && (name[k-1] == ' ' || name[k-1] == '\t')) name[--k] = 0;
        if (!k) continue;

        snprintf(vjson, sizeof(vjson), "%s/versions/%s/%s.json", cands[r], name, name);
        FILE *j = fopen(vjson, "rb");
        if (!j) continue;
        static char jb[131072];
        size_t jn = fread(jb, 1, sizeof(jb) - 1, j);
        jb[jn] = 0;
        fclose(j);

        const char *q = jb;
        while ((q = strstr(q, "\"version\"")) != NULL) {
            const char *v = q + 9;
            while (*v && (*v == ':' || *v == ' ' || *v == '\t' || *v == '\n' || *v == '"')) v++;
            int a = 0, b = 0, c = 0;
            int got = sscanf(v, "%d.%d.%d", &a, &b, &c);
            if (got >= 2 && a == 1 && b >= 0 && b <= 40) {
                if (got < 3) c = 0;
                int modern = (a > 1 || (a == 1 && b >= 17));
                snprintf(d->ver, sizeof(d->ver), "%d.%d.%d", a, b, c);
                if (!modern && has_modern_gl_stack(cands[r], name)) {
                    snprintf(d->src, sizeof(d->src), "lwjgl3ify");
                    fus_log("zalith cfg: '%s' mc=%d.%d.%d + modern GL stack -> FORCE MODERN",
                            name, a, b, c);
                    return 1;
                }
                snprintf(d->src, sizeof(d->src), "zalith-cfg");
                if (modern) d->pathM++; else d->pathL++;
                fus_log("zalith cfg: '%s' -> mc=%d.%d.%d", name, a, b, c);
                return modern ? 1 : 0;
            }
            q += 9;
        }
    }
    return -1;
}

static int detect_modern(struct det *d) {
    memset(d, 0, sizeof(*d));
    int cfg = scan_zalith_cfg(d);
    if (cfg >= 0) return cfg;   // 配置文件给出明确版本，优先级最高
    scan_file(d, "/proc/self/maps", 0);
    scan_file(d, "/proc/self/cmdline", 1);
    scan_file(d, "/proc/self/environ", 1);
    scan_fds(d);
    if (d->pathL == 0 && d->pathM == 0 && d->kwL == 0 && d->kwM == 0)
        scan_versions_dir(d);
    if (d->pathM > 0 && d->pathL == 0) return 1;
    if (d->kwM   > 0 && d->kwL == 0 && d->pathL == 0) return 1;
    return 0; // 其余全部 legacy（保证进得去）
}

static void *open_kernel(const char *name) {
    char path[640];
    snprintf(path, sizeof(path), "%s/%s", g_self_dir, name);
    void *h = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    if (h) { fus_log("dlopen OK: %s", name); return h; }
    fus_log("dlopen miss: %s (%s)", name, dlerror() ? dlerror() : "?");
    return NULL;
}

// eglGetProcAddress 手写：按命名空间分流，绝不把 EGL 核心函数查询
// 交给系统 eglGetProcAddress（规范上它对核心函数返回 NULL，会让 SDL 拿到空指针）。
//   egl*  -> 直接返回 EGL 后端真实地址（legacy=系统 libEGL，modern=MGL 内核），零转发
//   gl*   -> 返回 GL 内核真实地址，避免 legacy 路径绕过 gl4es 转译层
void *eglGetProcAddress(const char *name) {
    if (!name) return NULL;
    if (!strncmp(name, "egl", 3)) {
        void *p = g_egl ? dlsym(g_egl, name) : NULL;
        if (p) return p;
        void *(*gp)(const char *) = g_egl
            ? (void *(*)(const char *))dlsym(g_egl, "eglGetProcAddress") : NULL;
        return gp ? gp(name) : NULL;
    }
    void *p = g_gl ? dlsym(g_gl, name) : NULL;
    if (p) return p;
    void *(*gp)(const char *) = g_egl
        ? (void *(*)(const char *))dlsym(g_egl, "eglGetProcAddress") : NULL;
    return gp ? gp(name) : NULL;
}

__attribute__((constructor(101)))
static void fus_construct(void) {
    Dl_info info;
    if (dladdr((void *)fus_construct, &info) && info.dli_fname) {
        const char *slash = strrchr(info.dli_fname, '/');
        if (slash && (size_t)(slash - info.dli_fname) < sizeof(g_self_dir) - 1) {
            size_t len = (size_t)(slash - info.dli_fname);
            memcpy(g_self_dir, info.dli_fname, len);
            g_self_dir[len] = 0;
        }
    }
    // G720 / TBDR 专用配置：源码与真机日志确认过的开关，必须在内核 dlopen 之前设好。
    // 1) 关 robustBufferAccess：Mali 上偏贵，且 VulkanRendererDevice.cpp:835 会因它
    //    屏蔽 descriptor indexing / update-after-bind，关掉可连带启用（每帧几百次更新是 Mali 最贵项）。
    // 2) 关 timer query：真机日志出现 "frame 1 timestamp pool exhausted"。
    // 3) G720 原生 subgroup size=16，无需模拟。
    // 4) TBDR 上 3 帧在飞较优。
    if (!getenv("MOBILEGL_MAGMA_DISABLE_ROBUST_BUFFER_ACCESS"))
        setenv("MOBILEGL_MAGMA_DISABLE_ROBUST_BUFFER_ACCESS", "1", 1);
    if (!getenv("MOBILEGL_DISABLE_TIMERQUERY"))
        setenv("MOBILEGL_DISABLE_TIMERQUERY", "1", 1);
    if (!getenv("MOBILEGL_SUBGROUP_EMULATION"))
        setenv("MOBILEGL_SUBGROUP_EMULATION", "ForceOff", 1);
    if (!getenv("MOBILEGL_FRAMES_IN_FLIGHT"))
        setenv("MOBILEGL_FRAMES_IN_FLIGHT", "3", 1);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    fus_log("========== FusionGL v3.0 (G720 tuned) ==========");
    fus_log("time %04d-%02d-%02d %02d:%02d:%02d pid=%d dir=%s",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)getpid(), g_self_dir);

    struct det d;
    int modern = detect_modern(&d);
    const char *f1 = getenv("FUSION_FORCE_MODERN");
    const char *f0 = getenv("FUSION_FORCE_LEGACY");
    if (f1 && f1[0] == '1') modern = 1;
    if (f0 && f0[0] == '1') modern = 0;
    fus_log("version detect: ver=%s src=%s pathL=%d pathM=%d kwL=%d kwM=%d -> %s",
            d.ver[0] ? d.ver : "-", d.src[0] ? d.src : "-",
            d.pathL, d.pathM, d.kwL, d.kwM, modern ? "MODERN" : "LEGACY");

    // legacy 必需：ZL2 Krypton Wrapper 官方 5 项预设。必须在 gl4es 内核 dlopen
    // 之前设好——gl4es 在加载/首次初始化时读环境变量，晚了就全部失效。
    if (!modern) {
        if (!getenv("LIBGL_USE_MC_COLOR")) setenv("LIBGL_USE_MC_COLOR", "1", 1);
        if (!getenv("LIBGL_GL"))          setenv("LIBGL_GL", "31", 1);
        if (!getenv("LIBGL_ES"))          setenv("LIBGL_ES", "3", 1);
        if (!getenv("LIBGL_NORMALIZE"))   setenv("LIBGL_NORMALIZE", "1", 1);
        if (!getenv("LIBGL_NOERROR"))     setenv("LIBGL_NOERROR", "1", 1);
        fus_log("legacy env: LIBGL_* presets applied before kernel load");
    }

    if (modern) {
        setenv("MOBILEGL_BACKEND_TYPE", "DirectVulkan", 1);
        // 高版本内核 DT_NEEDED libc++_shared.so。ZL2 的 LD_LIBRARY_PATH 不含插件目录，
        // dlopen 插件 so 时 linker 无法解析该依赖 -> 整个 dlopen 失败。
        // 先用绝对路径以 RTLD_GLOBAL 预载，linker 才能按 SONAME 命中。
        char cxx[640];
        snprintf(cxx, sizeof(cxx), "%s/libc++_shared.so", g_self_dir);
        void *hcxx = dlopen(cxx, RTLD_GLOBAL | RTLD_NOW);
        fus_log("preload %s -> %p", cxx, hcxx);
        // 只加载 G720 定制内核。找不到就明确失败，绝不退回通用/现成实现凑数。
        g_gl = open_kernel("libMobileGL.so");
        if (!g_gl) fus_log("FATAL: G720-tuned MobileGL kernel missing; no fallback by design");
        g_egl = g_gl;
        fus_log("backend: MobileGL G720-tuned DirectVulkan (gl=%p)", g_gl);
    } else {
        // 只加载 G720 定制的 NG-GL4ES（从可进 GTNH 的内核改出，兼容性自带）。
        g_gl = open_kernel("libng_gl4es_g720.so");
        if (!g_gl) fus_log("FATAL: G720-tuned NG-GL4ES kernel missing; no fallback by design");
        g_egl = dlopen("libEGL.so", RTLD_LOCAL | RTLD_LAZY);
        if (!g_egl) g_egl = dlopen("/system/lib64/libEGL.so", RTLD_LOCAL | RTLD_LAZY);
        fus_log("backend: NG-GL4ES (gl=%p egl=%p)", g_gl, g_egl);
    }

    int missing = 0;
    for (int i = 0; i < g_slot_count; i++) {
        const char *s = g_sym_names[i];
        void *h = (strncmp(s, "egl", 3) == 0) ? g_egl : g_gl;
        void *p = h ? dlsym(h, s) : NULL;
        if (!p) { p = (void *)fus_null_stub; missing++; }
        g_slots[i] = p;
    }
    fus_log("slots=%d resolved=%d missing=%d", g_slot_count,
            g_slot_count - missing, missing);

    // 关键符号自检：ZL2 egl_loader.c 需要的 19 个 EGL 符号 + gl4es 尺寸回调 + LWJGL 常用入口。
    // 任何一个落到 fus_null_stub 都会直接导致进不去，必须逐条打印，方便一次日志定位。
    static const char *critical[] = {
        "eglBindAPI", "eglChooseConfig", "eglCreateContext", "eglCreatePbufferSurface",
        "eglCreateWindowSurface", "eglDestroyContext", "eglDestroySurface",
        "eglGetConfigAttrib", "eglGetCurrentContext", "eglGetDisplay", "eglGetError",
        "eglInitialize", "eglMakeCurrent", "eglSwapBuffers", "eglReleaseThread",
        "eglSwapInterval", "eglTerminate", "eglGetCurrentSurface", "eglQuerySurface",
        "set_getmainfbsize", "glGetString", "glGetError", "glBegin", "glDrawArrays",
        "glCreateShader", "glViewport", "glClear", NULL
    };
    int crit_ok = 0, crit_bad = 0;
    for (int i = 0; critical[i]; i++) {
        const char *s = critical[i];
        // set_getmainfbsize 是 gl4es 专用的尺寸回调，MGL 内核本来就不导出；
        // MODERN 路径下落到空桩完全无害，不该报成故障。
        if (modern && strcmp(s, "set_getmainfbsize") == 0) { crit_ok++; continue; }
        void *h = (strncmp(s, "egl", 3) == 0) ? g_egl : g_gl;
        if (h && dlsym(h, s)) crit_ok++;
        else { crit_bad++; fus_log("CRITICAL MISSING: %s", s); }
    }
    fus_log("critical symbols ok=%d bad=%d (bad must be 0)", crit_ok, crit_bad);
    if (!g_gl) fus_log("FATAL: no GL kernel loaded");
    if (!g_egl) fus_log("FATAL: no EGL backend loaded");
}
