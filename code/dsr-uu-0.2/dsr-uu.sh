#!/bin/bash
command=$1
IFNAME=eth1
DSRUUPATH=/lib/modules/`uname -r`/dsr/
MODPREFIX=ko

killproc() {
    pidlist=$(/sbin/pidof $1)
    for pid in $pidlist; do
	kill $pid &>/dev/null
    done
    return 0
}

if [ -z "$1" ]; then
    echo "Must specify \"start\" or \"stop\""
    exit
fi

if [ -n "$2" ]; then
    IFNAME=$2
fi

echo "Slave interface is $IFNAME"

if [ "$command" = "start" ]; then 
   
    # Start DSR-UU
    IP=`/sbin/ifconfig $IFNAME | grep inet`
    IP=${IP%%" Bcast:"*}
    IP=${IP##*"inet addr:"}
    echo $IP > .$IFNAME.ip
    host_nr=`echo $IP | awk 'BEGIN{FS="."} { print $4 }'`

    if [ -f $DSRUUPATH/linkcache.$MODPREFIX ] && [ -f $DSRUUPATH/dsr.$MODPREFIX ]; then
	# Reconfigure the default interface
	insmod $DSRUUPATH/linkcache.$MODPREFIX
	insmod $DSRUUPATH/dsr.$MODPREFIX ifname=$IFNAME
	#/sbin/ifconfig $IFNAME 192.168.45.$host_nr up
	/sbin/ifconfig dsr0 192.168.45.$host_nr up
	# Disable debug output
	echo "PrintDebug=0" > /proc/net/dsr_config
	echo "DSR-UU started with virtual host IP $IP"
	# Enable IP-forwarding...
	#echo 1 > /proc/sys/net/ipv4/ip_forward
	#echo 0 > /proc/sys/net/ipv4/conf/$IFNAME/rp_filter
    else
	echo "DSR-UU not installed"
	exit
    fi
elif [ "$command" = "stop" ]; then 
    IP=`cat .$IFNAME.ip`
    /sbin/ifconfig dsr0 down
    rmmod dsr linkcache
#    /sbin/ifconfig $IFNAME $IP up
    rm -f .dsr.ip
fi
