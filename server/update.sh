#!/bin/sh
cd ../addons2stp
echo "laucnhin converter"
python3 converter.py
echo "launchin mov_file"
./move_files.sh
