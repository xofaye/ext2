#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;

int main(int argc, char **argv) {

    // Verify cmd line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: ext2_rm <virtual disk name> <absolute path to file or link>\n");
        exit(1);
    }

    // Open file disk
    int fd = open(argv[1], O_RDWR);

    // Verify target path starts from root
    int target_len = strlen(argv[2]);
    char path [target_len + 2];
    set_target_path(path, argv[2]);

    // Read disk
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    if(locate(path) != -1){
        fprintf(stderr, "File already exists\n");
        exit(EEXIST);
    }
    set_target_path(path, argv[2]);

    // Get inode bitmap and block bitmap 
    struct ext2_group_desc* bg = (void*) disk + 1024 + EXT2_BLOCK_SIZE;
      
    void* inodes = disk + EXT2_BLOCK_SIZE * bg->bg_inode_table;

    // Get name of file 
    char name[256]; 
    int i = get_name_from_path(path, name);

    // Parent path for new directory 
    char *parent_path = malloc(sizeof(char) * strlen(path));
    strncpy(parent_path, path, i);
    parent_path[i] = '\0';
    if(i == 0){
        parent_path[0] = '/';
        parent_path[1] = '\0';
    }

    int p_inode_idx = locate(parent_path); 
    if(p_inode_idx == -1){
        fprintf(stderr, "Could not find parent path\n");
        exit(1);
    }

    struct ext2_inode *parent_inode = (struct ext2_inode *) (inodes + sizeof(struct ext2_inode) * (p_inode_idx-1));

    int name_len = strlen(name);

    int l;
    struct ext2_dir_entry *prev;

    for (l = 0; l < parent_inode->i_blocks / 2; l++) {
        struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[l]) * EXT2_BLOCK_SIZE);

        int read_bytes = 0; 
        while(read_bytes < EXT2_BLOCK_SIZE){
            char * curr_name = malloc(dir_entry->name_len + 2);
            strncpy(curr_name, dir_entry->name, dir_entry->name_len);
            if(dir_entry->inode < 32){
                struct ext2_inode* inode = inodes + sizeof(struct ext2_inode) * (dir_entry->inode - 1);

                if ((name_len == dir_entry->name_len) && strncmp(dir_entry->name, name, dir_entry->name_len) == 0){
                    prev->rec_len -= dir_entry->rec_len;                
                    inode->i_links_count++;
                    inode->i_dtime = 0; 
                    set_inode_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap, dir_entry->inode - 1, 1);

                    int num_blocks = inode->i_blocks / 2;
                    if (num_blocks > 12) {
                        for (i = 0; i < 12; i++) {
                            set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, inode->i_block[i] - 1, 1);
                        }
                        int master_block_idx = inode->i_block[12] - 1;
                        set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, master_block_idx, 1);
                        int* blocks = (void*)disk + EXT2_BLOCK_SIZE * (master_block_idx + 1);
                        for (i = 0; i < num_blocks - 13; i++) {
                            set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, *blocks - 1, 1);
                            blocks++;
                        }
                    } else {
                        for (i = 0; i < num_blocks; i++) {
                            set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, inode->i_block[i] - 1, 1);
                         }
                    }      
                    
                    bg->bg_free_blocks_count = total_free_blocks(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap);
                    bg->bg_free_inodes_count = total_free_inodes(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap);

                    return 0;
                }
                //Keep pointer to previous valid directory entry
                if(inode->i_dtime == 0){
                    prev = dir_entry;
                }

                int size = align4(dir_entry->name_len) + 8 ;
                read_bytes += size;  

                dir_entry = (void*) dir_entry + size;
            }else{
                break; 
            }
        }
        fflush(stdout);
    }

    fprintf(stderr, "Could not find file to restore\n");

  return 0;
}
