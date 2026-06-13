#!/bin/bash
export SYSTEMC_HOME=/home/alterhans/systemc-2.3.3
export LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64:$LD_LIBRARY_PATH
cd /home/alterhans/projects/bachelor/systemc/"$1" || exit 1
g++ -I"$SYSTEMC_HOME/include" -o "$2" "$3" -L"$SYSTEMC_HOME/lib-linux64" -lsystemc 2>&1
echo "BUILD_EXIT=$?"
