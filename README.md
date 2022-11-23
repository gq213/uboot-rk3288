
1、编译

make firefly-rk3288_defconfig

make V=1


2、烧录

sudo dd if=u-boot-rockchip.bin of=/dev/sdb seek=64 bs=512

或

sudo dd if=idbloader.img of=/dev/sdb seek=64 bs=512

sudo dd if=u-boot.img of=/dev/sdb seek=16384 bs=512


3、kernel和dtb加载

默认从Micro SD卡的第2分区加载，如果想默认从tftp加载

setenv bootcmd "run bootk"

saveenv


4、Micro SD卡使用gpt分区表，分区如下：

sudo gdisk /dev/sdb

Found valid GPT with protective MBR; using GPT.

Disk identifier (GUID): 74E035E9-4E55-4DC3-A534-D9A8EB1D1490

Number  Start (sector)    End (sector)  Size       Code  Name

   1          264192          788479   256.0 MiB   8300  new_rootfs

   2          788480        62333918   29.3 GiB    8300  Linux filesystem

rootfs放到第1分区，分区名称必须是"new_rootfs"；kernel和dtb放到第2分区。


注意：

如果板子emmc之前烧录过固件，上电之后不会从Micro SD卡启动，需要先擦除emmc里面的引导。

1、可以正常进入之前的系统，然后执行

dd if=/dev/zero of=/dev/mmcblkX bs=512 seek=64 count=7104

2、使用RKDevTool.exe擦除

起始扇区：0x40；扇区数：0x1bc0
