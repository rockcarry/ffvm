+---------------------------+
 ffvm 是一个 riscv32 虚拟机 
+---------------------------+

ffvm 500 多行代码，就实现了一个 riscv32 的虚拟机

目前的 ffmv 已经支持以下特性：
1. 支持 rv32imac 指令集
2. 自带 64MB RAM 内存
3. 0xFF000000 以上的地址空间为 IO 寄存器
4. 默认以 100MHz 的频率运行


对应的 toolchain 和 test 程序项目地址：
https://github.com/rockcarry/riscv32-toolchain
https://github.com/rockcarry/riscv32-test

目前已经可以支持运行 2048、俄罗斯方块、贪吃蛇等小游戏了
支持 disp 显示控制器，32bit 真彩色显示

TODO:
1. 后面要进一步 debug, 发现并解决指令 bug
2. 实现音频、按键等 IO 寄存器功能


rockcarry
2020-11-5
