# a demo linux project for HISI hi3516cv610_20s



这是一个海思hi3516cv610_20s的测试工程，用于开发一个IPC（网络摄像头）的功能，是一个比较适合自己开发练习的项目。

目前支持：

1. 两路码流，h264 1920x1080@15fps ，h264 640x360@25fps

2. auto ir

3. 录制两路码流到sdcard，存为mp4文件

4. 设备发现功能

5. 日志输出到syslogd，logread读取输出到stdout

6. 码流中实时显示osd时间戳

7. 后续还会再优化功能，加入更多功能....

## 硬件环境：

1 开发板：[迅为iTOP-Hi3516CV610开发板海思安防监控1Tops算力AI智能视觉-淘宝网](https://item.taobao.com/item.htm?ali_refid=a3_430582_1006%3A1348050097%3AN%3A19UEY2W%2BBu17H4NQxS3FSw%3D%3D%3Afd2f053c88bad274c7eae646a51bb1a1&ali_trackid=1_fd2f053c88bad274c7eae646a51bb1a1&id=977007282042&mi_id=0000k4r3ojOtUWfWwGu0n8MdswFIn8FiKtTKaaGef3Uonec&mm_sceneid=1_0_932850012_0&skuId=5950616805238&spm=a21n57.1.hoverItem.1&utparam=%7B%22aplus_abtest%22%3A%22ed7b620fb9fdccaf35095242dded014e%22%7D&xxc=ad_ztc)

2 使用开发板默认配置环境就可以运行（最好是release编译），默认环境os内存仅32MB，比较紧张，建议将os内存提到48MB。

## 软件环境：

### 1 build system

Linux topeet 6.8.0-111-generic #111~22.04.1-Ubuntu SMP PREEMPT_DYNAMIC Tue Apr 14 17:13:45 UTC  x86_64 x86_64 x86_64 GNU/Linux

### 2 board system

Linux (none) 5.10.221 #3 SMP Tue May 19 17:51:49 CST 2026 armv7l GNU/Linux

### 3 cross-toolchain

arm-linux-musleabi-

### 4 board rootfs

BusyBox v1.34.1 (2026-01-06 19:49:28 CST) multi-call binary.
BusyBox is copyrighted by many authors between 1998-2015.
Licensed under GPLv2. See source distribution for detailed
copyright notices.

Usage: busybox [function [arguments]...]
   or: busybox --list[-full]
   or: busybox --show SCRIPT
   or: busybox --install [-s] [DIR]
   or: function [arguments]...
        BusyBox is a multi-call binary that combines many common Unix
        utilities into a single executable.  Most people will create a
        link to busybox for each function they wish to use and BusyBox
        will act like whatever it was invoked as.

Currently defined functions:
        [, [[, acpid, add-shell, addgroup, adduser, adjtimex, arch, arp,
        arping, ascii, ash, awk, base32, base64, basename, bc, beep,
        blkdiscard, blkid, blockdev, bootchartd, brctl, bunzip2, bzcat, bzip2,
        cal, cat, chat, chattr, chgrp, chmod, chown, chpasswd, chpst, chroot,
        chrt, chvt, cksum, clear, cmp, comm, conspy, cp, cpio, crc32, crond,
        crontab, cryptpw, cttyhack, cut, date, dc, dd, deallocvt, delgroup,
        deluser, depmod, devmem, df, dhcprelay, diff, dirname, dmesg, dnsd,
        dnsdomainname, dos2unix, dpkg, dpkg-deb, du, dumpkmap, dumpleases,
        echo, ed, egrep, eject, env, envdir, envuidgid, ether-wake, expand,
        expr, factor, fakeidentd, fallocate, false, fatattr, fbset, fbsplash,
        fdflush, fdformat, fdisk, fgconsole, fgrep, find, findfs, flock, fold,
        free, freeramdisk, fsck, fsck.minix, fsfreeze, fstrim, fsync, ftpd,
        ftpget, ftpput, fuser, getopt, getty, grep, groups, gunzip, gzip, halt,
        hd, hdparm, head, hexdump, hexedit, hostid, hostname, httpd, hush,
        hwclock, i2cdetect, i2cdump, i2cget, i2cset, i2ctransfer, id, ifconfig,
        ifdown, ifenslave, ifplugd, ifup, inetd, init, insmod, install, ionice,
        iostat, ip, ipaddr, ipcalc, ipcrm, ipcs, iplink, ipneigh, iproute,
        iprule, iptunnel, kbd_mode, kill, killall, killall5, klogd, last, less,
        link, linux32, linux64, linuxrc, ln, loadfont, loadkmap, logger, login,
        logname, logread, losetup, lpd, lpq, lpr, ls, lsattr, lsmod, lsof,
        lspci, lsscsi, lsusb, lzcat, lzma, lzop, makedevs, makemime, man,
        md5sum, mdev, mesg, microcom, mim, mkdir, mkdosfs, mke2fs, mkfifo,
        mkfs.ext2, mkfs.minix, mkfs.vfat, mknod, mkpasswd, mkswap, mktemp,
        modinfo, modprobe, more, mount, mountpoint, mpstat, mt, mv, nameif,
        nanddump, nandwrite, nbd-client, nc, netstat, nice, nl, nmeter, nohup,
        nologin, nproc, nsenter, nslookup, ntpd, od, openvt, partprobe, passwd,
        paste, patch, pgrep, pidof, ping, ping6, pipe_progress, pivot_root,
        pkill, pmap, popmaildir, poweroff, powertop, printenv, printf, ps,
        pscan, pstree, pwd, pwdx, raidautorun, rdate, rdev, readahead,
        readlink, readprofile, realpath, reboot, reformime, remove-shell,
        renice, reset, resize, resume, rev, rm, rmdir, rmmod, route, rpm,
        rpm2cpio, rtcwake, run-init, run-parts, runlevel, runsv, runsvdir, rx,
        script, scriptreplay, sed, sendmail, seq, setarch, setconsole,
        setfattr, setfont, setkeycodes, setlogcons, setpriv, setserial, setsid,
        setuidgid, sh, sha1sum, sha256sum, sha3sum, sha512sum, showkey, shred,
        shuf, slattach, sleep, smemcap, softlimit, sort, split, ssl_client,
        start-stop-daemon, stat, strings, stty, su, sulogin, sum, sv, svc,
        svlogd, svok, swapoff, swapon, switch_root, sync, sysctl, syslogd, tac,
        tail, tar, taskset, tc, tcpsvd, tee, telnet, telnetd, test, tftp,
        tftpd, time, timeout, top, touch, tr, traceroute, traceroute6, true,
        truncate, ts, tty, ttysize, tunctl, ubiattach, ubidetach, ubimkvol,
        ubirename, ubirmvol, ubirsvol, ubiupdatevol, udhcpc, udhcpc6, udhcpd,
        udpsvd, uevent, umount, uname, unexpand, uniq, unix2dos, unlink,
        unlzma, unshare, unxz, unzip, uptime, users, usleep, uudecode,
        uuencode, vconfig, vi, vlock, volname, w, wall, watch, watchdog, wc,
        wget, which, who, whoami, whois, xargs, xxd, xz, xzcat, yes, zcat,
        zcip



## 编译使用

> ./build.sh Debug
> 
> ./build.sh Release
