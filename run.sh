#!/bin/bash

# ─── 颜色定义 ───────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ─── 路径与文件配置 ─────────────────────────────────────────
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SRC_FILE="opacity_hotkey.cpp"
BIN_FILE="opacity_hotkey"

# 必需的 apt 依赖包
REQUIRED_DEPS=("build-essential" "pkg-config" "libgtk-3-dev" "libx11-dev")

# ─── 1. 检查并安装依赖 ──────────────────────────────────────
echo -e "\n${GREEN}[1/4] 检查系统依赖...${NC}"
MISSING_DEPS=()

for dep in "${REQUIRED_DEPS[@]}"; do
    # 使用 dpkg -s 检查包是否已安装
    if ! dpkg -s "$dep" >/dev/null 2>&1; then
        MISSING_DEPS+=("$dep")
    fi
done

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo -e "${YELLOW}  发现缺失依赖: ${MISSING_DEPS[*]}${NC}"
    echo -e "${YELLOW}  正在请求 sudo 权限进行安装...${NC}"
    
    # 更新软件源并安装
    sudo apt-get update -qq
    sudo apt-get install -y "${MISSING_DEPS[@]}"
    
    if [ $? -ne 0 ]; then
        echo -e "${RED} 依赖安装失败！请检查网络连接或手动执行: sudo apt install ${MISSING_DEPS[*]}${NC}"
        exit 1
    fi
    echo -e "${GREEN} 依赖安装完成。${NC}"
else
    echo -e "${GREEN} 所有依赖均已安装。${NC}"
fi

