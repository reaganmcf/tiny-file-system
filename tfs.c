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
int DIRENTS_PER_BLOCK;

pthread_mutex_t file_system_lock;

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
  if (SUPERBLOCK == NULL) {
    perror("ERROR:: Superblock is NULL.");
    exit(-1);
  }

  void* buf = malloc(BLOCK_SIZE);
  bio_read(SUPERBLOCK->i_bitmap_blk, buf);
  memcpy(INODE_BITMAP, buf, MAX_INUM / 8);

  if (INODE_BITMAP == NULL) {
    perror("ERROR:: Could not read inode bitmap from disk.");
    exit(-1);
  }

  // Step 2: Traverse inode bitmap to find an available slot
  int idx = -1;
  for (int i = 0; i < MAX_INUM; i++) {
    int avail = get_bitmap(INODE_BITMAP, i);
    if (avail == 0) {
      idx = i;
      break;
    }
  }
  if (idx == -1) {
    perror("ERROR:: No available inodes in inode table.");
    exit(-1);
  }

  // Step 3: Update inode bitmap and write to disk
  set_bitmap(INODE_BITMAP, idx);
  bio_write(SUPERBLOCK->i_bitmap_blk, (void*)INODE_BITMAP);
  for (int i = 0; i < INODE_BITMAP_SIZE_IN_BLOCKS; i++) {
    bio_write(SUPERBLOCK->i_bitmap_blk + i,
              INODE_BITMAP[(int)(i * BLOCK_SIZE_IN_CHARACTERS)]);
  }
  //free(buf);

  return idx;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	 // Step 1: Read data block bitmap from disk
  if (SUPERBLOCK == NULL) {
    perror("ERROR:: Superblock is NULL.");
    exit(-1);
  }

  void* buf = malloc(BLOCK_SIZE);
  bio_read(SUPERBLOCK->d_bitmap_blk, buf);
  memcpy(DBLOCK_BITMAP, buf, MAX_DNUM / 8);

  if (DBLOCK_BITMAP == NULL) {
    perror("ERROR:: Could not read dnode bitmap from disk.");
    exit(-1);
  }


  // Step 2: Traverse data block bitmap to find an available slot
  int idx = -1;
  for (int i = 0; i < MAX_DNUM; i++) {
    int avail = get_bitmap(DBLOCK_BITMAP, i);
    if (avail == 0) {
      idx = i;
      break;
    }
  }

  if (idx == -1) {
    perror("ERROR:: No available inodes in dnode table.");
    exit(-1);
  }

  // Step 3: Update data block bitmap and write to disk
  set_bitmap(DBLOCK_BITMAP, idx);
  write_dblock_bitmap();
  free(buf);

  return idx;
}

/**
 * write a dirent to an index of a dblock number. 
 * block_no of 0 means the first dblock, so make sure to add SUPERBLOCK->d_start_blk
 */
int write_dirent(int block_no, int offset, struct dirent* directory_entry){
  void *block = malloc(BLOCK_SIZE);
	bio_read(SUPERBLOCK->d_start_blk + block_no, block);
	struct dirent* block_of_dirents = (struct dirent*)block;
	if(block_of_dirents == NULL){
		perror("ERROR:: Could not read data block");
		exit(-1);
	}
	block_of_dirents[offset] = *directory_entry;
	bio_write(SUPERBLOCK->d_start_blk + block_no, (void*)block_of_dirents);
	free(block);
}

/**
 * Format a block as a list of dirents. This doesn't populate them with anything particularly useful though
 */
