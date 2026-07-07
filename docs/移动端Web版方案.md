# 2048 移动端 Web 版技术方案

## 1. 整体架构

```
              同一份 C++ 源码
                    │
       ┌────────────┼────────────┐
       ▼                         ▼
 [Native 构建]            [Emscripten 构建]
  g++ + SDL2 本地             emcc + SDL2 port
       │                         │
       ▼                         ▼
  桌面可执行文件           index.html + .wasm
  (开发调试)              (手机浏览器打开)
```

**原则**：一份 C++ 源码，通过 `#ifdef __EMSCRIPTEN__` 条件编译处理平台差异，不写任何独立 JS 文件。

---

## 2. Emscripten 编译流程

### 2.1 CMakeLists.txt 改造

在现有 CMakeLists.txt 中增加 Emscripten 分支：

```cmake
if(EMSCRIPTEN)
    # SDL2 由 Emscripten port 提供，无需 find_package
    set(USE_FLAGS "-sUSE_SDL=2 -sUSE_SDL_TTF=2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")

    # 输出 .html + .js + .wasm
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # 链接选项
    target_link_options(game2048 PRIVATE
        -sUSE_SDL=2
        -sUSE_SDL_TTF=2
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_RUNTIME_METHODS=["cwrap"]
        --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
        --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
    )
else()
    # 现有 Native 构建逻辑保持不变
    find_package(SDL2 REQUIRED)
    ...
endif()
```

### 2.2 构建命令

```bash
# 安装 Emscripten (emsdk)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source emsdk_env.sh

# Web 构建
cd 2048-cpp
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-web

# 产物: build-web/game2048.html, .js, .wasm, .data
```

### 2.3 产物结构

```
build-web/
  game2048.html   ← 入口页面（自定义 shell）
  game2048.js     ← Emscripten 胶水代码
  game2048.wasm   ← 编译后的 WASM 二进制
  game2048.data   ← --preload-file 打包的资源（字体）
```

---

## 3. 主循环适配

当前 `Game::run()` 是阻塞式 `while(running_)`，Emscripten 要求让出控制权给浏览器事件循环。

### 改造方案

```cpp
// game.cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// 将单帧逻辑抽出
void Game::tick() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) { ... }
    float dt = calc_dt();
    renderer_.update_anims(dt);
    if (ai_active_ && ...) ai_step();
    renderer_.render(...);
}

void Game::run() {
#ifdef __EMSCRIPTEN__
    // 交给浏览器调度，0 = requestAnimationFrame
    emscripten_set_main_loop_arg(
        [](void* arg) { static_cast<Game*>(arg)->tick(); },
        this, 0, true);
#else
    while (running_) { tick(); SDL_Delay(1); }
#endif
}
```

影响范围：仅 `src/game.cpp`，抽取 `tick()` 方法。

---

## 4. 触控手势适配

### 4.1 滑动手势检测

SDL2 在 Emscripten 中将触摸事件映射为 `SDL_FINGERDOWN/UP/MOTION`。

```cpp
// game.cpp — handle_input 新增
case SDL_FINGERDOWN:
    touch_start_x_ = e.tfinger.x;  // 归一化 [0,1]
    touch_start_y_ = e.tfinger.y;
    touch_id_ = e.tfinger.fingerId;
    break;

case SDL_FINGERUP: {
    float dx = e.tfinger.x - touch_start_x_;
    float dy = e.tfinger.y - touch_start_y_;
    float threshold = 0.05f;  // 最小滑动距离（屏幕比例）
    if (fabs(dx) > threshold || fabs(dy) > threshold) {
        if (fabs(dx) > fabs(dy))
            do_move(dx > 0 ? RIGHT : LEFT);
        else
            do_move(dy > 0 ? DOWN : UP);
    }
    break;
}
```

### 4.2 防止浏览器默认行为

在 HTML shell 中禁止页面滚动和缩放：

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0,
      maximum-scale=1.0, user-scalable=no">
<style>
  html, body { touch-action: none; overscroll-behavior: none;
               overflow: hidden; margin: 0; }
  canvas { display: block; }
</style>
```

---

## 5. 自适应布局

### 5.1 动态获取屏幕尺寸

```cpp
void Game::init() {
#ifdef __EMSCRIPTEN__
    int w = EM_ASM_INT({ return window.innerWidth; });
    int h = EM_ASM_INT({ return window.innerHeight; });
    renderer_.init("2048", w, h);
#else
    renderer_.init("2048", 500, 730);
#endif
}
```

### 5.2 Renderer 响应式计算

`calc_layout()` 已用 `win_w_/win_h_` 计算布局，只需确保：

- 棋盘尺寸 = min(宽, 高 - header - toolbar) 取正方形
- 字体大小按棋盘尺寸比例缩放
- 按钮尺寸相对计算，不硬编码像素

### 5.3 屏幕旋转处理

监听 `resize` 事件重新计算：

```cpp
// 在 tick() 中检测
#ifdef __EMSCRIPTEN__
int new_w = EM_ASM_INT({ return window.innerWidth; });
int new_h = EM_ASM_INT({ return window.innerHeight; });
if (new_w != win_w_ || new_h != win_h_) {
    SDL_SetWindowSize(window_, new_w, new_h);
    calc_layout();
}
#endif
```

---

## 6. 字体嵌入

当前硬编码 Windows 字体路径，Web 版无法访问。

### 方案：嵌入开源字体

```
2048-cpp/
  assets/
    font.ttf   ← 选用 Inter 或 Noto Sans（开源、体积小、支持数字美观）
