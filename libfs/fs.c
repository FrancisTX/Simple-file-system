#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define INVALID_INDEX 0xFFFF
#define INVALID -1



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
    __uint8_t file_name[FS_FILENAME_LEN];
    __uint32_t size_of_file;
    __uint16_t index_first_data_block;
    __uint8_t unused[10];
} root_entry_class;

typedef struct root_dir_class {
    root_entry_class dic[FS_FILE_MAX_COUNT];
} root_dir_class;

typedef struct open_file_class {
    int root_entry_index;
    int offset;
} open_file_class;

typedef struct user_define_open_table {
    int count;
    open_file_class open_files[FS_OPEN_MAX_COUNT];
} user_define_open_file_table;

user_define_open_file_table *open_table;
super_block_class *super_block = NULL;
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
    for (int i = 1; i < super_block->data_block_count; i++) {
        if (FAT_ptr[i] == 0){
            FAT_ptr[i] = FAT_EOC;
            return i;
        }
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
    if (memcmp((char *) super_block->signature, "ECS150FS",8) || super_block->block_count != block_disk_count()) {
        free(super_block);
        return -1;
    }

    /*read FAT*/
    FAT_ptr = malloc(BLOCK_SIZE * super_block->FAT_block_count);
    
    for (int i = 0; i < super_block->FAT_block_count; ++i) {
        if(block_read(i+1, (char*)FAT_ptr + i*BLOCK_SIZE) == -1){
            return -1;
        }
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

    if (super_block == NULL){
        return -1;
    }

    if(open_table->count > 0){
        return -1;
    }
    
    for (int i = 0; i < super_block->FAT_block_count; ++i) {
        block_write(1+i,(char*)FAT_ptr+i*BLOCK_SIZE);
    }
    block_write(super_block->root_block_index,root_block);

    free(super_block);
    free(FAT_ptr);
    free(root_block);

    /*close the disk*/
    if (block_disk_close() == -1)
        return -1;
    
    return 0;
}

int fs_info(void) {
    if (super_block == NULL) {
        return -1;
    }

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
    
    /*if the directory contains more than 128 files*/
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if (!strcmp(root_block->dic[i].file_name, "")){
            break;
        } else if (i == 127)
            return -1;
    }

    /*find the first empty entry */
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        if (!strcmp(root_block->dic[i].file_name, "")) {
            strcpy((char *) root_block->dic[i].file_name, filename);
            root_block->dic[i].size_of_file = 0;
            root_block->dic[i].index_first_data_block = FAT_EOC;
            return 0;
        }
        
    }

    return -1;
}

