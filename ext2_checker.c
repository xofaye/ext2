#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "ext2_utils.h"


unsigned char *disk;
int total_fix_num = 0;

int diff(int a, int b){
    if (a > b){
        return a - b;

    }else{
      return b - a;
    }
}

void check_mismatch(struct ext2_inode *inode, struct ext2_dir_entry* dir_entry, int k){
    int read_bytes = 0; 
    while(read_bytes < EXT2_BLOCK_SIZE){
        char type = check_type(dir_entry); 
        char i_type = check_inode_type(inode); 

        
        //If the inode mode and the directory type do not match, trusts the inode's i_mode and fixes the file_type to match.
        if (i_type != type) {
            dir_entry->file_type = inode->i_mode;
            printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", dir_entry->inode);
            total_fix_num++;
        }
                
        //Increase count of read bytes.
        read_bytes += dir_entry->rec_len;
                    
        //Update pointer to possible nex entry.
        dir_entry = (void*)dir_entry + dir_entry->rec_len;
    }
}


//Check the types for all directories, files and symlinks
void check_types(char *inodes, int k, struct ext2_group_desc *bg, int* block_bitmap) {
    //Obtain pointer to inode in the inode table
    int l;
    struct ext2_dir_entry *dir_entry = NULL;
    int blocks_fixed = 0; 
    struct ext2_inode *inode = (struct ext2_inode*)(inodes + sizeof(struct ext2_inode) * k);

    int dir_num_blocks = inode->i_size / EXT2_BLOCK_SIZE;
    if (inode->i_mode & EXT2_S_IFDIR) {
        //Print blocks information
        

        for (l = 0; l < dir_num_blocks; l++) {
            
            if(block_bitmap[inode->i_block[l] - 1] == 0){
                set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, inode->i_block[l] - 1, 1);
                blocks_fixed++; 
            }
            dir_entry = (struct ext2_dir_entry *) (disk + (inode->i_block[l]) * EXT2_BLOCK_SIZE);
            if(dir_entry-> inode < 32 && dir_entry->inode > 0){
                struct ext2_inode *inode_match = (struct ext2_inode*)(inodes + sizeof(struct ext2_inode) * (dir_entry->inode - 1));
                check_mismatch(inode_match, dir_entry, dir_entry->inode - 1); 
            }
            
        }
    }else{
        int i;
        int num_blocks = inode->i_blocks / 2;

        dir_entry = (struct ext2_dir_entry *) (disk + (inode->i_block[0]) * EXT2_BLOCK_SIZE);
        if(dir_entry-> inode < 32 && dir_entry->inode > 0){

            check_mismatch(inode, dir_entry, k);
        }

        if (num_blocks > 12) {
          for (i = 0; i < 12; i++) {
            if(block_bitmap[inode->i_block[i] - 1] == 0){
                set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, inode->i_block[i] - 1, 1);
                blocks_fixed++; 
            }
          }

          int master_block_idx = inode->i_block[12] - 1;
          if(block_bitmap[master_block_idx] == 0){
            set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, master_block_idx, 1);
            blocks_fixed++; 
          }
          int* blocks = (void*)disk + EXT2_BLOCK_SIZE * (master_block_idx + 1);
          for (i = 0; i < num_blocks - 13; i++) {
            if(block_bitmap[*blocks - 1] == 0){
                set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, *blocks - 1, 1);
                blocks_fixed++; 
            }

            blocks++;
          }
        } else {
            for (i = 0; i < num_blocks; i++) {
                if(block_bitmap[inode->i_block[i] - 1] == 0){
                    set_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap, inode->i_block[i] - 1, 1);
                    blocks_fixed++; 
                }
            }
        }
    }
    total_fix_num += blocks_fixed; 
    if(blocks_fixed > 0){
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", blocks_fixed, k + 1); 
    }

}

