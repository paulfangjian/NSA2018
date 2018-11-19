# Copyright (c) 1999 Regents of the University of Southern California.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#      This product includes software developed by the Computer Systems
#      Engineering Group at Lawrence Berkeley Laboratory.
# 4. Neither the name of the University nor of the Laboratory may be used
#    to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#
# roaming-aodv-uu.tcl:
# Roaming node scenario using AODV-UU (inspired by APE roaming node scenario)
#
# Some source code was borrowed from wireless1.tcl.
#
# NOTE: Requires tracing support for packets of type PT_PING to be added
# to cmu-trace.cc.

# ======================================================================
# Define options
# ======================================================================

set val(chan)           Channel/WirelessChannel
#set val(prop)           Propagation/Shadowing
set val(prop)           Propagation/TwoRayGround
set val(netif)          Phy/WirelessPhy
set val(mac)            Mac/802_11
set val(ifq)            Queue/DropTail/PriQueue
set val(ll)             LL
set val(ant)            Antenna/OmniAntenna
set val(x)              50      ;# X dimension of the topography
set val(y)              15      ;# Y dimension of the topography
set val(ifqlen)         50      ;# max packet in ifq
set val(seed)           0.0
set val(adhocRP)        AODVUU
set val(nn)             4       ;# how many nodes are simulated
set val(stop)           290.0   ;# simulation time
set opt(prot)           dsruu
# =====================================================================
# Main Program
# ======================================================================
proc getopt {argc argv} {
    global opt
    lappend optlist prot

    for {set i 0} {$i < $argc} {incr i} {
	set arg [lindex $argv $i]

	if {[string range $arg 0 0] != "-"} continue
	
	set name [string range $arg 1 end]
	set opt($name) [lindex $argv [expr $i+1]]
    }
}

getopt $argc $argv

if { $opt(prot) == "aodvuu" } {
    set val(adhocRP) AODVUU;
} elseif { $opt(prot) == "aodv" } {
    set val(adhocRP) AODV;
} elseif { $opt(prot) == "dsruu" } {
    set val(adhocRP) DSRUU;
    set val(ifq)  CMUPriQueue;
} elseif { $opt(prot) == "dsr" } {
    set val(adhocRP) DSR;
    set val(ifq)  CMUPriQueue;
} else {
    puts "Invalid routing protocol"
    exit
}

set val(tr)             roaming-node-$val(adhocRP).tr
set val(nam)            roaming-node-$val(adhocRP).nam

puts "Running simulation with $val(adhocRP)"

#
# Initialize global variables
#

# Create simulator instance
set ns_ [new Simulator]

# Setup topography object
set topo [new Topography]

# Create trace object for ns and nam
set tracefd  [open $val(tr) w]
set namtrace [open $val(nam) w]

$ns_ trace-all $tracefd
$ns_ namtrace-all-wireless $namtrace $val(x) $val(y)

# Define topology
$topo load_flatgrid $val(x) $val(y)

# Create God
set god_ [create-god $val(nn)]

# Make queue prioritize routing packets
Queue/DropTail/PriQueue set Prefer_Routing_Protocols 1

# Set parameters for wireless interface (ORiNOCO PC Card)
# Figures taken from ORiNOCO PC Card Specifications
#
#        (dBm/10)
# mW = 10
#
#
# dBm = 10 * log  (mW / 1) = 10 * log  (mW)
#               10                   10
#
# NOTE: Because receiver sensitivity depends on speed, and speed depends on
# packet type (unicast/broadcast), this might not model the real world
# perfectly. More specifically, the carrier sense threshold should be set to
# a smaller value for speeds < 11 Mbps (e.g. for broadcast packets).

Phy/WirelessPhy set Pt_ 0.031622777        ;# Tx power (W),
                                           ;# 15 dBm

Phy/WirelessPhy set bandwidth_ 11Mb        ;# 11 Mbps bandwidth
Mac/802_11 set dataRate_ 11Mb              ;# 11 Mbps for data
Mac/802_11 set basicRate_ 1Mb              ;# 1 Mbps for broadcasts

Phy/WirelessPhy set freq_ 2.472e9          ;# Europe, Channel 13, 2.472 GHz
Phy/WirelessPhy set CPThresh_ 10.0         ;# Capture threshold (dB)

Phy/WirelessPhy set CSThresh_ 5.011872e-12 ;# Carrier sense threshold (W),
                                           ;# receiver sensitivity -83 dBm

Phy/WirelessPhy set L_ 1.0                 ;# System loss

Phy/WirelessPhy set RXThresh_ 5.82587e-09  ;# Receive threshold (W),
                                           ;# from threshold utility (22.5 m)
                                           ;# given the other parameters

# Set parameters for shadowing propagation model
#set propinst [new Propagation/Shadowing]
#$propinst set pathlossExp_ 4.0
#$propinst set std_db_ 6.9
#$propinst set dist0_ 1.0
#$propinst seed predef 0

# Define how nodes should be created
$ns_ node-config -adhocRouting $val(adhocRP) \
	-llType $val(ll) \
	-macType $val(mac) \
	-ifqType $val(ifq) \
	-ifqLen $val(ifqlen) \
	-antType $val(ant) \
	-propType $val(prop) \
	-phyType $val(netif) \
	-channelType $val(chan) \
	-topoInstance $topo \
	-agentTrace ON \
	-routerTrace ON \
	-macTrace ON
#        -propInstance $propinst

#
# Setting up the nodes
#

