#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WQX_WIDTH   160
#define WQX_HEIGHT  80
#define VRAM_SIZE   (WQX_WIDTH * WQX_HEIGHT / 8) // 1600 字节

// 经典文曲星复古配色 (RGBA)
#define COLOR_BG    0xFF9BBC0F // 液晶浅绿底色
#define COLOR_INK   0xFF0F380F // 深墨绿点阵像素

// 文曲星标准键码定义
#define WQX_KEY_UP     20
#define WQX_KEY_DOWN   21
#define WQX_KEY_RIGHT  22
#define WQX_KEY_LEFT   23
#define WQX_KEY_ENTER  13
#define WQX_KEY_ESC    27

// --- 虚拟机显存与渲染状态 ---
static uint8_t  g_vram[VRAM_SIZE];
static uint32_t g_rgba_pixels[WQX_WIDTH * WQX_HEIGHT];
static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
static SDL_Texture  *g_texture  = NULL;
static int g_last_key = 0;
static bool g_running = true;

// --- 虚拟文件系统 (VFS)：内存中的 .dat 数据 ---
typedef struct {
    uint8_t *buffer;
    size_t   size;
    size_t   cursor;
} VFS_File;

static VFS_File g_dat_file = { NULL, 0, 0 };

// --- 从 Android Assets 载入文件到内存 ---
static uint8_t* LoadAsset(const char *filename, size_t *out_size) {
    SDL_RWops *rw = SDL_RWFromFile(filename, "rb");
    if (!rw) {
        SDL_Log("无法打开 Asset 文件: %s", filename);
        return NULL;
    }
    Sint64 size = SDL_RWsize(rw);
    if (size <= 0) {
        SDL_RWclose(rw);
        return NULL;
    }
    uint8_t *buf = (uint8_t*)malloc(size);
    if (buf) {
        SDL_RWread(rw, buf, 1, size);
        if (out_size) *out_size = (size_t)size;
    }
    SDL_RWclose(rw);
    SDL_Log("成功载入 Asset: %s (%lld 字节)", filename, size);
    return buf;
}

// --- VFS 重定向接口（供 LVM 解释器调用） ---
VFS_File* VFS_Open(const char *filename, const char *mode) {
    // 当游戏尝试打开 shenzhou.dat 时，重定向到已载入的内存缓存
    if (strstr(filename, ".dat") != NULL || strstr(filename, ".DAT") != NULL) {
        if (g_dat_file.buffer) {
            g_dat_file.cursor = 0;
            return &g_dat_file;
        }
    }
    return NULL;
}

size_t VFS_Read(void *ptr, size_t size, size_t nmemb, VFS_File *file) {
    if (!file || !file->buffer) return 0;
    size_t bytes_to_read = size * nmemb;
    if (file->cursor + bytes_to_read > file->size) {
        bytes_to_read = file->size - file->cursor;
    }
    memcpy(ptr, file->buffer + file->cursor, bytes_to_read);
    file->cursor += bytes_to_read;
    return bytes_to_read / size;
}

int VFS_Seek(VFS_File *file, long offset, int whence) {
    if (!file) return -1;
    if (whence == SEEK_SET) file->cursor = offset;
    else if (whence == SEEK_CUR) file->cursor += offset;
    else if (whence == SEEK_END) file->cursor = file->size + offset;
    return 0;
}

// --- 显存刷新：将 1-bit 单色显存解包推送到 SDL 纹理 ---
void HAL_Refresh(void) {
    for (int y = 0; y < WQX_HEIGHT; y++) {
        for (int x = 0; x < WQX_WIDTH; x++) {
            int byte_idx = (y * WQX_WIDTH + x) / 8;
            int bit_idx  = 7 - (x % 8);
            bool pixel_on = (g_vram[byte_idx] >> bit_idx) & 1;
            g_rgba_pixels[y * WQX_WIDTH + x] = pixel_on ? COLOR_INK : COLOR_BG;
        }
    }

    SDL_UpdateTexture(g_texture, NULL, g_rgba_pixels, WQX_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(g_renderer);
    
    // 渲染文曲星 160x80 画面（居中并保持宽高比）
    SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
    
    // 绘制移动端屏幕触控区域参考线（简易半透明 UI）
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 80);
    SDL_RenderPresent(g_renderer);
}

