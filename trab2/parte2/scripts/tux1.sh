#!/bin/bash

#IP Address -> 172.16.y0.1/24
ifconfig eth0 up
ifconfig eth0 172.16.50.1/24

#Defalt gw
route add default gw 172.16.50.254

#Disable ICMP echo-ignore-broadcast
echo 0 > /proc/sys/net/ipv4/icmp_echo_ignore_broadcasts

# configure dns
printf "search netlab.fe.up.pt\nnameserver 172.16.1.2" > /etc/resolv.conf

