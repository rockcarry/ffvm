+---------------------------+
 ffvm 是一个 riscv32 虚拟机 
+---------------------------+

ffvm 500 多行代码，就实现了一个 riscv32 的虚拟机

目前的 ffmv 已经支持以下特性：
1. 支持 rv32imac 指令集
2. 自带 64MB RAM 内存
3. 0xF0000000 以上的地址空间为 IO 寄存器
4. 默认以 50MHz 的频率运行


对应的 toolchain 和 test 程序项目地址：
https://github.com/rockcarry/riscv32-toolchain
https://github.com/rockcarry/riscv32-test

目前已经可以支持运行 2048 的小游戏了


TODO:
1. 后面要进一步 debug, 发现并解决指令 bug
2. 完成 IO 寄存器定义，包括显示、音频、按键


rockcarry
2020-11-5