void format_block_as_dirents(int block_no) {
	struct dirent* new_dirent = (struct dirent*)malloc(sizeof(struct dirent));
    memset(new_dirent, 0, sizeof(struct dirent));
    new_dirent->ino = 0;
    new_dirent->valid = INVALID;
    new_dirent->len = 0;
	for(int i = 0; i < DIRENTS_PER_BLOCK; i++) {
		write_dirent(block_no, i, new_dirent);
	}
	free(new_dirent);
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

  // update stat for inode (st_ctime is the timestamp of the last change to the inode)
  time(&(inode->vstat.st_ctime));

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
  struct inode cur_inode;
  readi(ino, &cur_inode);


  // Step 2: Get data block of current directory from inode
  
  void *buf = malloc(BLOCK_SIZE);
  for(int direct_ptr_index = 0; direct_ptr_index < DIRECT_PTR_NUM; direct_ptr_index++) {
    // If the direct ptr is valid (i.e., pointing to the dblock that contains dirents)
    if(cur_inode.direct_ptr[direct_ptr_index] != INVALID_PTR) {
	    bio_read(SUPERBLOCK->d_start_blk + cur_inode.direct_ptr[direct_ptr_index], buf);
	    struct dirent* block_of_dirents = (struct dirent*)buf;

      // Safety check: this should never happen but just incase
	    if(block_of_dirents == NULL){
		    perror("ERROR:: Could not read data block");
		    exit(-1);
	    }
      
      // Step 3: Read directory's data block and check each directory entry.
      for(int dirent_index = 0; dirent_index < DIRENTS_PER_BLOCK; dirent_index++) {
        struct dirent cur_dirent = block_of_dirents[dirent_index];
        if(cur_dirent.valid == VALID) { 
          //If the name matches, then copy directory entry to dirent structure
          if(strncmp(fname, cur_dirent.name, name_len) == 0) {
            *dirent = cur_dirent;
            
            free(buf);
            return DIRENT_FOUND;
          }
        }
      }
    }
  }

  // If we get all the way to here, then no dirent was found
  free(buf);
	return NO_DIRENT_FOUND;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	for(int i = 0; i < DIRECT_PTR_NUM; i++) {
		if(dir_inode.direct_ptr[i] != INVALID_PTR) {
			void *tmp = malloc(BLOCK_SIZE);
			bio_read(SUPERBLOCK->d_start_blk + dir_inode.direct_ptr[i], tmp);
			struct dirent *dirent_block = (struct dirent*) tmp;			

			// Step 2: Check if fname (directory name) is already used in other entries
			for(int j = 0; j < DIRENTS_PER_BLOCK; j++) {
				if(dirent_block[j].valid == VALID){
					if(strcmp(dirent_block[j].name, fname) == 0) {
            			return FILE_ALREADY_EXISTS;
					}
				}
			}
		}	
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	int direct_ptr_index = 0;
	int dirent_index = 0;
	int still_searching = 1;
	struct dirent* found_block;

	for(direct_ptr_index = 0; direct_ptr_index < DIRECT_PTR_NUM; direct_ptr_index++) {
		if(still_searching == 0){

      // since we found the direct ptr index on the LAST iteration, we need to reset
      // direct_ptr_index to what it was LAST iteration, as well as dirent_index
      direct_ptr_index--;
      dirent_index--;

			break;
		}

		if(dir_inode.direct_ptr[direct_ptr_index] != INVALID_PTR) {
			struct dirent *dirent_block = (struct dirent*)malloc(BLOCK_SIZE);
			bio_read(SUPERBLOCK->d_start_blk + dir_inode.direct_ptr[direct_ptr_index], dirent_block);

			for(dirent_index = 0; dirent_index < DIRENTS_PER_BLOCK && still_searching; dirent_index++) {
				if(dirent_block[dirent_index].valid == INVALID) {
          printf("dir_add:: first invalid dirent is at [%d][%d]\n", direct_ptr_index, dirent_index);
					still_searching = 0;
					found_block = dirent_block;
				}
			}	  
		}
	}

	// Allocate a new data block for this directory if it does not exist
	if(still_searching == 0) {

    printf("dir_add::\t%s\n", fname);

    printf("dir_add:: we found a spot in an already allocated dirent block!\n");
    found_block[dirent_index].len = name_len;
		strcpy(found_block[dirent_index].name, fname);
		found_block[dirent_index].ino = f_ino;
		found_block[dirent_index].valid = VALID;

    // write the dirent block back to disk
    printf("dir_add:: direct_ptr_index = %d, dirent data block is at offset %d\n", direct_ptr_index, dir_inode.direct_ptr[direct_ptr_index]);
    bio_write(SUPERBLOCK->d_start_blk + dir_inode.direct_ptr[direct_ptr_index], (void*)found_block);
		return FILE_ADDED_SUCCESSFULLY;
	} else {
    printf("dir_add having to make a new dirent block!\n");
		
    // We didn't find a spot in the valid dirent blocks, so make a new one!
		int new_dirent_block_no = get_avail_blkno(); 
    set_bitmap(DBLOCK_BITMAP, new_dirent_block_no);
    write_dblock_bitmap();
		
    // find first invalid direct ptr index
		int first_invalid_direct_ptr_index = -1;
		for(int i = 0; i < DIRECT_PTR_NUM; i++) {
			if(dir_inode.direct_ptr[i] == INVALID_PTR) {
				first_invalid_direct_ptr_index = i;
				break;
			}
		}
		
		// if we couldn't find any direct ptrs, then it's impossible to add another dirent
		if(first_invalid_direct_ptr_index == -1) {
			perror("ERROR:: No more space in the directory inode!");
			return NO_MORE_SPACE_FOR_DIRENTS;
		}

		dir_inode.direct_ptr[first_invalid_direct_ptr_index] = new_dirent_block_no;
		format_block_as_dirents(new_dirent_block_no);

		writei(dir_inode.ino, &dir_inode);

    return dir_add(dir_inode, f_ino, fname, name_len);
	}

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	for(int i = 0; i < DIRECT_PTR_NUM; i++) {
		if(dir_inode.direct_ptr[i] != INVALID_PTR) {
			void *tmp = malloc(BLOCK_SIZE);
			bio_read(SUPERBLOCK->d_start_blk + dir_inode.direct_ptr[i], tmp);
			struct dirent *dirent_block = (struct dirent*) tmp;			

			// Step 2: Check if fname exist
			for(int j = 0; j < DIRENTS_PER_BLOCK; j++) {
				if(dirent_block[j].valid == VALID){
					if(strcmp(dirent_block[j].name, fname) == 0) {
            			// Step 3: If exist, then remove it from dir_inode's data block and write to disk
						struct dirent* new_dirent = (struct dirent*)malloc(sizeof(struct dirent));
						memset(new_dirent, 0, sizeof(struct dirent));
						new_dirent->ino = 0;
						new_dirent->valid = INVALID;
						new_dirent->len = 0;
						write_dirent(dir_inode.direct_ptr[i], j, new_dirent);
						free(new_dirent);
					}
				}
			}
		}	
	}

	return 1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	
	// Step 1: Resolve the path name, walk through path, and finally, find its
	// inode. Note: You could either implement it in a iterative way or recursive
	// way get inode struct corresponding to it

  // Read the inode given the inode number
  readi(ino, inode);

  // get the next token in the path, setting path to the remaining path str
  //    i.e., if path is /foo/bar, then token will be "foo" and path will now be "/bar"
  char *token = strtok_r(path, "/", &path);
  printf("GET_NODE_BY_PATH:: token = %s\n", token);

  // if the token is null, there is no more path to walk through.
  // Therefore, we know we found the inode if inode->valid == VALID or not
  if(token == NULL) {
    if (inode->valid == VALID)
      return FOUND_INODE;
    return NO_INODE_FOUND;
  }

  // If the token is not NULL, then we still have more path to walk (so the latest token of the path must be a directory)
  // We first need to ensure that the current token of the path is actually inside of the current directory
  // that is, some dirent entry in one of the directories data blocks
  struct dirent *cur_dirent = malloc(sizeof(struct dirent));
  int status = dir_find(ino, token, strlen(token) + 1, cur_dirent);
  
  // If no corresponding dirent was found for this path, then there is no corresponding inode and we should return
  if (status == NO_DIRENT_FOUND) {
    printf("GET_NODE_BY_PATH:: no dirent for the token!\n");
    free(cur_dirent);
    return NO_INODE_FOUND;
  }

  // If a corresponding dirent WAS found ,then we need to recursively call this function on the dirent
  int new_ino = cur_dirent->ino;
  free(cur_dirent);
  return get_node_by_path(path, new_ino, inode);
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Lock the file system
	pthread_mutex_unlock(&file_system_lock);

	SUPERBLOCK_SIZE_IN_BLOCKS = ceil((double)sizeof(struct superblock) / BLOCK_SIZE);
	INODE_BITMAP_SIZE_IN_BLOCKS = ceil((double)(MAX_INUM / 8) / BLOCK_SIZE);
	DBLOCK_BITMAP_SIZE_IN_BLOCKS = ceil((double)(MAX_DNUM / 8) / BLOCK_SIZE);
	INODES_PER_BLOCK = floor((double)BLOCK_SIZE / sizeof(struct inode));
 	DIRENTS_PER_BLOCK = floor((double)BLOCK_SIZE / sizeof(struct dirent));
	INODE_TABLE_SIZE_IN_BLOCKS = ceil(MAX_INUM / INODES_PER_BLOCK);
	BLOCK_SIZE_IN_CHARACTERS = ceil(BLOCK_SIZE / 8);

	// Call dev_init() to initialize (Create) Diskfile

 	// write superblock information
	SUPERBLOCK = malloc(sizeof(struct superblock));
	memset(SUPERBLOCK, 0, sizeof(struct superblock));
	if(SUPERBLOCK == NULL) {
		perror("ERROR:: Unable to allocate the superblock!");
		exit(-1);
	}

	INODE_BITMAP = (bitmap_t)malloc(MAX_INUM / 8);
	DBLOCK_BITMAP = (bitmap_t)malloc(MAX_DNUM / 8);


	if(dev_open(diskfile_path) < 0) {
		
		printf("Unable to find diskfile path!\n");
		dev_init(diskfile_path);
		SUPERBLOCK->magic_num = MAGIC_NUM;
		SUPERBLOCK->max_inum = MAX_INUM;
		SUPERBLOCK->max_dnum = MAX_DNUM;
		SUPERBLOCK->i_bitmap_blk = SUPERBLOCK_SIZE_IN_BLOCKS;
		SUPERBLOCK->d_bitmap_blk = SUPERBLOCK->i_bitmap_blk + INODE_BITMAP_SIZE_IN_BLOCKS;
		SUPERBLOCK->i_start_blk = SUPERBLOCK->d_bitmap_blk + DBLOCK_BITMAP_SIZE_IN_BLOCKS;
		SUPERBLOCK->d_start_blk = SUPERBLOCK->i_start_blk + INODE_TABLE_SIZE_IN_BLOCKS;
		bio_write(0, (void*)SUPERBLOCK);

		// initialize inode bitmap
		write_inode_bitmap();

		// initialize data block bitmap
		write_dblock_bitmap();

		// allocate all the inodes and write them in the disk   
		for(int i = 1; i < MAX_INUM; i++) {
			struct inode* inode = malloc(sizeof(struct inode));
			memset(inode, 0, sizeof(struct inode));
			inode->valid = INVALID;

			// Set all direct links to invalid
			for (int k = 0; k < 16; k++)
				inode->direct_ptr[k] = INVALID;

			// Set all indirect links to invalid
			for (int k = 0; k < 8; k++)
				inode->indirect_ptr[k] = INVALID;

			inode->ino = i;

			// Write inode block to disk
			writei(i, inode);
			free(inode);
		}

		// update bitmap information for root directory
		set_bitmap(INODE_BITMAP, 0);
		write_inode_bitmap();

		// update inode for root directory
		struct inode* root_inode = create_inode("/", 0, FOLDER, VALID, 2); // this has 2 links because "." points to this inode as well

		// root directory's first direct ptr should point to a block of dirents	
		int root_dirent_block_no = get_avail_blkno(); 
		set_bitmap(DBLOCK_BITMAP, root_dirent_block_no);
		write_dblock_bitmap();

		root_inode->direct_ptr[0] = root_dirent_block_no;
		format_block_as_dirents(root_dirent_block_no);
		writei(0, root_inode);
		
		// Add "." to root inode
		dir_add(*root_inode, root_inode, ".", 2);

		free(root_inode);
	}else{

    // Create a buffer we will be reading from
		void* buf = calloc(1, BLOCK_SIZE);
		
    // Load superblock into memory
    bio_read(0, buf);
		memcpy(SUPERBLOCK, buf, sizeof(struct superblock));

    // Load inode bitmap into memory
		memset(buf, 0, BLOCK_SIZE);
		bio_read(SUPERBLOCK->i_bitmap_blk, buf);
		memcpy(INODE_BITMAP, buf, MAX_INUM / 8);
		

    // Load datablock bitmap into memory
    memset(buf, 0, BLOCK_SIZE);
		bio_read(SUPERBLOCK->d_bitmap_blk, buf);
		memcpy(DBLOCK_BITMAP, buf, MAX_DNUM / 8);

		free(buf);
	}


  diskfile_found = 1;

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);
	
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {	
	// Step 1a: If disk file is not found, call mkfs
	if (diskfile_found == 0) {
		tfs_mkfs();
	}

	// init lock
	pthread_mutex_init(&file_system_lock);

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	if (!INODE_BITMAP) {
		if ((INODE_BITMAP = calloc(1, MAX_INUM / 8)) == NULL) {
		perror("ERROR:: Unable to allocate the inode bitmap.");
		exit(-1);
		}
	}

	if (!DBLOCK_BITMAP) {
		if ((DBLOCK_BITMAP = calloc(1, MAX_DNUM / 8)) == NULL) {
		perror("ERROR:: Unable to allocate the datablock bitmap.");
		exit(-1);
		}
	}

	if (!SUPERBLOCK) {
		void* buf = malloc(BLOCK_SIZE);
		bio_read(0, buf);
		SUPERBLOCK = (struct superblock*)buf;
		free(buf);
	}

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
  free(SUPERBLOCK);
  free(INODE_BITMAP);
  free(DBLOCK_BITMAP);
  pthread_mutex_destroy(&file_system_lock);

	// Step 2: Close diskfile
  dev_close();

}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: call get_node_by_path() to get inode from path
  struct inode inode;
  int status = get_node_by_path(path, ROOT_INODE_NUMBER, &inode);
  if (status == NO_INODE_FOUND) {
    printf("GETATTR:: Couldn't find inode for the path!\n");
		pthread_mutex_unlock(&file_system_lock);
    return -ENOENT;
  }

  printf("GETATTR:: Found inode for the file!\n");
  
  if(inode.type == FOLDER) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2; //. and itself
  } else {
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1; // ?
  	stbuf->st_size = inode.size;
  }

	// Step 2: fill attribute of file into stbuf from inode

  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();

  stbuf->st_atime = inode.vstat.st_atime;
  stbuf->st_mtime = inode.vstat.st_mtime;
  stbuf->st_ctime = inode.vstat.st_ctime;

  // Unlock the file system
	pthread_mutex_unlock(&file_system_lock);
  return FOUND_INODE;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Call get_node_by_path() to get inode from path
  struct inode dir_inode;
  int status = get_node_by_path(path, ROOT_INODE_NUMBER, &dir_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);
	// Step 2: If not find, return -1
  if(status == FOUND_INODE)
    return 0;
  
  return -1;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	// Lock the ifle system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Call get_node_by_path() to get inode from path
  struct inode dir_inode;
  int status = get_node_by_path(path, ROOT_INODE_NUMBER, &dir_inode);

  printf("dir_inode ino is = %d\n", dir_inode.ino);

	// Step 2: Read directory entries from its data blocks, and copy them to filler
  for(int i = 0; i < DIRECT_PTR_NUM; i++) {
    if(dir_inode.direct_ptr[i] != INVALID_PTR) {
      printf("READDIR:: direct_ptr[%d] is not invalid!\n", i);
      
      struct dirent *dirent_block = malloc(BLOCK_SIZE);
      bio_read(SUPERBLOCK->d_start_blk + dir_inode.direct_ptr[i], dirent_block);

      for(int j = 0; j < DIRENTS_PER_BLOCK; j++) {
        printf("\tdirent_block[j].name = %s\n", dirent_block[j].name);
        if(dirent_block[j].valid == VALID) {
          filler(buffer, dirent_block[j].name, NULL, 0);
        }
      };

      free(dirent_block);
    }
  }

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
  // Note: this won't work without a strcpy because it fucks with the path ptr
  char *path_copy = calloc(1, strlen(path) + 1);
  strcpy(path_copy, path);
  char* directory_name = dirname(path_copy);
  char* base_name = basename(path);

  printf("MKDIR:: directory_name = %s, base_name = %s\n",
         directory_name,
         base_name);

	// Step 2: Call get_node_by_path() to get inode of parent directory
  struct inode parent_dir_inode;
  get_node_by_path(directory_name, ROOT_INODE_NUMBER, &parent_dir_inode);

	// Step 3: Call get_avail_ino() to get an available inode number
  int dir_ino = get_avail_ino();
  set_bitmap(INODE_BITMAP, dir_ino);
  write_inode_bitmap();

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
  dir_add(parent_dir_inode, dir_ino, base_name, strlen(base_name) + 1);

	// Step 5: Update inode for target directory and write it to disk
  struct inode* new_dir_inode = create_inode(path, dir_ino, FOLDER, VALID, 2);
  writei(dir_ino, new_dir_inode);

  // add the "." for the new dir
  dir_add(*new_dir_inode, dir_ino, ".", 2);

  // Note, we have to re-read since we just wrote a dir_add to disk for this inode
  readi(dir_ino, new_dir_inode);

  // add the ".." for the parent inode
  dir_add(*new_dir_inode, parent_dir_inode.ino, "..", 3);

  free(new_dir_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

  return 0;
}

