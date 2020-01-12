#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdio.h>
#include "disk_emu.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fuse.h>

#define MAXFILENAME 16;
//since there are 101 files (100 + root directory) and we need an inode for each
//since each inode is 72 bytes (6*4+12*4), wee need 72*101/1024 = 7.1 so we need 8 blocks of 1024 bytes
#define BlockForInodes 8;

//my implemenation assumes all inode index are the same as file id and directory.inode_num
int maxOpen = 0;

typedef struct spb{
	int magic;
	int block_size;
	int fs_size;
	int inode_table_length;
	int root_dir_iNode;
} super_block ;

typedef struct inode{
	bool mode;
	int link_count;
	int uid;
	int gid;
	int size;
	int blocks[12];
	int indirect;
} inode;

inode iNodeTable [101];

int directoryWalker = 0;


typedef struct directory{
    int inode_num;
    char filename[21];
} directory;

// initialize a directory table
directory directories[100];

typedef struct open_file {
	bool isOpen;
	int fd;
	int inodePtr;
	int readPtr;
	int writePtr;
}open_file;

int findFreeFromBitmap ();

open_file open_file_table[128];

//initialize super block on top of all
super_block newSuperBlock;

bool bitmap [3990];

//private personal ceiling function
int ceiling(float num) {
    int inum = (int)num;
    if (num == (float)inum) {
        return inum;
    }
    return inum + 1;
}

void mksfs(int fresh){
	if(fresh){
		//set new disk with block size 1024 and 4096 blocks;
		init_fresh_disk("newDisk", 1024, 4096);
		//set new super block and assign a value of 1 to the magic block 
		newSuperBlock.magic = 1;
		newSuperBlock.block_size = 1024;
		newSuperBlock.fs_size = 1024;
		newSuperBlock.inode_table_length = 101;
		newSuperBlock.root_dir_iNode = 0;
		//write it at address 0 for 1 block
		write_blocks( 0 , 1 , &newSuperBlock);

		//set all inodes to these parameters
		for(int i =0;i<101;i++){
			//set all modes, == false for free, true for occupied
            iNodeTable[i].mode = false;
            iNodeTable[i].link_count = 0;
            iNodeTable[i].uid = 0;
            iNodeTable[i].gid = 0;
            iNodeTable[i].size = 0;
			//set such that there is no indirect pointers at first
            iNodeTable[i].indirect = 0;
        }
		write_blocks(1, 8, &iNodeTable);
		
		//set all inodePtr to each iNode index
		for(int i = 0 ; i < 101 ; i ++){
			open_file_table[i].inodePtr = -1;
			open_file_table[i].fd = -1;
		}

		//flush all to disk
		memset(&directories, 0, sizeof(directories));

		write_blocks (4090, 2, &directories);

		write_blocks (4092, 4, &bitmap);

	}else{
		
		//read all from disk
		init_disk("newDisk", 1024, 4096);
		
		read_blocks(0, 1 , &newSuperBlock);

        read_blocks (1 ,8 , &iNodeTable);

        read_blocks(4090,2,&directories);

        read_blocks (4092, 4, &bitmap);
	}
}

int sfs_getnextfilename(char *fname){

	if(directoryWalker == 101){
		//reset directly walker and return 0
		directoryWalker = 0;
		return 0;
	}else{
		//if found a file with a name, return this name as long as directory walker isn't out of it
		while( strcmp ( directories[directoryWalker].filename , "" )== 0){
			directoryWalker += 1;
			if(directoryWalker == 101){
				directoryWalker = 0;
				return 0;
			}
		}
		strcpy( fname, directories[directoryWalker].filename);
		directoryWalker += 1;
		return 1;
	}
}

int sfs_getfilesize(const char* path){
	for(int i = 0; i < 101; i ++){
		//if found the correct filename, return it's size
		if ( strcmp(directories[i].filename, path)==0){
			int num = directories[i].inode_num;
			return iNodeTable[num].size;
		}
	}
	return -1;
}
int sfs_fopen(char *name){
	//check if the name is too big
	if(strlen(name) > 20){
		printf("Too big name\n");
		return -1;
	}
	//loop through directory to check for the file
	for (int i = 0 ; i < 101; i++){
		int num = directories[i].inode_num;
		//if found it, it means there is this filename in the directories, we gotta open it now
		if ( strcmp (directories[i].filename, name)==0 && open_file_table[num].isOpen == false){
			open_file_table[num].readPtr = 0;
			open_file_table[num].writePtr = iNodeTable[num].size;
			open_file_table[num].isOpen = true;
			return num;
		}
		//if already open, simply return the file inode number already there
		if ( strcmp (directories[i].filename, name)==0 && open_file_table[num].isOpen == true){
			return num;
		}
	}
	//if file doesn't exist, create it
	//check for first available spot in directories
	//return the entry
	for (int i = 0 ; i < 101 ; i++){
		//available slot
		if (strcmp(directories[i].filename,"")==0){
			//set filename and inodeNum
			directories[i].inode_num = i;
			int num = directories[i].inode_num;
			//set open to true
			open_file_table[num].isOpen = true;
			//copy the name
			memcpy(directories[i].filename, name,21);
			open_file_table[num].fd = i;
			iNodeTable[num].size = 0;
			open_file_table[num].writePtr = iNodeTable[num].size;
			write_blocks (4090, 2, &directories);
			write_blocks( 1 , 8 , &iNodeTable);
			//set up inode pointer
			for(int j = 1 ; j < 101; j ++){
				if(iNodeTable[j].mode == false){
					iNodeTable[j].mode = true;
					open_file_table[num].inodePtr = j;
					break;
				}
			}
			return open_file_table[num].fd;

		}
	}
	return -1;
}

