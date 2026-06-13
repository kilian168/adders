#!/bin/bash
export SYSTEMC_HOME=/home/alterhans/systemc-2.3.3
export LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64:$LD_LIBRARY_PATH
cd /home/alterhans/projects/bachelor/systemc/"$1" || exit 1
shift
./"$@"
