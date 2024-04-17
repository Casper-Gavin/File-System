#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <sys/mman.h>

#define BLKSIZE 4048

const int root_inode_number = 2;       // 0 is "invalid", 1 is reserved for badsectors data
const int badsectors_inode_number = 1; // 1 for badsector data
const int invalid_inode_number = 0;    // 0 is "invalid"

const int root_datablock_number = 0; // 1 because 0 reserved for badsectors data

typedef struct _block_t
{
  char data[BLKSIZE];
} block_t;

typedef struct _inode_t
{
  // int uid;
  // int gid;
  // char rwx;
  int size;   // of file in bytes
  int blocks; // blocks allocated to this file - used or not
  // struct timeval atime;
  // struct timeval mtime;
  //  struct timeval ctime;  // creation time
  // int links_count;  // hardlinks
  block_t *data[15]; // 12 direct, 1 single-indirect, 1 double-indirect, 1 triple-indirect
} inode_t;

typedef struct _dirent_t
{
  int inode;      // entry's inode number
  char file_type; // entry's file type (0: unknown, 1: file, 2: directory)
  int name_len;   // entry's name length ('\0'-excluded)
  char name[255]; // entry's name ('\0'-terminated)
} dirent_t;

typedef struct _superblock_info_t
{
  int blocks;     // total number of filesystem data blocks
  char name[255]; // name identifier of filesystem
} superblock_info_t;

union superblock_t
{
  block_t block;                     // ensures superblock_t size matches block_t size (1 block)
  superblock_info_t superblock_info; // to access the superblock info
};

typedef struct _groupdescriptor_info_t
{
  inode_t *inode_table; // location of inode_table (first inode_t in region)
  block_t *block_data;  // location of block_data (first block_t in region)
} groupdescriptor_info_t;

union groupdescriptor_t
{
  block_t block;                               // ensures groupdescriptor_t size matches block_t size (1 block)
  groupdescriptor_info_t groupdescriptor_info; // to access the groupdescriptor info
};

typedef struct _myfs_t
{
  union superblock_t super;                // superblock
  union groupdescriptor_t groupdescriptor; // groupdescriptor
  block_t bmap;                            // (free/used) block bitmap
  block_t imap;                            // (free/used) inode bitmap
} myfs_t;

myfs_t *my_mkfs(int size, int maxfiles);
void my_dumpfs(myfs_t *myfs);
void dump_dirinode(myfs_t *myfs, int inode_number, int level);
void my_crawlfs(myfs_t *myfs);

void my_creatdir(myfs_t *myfs, int cur_dir_inode_number, const char *new_dirname);

int checkDebug(int debugValue);
int stepOne(myfs_t *myfs);
int stepTwo(myfs_t *myfs) ;

int roundup(int x, int y)
{
  return x == 0 ? 0 : 1 + ((x - 1) / y);
}

int main(int argc, char *argv[])
{

  inode_t *cur_dir_inode = NULL;

  myfs_t *myfs = my_mkfs(100 * BLKSIZE, 10);

  // create 2 dirs inside [/] (root dir)
  int cur_dir_inode_number = 2;                        // root inode
  my_creatdir(myfs, cur_dir_inode_number, "mystuff");  // will be inode 3
  my_creatdir(myfs, cur_dir_inode_number, "homework"); // will be inode 4

  // create 1 dir inside [/homework] dir
  cur_dir_inode_number = 4;
  my_creatdir(myfs, cur_dir_inode_number, "assignment5"); // will be inode 5

  // create 1 dir inside [/homework/assignment5] dir
  cur_dir_inode_number = 5;
  my_creatdir(myfs, cur_dir_inode_number, "mycode"); // will be inode 6

  // create 1 dir inside [/homework/mystuff] dir
  cur_dir_inode_number = 3;
  my_creatdir(myfs, cur_dir_inode_number, "mydata"); // will be inode 7

  printf("\nDumping filesystem structure:\n");
  my_dumpfs(myfs);

  printf("\nCrawling filesystem structure:\n");
  my_crawlfs(myfs);

  return 0;
}

