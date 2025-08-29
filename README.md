# pdes-ptswitch-lkm

Richiede Kernel >= 6.0

## Build & run

1) monatre il modulo kernel
(dalla cartella kernel)
make
sudo insmod ptswitch.ko

2) leggere il major number assegnato
sudo dmesg | tail -n 20

3) creare il chardriver con il major number relativo e renderlo scrivibile
sudo mknod /dev/ptswitch c <MAJOR> 0
sudo chmod 666 /dev/ptswitch

4) compilare programma demo userland ed eseguirlo
gcc -o user_ioctl user_ioctl.c

5) rimuovere modulo
sudo rmmod ptswitch