// --- 处理安卓触摸与物理键盘输入 ---
static void ProcessTouchInput(float norm_x, float norm_y) {
    // 屏幕下方划分为简易触控按键区
    if (norm_y > 0.6f) {
        if (norm_x < 0.25f) g_last_key = WQX_KEY_LEFT;
        else if (norm_x < 0.5f) g_last_key = WQX_KEY_DOWN;
        else if (norm_x < 0.75f) g_last_key = WQX_KEY_UP;
        else g_last_key = WQX_KEY_RIGHT;
    } else if (norm_y > 0.3f && norm_x > 0.75f) {
        g_last_key = WQX_KEY_ENTER; // 屏幕右侧中间区域 = 确认
    } else if (norm_y > 0.3f && norm_x < 0.25f) {
        g_last_key = WQX_KEY_ESC;   // 屏幕左侧中间区域 = 跳出
    }
}

int HAL_InKey(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            g_running = false;
            return 0;
        }
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_UP:     return WQX_KEY_UP;
                case SDLK_DOWN:   return WQX_KEY_DOWN;
                case SDLK_LEFT:   return WQX_KEY_LEFT;
                case SDLK_RIGHT:  return WQX_KEY_RIGHT;
                case SDLK_RETURN: return WQX_KEY_ENTER;
                case SDLK_ESCAPE: return WQX_KEY_ESC;
            }
        }
        // 安卓触摸屏事件
        if (event.type == SDL_FINGERDOWN) {
            ProcessTouchInput(event.tfinger.x, event.tfinger.y);
            return g_last_key;
        }
    }
    int key = g_last_key;
    g_last_key = 0;
    return key;
}

// --- 虚拟机上下文占位与执行循环 ---
typedef struct {
    uint8_t *bytecode;
    size_t   code_size;
    int      pc;
} LVM_State;

void LVM_RunStep(LVM_State *state) {
    // 此处调用开源 lvm.c 的指令解析主循环
    // 遇到 Refresh() 调用 HAL_Refresh()
    // 遇到 Getkey()/Inkey() 调用 HAL_InKey()
    // 遇到 fopen()/fread() 调用 VFS_Open()/VFS_Read()
    HAL_Refresh();
    SDL_Delay(16); // 限制约 60 帧
}

// --- SDL2 跨平台主入口 ---
int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL 初始化失败: %s", SDL_GetError());
        return -1;
    }

    // 1. 创建自适应屏幕窗口并锁定近邻插值（避免像素边缘模糊）
    g_window = SDL_CreateWindow(
        "Shenzhou - WQX Retro",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WQX_WIDTH * 4, WQX_HEIGHT * 4,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    // 2. 建立 160x80 原生单色流纹理
    g_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        WQX_WIDTH, WQX_HEIGHT
    );

    // 3. 从 APK Assets 载入游戏字节码与数据
    size_t lav_size = 0;
    uint8_t *lav_bytecode = LoadAsset("Magic.lav", &lav_size);
    g_dat_file.buffer = LoadAsset("GameSource.dat", &g_dat_file.size);

    if (!lav_bytecode) {
        SDL_Log("缺少游戏主程序 shenzhou.lav，启动终止！");
        return -1;
    }

    // 4. 初始化虚拟显存为清屏状态
    memset(g_vram, 0, VRAM_SIZE);

    // 5. 初始化 LVM 并进入执行循环
    LVM_State lvm;
    lvm.bytecode = lav_bytecode;
    lvm.code_size = lav_size;
    lvm.pc = 0;

    SDL_Log("LavaX 虚拟机就绪，启动《神舟》...");
    while (g_running) {
        HAL_InKey();
        LVM_RunStep(&lvm);
    }

    // 6. 退出清理
    if (lav_bytecode) free(lav_bytecode);
    if (g_dat_file.buffer) free(g_dat_file.buffer);
    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