# Create the specified number of nodes and "attach" them to the channel
for {set i 0} {$i < $val(nn) } {incr i} {
    set node_($i) [$ns_ node]	
    $node_($i) random-motion 0		;# disable random motion

    set r [$node_($i) set ragent_]      ;# get hold of routing agent
    $r set debug_ 1                     ;# set routing agent options...
    $r set log_to_file_ 1
    $r set rt_log_interval_ 1000
    
    # set nodetr($i) [open dsr-$i.log w]
#     $r tracetarget $nodetr($i)
}

# Node 0: "Gateway", i.e. node being pinged
# Node 1: Client 1 at the corner (C1)
# Node 2: Client 2 at Richard's room (C2)
# Node 3: Mobile Node, i.e. pinging node (MN)

# Ping agent recv instance procedure, required for Ping agent to work
Agent/Ping instproc recv {from rtt} {
#    puts "Received ping reply from $from, RTT = $rtt ms"
}

# Create Ping agents at GW and mobile node
set ping(0) [new Agent/Ping]
$ping(0) set packetSize_ 580
$ns_ attach-agent $node_(0) $ping(0)

set ping(1) [new Agent/Ping]
$ping(1) set packetSize_ 580
$ns_ attach-agent $node_(3) $ping(1)

# Ping client at mobile node should generate 10 packets/second
set pingrate 10

# Schedule sending of Ping packets from mobile node
for {set i 0} {$i < [expr $val(stop) * $pingrate]} {incr i} {
$ns_ at [expr $i * 1.0/$pingrate] "$ping(1) send"
}
 
# Connect Ping agents
$ns_ connect $ping(1) $ping(0)

puts $tracefd "M 0.0 nn $val(nn) x $val(x) y $val(y) rp $val(adhocRP)"
puts $tracefd "M 0.0 prop $val(prop) ant $val(ant)"

# Positions of nodes etc.
set GW_X 0.0
set GW_Y 0.0
set GW_Z 0.0

set C1_X 10.8
set C1_Y 8.0
set C1_Z 0.0

set C2_X 22.8
set C2_Y 8.6
set C2_Z 0.0

set MN_X $C1_X
set MN_Y $C1_Y
set MN_Z 0.0

set ROOM_X 44.8
set ROOM_Y 8.0
set ROOM_Z 0.0

# Initial positioning of nodes
$node_(0) set X_ $GW_X
$node_(0) set Y_ $GW_Y
$node_(0) set Z_ $GW_Z

$node_(1) set X_ $C1_X
$node_(1) set Y_ $C1_Y
$node_(1) set Z_ $C1_Z

$node_(2) set X_ $C2_X
$node_(2) set Y_ $C2_Y
$node_(2) set Z_ $C2_Z

$node_(3) set X_ $MN_X
$node_(3) set Y_ $MN_Y
$node_(3) set Z_ $MN_Z

# Define node initial positions in nam
for {set i 0} {$i < $val(nn)} {incr i} {

   # The function must be called after mobility model is defined
   # (10 defines the node size in nam)
   $ns_ initial_node_pos $node_($i) 10
}

# Speed of movement between C1 and C2
set SPEED_1 [expr 13.55 / 15.0] ; # Total 13.55 m during 15 seconds

# Speed of movement between C2 and room 1412
set SPEED_2 [expr 25.3 / 30.0] ;  # Total 25.3 m during 30 seconds

# At 40.0: Move mobile node towards C2
$ns_ at 40.0 "$node_(3) setdest [expr $C1_X + 1.25] $C1_Y $SPEED_1"
$ns_ at 41.38 "$node_(3) setdest [expr $C1_X + 1.25] [expr $C1_Y + 1.55] $SPEED_1"
$ns_ at 43.1 "$node_(3) setdest $C2_X [expr $C2_Y + 0.95] $SPEED_1"

# At 55.0: Pause mobile node at C2 for 40 seconds

# At 95.0: Move mobile node towards room 1412
$ns_ at 95.0 "$node_(3) setdest [expr $C2_X + 11.0] [expr $C2_Y + 0.95] $SPEED_2"
$ns_ at 108.04 "$node_(3) setdest [expr $C2_X + 11.0] [expr $C2_Y - 2.35] $SPEED_2"
$ns_ at 111.95 "$node_(3) setdest $ROOM_X [expr $ROOM_Y - 1.75] $SPEED_2"

# At 125.0: Pause mobile node at 1412 for 40 seconds

# At 165.0: Move mobile node towards C2
$ns_ at 165.0 "$node_(3) setdest [expr $C2_X + 11.0] [expr $C2_Y - 2.35] $SPEED_2"
$ns_ at 178.04 "$node_(3) setdest [expr $C2_X + 11.0] [expr $C2_Y + 0.95] $SPEED_2"
$ns_ at 181.95 "$node_(3) setdest $C2_X [expr $C2_Y + 0.95] $SPEED_2"

# At 195.0: Pause mobile node at C2 for 40 seconds

# At 235.0: Move mobile node towards C1
$ns_ at 235.0 "$node_(3) setdest [expr $C1_X + 1.25] [expr $C1_Y + 1.55] $SPEED_1"
$ns_ at 246.90 "$node_(3) setdest [expr $C1_X + 1.25] $C1_Y $SPEED_1"
$ns_ at 248.62 "$node_(3) setdest $C1_X $C1_Y $SPEED_1"

# At 250.0: Pause mobile node at C1 for 40 seconds

# At end: Tell nodes that the simulation ends
for {set i 0} {$i < $val(nn) } {incr i} {
    $ns_ at $val(stop).0 "$node_($i) reset";
}

proc stop {} {
    global ns_
    $ns_ flush-trace
}

$ns_ at $val(stop).0001 "stop"
$ns_ at $val(stop).0002 "puts \"NS EXITING...\" ; $ns_ halt"

puts "Starting Simulation..."
$ns_ run