myfs_t *my_mkfs(int size, int maxfiles)
{
  int num_data_blocks = roundup(size, BLKSIZE);

  int num_inode_table_blocks = roundup(maxfiles * sizeof(inode_t), BLKSIZE); // Note: not quite, inode should
                                                                             // not be split between blocks

  size_t fs_size = sizeof(myfs_t) +                           // superblock_t + groupdescriptor_t + block bitmap + inode bitmap
                   num_inode_table_blocks * sizeof(block_t) + // inode_table
                   num_data_blocks * sizeof(block_t);         // data_blocks

  void *ptr;
  // int retval;
  // retval = posix_memalign(&ptr, /*(size_t) sysconf(_SC_PAGESIZE)*/BLKSIZE, fs_size);
  // if (retval) {
  //   printf("posix_memalign failed\n");
  //   return NULL;
  // }
  ptr = malloc(fs_size);
  if (ptr == NULL)
  {
    printf("malloc failed\n");
    return NULL;
  }
  if (mlock(ptr, size))
  {
    printf("mlock failed\n");
    free(ptr);
    return NULL;
  }

  myfs_t *myfs = (myfs_t *)ptr;

  // superblock
  void *super_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  union superblock_t *super = (union superblock_t *)super_ptr;
  super->superblock_info.blocks = num_data_blocks;
  strcpy(super->superblock_info.name, "MYFS");
  // write out to fs
  memcpy((void *)&myfs->super, super_ptr, BLKSIZE);

  // groupdescriptor
  void *groupdescriptor_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  union groupdescriptor_t *groupdescriptor = (union groupdescriptor_t *)groupdescriptor_ptr;
  groupdescriptor->groupdescriptor_info.inode_table = (inode_t *)((char *)ptr +
                                                                  sizeof(myfs_t));
  groupdescriptor->groupdescriptor_info.block_data = (block_t *)((char *)ptr +
                                                                 sizeof(myfs_t) +
                                                                 num_inode_table_blocks * sizeof(block_t));
  // write out to fs
  memcpy((void *)&myfs->groupdescriptor, groupdescriptor_ptr, BLKSIZE);

  // create filesystem root

  // inode
  void *inodetable_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  inode_t *inodetable = (inode_t *)inodetable_ptr;
  inodetable[root_inode_number].size = 2 * sizeof(dirent_t); // will contain 2 direntries ('.' and '..') at initialization
  inodetable[root_inode_number].blocks = 1;                  // will only take up 1 block (for just 2 direntries: '.' and '..') at initialization
  for (int i = 1; i < 15; ++i)                               // initialize all data blocks to NULL (1 data block only needed at initialization)
    inodetable[root_inode_number].data[i] = NULL;
  inodetable[root_inode_number].data[0] = &(groupdescriptor->groupdescriptor_info.block_data[root_datablock_number]);
  // write out to fs
  memcpy((void *)groupdescriptor->groupdescriptor_info.inode_table, inodetable_ptr, BLKSIZE);

  // data (dir)
  void *dir_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  dirent_t *dir = (dirent_t *)dir_ptr;
  // dirent '.'
  dirent_t *root_dirent_self = &dir[0];
  {
    root_dirent_self->name_len = 1;
    root_dirent_self->inode = root_inode_number;
    root_dirent_self->file_type = 2;
    strcpy(root_dirent_self->name, ".");
  }
  // dirent '..'
  dirent_t *root_dirent_parent = &dir[1];
  {
    root_dirent_parent->name_len = 2;
    root_dirent_parent->inode = root_inode_number;
    root_dirent_parent->file_type = 2;
    strcpy(root_dirent_parent->name, "..");
  }
  // write out to fs
  memcpy((void *)(inodetable[root_inode_number].data[0]), dir_ptr, BLKSIZE);

  // data  bitmap
  void *bmap_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  block_t *bmap = (block_t *)bmap_ptr;
  bmap->data[root_datablock_number / 8] |= 0x1 << (root_datablock_number % 8);
  // write out to fs
  memcpy((void *)&myfs->bmap, bmap_ptr, BLKSIZE);

  // inode  bitmap
  void *imap_ptr = calloc(BLKSIZE, sizeof(char));
  // read-in (not required, we are creating filesystem for first time, also zeroed because using calloc)
  block_t *imap = (block_t *)imap_ptr;
  imap->data[root_inode_number / 8] |= 0x1 << (root_inode_number % 8);
  imap->data[badsectors_inode_number / 8] |= 0x1 << (badsectors_inode_number % 8);
  imap->data[invalid_inode_number / 8] |= 0x1 << (invalid_inode_number % 8);
  // write out to fs
  memcpy((void *)&myfs->imap, imap_ptr, BLKSIZE);

  return myfs;
}

