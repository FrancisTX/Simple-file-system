#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define INVALID_INDEX 0xFFFF

typedef struct super_block_class {
  __int8_t signature[8];
  __int16_t block_count;
  __int16_t root_block_index;
  __int16_t data_block_index;
  __int16_t data_block_count;
  __int8_t FAT_block_count;
  __int8_t unused[4079];
} super_block_class;

typedef struct root_entry_class {
  __uint8_t file_name[16];
  __uint32_t size_of_file;
  __uint16_t index_first_data_block;
  __uint8_t unused[10];
} root_entry_class;

typedef struct root_dir_class {
  root_entry_class dic[128];
} root_dir_class;

typedef struct open_file_class {
  int root_entry_index;
  int offset;
} open_file_class;

typedef struct user_define_open_table {
  int count;
  open_file_class open_files[30];
} user_define_open_file_table;

user_define_open_file_table *open_table;
super_block_class *super_block;
root_dir_class *root_block;
__uint16_t *FAT_ptr;

/*helper function to calculate the fat_free_ratio*/
int FAT_unused_block() {
  int count_unused_block = 0;
  for (int i = 0; i < super_block->data_block_count; ++i) {
    if ((FAT_ptr[i]) == 0) {
      count_unused_block++;
    }
  }
  return count_unused_block;
}

/*helper function to calculate the rdir_free_ratio*/
int rdir_unused_block() {
  int count_unused_block = 0;
  for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
    if (!strcmp((char *) root_block->dic[i].file_name, ""))
      count_unused_block++;
  }
  return count_unused_block;
}

/*find the empty fat block for the file*/
int find_empty_data_block() {
  for (int i = 0; i < super_block->data_block_count; i++) {
    if (FAT_ptr[i] == 0)
      return i;
  }
  return -1;
}

/*free fat block when delete a file*/
void free_fat_block(int index) {

  int index_first_fat_block = root_block->dic[index].index_first_data_block;

  while (FAT_ptr[index_first_fat_block] != FAT_EOC) {
    int temp = FAT_ptr[index_first_fat_block];
    FAT_ptr[index_first_fat_block] = 0;
    index_first_fat_block = temp;
  }
  FAT_ptr[index_first_fat_block] = 0;
}

int fs_mount(const char *diskname) {
  /* open the file */
  if (block_disk_open(diskname))
    return -1;


  /*read super block*/
  super_block = malloc(sizeof(struct super_block_class));
  if (block_read(0, super_block) == -1)
    return -1;

  if (!strcmp((char *) super_block->signature, "ECS150FS") || super_block->block_count != block_disk_count()) {
    free(super_block);
    return -1;
  }

  /*read FAT*/
  FAT_ptr = malloc(BLOCK_SIZE * super_block->FAT_block_count);
  FAT_ptr[0] = FAT_EOC;
  for (int i = 1; i < super_block->FAT_block_count; i++) {
    if (block_read(i + 1, FAT_ptr + i) == -1)
      return -1;
  }

  /*read root directory*/
  root_block = malloc(sizeof(struct root_dir_class));
  if (block_read(super_block->root_block_index, root_block) == -1)
    return -1;

  //initialize open file table
  open_table = malloc(sizeof(user_define_open_file_table));
  open_table->count = 0;
  for (int i = 0; i < 30; ++i) {
    open_table->open_files[i].offset = -1;
    open_table->open_files[i].root_entry_index = INVALID_INDEX;
  }

  return 0;
}

int fs_umount(void) {
  free(super_block);
  free(FAT_ptr);
  free(root_block);

  /*close the disk*/
  if (block_disk_close() == -1)
    return -1;

  return 0;
}

int fs_info(void) {
  printf("FS Info:\n");

  int total_block = 1 + super_block->FAT_block_count + 1 + super_block->data_block_count;
  printf("total_blk_count=%d\n", total_block);

  printf("fat_blk_count=%d\n", super_block->FAT_block_count);

  printf("rdir_blk=%d\n", super_block->root_block_index);

  printf("data_blk=%d\n", super_block->data_block_index);

  printf("data_blk_count=%d\n", super_block->data_block_count);

  printf("fat_free_ratio=%d/%d\n", FAT_unused_block(), super_block->data_block_count);

  printf("rdir_free_ratio=%d/%d\n", rdir_unused_block(), FS_FILE_MAX_COUNT);

  return 0;
}

