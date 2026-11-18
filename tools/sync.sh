#!/bin/bash
# sync.sh —— 把本机 nachos-3.4-ualr-2022/ 的源码同步到远端构建目录。
#
# 本机 git 仓库是唯一的“真相来源”，远端只用来编译运行。
# 只传 git 跟踪的文件和未被忽略的新文件，构建产物不会被覆盖；tar 保留 mtime，
# 所以远端 make 只会重编改过的文件。
#
# 用法：tools/sync.sh [ssh别名，默认 oscp-temp]
set -euo pipefail
HOST=${1:-oscp-temp}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
git ls-files -z --cached --others --exclude-standard -- nachos-3.4-ualr-2022 \
  | tar czf - --null -T - \
  | ssh "$HOST" 'mkdir -p /root/oscp && tar xzf - -C /root/oscp --no-same-owner'
# Windows 磁盘上的文件没有可执行位，tar 覆盖后远端会丢失 x 权限；
# 按 git 索引里记录的 100755 模式恢复(参考程序 n2..n7 等)
git ls-files -s -- nachos-3.4-ualr-2022 | awk '$1 == "100755" { print $4 }' \
  | ssh "$HOST" 'cd /root/oscp && xargs -r chmod +x'
echo "已同步到 $HOST:/root/oscp/nachos-3.4-ualr-2022"
