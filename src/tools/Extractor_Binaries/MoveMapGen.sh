#!/bin/bash

# This code is part of MaNGOS. Contributor & Copyright details are in AUTHORS/THANKS.
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

## Syntax of this helper
## First param must be number of to be used CPUs (only 1, 2, 3, 4 supported) or "offmesh" to recreate the special tiles from the OFFMESH_FILE
## Second param can be an additional filename for storing log
## Third param can be an addition filename for storing detailed log

## Additional Parameters to be forwarded to MoveMapGen, see mmaps/readme for instructions
PARAMS="--silent"

## Already a few map extracted, and don't care anymore
EXCLUDE_MAPS=""
#EXCLUDE_MAPS="0 1 530 571" # example to exclude the continents
## Exclude file
EXCLUDE_MAPS_FILE="mmap_excluded.txt"

## The Exclude file contains a space delimited list of map id's to skip in the same format as EXCLUDE_MAPS

## Does an exclude file exist ?
if [ "$EXCLUDE_MAPS" == "" ]
then 
  ## Exclude file provided?
  if [ -f "$EXCLUDE_MAPS_FILE" ]
  then ## Yes, read the file
    read -d -r EXCLUDE_MAPS < $EXCLUDE_MAPS_FILE
	echo "Excluded maps: $EXCLUDE_MAPS"
  else ## No, remind the user that they can create the file
	echo "Excluded maps: NONE (no file called '$EXCLUDE_MAPS_FILE' was found.)"
  fi
fi

## Offmesh file
OFFMESH_FILE="offmesh.txt"

## Normal log file (if not overwritten by second param
LOG_FILE="MoveMapGen.log"
## Detailed log file
DETAIL_LOG_FILE="MoveMapGen_detailed.log"

## ! Use below only for finetuning or if you know what you are doing !

## All maps
MAP_LIST_A="1 37 543 595 289 572 529 562 269 47 650 599 559 429 349 13 25 229 43 48 553 129"
MAP_LIST_B="169 309 560 509 532 531 469 585 566 389 369 550 189 542 554 552 555 540 598 564 548 450 558 249 "
MAP_LIST_C="0 534 533 329 580 36 556 44 565 544 34 449 42 586 587 588 589 590 591 576 545"
MAP_LIST_D="530 575 33 574 30 109 568 209 489 593 451 582 584 230 409 546 35 90 547 557 70"
MAP_LIST_D1="568 575 33 574 30 109 70"
MAP_LIST_D2="530 209 489 593 451 582 557"
MAP_LIST_D3="584 230 409 546 35 90 547"

badParam()
{
  echo "ERROR! Bad arguments!"
  echo "You can (re)extract mmaps with this helper script,"
  echo "or recreate only the tiles from the offmash file"
  echo
  echo "Call with number of processes (1 - 4) to create mmaps"
  echo "Call with 'offmesh' to reextract the tiles from offmash file"
  echo
  echo "For further fine-tuning edit this helper script"
  echo
  read line
}

if [ "$#" = "3" ]
then
  LOG_FILE=$2
  DETAIL_LOG_FILE=$3
elif [ "$#" = "2" ]
then
  LOG_FILE=$2
fi

# Offmesh file provided?
OFFMESH=""
if [ "$OFFMESH_FILE" != "" ]
then
  if [ ! -f "$OFFMESH_FILE" ]
  then
	echo "ERROR! Offmesh file $OFFMESH_FILE could not be found."
	echo "Provide valid file or none. You need to edit the script"
	exit 1
  else
	OFFMESH="--offMeshInput $OFFMESH_FILE"
  fi
fi

# Function to process a list
createMMaps()
{
  for i in $@
  do
	for j in $EXCLUDE_MAPS
	do
	  if [ "$i" = "$j" ]
	  then
		continue 2
	  fi
	done
	./movemap-generator $PARAMS $OFFMESH $i | tee -a $DETAIL_LOG_FILE
	echo "`date`: (Re)created map $i" | tee -a $LOG_FILE
  done
}

createHeader()
{
#  read line
  echo
  echo "`date`: Start creating MoveMaps" | tee -a $LOG_FILE
  echo "Used params: $PARAMS $OFFMESH" | tee -a $LOG_FILE
  echo
  echo "Detailed log can be found in $DETAIL_LOG_FILE" | tee -a $LOG_FILE
  echo "Start creating MoveMaps" | tee -a $DETAIL_LOG_FILE
  echo
  echo "################################################################"
  echo "##                                                            ##"
  echo "##      BE PATIENT - This process will take a long time       ##"
  echo "##                                                            ##"
  echo "################################################################"
  echo "##                                                            ##"
  echo "##   There will also be periods where the display does not    ##"
  echo "##   update, this is normal behavior for this process         ##"
  echo "##                                                            ##"
  echo "##  Once you see the message 'creating MoveMaps' is finished  ##"
  echo "##  then the process is complete.                             ##"
  echo "################################################################"
  echo ""
}

# Create mmaps directory if not exist
if [ ! -d mmaps ]
then
  mkdir ./mmaps
fi

# Param control
case "$1" in
  "1" )
	createHeader $1
	createMMaps $MAP_LIST_A $MAP_LIST_B $MAP_LIST_C $MAP_LIST_D &
	;;
  "2" )
	createHeader $1
	createMMaps $MAP_LIST_A $MAP_LIST_D &
	createMMaps $MAP_LIST_B $MAP_LIST_C &
	;;
  "3" )
	createHeader $1
	createMMaps $MAP_LIST_A $MAP_LIST_D1&
	createMMaps $MAP_LIST_B $MAP_LIST_D2&
	createMMaps $MAP_LIST_C $MAP_LIST_D3&
	;;
  "4" )
	createHeader $1
	createMMaps $MAP_LIST_A &
	createMMaps $MAP_LIST_B &
	createMMaps $MAP_LIST_C &
	createMMaps $MAP_LIST_D &
	;;
  "offmesh" )
	echo "`date`: Recreate offmeshs from file $OFFMESH_FILE" | tee -a $LOG_FILE
	echo "Recreate offmeshs from file $OFFMESH_FILE" | tee -a $DETAIL_LOG_FILE
	while read map tile line
	do
	  ./movemap-generator $PARAMS $OFFMESH $map --tile $tile | tee -a $DETAIL_LOG_FILE
	  echo "`date`: Recreated $map $tile from $OFFMESH_FILE" | tee -a $LOG_FILE
	done < $OFFMESH_FILE &
	;;
  * )
	badParam
	exit 1
	;;
esac

wait

echo  | tee -a $LOG_FILE
echo  | tee -a $DETAIL_LOG_FILE
echo "`date`: Finished creating MoveMaps" | tee -a $LOG_FILE
echo "`date`: Finished creating MoveMaps" >> $DETAIL_LOG_FILE
echo
echo "Press any key"
read line