int fs_create(const char *filename) {
  if (!strcmp(filename, "")) {
    return -1;
  }

  if (strlen(filename) > FS_FILENAME_LEN) {
    return -1;
  }

  /*if the file have already existed*/
  for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if (!strcmp((char *) root_block->dic[i].file_name, filename)) {
      return -1;
    }
  }

  /*if the directory contains more than 128 files
  for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
      if (i == 127)
          return -1;
      else{
          if (!strcmp((char*)root_block->dic[i].file_name, "")){
              break;
          } else
              continue;
      }
  }*/

  /*find the first empty entry */
  for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
    if (strcmp((char *) root_block->dic[i].file_name, "") == 0) {
      strcpy((char *) root_block->dic[i].file_name, filename);
      root_block->dic[i].size_of_file = 0;
      root_block->dic[i].index_first_data_block = find_empty_data_block();
      FAT_ptr[i] = FAT_EOC;
      break;
    }
  }
  //root_dir_write_back();
  return 0;
}

int fs_delete(const char *filename) {
  if (filename == NULL) {
    return -1;
  }

  /*check if the file exist in the directory*/
  for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if (!strcmp((char *) root_block->dic[i].file_name, filename)) {
      strcpy(root_block->dic[i].file_name, "");
      root_block->dic[i].size_of_file = 0;

      free_fat_block(i);

      root_block->dic[i].index_first_data_block = FAT_EOC;

    }

    if (i == 127)
      return -1;
  }
  return 0;
}

int fs_ls(void) {
  if (super_block == NULL) {
    return -1;
  }

  printf("FS Ls:\n");
  for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if (strcmp((char *) root_block->dic[i].file_name, "")) {
      printf("%s", root_block->dic[i].file_name);
      printf("%d", root_block->dic[i].size_of_file);
      printf("%d", root_block->dic[i].index_first_data_block);
    }
  }

  return 0;
}

int fs_open(const char *filename) {

  /*if the file is invalid*/
  if (!filename)
    return -1;

  /*if the file in the directory*/
  int index_in_root = 0;
  for (; index_in_root < FS_FILE_MAX_COUNT; index_in_root++) {
    if (!strcmp((char *) root_block->dic[index_in_root].file_name, filename))
      break;
    if (index_in_root == 127)
      return -1;
  }

  //to add length check later.

  /*if the open files excess the max*/
  if (open_table->count >= FS_OPEN_MAX_COUNT)
    return -1;

  /*put the file into the open_files_array*/
  int file_dis = -1;
  for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
    if(open_table->open_files[i].root_entry_index != INVALID_INDEX){
      open_table->open_files[i].offset = 0;
      open_table->open_files[i].root_entry_index = index_in_root;
      open_table->count++;
      file_dis = i;
      break;
    }
  }
  return file_dis;
}

int fs_close(int fd) {
  /* TODO: Phase 3 */

  //if a file is never be opened it should not be closed
  if(open_table->open_files[fd].offset == -1){
    // not initialized
    return -1;
  }

  open_table->open_files[fd].offset = -1;
  open_table->open_files[fd].root_entry_index = INVALID_INDEX;

  return 0;

}

int fs_stat(int fd) {
  /* TODO: Phase 3 */
  return 0;
}

int fs_lseek(int fd, size_t offset) {
  /* TODO: Phase 3 */
  //fd is invalid
  if(fd >= FS_OPEN_MAX_COUNT){
    return -1;
  }

  //not initialized
  if(open_table->open_files[fd].offset == -1){
    return -1;
  }

  // offset is too big
  if(offset > root_block->dic[open_table->open_files[fd].root_entry_index].size_of_file){
    //too big
    return -1;
  }

  open_table->open_files[fd].offset = offset;

  return 0;
}

int fs_write(int fd, void *buf, size_t count) {
  /* TODO: Phase 4 */
  return 0;
}

int fs_read(int fd, void *buf, size_t count) {
  /* TODO: Phase 4 */
  return 0;
}