int main(int argc, char **argv) {
    
    if(argc != 2) {
        fprintf(stderr, "Usage: checker <ext2 virtual image name>\n");
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //Obtains superblock count for free blocks and free inodes
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    unsigned int super_free_blocks_count = sb->s_free_blocks_count;
    unsigned int super_free_inodes_count = sb->s_free_inodes_count;

    //Obtains block group count for free blocks and free inodes
    struct ext2_group_desc *bg = (struct ext2_group_desc *) (disk + 2 * EXT2_BLOCK_SIZE);
    unsigned int group_free_blocks_count = bg->bg_free_blocks_count;
    unsigned int group_free_inodes_count = bg->bg_free_inodes_count;

    int* inode_bitmap = get_inode_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap);
    int* block_bitmap = get_block_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap);

    int map_free_blocks_count = total_free_blocks(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap);
    int map_free_inodes_count = total_free_inodes(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap);

    //If inconsistencies are detected, fixes them using the bitmaps; also updates counters.
    //Free blocks
    if (super_free_blocks_count != map_free_blocks_count) {
        total_fix_num += diff(map_free_blocks_count, super_free_blocks_count);
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", diff(map_free_blocks_count, super_free_blocks_count));
        sb->s_free_blocks_count = map_free_blocks_count;
    }
    
    if (group_free_blocks_count != map_free_blocks_count) {
        total_fix_num += diff(group_free_blocks_count, map_free_blocks_count);
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", diff(group_free_blocks_count, map_free_blocks_count));
        bg->bg_free_blocks_count = map_free_blocks_count;
    }

    //Free inodes
    if (super_free_inodes_count != map_free_inodes_count) {
        total_fix_num += diff(super_free_inodes_count, map_free_inodes_count);
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", diff(super_free_inodes_count, map_free_inodes_count));
        sb->s_free_inodes_count = map_free_inodes_count;
    }
    
    if (group_free_inodes_count != map_free_inodes_count) {
        total_fix_num += diff(group_free_inodes_count, map_free_inodes_count);
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", diff(group_free_inodes_count, map_free_inodes_count));
        bg->bg_free_inodes_count = map_free_inodes_count;
    }
    
    //Check deletion times and in-use.
    int k;
    char* inodes = (char*)(disk + EXT2_BLOCK_SIZE * bg->bg_inode_table);
    for (k = EXT2_ROOT_INO - 1; k < sb->s_inodes_count; k++) {
        if ((k == EXT2_ROOT_INO - 1 || k >= EXT2_GOOD_OLD_FIRST_INO)){
            struct ext2_inode *inode = (struct ext2_inode*)(inodes + sizeof(struct ext2_inode) * k);
            if(inode->i_size > 0){
                if(k < 32){
                    if(inode_bitmap[k] == 0){
                        set_inode_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap, k, 1);
                        printf("Fixed: inode [%d] not marked as in-use\n", k + 1);
                        total_fix_num++; 
                    }
                    if(inode->i_dtime != 0){
                       inode->i_dtime = 0; 
                       printf("Fixed: valid inode marked for deletion: [%d]\n", k + 1); 
                       total_fix_num++;  
                    }  
                    //if(inode->i_mode & EXT2_S_IFDIR){
                    check_types(inodes, k, bg, block_bitmap);
                    //}else{
                    //    check_blocks(inodes, k, bg, block_bitmap);        
                    //}  
                }
            }else{
                set_inode_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap, k, 0);
            }
        }
    }

    struct ext2_inode *inode = (struct ext2_inode*)(inodes + sizeof(struct ext2_inode) * 10);

    if(inode->i_dtime != 0){
        inode->i_dtime = 0; 
        printf("Fixed: valid inode marked for deletion: [%d]\n", 11); 
        total_fix_num++;  
    } 

    if(inode_bitmap[10] == 0){
        set_inode_bitmap(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap, 10, 1);
        printf("Fixed: inode [%d] not marked as in-use\n", 11);
        total_fix_num++;
        check_types(inodes, 10, bg, block_bitmap);
    }

    sb->s_free_blocks_count = total_free_blocks(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap);
    sb->s_free_inodes_count = total_free_inodes(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap);
    
    bg->bg_free_blocks_count = total_free_blocks(disk + EXT2_BLOCK_SIZE * bg->bg_block_bitmap);
    bg->bg_free_inodes_count = total_free_inodes(disk + EXT2_BLOCK_SIZE * bg->bg_inode_bitmap);

    if(total_fix_num == 0){
        printf("No file system inconsistencies detected!\n");
    }else{
        printf("%d file system inconsistencies repaired!\n", total_fix_num);
    }
    
    return 0;
}