# ─── 2. 检查 Wayland 环境 ───────────────────────────────────
echo -e "\n${GREEN}[2/4] 检查显示服务器环境...${NC}"
if [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    echo -e "${YELLOW}  [!] 警告: 检测到当前为 Wayland 会话。${NC}"
    echo -e "${YELLOW}      本程序依赖 X11 特性 (XGrabKey, _NET_WM_WINDOW_OPACITY)。${NC}"
    echo -e "${YELLOW}      在 Wayland 下快捷键和透明度设置可能完全无效！${NC}"
    echo -e "${YELLOW}      建议在登录界面齿轮处选择 'Ubuntu on Xorg'。${NC}"
    read -p "  是否仍要继续启动？(y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo -e "${RED} 用户取消启动。${NC}"
        exit 0
    fi
else
    echo -e "${GREEN} 当前为 X11 会话 (Xorg)，环境正常。${NC}"
fi

# ─── 3. 智能编译 ────────────────────────────────────────────
echo -e "\n${GREEN}[3/4] 检查并编译源码...${NC}"

if [ ! -f "$SCRIPT_DIR/$SRC_FILE" ]; then
    echo -e "${RED} 找不到源码文件: $SCRIPT_DIR/$SRC_FILE${NC}"
    echo -e "${RED}   请确保 run.sh 和 opacity_hotkey.cpp 在同一目录下。${NC}"
    exit 1
fi

NEED_COMPILE=0
# 如果可执行文件不存在，或者源码比可执行文件新，则需要编译
if [ ! -f "$SCRIPT_DIR/$BIN_FILE" ]; then
    NEED_COMPILE=1
elif [ "$SCRIPT_DIR/$SRC_FILE" -nt "$SCRIPT_DIR/$BIN_FILE" ]; then
    NEED_COMPILE=1
fi

if [ $NEED_COMPILE -eq 1 ]; then
    echo -e "${YELLOW}  正在编译 $SRC_FILE ...${NC}"
    cd "$SCRIPT_DIR"
    
    # 执行编译命令
    g++ -O2 -o "$BIN_FILE" "$SRC_FILE" $(pkg-config --cflags --libs gtk+-3.0 x11) -std=c++17
    
    if [ $? -ne 0 ]; then
        echo -e "${RED} Error: 编译失败！请检查上方错误信息。${NC}"
        exit 1
    fi
    echo -e "${GREEN}编译成功。${NC}"
else
    echo -e "${GREEN}源码未修改，跳过编译。${NC}"
fi

# ─── 4. 启动程序 ────────────────────────────────────────────
echo -e "\n${GREEN}[4/4] 准备启动程序...${NC}"

cd "$SCRIPT_DIR"

# 定义 PID 文件和 Log 文件路径
PID_FILE="$SCRIPT_DIR/opacity_hotkey.pid"
LOG_FILE="$SCRIPT_DIR/opacity_hotkey.log"

# 判断是否传入 -b 或 --bg 参数
if [ "$1" = "-b" ] || [ "$1" = "--bg" ]; then
    echo -e "${BLUE}  [*] 模式: 后台运行 (终端将自动关闭)${NC}"
    
    # 检查是否已经有一个实例在运行
    if [ -f "$PID_FILE" ]; then
        OLD_PID=$(cat "$PID_FILE")
        if kill -0 "$OLD_PID" 2>/dev/null; then
            echo -e "${YELLOW}  [!] 检测到程序已在运行 (PID: $OLD_PID)。${NC}"
            read -p "  是否重启？(y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                exit 0
            fi
            echo -e "${YELLOW}  [*] 正在停止旧进程...${NC}"
            kill "$OLD_PID"
            sleep 1
        fi
        rm -f "$PID_FILE"
    fi

    # 使用 nohup 后台运行
    # 注意：nohup 会忽略 SIGHUP，& 表示后台运行
    nohup ./"$BIN_FILE" > "$LOG_FILE" 2>&1 &
    NEW_PID=$!
    
    # 将 PID 写入文件
    echo "$NEW_PID" > "$PID_FILE"
    
    echo -e "${GREEN}  [✓] 程序已在后台启动！${NC}"
    echo -e "${GREEN}      PID: $NEW_PID (已保存至 $PID_FILE)${NC}"
    echo -e "${GREEN}      Log: $LOG_FILE${NC}"
    echo -e "${BLUE}  提示: 可通过右键系统托盘图标退出，或执行 ./run.sh -s 停止。${NC}"
    exit 0

elif [ "$1" = "-s" ] || [ "$1" = "--stop" ]; then
    # 新增：停止功能
    if [ -f "$PID_FILE" ]; then
        STOP_PID=$(cat "$PID_FILE")
        if kill -0 "$STOP_PID" 2>/dev/null; then
            echo -e "${GREEN}  [*] 正在停止进程 (PID: $STOP_PID)...${NC}"
            kill "$STOP_PID"
            # 等待进程完全退出
            for i in {1..5}; do
                if ! kill -0 "$STOP_PID" 2>/dev/null; then
                    break
                fi
                sleep 0.5
            done
            # 如果还在运行，强制杀死
            if kill -0 "$STOP_PID" 2>/dev/null; then
                kill -9 "$STOP_PID"
            fi
            rm -f "$PID_FILE"
            echo -e "${GREEN}  [✓] 程序已停止。${NC}"
        else
            echo -e "${YELLOW}  [!] 进程 (PID: $STOP_PID) 不存在，清理无效 PID 文件。${NC}"
            rm -f "$PID_FILE"
        fi
    else
        echo -e "${YELLOW}  [!] 未找到 PID 文件，尝试通过进程名停止...${NC}"
        killall opacity_hotkey 2>/dev/null && echo -e "${GREEN}  [✓] 已发送停止信号。${NC}" || echo -e "${RED}  [✗] 未找到运行中的进程。${NC}"
    fi
    exit 0

else
    # 前台运行模式
    echo -e "${BLUE}  模式: 前台运行 (按 Ctrl+C 可终止)${NC}"
    echo -e "${BLUE}  提示: 若要后台运行，请使用 ./run.sh -b${NC}"
    echo -e "${BLUE}  提示: 若要停止后台进程，请使用 ./run.sh -s${NC}"
    echo -e "${BLUE}──────────────────────────────────────────────${NC}"
    
    # 前台运行时也更新一下 PID 文件（可选，方便其他脚本检测）
    echo $$ > "$PID_FILE"
    
    # 捕获退出信号以清理 PID 文件
    trap 'rm -f "$PID_FILE"' EXIT
    
    exec ./"$BIN_FILE"
fi