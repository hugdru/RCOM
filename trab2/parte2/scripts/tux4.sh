#!/bin/bash

#IP Address -> 172.16.Y0.254/24
ifconfig eth0 up
ifconfig eth0 172.16.50.254/24

#IP Address -> 172.16.y1.253/24
ifconfig eth1 up
ifconfig eth1 172.16.51.253/24

#Defalt gw
route add default gw 172.16.51.254

#Disable ICMP echo-ignore-broadcast
echo 0 > /proc/sys/net/ipv4/icmp_echo_ignore_broadcasts

#Enable IP forwarding
echo 1 > /proc/sys/net/ipv4/ip_forward

printf "search netlab.fe.up.pt\nnameserver 172.16.1.2" > /etc/resolv.conf
