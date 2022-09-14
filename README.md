# Linux0.11
将Linux0.11修改为4G的虚拟内存模式，0-3G用户空间，3G-4G内核空间.  
可以挂载oldlinux上的文件系统.  
源代码基线是在网上找的，具体已经不知道出处了.  
纯粹是为了学习记录，如果有侵权请联系删除sudochen@163.com.  
# 编译
进入rootfs目录，将所有的tar.bz2解压  
执行make命令编译，默认使用内核堆栈进行任务切换，serial作为stdio等  
make qemu，使用qemu仿真，serial重定向到当前终端的标准输入输出上  
make help获取更多信息  