void my_dumpfs(myfs_t *myfs)
{
  printf("superblock_info.name: %s\n", myfs->super.superblock_info.name);
  printf("superblock_info.blocks: %d\n", myfs->super.superblock_info.blocks);
  printf("groupdescriptor_info.inode_table: %p\n", myfs->groupdescriptor.groupdescriptor_info.inode_table);
  printf("groupdescriptor_info.block_data: %p\n", myfs->groupdescriptor.groupdescriptor_info.block_data);
  for (size_t byte = 0; byte < BLKSIZE; ++byte)
  {
    for (int bit = 0; bit < 8; ++bit)
    {
      if (myfs->imap.data[byte] & (0x1 << bit))
      {
        int inode_number = byte * 8 + bit;
        if (inode_number < root_inode_number)
        { // 0, 1 inode numbers are reserved for "invalid" and badsectors data, skip
          continue;
        }
        if (inode_number == root_inode_number)
        { // root inode is known to be a directory, safely assume data holds direntries
          printf("  ROOT inode %d occupied - initialized filesystem found!\n", inode_number);
          dump_dirinode(myfs, inode_number, 0);
        }
      }
    }
  }
}

#define LEVEL_TAB                     \
  for (int o = 0; o < 4 * level; ++o) \
  {                                   \
    printf(" ");                      \
  }
#define NEXTLEVEL_TAB                       \
  for (int o = 0; o < 4 * (level + 1); ++o) \
  {                                         \
    printf(" ");                            \
  }

void dump_dirinode(myfs_t *myfs, int inode_number, int level)
{
  inode_t *inodetable = myfs->groupdescriptor.groupdescriptor_info.inode_table;
  LEVEL_TAB printf("  inode.size: %d\n", inodetable[inode_number].size);
  LEVEL_TAB printf("  inode.blocks: %d\n", inodetable[inode_number].blocks);
  for (int block_nr = 0; block_nr < 11; ++block_nr)
  { // 12 first blocks are direct (only deal with direct blocks for simplicity)
    if (inodetable[inode_number].data[block_nr] != NULL)
    {
      int num_direntries = inodetable[inode_number].size / sizeof(dirent_t); // get current number of direntries
      LEVEL_TAB printf("    num_direntries: %d\n", num_direntries);
      dirent_t *direntries = (dirent_t *)(inodetable[inode_number].data[block_nr]);
      for (size_t dirent_num = 0; dirent_num < num_direntries; ++dirent_num)
      {
        LEVEL_TAB printf("    direntries[%ld].inode: %d\n", dirent_num, direntries[dirent_num].inode);
        LEVEL_TAB printf("    direntries[%ld].name: %s\n", dirent_num, direntries[dirent_num].name);
        LEVEL_TAB printf("    direntries[%ld].file_type: %s\n", dirent_num, (int)direntries[dirent_num].file_type == 2 ? "folder" : (int)direntries[dirent_num].file_type == 1 ? "file"
                                                                                                                                                                               : "unknown");
        if ((int)direntries[dirent_num].file_type == 2)
        { // folder type
          if (strcmp(direntries[dirent_num].name, ".") && strcmp(direntries[dirent_num].name, ".."))
          { // don't dump direntries for slef ('.') and parent ('..'), these are cycles
            NEXTLEVEL_TAB printf("  inode %d occupied:\n", direntries[dirent_num].inode);
            dump_dirinode(myfs, direntries[dirent_num].inode, level + 1);
          }
        }
        if ((int)direntries[dirent_num].file_type == 1)
        { // file type
          LEVEL_TAB printf("    FILE:\n");
          LEVEL_TAB printf("    inode.size: %d\n", inodetable[direntries[dirent_num].inode].size);
          LEVEL_TAB printf("    inode.blocks: %d\n", inodetable[direntries[dirent_num].inode].blocks);
          LEVEL_TAB printf("    inode.data[0]: ");
          for (int i = 0; i < inodetable[direntries[dirent_num].inode].size; ++i)
          {
            printf("%c", ((char *)inodetable[direntries[dirent_num].inode].data[0])[i]);
          }
          printf("\n"); // only print out block[0] for simplicity
        }
      }
    }
  }
}

