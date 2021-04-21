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

// Data structures. Note, these must be written to disk after each modification!
struct superblock* SUPERBLOCK;
bitmap_t INODE_BITMAP;
bitmap_t DBLOCK_BITMAP;


int diskfile_found = 0;

// Size constants used for calculating where everything goes in the disk
#define SUPERBLOCK_SIZE_IN_BLOCKS (ceil((double)sizeof(SUPERBLOCK) / BLOCK_SIZE))
#define INODE_BITMAP_SIZE_IN_BLOCKS (ceil((double)(MAX_INUM / 8) / BLOCK_SIZE))
#define DBLOCK_BITMAP_SIZE_IN_BLOCKS (ceil((double)(MAX_DNUM / 8) / BLOCK_SIZE))
#define INODE_TABLE_SIZE_IN_BLOCKS (ceil((double)(sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE))
#define BLOCK_SIZE_IN_CHARACTERS ((int)ceil(BLOCK_SIZE / sizeof(char)))
#define INODES_PER_BLOCK (floor(BLOCK_SIZE / sizeof(struct inode)))

/* TODO:
		-test get_avail_ino
		-test get_avail_blkno
		-test readi
		-test writei
		-finish the dynamic read/write function
*/


/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	if(SUPERBLOCK == NULL){
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}

	bitmap_t buf = (bitmap_t)malloc(sizeof(char) * (MAX_INUM / 8));
	for(int i = 0; i < INODE_BITMAP_SIZE_IN_BLOCKS; i++){
		bio_read(SUPERBLOCK->i_bitmap_blk + i, buf[(int)(i * BLOCK_SIZE_IN_CHARACTERS)]);
	}

	INODE_BITMAP = (bitmap_t)buf;
	if(INODE_BITMAP == NULL){
		perror("ERROR:: Could not read inode bitmap from disk.");
		exit(-1);
	}
	
	// Step 2: Traverse inode bitmap to find an available slot
	int idx = -1;
	for(int i = 0; i < MAX_INUM; i++){
		int avail = get_bitmap(INODE_BITMAP, i);
		if(avail == 0){
			idx = i;
			break;
		}
	}
	if(idx == -1){
		perror("ERROR:: No available inodes in inode table.");
		exit(-1);
	}

	// Step 3: Update inode bitmap and write to disk
	set_bitmap(INODE_BITMAP, idx);
	for(int i = 0; i < INODE_BITMAP_SIZE_IN_BLOCKS; i++){
		bio_write(SUPERBLOCK->i_bitmap_blk + i, INODE_BITMAP[(int)(i * BLOCK_SIZE_IN_CHARACTERS)]);
	}
	free(buf);

	return 1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	if(SUPERBLOCK == NULL){
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}

	bitmap_t buf = (bitmap_t)malloc(sizeof(char) * (MAX_DNUM / 8));
	for(int i = 0; i < DBLOCK_BITMAP_SIZE_IN_BLOCKS; i++){
		bio_read(SUPERBLOCK->d_bitmap_blk + i, buf[(int)(i * BLOCK_SIZE_IN_CHARACTERS)]);
	}

	DBLOCK_BITMAP = (bitmap_t)buf;
	if(DBLOCK_BITMAP == NULL){
		perror("ERROR:: Could not read dnode bitmap from disk.");
		exit(-1);
	}
	
	// Step 2: Traverse data block bitmap to find an available slot
	int idx = -1;
	for(int i = 0; i < MAX_DNUM; i++){
		int avail = get_bitmap(DBLOCK_BITMAP, i);
		if(avail == 0){
			idx = i;
			break;
		}
	}
	if(idx == -1){
		perror("ERROR:: No available inodes in dnode table.");
		exit(-1);
	}

	// Step 3: Update data block bitmap and write to disk 
	set_bitmap(DBLOCK_BITMAP, idx);
	for(int i = 0; i < DBLOCK_BITMAP_SIZE_IN_BLOCKS; i++){
		bio_write(SUPERBLOCK->d_bitmap_blk + i, DBLOCK_BITMAP[(int)(i * BLOCK_SIZE_IN_CHARACTERS)]);
	}
	free(buf);
	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
	if(SUPERBLOCK == NULL){
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}

	int block_number = SUPERBLOCK->i_start_blk + floor(ino / INODES_PER_BLOCK);

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = ino % (int)INODES_PER_BLOCK;
	

	// Step 3: Read the block from disk and then copy into inode structure
	void* buf = malloc(BLOCK_SIZE);
	bio_read(block_number, buf);
	if(buf == NULL){
		perror("ERROR:: Could not read inode from inode table");
		exit(-1);
	}
	struct inode* block_of_inodes = (struct inode*)buf;
	printf("\n\n\nino = %d, block num = %d, offset = %d\n\n\n", ino, block_number, offset);
	inode = &block_of_inodes[offset];
	free(buf);
	return 1;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	if(SUPERBLOCK == NULL){
		perror("ERROR:: Superblock is NULL.");
		exit(-1);
	}
	int block_number = SUPERBLOCK->i_start_blk + floor(ino / INODES_PER_BLOCK);
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % (int)INODES_PER_BLOCK;

	// Step 3: Write inode to disk 
	void* buf = malloc(BLOCK_SIZE);
	bio_read(block_number, buf);
	if(buf == NULL){
		perror("ERROR:: Could not read inode from inode table");
		exit(-1);
	}
	struct inode* block_of_inodes = (struct inode*)buf;
	*(block_of_inodes + offset) = *(inode);

	bio_write(block_number, (void*)block_of_inodes);
	free(buf);
	return 1;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode* cur_inode = malloc(sizeof(struct inode));
	readi(ino, cur_inode);

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
	// get inode struct corresponding to ino
      
	// Get the inode we are currently at
	struct inode *cur_inode;
	readi(ino, cur_inode);
	inode = cur_inode;

	// base case
	char *token = strtok_r(path, "/", &path);
	printf("token = %s\n", token);
	if(token == NULL) {
	  return 0;
	}

	// find the dirent we are at, since we haven't found the terminal inode yet
	struct dirent *cur_dirent = malloc(sizeof(struct dirent));
	dir_find(ino, token, sizeof(token), cur_dirent);
	return get_node_by_path(path, cur_dirent->ino, inode);

}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	diskfile_found = 1;
	
	// write superblock information
	if((SUPERBLOCK = calloc(sizeof(struct superblock), 0)) == NULL){
		perror("ERROR:: Unable to allocate the superblock.");
		exit(-1);
	}
	SUPERBLOCK->magic_num = MAGIC_NUM;
	SUPERBLOCK->max_inum = MAX_INUM;
	SUPERBLOCK->max_dnum = MAX_DNUM;
	// inode bitmap is the block right after the super block(s)
	SUPERBLOCK->i_bitmap_blk = SUPERBLOCK_SIZE_IN_BLOCKS;
	// dblock bitmap is the block right after the inode bitmap block(s)
	SUPERBLOCK->d_bitmap_blk = SUPERBLOCK->i_bitmap_blk + INODE_BITMAP_SIZE_IN_BLOCKS;
	// inode table block is the block right after the dblock bitmap block(s)
	SUPERBLOCK->i_start_blk = SUPERBLOCK->d_bitmap_blk + DBLOCK_BITMAP_SIZE_IN_BLOCKS;
	// data blocks start at the block right after the inode table block
	SUPERBLOCK->d_start_blk = SUPERBLOCK->i_start_blk + INODE_TABLE_SIZE_IN_BLOCKS;
	bio_write(0, (void*)SUPERBLOCK);

	// printf("\tsuperblock is at block %d\n \
	// 	inode bitmap is at block %d\n \
	// 	dblock bitmap is at block %d\n \
	// 	inodetable is at block %d\n \
	// 	dataregion is at block %d\n",
	// 	0, SUPERBLOCK->i_bitmap_blk, SUPERBLOCK->d_bitmap_blk, SUPERBLOCK->i_start_blk, SUPERBLOCK->d_start_blk);

	// initialize inode bitmap
	if((INODE_BITMAP = calloc(MAX_INUM / 8, 0)) == NULL){
		perror("ERROR:: Unable to allocate the inode bitmap.");
		exit(-1);
	}

	// initialize data block bitmap
	if((DBLOCK_BITMAP = calloc(MAX_DNUM / 8, 0)) == NULL){
		perror("ERROR:: Unable to allocate the datablock bitmap.");
		exit(-1);
	}
	
	// update bitmap information for root directory
	// update inode for root directory
	struct inode* root_dir_inode = (struct inode*)malloc(sizeof(struct inode));
	if(root_dir_inode == NULL){
	    perror("ERROR:: Unable to allocate the root directory inode.");
	    exit(-1);
	}

	root_dir_inode->ino = 0;
	root_dir_inode->valid = 1;
	root_dir_inode->size = 69;
	root_dir_inode->type = DIRECTORY;
	root_dir_inode->link = 0;
	
	struct stat* buff;
	int stat_result = stat("/", buff);
	if(buff == NULL){
	    perror("ERROR:: Unable to build a stat structure.");
	    exit(-1);
	}
	root_dir_inode->vstat = *buff;

	bio_write(SUPERBLOCK->i_start_blk, (void*)(root_dir_inode));

	//TESTING
	struct inode* test = malloc(sizeof(struct inode));
	readi(0, test);
	printf("\n\ntesting: %d\n\n", test->size);
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {
    
	// Step 1a: If disk file is not found, call mkfs
	if( diskfile_found == 0){
		tfs_mkfs();
	}
	

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	if(!INODE_BITMAP){
		if((INODE_BITMAP = calloc(MAX_INUM / 8, 0)) == NULL){
			perror("ERROR:: Unable to allocate the inode bitmap.");
			exit(-1);
		}
	}

	if(!DBLOCK_BITMAP){
		if((DBLOCK_BITMAP = calloc(MAX_DNUM / 8, 0)) == NULL){
			perror("ERROR:: Unable to allocate the datablock bitmap.");
			exit(-1);
		}
	}

	if(!SUPERBLOCK){
		void* buf = malloc(sizeof(struct superblock*));
		bio_read(0, buf);
		SUPERBLOCK = (struct superblock*)buf;
		free(buf);
	}

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(DBLOCK_BITMAP);
	free(INODE_BITMAP);
	free(SUPERBLOCK);

	// Step 2: Close diskfile
	dev_close();

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
	
	printf("\n\n\n");

	printf("PATH: %s", path); 
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode *dir_inode; 
	get_node_by_path(path, ROOT_INODE_NUMBER, dir_inode);

	// Step 2: Read directory entries from its data blocks, and copy them to fille
	struct stat *t;
	
	filler(buffer, "/", NULL, offset);

	printf("\n\n\n");
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

static int tfs_release(const char *path, struct fuse_file_info *fi){
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

// TODO: Finish this func, test if it works
// To dynamically read/write as many blocks as needed
void dynamic_io(int mode, int block_num, int num_blocks, void* buffer){
	// 1 --> read
	if(mode == 1){
		char buff[num_blocks * (BLOCK_SIZE / 8)];
		for(int i = 0; i < num_blocks; i++) {
			void* temp = malloc(sizeof(char) * BLOCK_SIZE_IN_CHARACTERS);
			bio_read(block_num + i, temp);
			memcpy(buff[(BLOCK_SIZE / 8) * i], temp, BLOCK_SIZE);
		}
		memcpy(buffer, buff, (BLOCK_SIZE / 8) * num_blocks);
	}
}