static int tfs_rmdir(const char *path) {
	// Lock the file system 
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char *path_copy = calloc(1, strlen(path) + 1);
	strcpy(path_copy, path);
	char* directory_name = dirname(path_copy);
	char* base_name = basename(path);

	printf("MKDIR:: directory_name = %s, base_name = %s\n",
			directory_name,
			base_name);

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode target_dir_inode;
  if(get_node_by_path(path, ROOT_INODE_NUMBER, &target_dir_inode) == NO_INODE_FOUND){
		pthread_mutex_unlock(&file_system_lock);
		return -1;
	}

	// Step 3: Clear data block bitmap of target directory
	for(int i = 0; i < DIRECT_PTR_NUM; i++){
		if(target_dir_inode.direct_ptr[i] != INVALID_PTR){
			unset_bitmap(DBLOCK_BITMAP, target_dir_inode.direct_ptr[i]);
		}
	}
	write_dblock_bitmap();

	// Step 4: Clear inode bitmap and its data block
	target_dir_inode.link--;
	if(target_dir_inode.link <= 0){
		unset_bitmap(INODE_BITMAP, target_dir_inode.ino);
		write_inode_bitmap();
		target_dir_inode.valid = INVALID;
	}
	writei(target_dir_inode.ino, &target_dir_inode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode parent_inode;
  	get_node_by_path(directory_name, ROOT_INODE_NUMBER, &parent_inode);

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(parent_inode, base_name, strlen(base_name));

	pthread_mutex_unlock(&file_system_lock);
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
  return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

  // Note: this won't work without a strcpy because it fucks with the path ptr
  char *path_copy = calloc(1, strlen(path) + 1);
  strcpy(path_copy, path);
  char* directory_name = dirname(path_copy);
  char* base_name = basename(path);

  printf("CREATE:: directory_name = %s, base_name = %s\n",
         directory_name,
         base_name);

	// Step 2: Call get_node_by_path() to get inode of parent directory
  struct inode parent_dir_inode;
  get_node_by_path(directory_name, ROOT_INODE_NUMBER, &parent_dir_inode);

	// Step 3: Call get_avail_ino() to get an available inode number
  int file_ino = get_avail_ino();
  set_bitmap(INODE_BITMAP, file_ino);
  write_inode_bitmap();

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
  dir_add(parent_dir_inode, file_ino, base_name, strlen(base_name) + 1);

	// Step 5: Update inode for target file
  struct inode* new_file_inode = create_inode(path, file_ino, FILE, VALID, 1);

	// Step 6: Call writei() to write new inode to disk
  writei(file_ino, new_file_inode);
  free(new_file_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

  return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Call get_node_by_path() to get inode from path
  struct inode dir_inode;
  int status = get_node_by_path(path, ROOT_INODE_NUMBER, &dir_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

	// Step 2: If not find, return -1
  if(status == FOUND_INODE)
    return 0;
  
  return -1;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode file_inode;
	int status = get_node_by_path(path, ROOT_INODE_NUMBER, &file_inode);
	if(status != FOUND_INODE) {
		pthread_mutex_unlock(&file_system_lock);
		return -ENOENT;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int size_in_blocks = ceil(size / BLOCK_SIZE);
	int bytes_read = 0;
	int starting_block_idx  = offset / BLOCK_SIZE;
	printf("\noffset: %d, size in blocks : %d, size: %d\n\n", offset, size_in_blocks, size);
	for(int i = 0; i < size_in_blocks; i++){
		// If the file does not have an existing datablock where we need to write to, we can't read anything
		if(file_inode.direct_ptr[i] == INVALID_PTR){
			printf("\nNO DATABLOCK, iteration %d\n", i);
			pthread_mutex_unlock(&file_system_lock);
			return 0;

		// The file has a datablock where we need to read, so copy it to the correct spot in the buffer
		}else{
			printf("\nHAS DATABLOCK, iteration %d\n", i);
			void* buf = malloc(BLOCK_SIZE);
			bio_read(SUPERBLOCK->d_start_blk + file_inode.direct_ptr[starting_block_idx], buf);
			if(size >= BLOCK_SIZE){
				printf("\nThis probably should not be happening\n");
				memcpy((buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), buf, BLOCK_SIZE);
				size -= BLOCK_SIZE;
				bytes_read += BLOCK_SIZE;
			}else{
				memcpy((buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), buf, size);
				bytes_read += size;
				size = 0;
			}
			offset = 0;
			starting_block_idx++;	
			free(buf);
		}		
	}

	printf("\n buffer: %s, bytes read: %d\n", buffer, bytes_read);

	// update stat for the inode
	time(&(file_inode.vstat.st_atime));
	writei(file_inode.ino, &file_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

	// Note: this function should return the amount of bytes you read from disk
	return bytes_read;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Lock the file system
	pthread_mutex_lock(&file_system_lock);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode file_inode;
	int status = get_node_by_path(path, ROOT_INODE_NUMBER, &file_inode);
	if(status != FOUND_INODE) {
		pthread_mutex_unlock(&file_system_lock);
		return 0;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int size_in_blocks = (size / BLOCK_SIZE)+1;
	int bytes_written = 0;
	int starting_block_idx  = offset / BLOCK_SIZE;
	printf("\n WRITING buffer: %s\noffset: %d, size in blocks : %d\n\n", buffer, offset, size_in_blocks);
	for(int i = 0; i < size_in_blocks; i++){
		// If the file does not have an existing datablock where we need to write to, we need to create one
		if(file_inode.direct_ptr[starting_block_idx] == INVALID_PTR){
			printf("\nWRITING: NO DATABLOCK, iteration %d\n", i);
			int new_block_num = get_avail_blkno();
			file_inode.direct_ptr[starting_block_idx] = new_block_num;
			
			void* buf = malloc(BLOCK_SIZE);
			if(size >= BLOCK_SIZE){
				memcpy(buf, (buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), BLOCK_SIZE);
				size -= BLOCK_SIZE;
				bytes_written += BLOCK_SIZE;
				file_inode.size += BLOCK_SIZE;
			}else{
				memcpy(buf, (buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), size);
				bytes_written += size;
				size = 0;
				file_inode.size += size;
			}
			bio_write(SUPERBLOCK->d_start_blk + new_block_num, buf);
			printf("\nWROTE TO BLOCK %d\n", SUPERBLOCK->d_start_blk + new_block_num);
			free(buf);

		// The file has a datablock where we need to write, so read it and memcpy at the appropriote offset
		}else{
			printf("\nHAS DATABLOCK\n");
			char* buf = malloc(BLOCK_SIZE);
			bio_read(SUPERBLOCK->d_start_blk + file_inode.direct_ptr[starting_block_idx], buf);
			if(size >= BLOCK_SIZE){
				memcpy(buf + (offset % BLOCK_SIZE), (buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), BLOCK_SIZE);
				size -= BLOCK_SIZE;
				bytes_written += BLOCK_SIZE;
				file_inode.size += BLOCK_SIZE;
				// TODO: Figure out how to distinct when file is actually getting bigger or not
			}else{
				memcpy(buf + (offset % BLOCK_SIZE), (buffer + (i*BLOCK_SIZE_IN_CHARACTERS)), size);
				if(size + offset <= strlen(buf)){
					memset(buf + (offset % BLOCK_SIZE) + size, '\0', BLOCK_SIZE - size - offset);
				}
				bytes_written += size;
				size = 0;
				file_inode.size += size;
			}
			bio_write(SUPERBLOCK->d_start_blk + file_inode.direct_ptr[starting_block_idx], buf);
			free(buf);
		}
		offset = 0;
		starting_block_idx++;	
	}

	// Step 3: Write the correct amount of data from offset to disk
  

	// Step 4: Update the inode info and write it to disk
 	time(&(file_inode.vstat.st_mtime));

	writei(file_inode.ino, &file_inode);

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

	// Note: this function should return the amount of bytes you write to disk
	return bytes_written;
}

static int tfs_unlink(const char *path) {

	// Lock the file systme
	pthread_mutex_lock(&file_system_lock);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char *path_copy = calloc(1, strlen(path) + 1);
	strcpy(path_copy, path);
	char* directory_name = dirname(path_copy);
	char* base_name = basename(path);

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode target_inode;
  	get_node_by_path(path, ROOT_INODE_NUMBER, &target_inode);

	// Step 3: Clear data block bitmap of target file
	for(int i = 0; i < DIRECT_PTR_NUM; i++){
		if(target_inode.direct_ptr[i] != INVALID_PTR){
			unset_bitmap(DBLOCK_BITMAP, target_inode.direct_ptr[i]);
		}
	}
	write_dblock_bitmap();

	// Step 4: Clear inode bitmap and its data block
	target_inode.link--;
	if(target_inode.link <= 0){
		unset_bitmap(INODE_BITMAP, target_inode.ino);
		write_inode_bitmap();
		target_inode.valid = INVALID;
	}
	writei(target_inode.ino, &target_inode);
	

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode parent_inode;
  	get_node_by_path(directory_name, ROOT_INODE_NUMBER, &parent_inode);

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	dir_remove(parent_inode, base_name, strlen(base_name));

	// Unlock the file system
	pthread_mutex_unlock(&file_system_lock);

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
  new_inode->type = type;
	new_inode->valid = is_valid;
	new_inode->size = 0;
	new_inode->link = link;
	for(int i = 0; i < 16; i++){
	  new_inode->direct_ptr[i] = INVALID_PTR;
	}
	for(int i = 0; i < 8; i++){
		new_inode->indirect_ptr[i] = INVALID_PTR;
	}
  time(&(new_inode->vstat.st_atime));
  time(&(new_inode->vstat.st_mtime));
  time(&(new_inode->vstat.st_ctime));
	
  return new_inode;
}
