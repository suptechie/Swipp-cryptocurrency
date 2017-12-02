#!/bin/bash
# Copyright (c) 2017 The Swipp developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

SWIPP_BINARY=src/swippd
ARGS="-debug -testnet -datadir="
TMP_TEMPLATE=/tmp/swipp.XXXXXXX

# Return a randomly generated UUID.

uuid() {
	cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1
}

# Stall and wait for the process in $1 to exit. Returns instantly if the
# process is not running.
#
# $1 [pid] The process id to wait for.

waitpid() {
	tail --pid=$1 -f /dev/null
}

# Return true/false exit value depending on if the process in $1 exists.
#
# $1 [pid] The process id to check for.

pidexists() {
	kill -0 $1 &> /dev/null
}

# Start the swipp daemon executable specified in SWIPP_BINARY.
#
# $1 [ip]      IP address to bind daemon to.
# $2 [port]    Port to bind daemon to.
# $3 [rpcport] Port to run the rpc interface on.

start_swipp_exe () {
	dir=$(mktemp -d $TMP_TEMPLATE)

	user=$(uuid)
	echo $user > $dir/.user

	password=$(uuid)
	echo $password > $dir/.password

	extraargs="-rpcuser=$user -rpcpassword=$password "
	extraargs+="-externalip=$1 -bind=$1:$2 -rpcport=$3 "

	for i in "${peers[@]}"; do
		extraargs+="-addnode=$i "
	done

	$SWIPP_BINARY $ARGS$dir $extraargs &
	pid=$!

	echo $pid > $dir/.pid
	sleep 2

	if ! pidexists $pid; then
		echo Failed to start swipp instance "$1$dir $extrargs" \
		     Aborting startup... > /dev/null
		stop
		exit 1
	fi
}

# Create a list of peers in the given interval and put them into $peers.
# Exclude the IP specified in $1. Peers are generated frm 127.0.10.<startindex>
# to 127.0.10.<endindex>.
#
# $1 [ip]         IP (peer) to exclude from the final list.
# $2 [startindex] The index to start from when generating peer list.
# $3 [endindex]   The index to end at when generating peer list.

get_peers() {
	tmp_peers=()
	for j in $(seq $2 $3); do
		tmp_peers[$j]="127.0.10."$j
	done
	peers=(${tmp_peers[@]/$1*/})
}

start () {
	echo Starting Swipp instances... > /dev/null

	for i in $(seq 1 $1); do
		ip="127.0.10."$i
		get_peers $ip 1 $1
		start_swipp_exe $ip 18065 $((15074 + $i))
	done
}

stop () {
	echo Stopping all running Swipp instances... > /dev/null


	if ls /tmp/swipp.* 1> /dev/null 2>&1; then
		for i in /tmp/swipp.*/.pid; do
			kill $(<$i) &> /dev/null # We don't care if the kill fails
			waitpid $(<$i)
		done
	fi

	echo Deleting data directories... > /dev/null
	rm -rf /tmp/swipp.*
}

run () {
	echo "Running..." > /dev/null
}

set -o xtrace

case $1 in
	start)

	if [ "$#" -lt 2 ]; then
		echo Syntax for starting swipp test instances is "start <n>", \
		     where n denotes the number of nodes. Exiting... > /dev/null
		exit 1
	fi

	start $2
	;;

	stop)
	stop
	;;

	status)
	status_command="ps -eo pid,args | grep swippd | grep testnet"
	eval $status_command
	echo There are $(eval $status_command | wc -l) nodes up and running. > /dev/null
	;;

	run)
	run
	;;

	# unknown option
	*)
	echo Please specify one of the following: start/stop/run. \
	     Exiting... > /dev/null
	exit 1
	;;
esac

exit 0
