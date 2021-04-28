/*
 *  Copyright (C) 2021 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

volatile struct superblock* SUPERBLOCK;
bitmap_t INODE_BITMAP;
bitmap_t DBLOCK_BITMAP;

int diskfile_found = 0;

int SUPERBLOCK_SIZE_IN_BLOCKS;
int INODE_BITMAP_SIZE_IN_BLOCKS;
int DBLOCK_BITMAP_SIZE_IN_BLOCKS;
int INODE_TABLE_SIZE_IN_BLOCKS;
int BLOCK_SIZE_IN_CHARACTERS;
int INODES_PER_BLOCK;

/**
 * Write the inode bitmap
 */
void write_inode_bitmap() {
  bio_write(SUPERBLOCK->i_bitmap_blk, (void*)INODE_BITMAP) ;
}

/**
 * Write the dblock bitmap
 */
void write_dblock_bitmap() {
  bio_write(SUPERBLOCK->d_bitmap_blk, (void*)DBLOCK_BITMAP);
}

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 

	return 0;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
	if (SUPERBLOCK == NULL) {
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}

	int block_number = SUPERBLOCK->i_start_blk + ino / INODES_PER_BLOCK;

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % INODES_PER_BLOCK;

	// Step 3: Read the block from disk and then copy into inode structure
	void* buf = malloc(BLOCK_SIZE);
	bio_read(block_number, buf);
	if (buf == NULL) {
		perror("ERROR:: Could not read inode from inode table");
		exit(-1);
	}

	struct inode* block_of_inodes = (struct inode*)buf;
	printf("READI:: ino = %d, block num = %d, offset = %d\n",
			ino,
			block_number,
			offset);

	*inode = block_of_inodes[offset];
	free(buf);
	return 1;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	if (SUPERBLOCK == NULL) {
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}
	int block_number = SUPERBLOCK->i_start_blk + ino / INODES_PER_BLOCK;
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % INODES_PER_BLOCK;

	// Step 3: Write inode to disk 
	void* buf = malloc(BLOCK_SIZE);
	bio_read(block_number, buf);
	if (buf == NULL) {
		perror("ERROR:: Could not read inode from inode table");
		exit(-1);
	}
	struct inode* block_of_inodes = (struct inode*)buf;
	printf("WRITEI:: ino = %d, block num = %d, offset = %d\n",
			ino,
			block_number,
			offset);
	block_of_inodes[offset] = *(inode);

	bio_write(block_number, (void*)block_of_inodes);
	free(buf);
	return 1;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)

  // Step 2: Get data block of current directory from inode

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	SUPERBLOCK_SIZE_IN_BLOCKS = ceil(sizeof(struct superblock) / BLOCK_SIZE);
	INODE_BITMAP_SIZE_IN_BLOCKS = ceil((double)(MAX_INUM / 8) / BLOCK_SIZE);
	DBLOCK_BITMAP_SIZE_IN_BLOCKS = ceil((double)(MAX_DNUM / 8) / BLOCK_SIZE);
	INODES_PER_BLOCK = ceil((double)BLOCK_SIZE / sizeof(struct inode));
	INODE_TABLE_SIZE_IN_BLOCKS = ceil(MAX_INUM / INODES_PER_BLOCK);
	BLOCK_SIZE_IN_CHARACTERS = ceil(BLOCK_SIZE / 8);

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
  	diskfile_found = 1;

 	// write superblock information
	SUPERBLOCK = (struct superblock*)malloc(sizeof(struct superblock));
	if(SUPERBLOCK == NULL) {
		perror("ERROR:: Unable to allocate the superblock!");
		exit(-1);
	}
	SUPERBLOCK->magic_num = MAGIC_NUM;
	SUPERBLOCK->max_inum = MAX_INUM;
	SUPERBLOCK->max_dnum = MAX_DNUM;
	SUPERBLOCK->i_bitmap_blk = SUPERBLOCK_SIZE_IN_BLOCKS;
	SUPERBLOCK->d_bitmap_blk = SUPERBLOCK->i_bitmap_blk + INODE_BITMAP_SIZE_IN_BLOCKS;
	SUPERBLOCK->i_start_blk = SUPERBLOCK->d_bitmap_blk + DBLOCK_BITMAP_SIZE_IN_BLOCKS;
	SUPERBLOCK->d_start_blk = SUPERBLOCK->i_start_blk + INODE_TABLE_SIZE_IN_BLOCKS;
	bio_write(0, (void*)SUPERBLOCK);

	// initialize inode bitmap
	INODE_BITMAP = (bitmap_t)malloc(MAX_INUM / 8);
  	write_inode_bitmap();

	// initialize data block bitmap
	DBLOCK_BITMAP = (bitmap_t)malloc(MAX_DNUM / 8);
	write_dblock_bitmap();

	// allocate all the inodes and write them in the disk   
	for(int i = 0; i < MAX_INUM; i++) {
		struct inode* inode = create_inode("", i, FILE, INVALID, 0);
		writei(i, inode);
		free(inode);
	}

	// update bitmap information for root directory
	set_bitmap(INODE_BITMAP, 0);
  	write_inode_bitmap();

	// update inode for root directory
	struct inode* root_inode = create_inode("/", 0, 1, VALID, 1);
	writei(0, root_inode);
	
	

	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}

struct inode* create_inode(char* path, uint16_t ino, uint32_t type, uint8_t is_valid, int link){
	struct inode* new_inode = malloc(sizeof(struct inode));
 	memset(new_inode, 0, sizeof(struct inode));
	new_inode->ino = ino;
	new_inode->valid = is_valid;
	new_inode->size = 0;
	new_inode->link = link;
	for(int i = 0; i < 16; i++){
	  new_inode->direct_ptr[i] = INVALID;
	}
	for(int i = 0; i < 8; i++){
		new_inode->indirect_ptr[i] = INVALID;
	}
	// struct stat* buff;
	// int stat_result = stat(path, buff);
	// if (buff == NULL) {
	// 	perror("ERROR:: Unable to build a stat structure.");
	// 	exit(-1);
	// }
	// new_inode.vstat = *buff;
	return new_inode;
	
}