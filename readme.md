opacity_hotkey:Vitrite for Ubuntu

快速开始

1. 获取代码  
确保您拥有以下两个文件在同一目录下：
```
opacity_hotkey.cpp (源码)
run.sh (自动化脚本)
```

2. 赋予执行权限  
打开终端，进入项目目录，运行：
```sh
chmod +x run.sh
```

3. 运行程序

脚本会自动检查依赖、编译代码并启动程序。  

按 Ctrl+C 可终止程序。
```sh
./run.sh [-b/-s]
```

参数说明：

`./run.sh -b` 后台运行

`./run.sh -s` 关闭/杀死正在运行的程序

补充：  
运行脚本后，目录下可能会生成以下文件：
```
opacity_hotkey
:编译生成的可执行二进制文件

opacity_hotkey.pid
:记录后台运行的进程 ID (PID)

opacity_hotkey.log
:后台运行时的标准输出日志
```