#define LEVEL_TREE                    \
  for (int o = 0; o < 2 * level; ++o) \
  {                                   \
    printf(" ");                      \
  }                                   \
  printf("|");                        \
  for (int o = 0; o < 2 * level; ++o) \
  {                                   \
    printf("_");                      \
  }
void crawl_dirinode(myfs_t *myfs, int inode_number, int level)
{
  inode_t *inodetable = myfs->groupdescriptor.groupdescriptor_info.inode_table;
  for (int block_nr = 0; block_nr < 11; ++block_nr)
  { // 12 first blocks are direct (only deal with direct blocks for simplicity)
    if (inodetable[inode_number].data[block_nr] != NULL)
    {
      int num_direntries = inodetable[inode_number].size / sizeof(dirent_t); // get current number of direntries
      dirent_t *direntries = (dirent_t *)(inodetable[inode_number].data[block_nr]);
      for (size_t dirent_num = 0; dirent_num < num_direntries; ++dirent_num)
      {
        LEVEL_TREE printf("_ %s\n", direntries[dirent_num].name);
        if ((int)direntries[dirent_num].file_type == 2)
        { // folder type
          if (strcmp(direntries[dirent_num].name, ".") && strcmp(direntries[dirent_num].name, ".."))
          { // don't dump direntries for slef ('.') and parent ('..'), these are cycles
            crawl_dirinode(myfs, direntries[dirent_num].inode, level + 1);
          }
        }
      }
    }
  }
}

void my_crawlfs(myfs_t *myfs)
{
  for (size_t byte = 0; byte < BLKSIZE; ++byte)
  {
    for (int bit = 0; bit < 8; ++bit)
    {
      if (myfs->imap.data[byte] & (0x1 << bit))
      {
        int inode_number = byte * 8 + bit;
        if (inode_number < root_inode_number)
        { // 0, 1 inode numbers are reserved for "invalid" and badsectors data, skip
          continue;
        }
        if (inode_number == root_inode_number)
        { // root inode is known to be a directory, safely assume data holds direntries
          printf("/\n");
          crawl_dirinode(myfs, inode_number, 0);
        }
      }
    }
  }
}





