#!/bin/sh
RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
NC='\033[0m'

# Where you want to mount the filesystem
MOUNTDIR=${MOUNTDIR:-/tmp/$(eval whoami)/mountdir}

### Undo mount
if findmnt | grep $MOUNTDIR > /dev/null; then
  printf "${YELLOW}TFS is already mounted at ${MOUNTDIR} - Unmounting...${NC}\n"
  fusermount -u $MOUNTDIR;
  if [ $? -eq 0 ]; then
    printf "${GREEN}Unmounting was successful${NC}\n";
  else
    printf "${RED}Failed to unmount${NC}\n"
    exit 1
  fi
fi

printf "${YELLOW}Building project...${NC}\n"
make clean;
make;
if [ $? -eq 0 ]; then
  printf "${GREEN}Project built succesffully${NC}\n" 
else
  printf "${RED}Project failed to build${NC}\n"
  exit 1
fi

printf "${YELLOW}Checking that mount point exists...${NC}\n"
if [ -d "$MOUNTDIR" ]; then
  printf "${YELLOW}Mount point exists, deleting files if there are any...${NC}\n"
  if [ $(ls -A $MOUNTDIR) ] 
  then
    rm -r $MOUNTDIR/*
    if [ $? -eq 0 ]; then
      printf "${GREEN}Successfully deleted files in mount point.${NC}\n"
    else
      printf "${RED}Failed to delete files in mount point!${NC}\n"
    fi
  fi 
else
  printf "${YELLOW}Mount point doesn't exist. Attempting to create it.${NC}\n";
  mkdir -p $MOUNTDIR
  if [ $? -eq 0 ]; then
    printf "${GREEN}Mount point created successfully.${NC}\n"
  else
    printf "${RED}Mount point could not be created!${NC}\n"
    exit 1
  fi
fi


printf "${YELLOW}Attempting to mount TFS at ${MOUNTDIR}...${NC}\n"
# If no argument passed in, then dont run the test
  if [ -z "$1" ]; then
    ./tfs -s $MOUNTDIR -d
  else
    ./tfs -s $MOUNTDIR
  fi
if [ $? -eq 0 ]; then
  printf "${GREEN}Successfully mounted TFS at ${MOUNTDIR}!${NC}\n"
else
  printf "${RED}Failed to mount TFS at ${MOUNTDIR}.${NC}\n"
  exit 1
fi


# Just like above, we only run this test if an argument was passed in
if [ ! -z "$1" ]; then
printf "${YELLOW} Attempting to build benchmark...${NC}\n"
cd benchmark;
make clean;
make;
if [ $? -eq 0 ]; then
  printf "${GREEN}Benchmark built succesffully${NC}\n"
else
  printf "${RED}Failed to build benchmark${NC}\n";
  exit 1
fi

printf "${YELLOW}Running simple_test.c${NC}\n";
./simple_test $MOUNTDIR
fi