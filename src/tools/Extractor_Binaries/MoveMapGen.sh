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
#EXCLUDE_MAPS="13 25 29 35 37 42 44 169 451 598" # example to exclude 'junk' maps

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

## Continent Maps
MAP_Continent1="0"    ## Eastern Kingdoms     340mb
MAP_Continent2="1"    ## Kalimdor             470mb
MAP_Continent3="530"  ## Outlands             313mb

## Big Maps > 9mb
MAP_Big1="534"
MAP_Big2="560"
MAP_Big3="309"
MAP_Big4="533"
MAP_Big5="568"
MAP_Big6="509"
MAP_Big7="30"
MAP_Big8="532"
MAP_Big9="469"

## Medium Maps <10mb
MAP_Medium1="33"
MAP_Medium2="209"
MAP_Medium3="289"
MAP_Medium4="329"
MAP_Medium5="564"
MAP_Medium6="580"
MAP_Medium7="543"
MAP_Medium8="585"
MAP_Medium9="529"
MAP_Medium10="572"
MAP_Medium11="36"
MAP_Medium12="269"
MAP_Medium13="489"
MAP_Medium14="47"
MAP_Medium15="531"
MAP_Medium16="562"
MAP_Medium17="566"


## Small Maps < 3mb
MAP_Small1="230"
MAP_Small2="429"
MAP_Small3="548"
MAP_Small4="559"
MAP_Small5="34"
MAP_Small6="43"
MAP_Small7="48"
MAP_Small8="70"
MAP_Small9="90"
MAP_Small10="109"
MAP_Small11="129"
MAP_Small12="189"
MAP_Small13="229"
MAP_Small14="249"
MAP_Small15="349"
MAP_Small16="369"
MAP_Small17="389"
MAP_Small18="409"
MAP_Small19="449"
MAP_Small20="450"
MAP_Small21="540"
MAP_Small22="542"
MAP_Small23="544"
MAP_Small24="545"
MAP_Small25="546"
MAP_Small26="547"
MAP_Small27="550"
MAP_Small28="552"
MAP_Small29="553"
MAP_Small30="554"
MAP_Small31="555"
MAP_Small32="556"
MAP_Small33="557"
MAP_Small34="558"
MAP_Small35="565"

## The following are technically 'Junk' Maps that do not need to be extracted
MAP_LIST_Junk1="169"
MAP_LIST_Junk2="37"
MAP_LIST_Junk3="451"
MAP_LIST_Junk4="13"
MAP_LIST_Junk5="29"
MAP_LIST_Junk6="35"
MAP_LIST_Junk7="42"
MAP_LIST_Junk8="44"
MAP_LIST_Junk9="598"
MAP_LIST_Junk10="25"

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

