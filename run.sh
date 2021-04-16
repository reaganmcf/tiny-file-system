#!/bin/sh
RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
NC='\033[0m'

# Where you want to mount the filesystem
MOUNTDIR=${MOUNTDIR:-/tmp/rpm141/mountdir}

### Undo mount
if findmnt | grep $MOUNTDIR > /dev/null; then
  printf "${YELLOW}TFS is already mounted. Unmounting...${NC}\n"
  umount $MOUNTDIR;
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
  printf "${GREEN}Project build succesffully${NC}\n" 
else
  printf "${RED}Project failed to build${NC}\n"
  exit 1
fi

printf "${YELLOW}Checking that mount point exists...${NC}\n"
if [ -d "$MOUNTDIR" ]; then
  printf "${GREEN}Mount point exists, safe to continue.${NC}\n"
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
./tfs -s $MOUNTDIR
if [ $? -eq 0 ]; then
  printf "${GREEN}Successfully mounted TFS at ${MOUNTDIR}!${NC}\n"
else
  printf "${RED}Failed to mount TFS at ${MOUNTDIR}.${NC}\n"
  exit 1
fi

