#!/bin/bash

#IP Address -> 172.16.y1.1/24
ifconfig eth0 up
ifconfig eth0 172.16.51.1/24

#Route para comunicar com tux1
route add -net 172.16.50.0/24 gw 172.16.51.253

#Defalt gw
route add default gw 172.16.51.254

#Disable ICMP echo-ignore-broadcast
echo 0 > /proc/sys/net/ipv4/icmp_echo_ignore_broadcasts

printf "search netlab.fe.up.pt\nnameserver 172.16.1.2" > /etc/resolv.conf