int fs_delete(const char *filename) {

    if (filename == NULL) {
        return -1;
    }

    /*check if open*/
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if (!strcmp(root_block->dic[open_table->open_files[i].root_entry_index].file_name,filename)){
            return -1;
        }
    }

    /*check if the file exist in the directory*/
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (!strcmp((char *) root_block->dic[i].file_name, filename)) {
            strcpy(root_block->dic[i].file_name, "");
            root_block->dic[i].size_of_file = 0;

            free_fat_block(i);

            root_block->dic[i].index_first_data_block = FAT_EOC;
            break;

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
            printf("file: ");
            printf("%s, ", root_block->dic[i].file_name);
            printf("size: ");
            printf("%d, ", root_block->dic[i].size_of_file);
            printf("data_blk: ");
            printf("%d \n", root_block->dic[i].index_first_data_block);
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
        if (open_table->open_files[i].root_entry_index == INVALID_INDEX) {
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
    if (fd > FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    //if a file is never be opened it should not be closed
    if (open_table->open_files[fd].offset == -1) {
        // not initialized
        return -1;
    }

    open_table->open_files[fd].offset = -1;
    open_table->open_files[fd].root_entry_index = INVALID_INDEX;
    open_table->count --;

    return 0;

}

int fs_stat(int fd) {
    if (fd > FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    int root_entry_index = open_table->open_files[fd].root_entry_index;
   
    if (root_entry_index < 0 || root_entry_index > FS_FILE_MAX_COUNT)
        return -1;

    if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
        return -1;

    int size = root_block->dic[root_entry_index].size_of_file;

    return size;
}

int fs_lseek(int fd, size_t offset) {
    //fd is invalid
    if (fd > FS_OPEN_MAX_COUNT || fd < 0) {
        return -1;
    }

    //not initialized
    if (open_table->open_files[fd].offset == -1) {
        return -1;
    }

    // offset is too big
    if (offset > root_block->dic[open_table->open_files[fd].root_entry_index].size_of_file) {
        //too big
        return -1;
    }

    open_table->open_files[fd].offset = offset;

    return 0;
}

int fs_write(int fd, void *buf, size_t count) {
    /*the entry index of fd in root directory*/
    int root_entry_index = open_table->open_files[fd].root_entry_index;

    /*check if the root entry index is valid*/
    if (root_entry_index == INVALID_INDEX)
        return -1;

    /*check if the fd is valid*/
    if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
        return -1;

    if (!count){
        return 0;
    }

    int num_block;                                  //the block we need to create to write
    int spanning_data_block;                        //the num of block before the offset
    __uint16_t temp_index;                          //the last index of the block we write
    int start_block_offset;                         //the offset at that block
    char *temp_data_block = malloc(BLOCK_SIZE);     //the bounce to store the data we read

    spanning_data_block = open_table->open_files[fd].offset / BLOCK_SIZE;
    start_block_offset = open_table->open_files[fd].offset % BLOCK_SIZE;

    int index_first_fat_block;
    int index_writing_block;

    /*check if the file is empty*/
    if (root_block->dic[root_entry_index].index_first_data_block != FAT_EOC){
        /*if it is not empty, get its first data block*/
        index_first_fat_block = root_block->dic[root_entry_index].index_first_data_block;
        index_writing_block = index_first_fat_block;

        /*go through to the the data block we start writing*/
        for (int i = 0; i < spanning_data_block; ++i) {
            index_writing_block = FAT_ptr[index_writing_block];
        }

    }else{
        /*if it is empty, find a avaliable data block for it*/
        index_first_fat_block = find_empty_data_block();

        /*set the first data block index*/
        root_block->dic[root_entry_index].index_first_data_block = index_first_fat_block;

        /*the writing block is just this first index*/
        index_writing_block = index_first_fat_block;
        printf("DATA BLOCK INDEX: %d\n", index_writing_block);
    }

    /*the size exceeds our data block */
    int new_block_size = count + open_table->open_files[fd].offset - root_block->dic[root_entry_index].size_of_file;

    /**
     *small operation
     *write the first block
     */
    if (count < BLOCK_SIZE - start_block_offset){
        block_read(super_block->data_block_index + index_writing_block, temp_data_block);
        memcpy(temp_data_block + start_block_offset, buf, count);
        block_write(super_block->data_block_index + index_writing_block, temp_data_block);
        //root_block->dic[root_entry_index].size_of_file += count;
        open_table->open_files[fd].offset += count;
        if (open_table->open_files[fd].offset >  root_block->dic[root_entry_index].size_of_file){
            root_block->dic[root_entry_index].size_of_file = open_table->open_files[fd].offset;
        }
        return count;
    }

    if (count == BLOCK_SIZE - start_block_offset){
        return 0;
    }

    /**
     * large operation
     * if the the exceed size are positive, create more blocks
     */
    
    if (new_block_size > 0) {
        temp_index = index_writing_block;

        /*get the block we need to create*/

        num_block = new_block_size / BLOCK_SIZE;


       
        /*go to the EOC*/
        while (FAT_ptr[temp_index] != FAT_EOC) {
            temp_index = FAT_ptr[temp_index];
        }
       
        int count_empty_block = 0;
        /*allocate more block at the end*/
        for (;  count_empty_block < num_block; count_empty_block++) {
            FAT_ptr[temp_index] = find_empty_data_block();
            temp_index = FAT_ptr[temp_index];

            if (temp_index == FAT_EOC){
                break;
            }
        }
        
        /**
         * write the beginning block
         */
      
        block_read(super_block->data_block_index + index_writing_block, temp_data_block);
        memcpy(temp_data_block + start_block_offset, buf, BLOCK_SIZE - start_block_offset);

        open_table->open_files[fd].offset += BLOCK_SIZE - start_block_offset;

        block_write(super_block->data_block_index + index_writing_block, temp_data_block);
        count -= (BLOCK_SIZE - start_block_offset);
        if(count_empty_block-- == 0){
            if (open_table->open_files[fd].offset >  root_block->dic[root_entry_index].size_of_file){
                    root_block->dic[root_entry_index].size_of_file = open_table->open_files[fd].offset;
                }
            return BLOCK_SIZE - start_block_offset;
        }

        index_writing_block = FAT_ptr[index_writing_block];

        /*write to the rest whole block*/
        int count_index = 1;
        while (count > BLOCK_SIZE) {
            block_read(super_block->data_block_index + index_writing_block, temp_data_block);
            memcpy(temp_data_block, buf + (BLOCK_SIZE - start_block_offset) + BLOCK_SIZE * (count_index - 1),
                   BLOCK_SIZE);

            open_table->open_files[fd].offset += BLOCK_SIZE;
            block_write(super_block->data_block_index + index_writing_block, temp_data_block);
            count -= BLOCK_SIZE;
            count_index++;
            if(count_empty_block-- == 0){
                if (open_table->open_files[fd].offset >  root_block->dic[root_entry_index].size_of_file){
                    root_block->dic[root_entry_index].size_of_file = open_table->open_files[fd].offset;
                }
                return BLOCK_SIZE - start_block_offset + BLOCK_SIZE * (count_index - 1);
            }
            index_writing_block = FAT_ptr[index_writing_block];
        }
	

        /*write the last block*/
        block_read(super_block->data_block_index + index_writing_block, temp_data_block);
        memcpy(temp_data_block, buf + (BLOCK_SIZE - start_block_offset) + BLOCK_SIZE * (count_index - 1), count);
         open_table->open_files[fd].offset += count;
        block_write(super_block->data_block_index + index_writing_block, temp_data_block);
        
        /*change the size in root directory*/
        //root_block->dic[root_entry_index].size_of_file += new_block_size;
         if (open_table->open_files[fd].offset >  root_block->dic[root_entry_index].size_of_file){
             root_block->dic[root_entry_index].size_of_file = open_table->open_files[fd].offset;
        }
        return new_block_size;
    }    
}


int fs_read(int fd, void *buf, size_t count) {
    int root_entry_index = open_table->open_files[fd].root_entry_index;

    if (root_entry_index == INVALID_INDEX)
        return -1;

    if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
        return -1;

    

    __int8_t *buffer_ptr = buf;
    void *temp_data_block = malloc(BLOCK_SIZE);

    int start_block_index = -1;
    int start_block_offset = -1;
    int end_block_index = -1;
    int end_block_offset = -1;
    int real_read_size = 0;

    // get the file entry

    // get virtual index
    start_block_index = open_table->open_files[fd].offset / BLOCK_SIZE;
    start_block_offset = open_table->open_files[fd].offset % BLOCK_SIZE;
    end_block_index = (open_table->open_files[fd].offset + count) / BLOCK_SIZE;
    end_block_offset = (open_table->open_files[fd].offset + count) % BLOCK_SIZE;

    //read the first data block
    __uint16_t real_index = root_block->dic[open_table->open_files[fd].root_entry_index].index_first_data_block;
    // add later
    for(int i = 0; i < start_block_index ; ++i){
        real_index  = FAT_ptr[real_index];
    }

    block_read(super_block->data_block_index + real_index, temp_data_block);

    /*read one block and we done*/
    if (start_block_index == end_block_index) {
        memcpy(buf, temp_data_block + start_block_offset, count);
        real_read_size += count;
        open_table->open_files[fd].offset += count;
    } else {
        // read the first block
        memcpy(buffer_ptr, (char*)temp_data_block + start_block_offset, BLOCK_SIZE - start_block_offset);
        real_read_size += BLOCK_SIZE - start_block_offset ;
        open_table->open_files[fd].offset += BLOCK_SIZE - start_block_offset;
        //update buffer_ptr
        buffer_ptr = buffer_ptr + (BLOCK_SIZE - start_block_offset);
        for (int i = start_block_index + 1; i < end_block_index; ++i) {
            // get next data block
            real_index = FAT_ptr[real_index];
            if(real_index == FAT_EOC){
                return real_read_size;
            }
            block_read(super_block->data_block_index + real_index, temp_data_block);
            memcpy(buffer_ptr, temp_data_block, BLOCK_SIZE);
            open_table->open_files[fd].offset += BLOCK_SIZE;
            real_read_size +=  BLOCK_SIZE;
            // update ptr
            buffer_ptr = buffer_ptr + BLOCK_SIZE;
        }
        // read the tail data
        
        real_index = FAT_ptr[real_index];
        if(real_index == FAT_EOC){
            return real_read_size;
        }
        block_read(super_block->data_block_index + real_index, temp_data_block);
        memcpy(buffer_ptr, temp_data_block, end_block_offset);
        real_read_size +=  end_block_offset;
        open_table->open_files[fd].offset += end_block_offset;
    }

    

    free(temp_data_block);

    return real_read_size;
}