int sfs_frseek(int fileID, int loc){
	for(int i = 0 ; i < 128 ; i++){
		if(open_file_table[i].fd == fileID){
			//if found file, set read ptr at location
			open_file_table[i].readPtr = loc;
			return 0;
		}
	}
	return -1;
}

int sfs_fwseek(int fileID, int loc){
	for(int i = 0 ; i < 128 ; i++){
		if(open_file_table[i].fd == fileID){
			//if found file, set write ptr at location
			open_file_table[i].writePtr = loc;
			return 0;
		}
	}
	return -1;
}

int sfs_fwrite(int fileID, char *buf, int length){
	//beginning is same as fread excep few more variables to keep track of
	//loop through all the open
	for(int i = 0 ; i < 100 ; i++){
		if(open_file_table[i].fd == fileID){
			//if it's not open, can't write to it
			if(open_file_table[i].isOpen == false){
				printf("\ncan't write to a closed file\n");
				return -1;
			}
		}
	}

	//check if the length to read actually fits in the filee
	//total number of blocks is 268*1024
	if((268*1024) < length + open_file_table[fileID].writePtr){
		printf("Can't write that many bytes");
		return -1;
	}

	//since we just want to make sure that we can handle the case with all blocks filled up
	//allBlocks mimics a full iNodeTable
	int allBlocks [268] = {0};
	int directBlocks = 0;
	int numOfIndirectPointers = 0;
	int bytesDoneWriting = 0;
	int totalNumOfBlocks = 0;
	bool containsIndirect = false;
	int numberOfNewBlocks = 0;

	//check if we need to check the indirect pointers in the current file
	if(iNodeTable[fileID].indirect != 0){
	//here we hit the indirect pointers
	//this means at least all the direct pointers are full
		containsIndirect = true;
	 	for(int i = 0;i<12;i++){
			//keep copying the addressses of the blocks we want
			allBlocks[i] = iNodeTable[fileID].blocks[i];
    	}
		directBlocks += 12;
		totalNumOfBlocks += 12;
		//malloc a whole block for the indirect pointer, since there are 256, and each is 4 bytes
		int* indirectBuffer = (int*)malloc(1024);
        read_blocks(iNodeTable[fileID].indirect,1,indirectBuffer);
		//get the number of indirect pointers by checking size - all direct pointers / block size
        numOfIndirectPointers = ceiling((((float)iNodeTable[fileID].size - (float)(12*1024))/1024));
		//copy these indirect pointers into all blocks, with correct offset to not overwirte the direct blocks
        memcpy((allBlocks+ 12), indirectBuffer, sizeof(int) * numOfIndirectPointers);
        free(indirectBuffer);
		totalNumOfBlocks += numOfIndirectPointers;
    }else{
		for(int i = 0; i < 12 ; i++){
			//if we have reached an empty block, then we have already all the blocks we need, all the ones that aren't empty
            if(iNodeTable[fileID].blocks[i] == 0){
                break;
            }else{
				//keep copying the addressses of the blocks we want
                allBlocks[i] = iNodeTable[fileID].blocks[i];
            }
			directBlocks ++;
			totalNumOfBlocks++;
        }
	}
	//see if it is necessary to add new blocks
	if(open_file_table[fileID].writePtr + length < totalNumOfBlocks * 1024){
		//if smaller, then no need to add a new block
		numberOfNewBlocks = 0;
	}else{
		numberOfNewBlocks = ceiling(((float)open_file_table[fileID].writePtr + (float)length - (float)(totalNumOfBlocks * 1024))/1024);

	}
	//start to fetch the bitmap entries

	int count = 0;
	int tempArr[numberOfNewBlocks];
	//make it point to free block by looking at bitmap for all these new blocks
	for(count = 0 ; count < numberOfNewBlocks; count++){

		int index = findFreeFromBitmap();
		allBlocks[totalNumOfBlocks+count] = index;
		if(allBlocks[totalNumOfBlocks+count]==-1){
			return -1;
		}
		tempArr[count] = index; 
	}
	if(containsIndirect == false){
		//then we only have direct blocks at first
		if(numberOfNewBlocks < 13 - directBlocks){
			//don't need to add indirect pointers to it
			for (int i = 0 ; i < numberOfNewBlocks ; i++){	
				iNodeTable[fileID].blocks[directBlocks + i] = tempArr[i];
			}
		}else{
			//need to add indirect pointers because we want too many new blocks
			//indirect ptr set to a new block from looking at bitmap
			iNodeTable[fileID].indirect = findFreeFromBitmap();
			int buffer[(numberOfNewBlocks - 12 + directBlocks)];
			for (int i = 0 ; i < numberOfNewBlocks ; i++){
				int differenceToFullDirect = 12 - directBlocks;
				//this means that there are still direct blocks to write
				if(i < ( differenceToFullDirect )){
					iNodeTable[fileID].blocks[directBlocks + i] = tempArr[i];
				}else{
					//this stores all the indirect blocks
					buffer[(i - differenceToFullDirect )]=tempArr[i];
				}		
			}   
            write_blocks(iNodeTable[fileID].indirect,1,buffer);
		}
	}else if (containsIndirect == true){
		int* readbuffer = (int*)malloc(1024);
		read_blocks(iNodeTable[fileID].indirect,1,readbuffer);
		memcpy(readbuffer+numOfIndirectPointers,tempArr,sizeof(tempArr));
		write_blocks(iNodeTable[fileID].indirect,1,readbuffer);
		free(readbuffer);
	}

	int counter = open_file_table[fileID].writePtr/1024;
	int bytesInFirst = 0;
	int bytesInLast = 0;
	int offSet = open_file_table[fileID].writePtr % 1024;
	if(1024-offSet < length){
		//want to check if the number of bytes in the first possible block to write is smaller or not to one block
		bytesInFirst = 1024-offSet;
	}else{
		bytesInFirst = length;
	}

	//write to first block with offset
	char tempReadBuf [1024];
	read_blocks(allBlocks[counter],1,tempReadBuf);
	memcpy(tempReadBuf + offSet, buf, bytesInFirst);
	write_blocks(allBlocks[counter],1, tempReadBuf);

	bytesDoneWriting += bytesInFirst;
	buf += bytesInFirst;

	//read middle blocks
	counter ++;
	while(1024 < (length - bytesDoneWriting))
	{
		write_blocks(allBlocks[counter],1, buf);
		buf += 1024;
		bytesDoneWriting +=1024;
		counter += 1;
	}

	//read last block
	bytesInLast = length - bytesDoneWriting;
	if( bytesInLast > 0)
	{
		char* tempWriteBuf = (char*) malloc(1024);
		read_blocks(allBlocks[counter],1,tempWriteBuf);
		memcpy(tempWriteBuf, buf, bytesInLast);
		write_blocks(allBlocks[counter],1, tempWriteBuf);
		free(tempWriteBuf);
		bytesDoneWriting += bytesInLast;
	}
	//check if required to update size parameter
	if(iNodeTable[fileID].size < open_file_table[fileID].writePtr+ bytesDoneWriting ){
		iNodeTable[fileID].size = open_file_table[fileID].writePtr+ bytesDoneWriting;
	}else{
		iNodeTable[fileID].size =iNodeTable[fileID].size;
	}
	write_blocks( 1 , 8 , &iNodeTable);
	//update write Ptr to the end
	open_file_table[fileID].writePtr += bytesDoneWriting;

	return bytesDoneWriting;
}