DisplayHeader()
{
##    clear
    echo "  __  __      _  _  ___  ___  ___        "
    echo " |  \\/  |__ _| \\| |/ __|/ _ \\/ __|    "
    echo " | |\\/| / _\` | .\` | (_ | (_) \\__ \\  "
    echo " |_|  |_\\__,_|_|\\_|\\___|\\___/|___/   "
    echo "                                         "
    echo " For help and support please visit:      "
    echo " Website/Forum/Wiki: https://getmangos.eu"
    echo "=========================================="
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
DisplayHeader
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

createSummary()
{
    echo
    echo "Build Summary:"
    echo "==============="
    case "$1" in
      "1" )
        echo "1 CPU selected:"
        echo "=============="
        echo " All maps will be build using this CPU"
        ;;
      "2" )
        echo "2 CPUs selected:"
        echo "==============="
		echo " CPU 1: Maps: $MAP_Continent1 $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big4 $MAP_Big5 $MAP_Big6 $MAP_Big7 $MAP_Big8 $MAP_Big9 $MAP_Medium1 $MAP_Medium2 $MAP_Medium3 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium9 $MAP_Medium10 $MAP_Medium12 $MAP_Medium15 $MAP_Small1 $MAP_Small2 $MAP_Small5 $MAP_Small6 $MAP_Small13 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small19 $MAP_Small20 $MAP_Small21 $MAP_Small27 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_LIST_Junk2 $MAP_LIST_Junk3 $MAP_LIST_Junk4 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk7 $MAP_LIST_Junk8 $MAP_LIST_Junk9 $MAP_LIST_Junk10"
		echo " CPU 2: Maps: $MAP_Continent2 $MAP_Continent3 $MAP_Medium7 $MAP_Medium8 $MAP_Medium11 $MAP_Medium13 $MAP_Medium14 $MAP_Medium16 $MAP_Medium17 $MAP_Small3 $MAP_Small4 $MAP_Small7 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk1"
        ;;
      "3" )
        echo "3 CPUs selected:"
        echo "==============="
		echo " CPU 1: Maps: $MAP_Continent1 $MAP_Medium10 $MAP_Medium11 $MAP_Small5 $MAP_Small13 $MAP_LIST_Junk1 $MAP_LIST_Junk7"
		echo " CPU 2: Maps: $MAP_Continent2 $MAP_Big4 $MAP_Big5 $MAP_Big8 $MAP_Medium1 $MAP_Medium2 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium7 $MAP_Medium8 $MAP_Small6 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small20 $MAP_Small21 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small27 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk2"
		echo " CPU 3: Maps: $MAP_Continent3 $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big6 $MAP_Big7 $MAP_Big9 $MAP_Medium3 $MAP_Medium9 $MAP_Medium12 $MAP_Medium13 $MAP_Medium14 $MAP_Medium15 $MAP_Medium16 $MAP_Medium17 $MAP_Small1 $MAP_Small2 $MAP_Small3 $MAP_Small4 $MAP_Small7 $MAP_Small19 $MAP_LIST_Junk3 $MAP_LIST_Junk4 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk8 $MAP_LIST_Junk9 $MAP_LIST_Junk10"
        ;;
      "4" )
        echo "4 CPUs selected:"
        echo "==============="
		echo " CPU 1: Maps: $MAP_Continent1"
		echo " CPU 2: Maps: $MAP_Continent2 $MAP_LIST_Junk4 $MAP_LIST_Junk9 $MAP_LIST_Junk10"
		echo " CPU 3: Maps: $MAP_Continent3 $MAP_Small21 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small27 $MAP_LIST_Junk2 $MAP_LIST_Junk3 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk8"
		echo " CPU 4: Maps: $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big4 $MAP_Big5 $MAP_Big6 $MAP_Big7 $MAP_Big8 $MAP_Big9 $MAP_Medium1 $MAP_Medium2 $MAP_Medium3 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium7 $MAP_Medium8 $MAP_Medium9 $MAP_Medium10 $MAP_Medium11 $MAP_Medium12 $MAP_Medium13 $MAP_Medium14 $MAP_Medium15 $MAP_Medium16 $MAP_Medium17 $MAP_Small1 $MAP_Small2 $MAP_Small3 $MAP_Small4 $MAP_Small5 $MAP_Small6 $MAP_Small7 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small13 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small19 $MAP_Small20 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk1 $MAP_LIST_Junk7"
        ;;
      * )
        badParam
        exit 1
        ;;
    esac

  echo
  echo "Starting to create MoveMaps" | tee -a $DETAIL_LOG_FILE
  wait
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
    createSummary $1
	createMMaps $MAP_Continent1 $MAP_Continent2 $MAP_Continent3 $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big4 $MAP_Big5 $MAP_Big6 $MAP_Big7 $MAP_Big8 $MAP_Big9 $MAP_Medium1 $MAP_Medium2 $MAP_Medium3 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium7 $MAP_Medium8 $MAP_Medium9 $MAP_Medium10 $MAP_Medium11 $MAP_Medium12 $MAP_Medium13 $MAP_Medium14 $MAP_Medium15 $MAP_Medium16 $MAP_Medium17 $MAP_Small1 $MAP_Small2 $MAP_Small3 $MAP_Small4 $MAP_Small5 $MAP_Small6 $MAP_Small7 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small13 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small19 $MAP_Small20 $MAP_Small21 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small27 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk1 $MAP_LIST_Junk2 $MAP_LIST_Junk3 $MAP_LIST_Junk4 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk7 $MAP_LIST_Junk8 $MAP_LIST_Junk9 $MAP_LIST_Junk10 &
    ;;
  "2" )
    createHeader $1
    createSummary $1
    createMMaps $MAP_Continent1 $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big4 $MAP_Big5 $MAP_Big6 $MAP_Big7 $MAP_Big8 $MAP_Big9 $MAP_Medium1 $MAP_Medium2 $MAP_Medium3 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium9 $MAP_Medium10 $MAP_Medium12 $MAP_Medium15 $MAP_Small1 $MAP_Small2 $MAP_Small5 $MAP_Small6 $MAP_Small13 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small19 $MAP_Small20 $MAP_Small21 $MAP_Small27 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_LIST_Junk2 $MAP_LIST_Junk3 $MAP_LIST_Junk4 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk7 $MAP_LIST_Junk8 $MAP_LIST_Junk9 $MAP_LIST_Junk10 &
    createMMaps $MAP_Continent2 $MAP_Continent3 $MAP_Medium7 $MAP_Medium8 $MAP_Medium11 $MAP_Medium13 $MAP_Medium14 $MAP_Medium16 $MAP_Medium17 $MAP_Small3 $MAP_Small4 $MAP_Small7 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk1 &
    ;;
  "3" )
    createHeader $1
    createSummary $1
	createMMaps $MAP_Continent1 $MAP_Medium10 $MAP_Medium11 $MAP_Small5 $MAP_Small13 $MAP_LIST_Junk1 $MAP_LIST_Junk7 &
	createMMaps $MAP_Continent2 $MAP_Big4 $MAP_Big5 $MAP_Big8 $MAP_Medium1 $MAP_Medium2 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium7 $MAP_Medium8 $MAP_Small6 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small20 $MAP_Small21 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small27 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk2 &
	createMMaps $MAP_Continent3 $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big6 $MAP_Big7 $MAP_Big9 $MAP_Medium3 $MAP_Medium9 $MAP_Medium12 $MAP_Medium13 $MAP_Medium14 $MAP_Medium15 $MAP_Medium16 $MAP_Medium17 $MAP_Small1 $MAP_Small2 $MAP_Small3 $MAP_Small4 $MAP_Small7 $MAP_Small19 $MAP_LIST_Junk3 $MAP_LIST_Junk4 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk8 $MAP_LIST_Junk9 $MAP_LIST_Junk10 &
    ;;
  "4" )
    createHeader $1
    createSummary $1
	createMMaps $MAP_Continent1 &
	createMMaps $MAP_Continent2 $MAP_LIST_Junk4 $MAP_LIST_Junk9 $MAP_LIST_Junk10 &
	createMMaps $MAP_Continent3 $MAP_Small21 $MAP_Small22 $MAP_Small23 $MAP_Small24 $MAP_Small25 $MAP_Small26 $MAP_Small27 $MAP_LIST_Junk2 $MAP_LIST_Junk3 $MAP_LIST_Junk5 $MAP_LIST_Junk6 $MAP_LIST_Junk8 &
	createMMaps $MAP_Big1 $MAP_Big2 $MAP_Big3 $MAP_Big4 $MAP_Big5 $MAP_Big6 $MAP_Big7 $MAP_Big8 $MAP_Big9 $MAP_Medium1 $MAP_Medium2 $MAP_Medium3 $MAP_Medium4 $MAP_Medium5 $MAP_Medium6 $MAP_Medium7 $MAP_Medium8 $MAP_Medium9 $MAP_Medium10 $MAP_Medium11 $MAP_Medium12 $MAP_Medium13 $MAP_Medium14 $MAP_Medium15 $MAP_Medium16 $MAP_Medium17 $MAP_Small1 $MAP_Small2 $MAP_Small3 $MAP_Small4 $MAP_Small5 $MAP_Small6 $MAP_Small7 $MAP_Small8 $MAP_Small9 $MAP_Small10 $MAP_Small11 $MAP_Small12 $MAP_Small13 $MAP_Small14 $MAP_Small15 $MAP_Small16 $MAP_Small17 $MAP_Small18 $MAP_Small19 $MAP_Small20 $MAP_Small28 $MAP_Small29 $MAP_Small30 $MAP_Small31 $MAP_Small32 $MAP_Small33 $MAP_Small34 $MAP_Small35 $MAP_LIST_Junk1 $MAP_LIST_Junk7 &
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
