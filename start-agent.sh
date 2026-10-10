#!/bin/bash
# lan-agent 启动脚本（Phase 1 运维用）
# 用法: cd /H/aisrc/lan-agent && bash start-agent.sh [start|stop|restart|status]

set -e

AGENT_DIR=/H/aisrc/lan-agent
BUILD_DIR=$AGENT_DIR/build
PID_FILE=/H/usr/aisrc/lan-agent.pid
LOG_FILE=/H/usr/aisrc/lan-agent.log
CFG=$AGENT_DIR/codex_lan_agent.cfg
MACHINE_CODE=E651-9C30-F7F6-520F
LOOPBACK_FIX=1

# 环境变量（必须，否则 agent 找不到 llvm 等）
export PATH=/H/usr/aisrc/usr/bin:/usr/bin:/bin
export LD_LIBRARY_PATH=/H/usr/aisrc/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu
export AGENT_DIR

cmd=${1:-start}

# 从 cfg 读 irc_ws_host / irc_ws_port，判断是否需要本机 UnrealIRCd
CFG_HOST=$(grep -E '^\s*irc_ws_host\s*=' "$CFG" 2>/dev/null | head -1 | sed 's/.*=\s*//')
CFG_PORT=$(grep -E '^\s*irc_ws_port\s*=' "$CFG" 2>/dev/null | head -1 | sed 's/.*=\s*//')
IS_LOCAL_IRC=no
case "$CFG_HOST" in
    127.0.0.1|localhost|0.0.0.0|"") IS_LOCAL_IRC=yes ;;
esac
echo "  cfg irc_ws_host=${CFG_HOST:-127.0.0.1} irc_ws_port=${CFG_PORT:-8067} → local_irc=$IS_LOCAL_IRC"

case $cmd in
    start)
        # 确保 loopback UP（devuan 默认可能 down）
        if [ $LOOPBACK_FIX = 1 ] && ! ip link show lo 2>/dev/null | grep -q "state UP"; then
            echo "  ↑ lo 接口 DOWN，尝试 sudo ip link set lo up"
            sudo ip link set lo up || echo "  ⚠️ sudo lo up 失败，如果 sudo 要密码请手动执行"
        fi

        # 仅在直连本机 UnrealIRCd 时才启动（cfg 指向远程 nginx proxy 则跳过）
        if [ "$IS_LOCAL_IRC" = "yes" ]; then
            if ! pgrep -f unrealircd > /dev/null 2>&1; then
                echo "  ↑ 直连本机 UnrealIRCd，启动它"
                cd /H/aisrc/IRC/unrealircd
                ./unrealircd start
            fi
        else
            echo "  → cfg 指向远程频道，跳过本机 UnrealIRCd 启动"
        fi

        if [ -f "$PID_FILE" ] && kill -0 $(cat $PID_FILE) 2>/dev/null; then
            echo "  agent 已在跑 (pid=$(cat $PID_FILE))"
            exit 0
        fi

        echo "  启动 codex_lan_agent serve ..."
        cd $BUILD_DIR
        nohup ./codex_lan_agent serve \
            --config "$CFG" \
            --machine-code "$MACHINE_CODE" \
            > "$LOG_FILE" 2>&1 &
        echo $! > $PID_FILE
        sleep 3

        if kill -0 $(cat $PID_FILE) 2>/dev/null; then
            echo "  ✅ agent started pid=$(cat $PID_FILE)"
            tail -5 "$LOG_FILE"
        else
            echo "  ❌ agent 启动失败"
            tail -20 "$LOG_FILE"
            exit 1
        fi
        ;;

    stop)
        if [ -f "$PID_FILE" ]; then
            pid=$(cat $PID_FILE)
            if kill -0 $pid 2>/dev/null; then
                kill $pid
                sleep 1
                kill -9 $pid 2>/dev/null || true
                echo "  agent stopped pid=$pid"
            fi
            rm -f $PID_FILE
        fi
        pkill -f codex_lan_agent 2>/dev/null || true
        ;;

    restart)
        $0 stop
        sleep 1
        $0 start
        ;;

    status)
        if [ -f "$PID_FILE" ] && kill -0 $(cat $PID_FILE) 2>/dev/null; then
            echo "  agent running pid=$(cat $PID_FILE)  port=$(ss -tlnp 2>/dev/null | grep 18080 || echo '?')"
            tail -3 "$LOG_FILE"
        else
            echo "  agent not running"
        fi
        pgrep -af unrealircd || echo "  unreal not running"
        ;;

    *)
        echo "Usage: $0 [start|stop|restart|status]"
        exit 1
        ;;
esac