void my_creatdir(myfs_t *myfs, int cur_dir_inode_number, const char *new_dirname) {
  // can't create all variables at start because some need other values
  // step 1
  // bamp and imap are defined in a struct
  // stepOne(myfs_t *myfs);
  block_t* imap = (block_t*)malloc(sizeof(block_t));
  memcpy(imap, &(myfs -> imap), sizeof(block_t));

  int freeInode = -1;

  // i is byte num, j is bit num
  for (int i = 0; i < BLKSIZE; i++) {
    for (int j = 0; j < 8; j++) {
      if (!(imap -> data[i] & (0x1 << j))) {
        // use inode and marked
        freeInode = i * 8 + j;
        imap -> data[i] |= (0x1 << j);
        break;
      }
    }
    // double break to exit for loop - mini function
    if (freeInode != -1) {
      break;
    }
  }

  // debug inode
  if (checkDebug(freeInode) == -1) {
    printf("error in my_creatdir");
    free(imap);
    return;
  }

  memcpy(&(myfs -> imap), imap, sizeof(block_t));
  free(imap);

  //stepTwo(myfs_t *myfs);
  // step 2 - like step 1 but with blocks
  block_t *bmap = (block_t*)malloc(sizeof(block_t));
  memcpy(bmap, &(myfs -> bmap), sizeof(block_t));

  int freeBlock = -1;
  for (int i = 0; i < BLKSIZE; i++) {
    for (int j = 0; j < 8; j++) {
      // bit math uses <<
      if (!(bmap -> data[i] & (0x1 << j))) {
        // use block and marked
        freeBlock = i * 8 + j;
        bmap -> data[i] |= (0x1 << j);
        break;
      }
    }
    if (freeBlock != -1) {
      break;
    }
  }

  // debug block
  if (checkDebug(freeBlock) == -1) {
    printf("error in my_creatdir");
    free(bmap);
    return;
  }

  memcpy(&(myfs -> bmap), bmap, sizeof(block_t));
  free(bmap);

  // step 3
  inode_t* inode_table = myfs -> groupdescriptor.groupdescriptor_info.inode_table;
  inode_t* parent_inode = &inode_table[cur_dir_inode_number];
  inode_t* new_dir_inode = &inode_table[freeInode];

  parent_inode -> size += sizeof(dirent_t);

  new_dir_inode -> size = 2 * sizeof(dirent_t); // for '.' and '..'
  new_dir_inode -> blocks = 1; // block 1 makes it easy
  new_dir_inode -> data[0] = &(myfs -> groupdescriptor.groupdescriptor_info.block_data[freeBlock]);

  memcpy(myfs -> groupdescriptor.groupdescriptor_info.inode_table, inode_table, BLKSIZE);

  // step 4
  block_t* parent_dir_block = parent_inode -> data[0];
  dirent_t* dir_entries = (dirent_t *)parent_dir_block;

  int num_entries = parent_inode -> size / sizeof(dirent_t);
  dirent_t* new_dir_entry = &dir_entries[num_entries];
  new_dir_entry -> inode = freeInode;
  new_dir_entry -> file_type = 2;
  new_dir_entry -> name_len = strlen(new_dirname);
  strcpy(new_dir_entry -> name, new_dirname);

  memcpy(parent_inode -> data[0], dir_entries, BLKSIZE);

  // step 5 - set up the directory structure
  dirent_t* new_dir_block = (dirent_t*)malloc(BLKSIZE);
  new_dir_block[0].inode = freeInode;
  new_dir_block[0].file_type = 2;
  new_dir_block[0].name_len = 1;
  strcpy(new_dir_block[0].name, ".");
  new_dir_block[1].inode = cur_dir_inode_number;
  new_dir_block[1].file_type = 2;
  new_dir_block[1].name_len = 2;
  strcpy(new_dir_block[1].name, "..");

  memcpy(new_dir_inode -> data[0], new_dir_block, BLKSIZE);
  free(new_dir_block);
}


int checkDebug(int debug) {
  if (debug == -1) {
    printf("Error: no free block available\n");
    return -1;
  }
  else {
    return 1;
  }
}

int stepOne(myfs_t *myfs) {
  block_t* imap = (block_t*)malloc(sizeof(block_t));
  memcpy(imap, &(myfs -> imap), sizeof(block_t));

  int freeInode = -1;

  // i is byte num, j is bit num
  for (int i = 0; i < BLKSIZE; i++) {
    for (int j = 0; j < 8; j++) {
      if (!(imap -> data[i] & (0x1 << j))) {
        // use inode and marked
        freeInode = i * 8 + j;
        imap -> data[i] |= (0x1 << j);
        break;
      }
    }
    // double break to exit for loop - mini function
    if (freeInode != -1) {
      break;
    }
  }

  // debug inode
  if (checkDebug(freeInode) == -1) {
    printf("error in my_creatdir");
    free(imap);
    return 1;
  }

  memcpy(&(myfs -> imap), imap, sizeof(block_t));
  free(imap);
}

int stepTwo(myfs_t *myfs) {
  // step 2 - like step 1 but with blocks
  block_t *bmap = (block_t*)malloc(sizeof(block_t));
  memcpy(bmap, &(myfs -> bmap), sizeof(block_t));

  int freeBlock = -1;
  for (int i = 0; i < BLKSIZE; i++) {
    for (int j = 0; j < 8; j++) {
      // bit math uses <<
      if (!(bmap -> data[i] & (0x1 << j))) {
        // use block and marked
        freeBlock = i * 8 + j;
        bmap -> data[i] |= (0x1 << j);
        break;
      }
    }
    if (freeBlock != -1) {
      break;
    }
  }

  // debug block
  if (checkDebug(freeBlock) == -1) {
    printf("error in my_creatdir");
    free(bmap);
    return 1;
  }

  memcpy(&(myfs -> bmap), bmap, sizeof(block_t));
  free(bmap);
}