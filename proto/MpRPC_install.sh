#!/bin/bash

set -e

# 1. 并没有什么所谓的“标准清理命令”，rm 是最直接的
rm -rf `pwd`/build/*

# 2. 自动创建目录（如果不存在）
mkdir -p build 
cd build 

# 3. 配置
cmake ..

# 4. 编译
cmake --build . --parallel 4   
# 解释：这也是现代写法，等同于 make -j4，但同样支持 Ninja/VS 等

# 5. 安装部署
# 注意：写入 /usr/local 通常需要 root 权限，所以加 sudo
sudo cmake --install . 

# 6. 刷新库缓存
sudo ldconfig

# 7. 完成提示
echo "============================================================"
echo "Build & Install Complete!"
echo "Library Path: /usr/local/mprpc/lib"
echo "Include Path: /usr/local/mprpc/include"
echo "============================================================"