int sfs_fread(int fileID, char *buf, int length){
	//check if the file is even open
	for(int i = 0 ; i < 100 ; i++){
		if(open_file_table[i].fd == fileID){
			if(open_file_table[i].isOpen == false){
				printf("\ncan't read to a closed file\n");
				return -1;
			}
		}
	}
	int numOfIndirectPointers = 0;
	int bytesToRead = length;

	//check if the length to read actually fits in the filee
	if(iNodeTable[fileID].size < length + open_file_table[fileID].readPtr){
		bytesToRead = iNodeTable[fileID].size - open_file_table[fileID].readPtr;
	}

	//since we just want to make sure that we can handle the case with all blocks filled up
	int allBlocks [268] ={0};
	//check if we need to check the indirect pointers
	if(iNodeTable[fileID].indirect != 0){
	//here we hit the indirect pointers
	//this means at least all the direct pointers are full
	 	for(int i = 0;i<12;i++){
			//keep copying the addressses of the direct blocks we want
			allBlocks[i] = iNodeTable[fileID].blocks[i];
    	}
		//malloc a whole block for the indirect pointer, since there are 256, and each is 4 bytes
		int* indirectBuffer = (int*)malloc(1024);
		//rread all the indirect pointers to the indirect buffer
        read_blocks(iNodeTable[fileID].indirect,1,indirectBuffer);
        numOfIndirectPointers = ceiling((((float)iNodeTable[fileID].size - (float)(12*1024))/1024));
        memcpy((allBlocks + 12), indirectBuffer, sizeof(int) * numOfIndirectPointers);
        free(indirectBuffer);
    }else{
		for(int i = 0;i<12;i++){
            if(iNodeTable[fileID].blocks[i] == 0){
                break;
            }else{
                allBlocks[i] = iNodeTable[fileID].blocks[i];
            }
        }
	}
	//at this point, we should have all the non empty blocks inside allBlocks pointer, both direct and indirect
	int counter = ((open_file_table[fileID].readPtr)/1024);
	int doneReading = 0;
	int offSet = open_file_table[fileID].readPtr % 1024;
	int byteInFirst = bytesToRead;
	int byteInLast = 0;

	if(1024-offSet < bytesToRead){
		byteInFirst = 1024-offSet;
	}else{
		byteInFirst = bytesToRead;
	}

	//read first potentially not full block
	char buffy[1024] = {0};
	read_blocks(allBlocks[counter],1,buffy);
	memcpy(buf,buffy+offSet, byteInFirst);
	//update variables
	buf += byteInFirst;
	doneReading += byteInFirst;
	counter++;

	//read middle full blocks if there are any
	while( bytesToRead - doneReading > 1024 )
	{
		read_blocks(allBlocks[counter],1, buf);
		counter++;
		doneReading += 1024;
		buf += 1024;
	}

	//read last blocks
	byteInLast = bytesToRead - doneReading;

	if (byteInLast > 0 ){
		//char is for per byte 
		char buffy [1024] = {0};
		
		read_blocks(allBlocks[counter],1, buffy);
		memcpy(buf, buffy, byteInLast);

	}
	doneReading += byteInLast;
	//update read Ptr
	open_file_table[fileID].readPtr += bytesToRead;
	return doneReading;

}