```

- 使用 `--preload-file assets@/assets` 打包到 .data 文件
- C++ 代码中加平台判断：

```cpp
const char* font_paths[] = {
#ifdef __EMSCRIPTEN__
    "/assets/font.ttf",
#else
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
#endif
};
```

推荐字体：**Inter**（~100KB subset，数字好看，开源 OFL 许可）。

---

## 7. 分数持久化（localStorage）

通过 `EM_ASM` 调用浏览器 localStorage，零 JS 文件：

```cpp
void save_best_score(int score) {
#ifdef __EMSCRIPTEN__
    EM_ASM_({ localStorage.setItem("best_score", $0.toString()); }, score);
#else
    // 写本地文件
#endif
}

int load_best_score() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        var v = localStorage.getItem("best_score");
        return v ? parseInt(v) : 0;
    });
#else
    // 读本地文件
#endif
}
```

---

## 8. PWA 离线支持

### 8.1 所需文件

```
web/
  shell.html       ← Emscripten HTML 模板
  manifest.json    ← PWA 清单
  sw.js            ← Service Worker（缓存 WASM + 资源）
  icon-192.png     ← 应用图标
  icon-512.png
```

### 8.2 manifest.json

```json
{
  "name": "2048 AI",
  "short_name": "2048",
  "start_url": ".",
  "display": "fullscreen",
  "orientation": "portrait",
  "theme_color": "#faf8ef",
  "background_color": "#faf8ef",
  "icons": [
    { "src": "icon-192.png", "sizes": "192x192", "type": "image/png" },
    { "src": "icon-512.png", "sizes": "512x512", "type": "image/png" }
  ]
}
```

### 8.3 Service Worker（sw.js）

```javascript
const CACHE = "2048-v1";
const FILES = ["./", "game2048.js", "game2048.wasm", "game2048.data"];
self.addEventListener("install", e => e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES))));
self.addEventListener("fetch", e => e.respondWith(caches.match(e.request).then(r => r || fetch(e.request))));
```

### 8.4 shell.html 中注册

```html
<script>
if ('serviceWorker' in navigator)
  navigator.serviceWorker.register('sw.js');
</script>
```

---

## 9. HTML Shell 模板

```html
<!DOCTYPE html>
<html lang="zh">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <link rel="manifest" href="manifest.json">
  <title>2048 AI</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    html, body { width: 100%; height: 100%; overflow: hidden;
                 touch-action: none; overscroll-behavior: none;
                 background: #faf8ef; }
    canvas { display: block; width: 100%; height: 100%; }
    #loading { position: fixed; inset: 0; display: flex;
               align-items: center; justify-content: center;
               font: 24px sans-serif; color: #776e65; }
  </style>
</head>
<body>
  <div id="loading">Loading...</div>
  <canvas id="canvas" oncontextmenu="event.preventDefault()"></canvas>
  <script>
    var Module = {
      canvas: document.getElementById("canvas"),
      onRuntimeInitialized: function() {
        document.getElementById("loading").style.display = "none";
      }
    };
  </script>
  {{{ SCRIPT }}}
</body>
</html>
```

---

## 10. 性能预估

| 指标 | 预估 | 说明 |
|------|------|------|
| WASM 体积 | ~200-300KB (gzip) | 核心逻辑 + SDL2 port + AI |
| .data (字体) | ~100KB (gzip) | Inter subset |
| 首次加载 | < 1s (4G) | 总传输 < 500KB |
| AI 每步耗时 | ~15-30ms | 手机 WASM 约为 Native 80% |
| 帧率 | 60fps | Canvas 2D + requestAnimationFrame |
| 内存 | ~10MB | Lookup tables + 渲染缓冲 |

---

## 11. 部署方案

### 推荐：GitHub Pages

```bash
# build-web 产物直接推送到 gh-pages 分支
cd build-web
git init && git add -A
git commit -m "deploy"
git push -f https://github.com/USER/2048-cpp.git main:gh-pages
```

### 备选

- **Vercel/Netlify**：直接拖拽 build-web 文件夹
- **Cloudflare Pages**：免费 + 全球 CDN

---

## 12. 需要改动的文件清单

| 文件 | 改动 | 说明 |
|------|------|------|
| `CMakeLists.txt` | 增加 `if(EMSCRIPTEN)` 分支 | 链接选项、shell file、preload |
| `src/game.cpp` | 主循环改 `emscripten_set_main_loop`；加触控处理 | 条件编译 |
| `include/game.h` | 加 `tick()` 方法、触控状态字段 | |
| `src/renderer.cpp` | 字体路径分平台；动态布局 | 条件编译 |
| `web/shell.html` | 新建 | 自定义 HTML 模板 |
| `web/manifest.json` | 新建 | PWA 清单 |
| `web/sw.js` | 新建 | Service Worker |
| `assets/font.ttf` | 新建 | 嵌入字体 |

---

## 13. 实施路径

分三步走，每步可独立验证：

1. **基础可运行** — CMake Emscripten 分支 + 主循环适配 + 字体嵌入 → 浏览器能看到棋盘
2. **触控 + 响应式** — 滑动手势 + 全屏自适应 + viewport 配置 → 手机可玩
3. **PWA + 部署** — manifest + sw.js + 图标 + GitHub Pages → 可安装离线玩
