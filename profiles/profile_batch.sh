#!/bin/bash

# 1. Create ProgressBar function
# 1.1 Input is currentState($1) and totalState($2)
function ProgressBar {
	# Process data
	let _progress=(${1}*100/${2}*100)/100
	let _done=(${_progress}*4)/10
	let _left=40-$_done
	# Build progressbar string lengths
	_fill=$(printf "%${_done}s")
	_empty=$(printf "%${_left}s")

# 1.2 Build progressbar strings and print the ProgressBar line
# 1.2.1 Output example:                           
# 1.2.1.1 Progress : [########################################] 100%
	printf "\rProgress : [${_fill// /#}${_empty// /-}] ${_progress}%%"

}

# Variables
	_start=1
# This accounts as the "totalState" variable for the ProgressBar function
	_end=100

ProgressBar 0 100
./profiles/profile_cateI_batch.sh > profile_cateI_batch.log
ProgressBar 25 100
./profiles/profile_cateII_batch.sh > profile_cateII_batch.log
ProgressBar 50 100
./profiles/profile_cateIII_batch.sh > profile_cateIII_batch.log
ProgressBar 75 100
./profiles/profile_cateI_supple_batch.sh > profile_cateI_supple_batch.log
ProgressBar 80 100
./profiles/profile_cateV_batch.sh > profile_cateV_batch.log
ProgressBar 100 100

printf "\n"