int sfs_remove(char *file){
	for(int i = 0; i < 101 ; i ++){
		if(strcmp(file, directories[i].filename)==0){
			//if we have found the file with the right name
			int iNodeNum = directories[i].inode_num;
			if(open_file_table[iNodeNum].isOpen == true){
				return 0;
			}

			//set as closed from inodetable
			iNodeTable[iNodeNum].mode = 0;
			iNodeTable[iNodeNum].gid = 0;
			iNodeTable[iNodeNum].link_count = 0;
			iNodeTable[iNodeNum].size = 0;
			iNodeTable[iNodeNum].uid = 0;
			for(int i = 0 ; i < 12 ; i ++){
				if(iNodeTable[iNodeNum].blocks[i] == 0){
					break;
				}else{
					int index = iNodeTable[iNodeNum].blocks[i];
					iNodeTable[iNodeNum].blocks[i] = 0;
					//set bitmap to free
					bitmap[index] = false;
				}
			}
			//free the indirect index of the bitmap if there is one
			if(iNodeTable[iNodeNum].indirect != 0){
				bitmap[iNodeTable[iNodeNum].indirect] = false;
				iNodeTable[iNodeNum].indirect = 0;
			}
			open_file_table[iNodeNum].fd = -1;
			open_file_table[iNodeNum].inodePtr = -1;
			open_file_table[iNodeNum].isOpen = false;
			open_file_table[iNodeNum].readPtr = 0;
			open_file_table[iNodeNum].writePtr = 0;
			strcpy(directories[i].filename, "" );
			directories[i].inode_num = -1;
			write_blocks (1 ,8 , &iNodeTable);

        	write_blocks(4090,2,&directories);

        	write_blocks (4092, 4, &bitmap);
			return 0;
		}
	}
	return 0;
}

int findFreeFromBitmap () {
	//loop through int array to find free index
	int availableEntry = 0;
	for (availableEntry = 10; availableEntry < 3990; availableEntry++) {
		if (availableEntry == 3990) {
		printf("No more blocks available\n");
		return -1;
		}
		if (bitmap[availableEntry] == false) {
			bitmap[availableEntry] = true;
			break;
		}
	}
	if (availableEntry == 3990) {
		printf("No more blocks available\n");
		return -1;
	}
	write_blocks (4092, 4, &bitmap);
	return availableEntry;
}

int sfs_fclose(int fileID){
	for(int i = 0 ; i < 128 ; i++){
		if(open_file_table[i].fd == fileID){
			//only close opened files
			if(open_file_table[i].isOpen == true){
				open_file_table[i].isOpen = false;
				return 0;
			}else{
				return -1;
			}
			return 0;
		}
	}
	return -1;
}

