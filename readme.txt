+---------------------------+
 ffvm 是一个 riscv32 虚拟机 
+---------------------------+

ffvm 500 多行代码，就实现了一个 riscv32 的虚拟机

目前的 ffmv 已经支持以下特性：
1. 支持 rv32imac 指令集
2. 自带 64MB RAM 内存
3. 0xFF000000 以上的地址空间为 IO 寄存器
4. 默认以 100MHz 的频率运行
5. cpu 频率可通过寄存器动态调节
6. 支持 disp 显示控制器
7. 支持音频输入输出控制器
8. 支持鼠标键盘输入功能
9. 支持 riscv 标准时钟中断
10.支持 RTC 实时日历时钟
11.支持存储设备（块设备）
12.支持以太网 phy 设备

对应的 toolchain 和 test 程序项目地址：
https://github.com/rockcarry/riscv32-toolchain
https://github.com/rockcarry/riscv32-test

目前已经可以支持运行 2048、俄罗斯方块、贪吃蛇等小游戏，可以运行 lvgl 图形 GUI 程序，fatfs 文件系统，fftask 多任务等。


rockcarry
2020-11-5
