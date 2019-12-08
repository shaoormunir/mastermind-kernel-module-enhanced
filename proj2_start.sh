#!/bin/sh
set -e
[ -f nf_cs421net.ko ] || make nf_cs421net.ko
sudo insmod nf_cs421net.ko
nc -d -k -l 4210 > /dev/null &
