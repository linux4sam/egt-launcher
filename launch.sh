#!/bin/sh

#
# Helper script for launching a background process.
#

handle_exit_key()
{
    # try to read KEY_0 from keyboard0 to exit the application
    device='/dev/input/keyboard0'
    if [ -c "$device" ]
    then
	(
	    key_0='*type 1 (EV_KEY), code 11 (KEY_0), value 1*'
	    evtest "$device" | while read line; do
		case $line in
		    ($key_0)
			killall -9 $1
			exit 0
			;;
		esac
	    done
	) &
    fi

    $@
}

run()
{
    handle_exit_key $@
    egt-launcher
}

# redirect stdout, stderr to /dev/null, close stdin and double fork - it's magic
((run $@ > /dev/null 2>&1 <&- &)&)
