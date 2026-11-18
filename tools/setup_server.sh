#!/bin/bash
# setup_server.sh —— 在一台全新的 x86_64 Ubuntu (含 AutoDL 容器) 上搭建 Nachos 实验环境。
#
# 用法(在本机 Git Bash 里，于仓库根目录执行)：
#   tools/setup_server.sh <ssh主机别名>        例如: tools/setup_server.sh oscp-temp
#
# 做的事：
#   1. 安装 gcc-multilib g++-multilib gdb
#   2. 上传并解压 MIPS 交叉编译器到 /usr/local/mips，nachos 源码到 /root/oscp
#      (源码包含符号链接，必须在 Linux 上解压)
#   3. 若文件系统 inode 号超过 32 位，为 MIPS 工具链装 LD_PRELOAD 垫片(见 stat64shim.c)
#   4. 编译 bin threads userprog filesys network test
# 可重复执行；已存在的 /root/oscp/nachos-3.4-ualr-2022 不会被覆盖。
set -euo pipefail

HOST=${1:?用法: $0 <ssh主机别名>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG="$ROOT/OSCP/安装环境"

echo ">>> [1/4] 安装软件包"
ssh "$HOST" 'export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq gcc-multilib g++-multilib gdb file >/dev/null && echo done'

echo ">>> [2/4] 上传并解压安装包"
ssh "$HOST" 'mkdir -p /root/oscp/pkgs'
for f in nachos-3.4-ualr-2022.tar.gz gcc-2.8.1-mips.tar.gz; do
    ssh "$HOST" "cat > /root/oscp/pkgs/$f" < "$PKG/$f"
done
ssh "$HOST" 'set -e
[ -x /usr/local/mips/bin/decstation-ultrix-gcc ] || tar xzf /root/oscp/pkgs/gcc-2.8.1-mips.tar.gz -C /usr/local
[ -d /root/oscp/nachos-3.4-ualr-2022 ] || tar xzf /root/oscp/pkgs/nachos-3.4-ualr-2022.tar.gz -C /root/oscp
echo done'

echo ">>> [3/4] 检查 inode 号，必要时安装 stat64 垫片"
ssh "$HOST" 'cat > /usr/local/mips/lib/stat64shim.c' < "$ROOT/tools/stat64shim.c"
ssh "$HOST" 'set -e
M=/usr/local/mips
INO=$(stat -c %i $M/lib/gcc-lib/decstation-ultrix/2.8.1/specs)
if [ "$INO" -le 4294967295 ] && [ ! -d $M/bin-real ]; then echo "inode=$INO，无需垫片"; exit 0; fi
gcc -m32 -shared -fPIC -O2 -o $M/lib/libstat64shim.so $M/lib/stat64shim.c
mkdir -p $M/bin-real
for t in gcc as ld; do
    b=decstation-ultrix-$t
    [ -f $M/bin-real/$b ] || mv $M/bin/$b $M/bin-real/$b
    printf "#!/bin/sh\n# 包装器：为 32 位老工具链注入 stat64 垫片，见 %s/lib/stat64shim.c\nLD_PRELOAD=%s/lib/libstat64shim.so\${LD_PRELOAD:+:\$LD_PRELOAD} exec %s/bin-real/%s \"\$@\"\n" $M $M $M $b > $M/bin/$b
    chmod 755 $M/bin/$b
done
echo "inode=$INO，已安装垫片和包装器"'

echo ">>> [4/4] 编译基础模块"
ssh "$HOST" 'cd /root/oscp/nachos-3.4-ualr-2022/code
for d in bin threads userprog filesys network test; do
    if make -C $d > /root/oscp/build_$d.log 2>&1; then echo "  $d: OK"; else echo "  $d: FAIL (见 /root/oscp/build_$d.log)"; tail -5 /root/oscp/build_$d.log; fi
done'
echo ">>> 完成"
