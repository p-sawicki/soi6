#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAT_ENTRY_SIZE		4
#define CLUSTER_FREE 		0xFFFFFFFF
#define CLUSTER_END  		0x00000000
#define CLUSTER_SIZE		4096
#define DIR_FIRST		0x01
#define DIR_CONT		0x02
#define DIR_FREE		0xFF
#define DIR_ENTRY_SIZE		32	
#define DIR_ENTRY_PER_CLUSTER	CLUSTER_SIZE / DIR_ENTRY_SIZE
#define FILE_NAME_OFFSET_FIRST	9
#define MAX_FILE_NAME_FIRST	23	
#define MAX_FILE_NAME_CONT	31	
#define MAX_FILE_NAME		MAX_FILE_NAME_FIRST + MAX_FILE_NAME_CONT * (DIR_ENTRY_PER_CLUSTER - 1)	

char *driveName;
unsigned int size;
unsigned int clustersAmount;
unsigned int clustersFree;
unsigned int firstClusterAddress;

void initDrive();
int addToDir(char *name, unsigned int fileSize, unsigned int fileBegin);
int copyToVirtual(char *sourceName, char *destName);
int copyFromVirtual(char *sourceName, char *destName);
int deleteFile(char *name);
int findFile(char *name, unsigned int *fileSize, unsigned int *fileBegin, unsigned int *dirCluster, unsigned int *dirEntryOffset);
void printDrive(char ifShowAll);
void printMap();
void deleteDrive(char *driveName);

void initDrive(){
	FILE *f = fopen(driveName, "wb");
	unsigned char filler = 0xFF;
	for(unsigned int i = 0; i < size; ++i)
		fwrite(&filler, 1, 1, f);
	clustersAmount = size / (CLUSTER_SIZE + FAT_ENTRY_SIZE);
	clustersFree = clustersAmount - 1;
	firstClusterAddress = clustersAmount * FAT_ENTRY_SIZE;
       	fseek(f, 0, SEEK_SET);
	unsigned int end = CLUSTER_END;
	fwrite(&end, FAT_ENTRY_SIZE, 1, f);
	fclose(f);
}
int addToDir(char *name, unsigned int size, unsigned int clusterNumber){
	char *n = strrchr(name, '/');
	if(n)
		name = n + 1;
	unsigned int length = strlen(name) + 1;
	unsigned char entriesNeeded = 1;
	if(length > MAX_FILE_NAME_FIRST)
		entriesNeeded += (length - MAX_FILE_NAME_FIRST - 1) / MAX_FILE_NAME_CONT + 1;
	FILE *f = fopen(driveName, "r+b");
	unsigned int clusterOfCurrentDir = 0;
	while(1){
		fseek(f, firstClusterAddress + clusterOfCurrentDir * CLUSTER_SIZE, SEEK_SET);
		unsigned char entriesAvailable = 0;
		for(unsigned char i = 0; i < CLUSTER_SIZE / DIR_ENTRY_SIZE; ++i){
			unsigned char entryState = 0;
			fread(&entryState, 1, 1, f);
			if(entryState == DIR_FREE){
				++entriesAvailable;
				if(entriesAvailable == entriesNeeded)
					break;
			}
			else
				entriesAvailable = 0;
			fseek(f, DIR_ENTRY_SIZE - 1, SEEK_CUR);
		}
		if(entriesAvailable == entriesNeeded){
			fseek(f, -1 - DIR_ENTRY_SIZE * (entriesNeeded - 1), SEEK_CUR);
			char entryState = DIR_FIRST;
			fwrite(&entryState, 1, 1, f);
			fwrite(&size, 4, 1, f);
			fwrite(&clusterNumber, FAT_ENTRY_SIZE, 1, f);
			char nameBuffer[MAX_FILE_NAME_FIRST];
			strncpy(nameBuffer, name, MAX_FILE_NAME_FIRST);
			unsigned char toWrite = length < MAX_FILE_NAME_FIRST ? length : MAX_FILE_NAME_FIRST;
			fwrite(nameBuffer, 1, toWrite, f);
			for(unsigned char i = 1; i < entriesNeeded; ++i){
				name = i == 1 ? name + MAX_FILE_NAME_FIRST : name + MAX_FILE_NAME_CONT;
				length = i == 1 ? length - MAX_FILE_NAME_FIRST : length - MAX_FILE_NAME_CONT;
				entryState = DIR_CONT;
				fwrite(&entryState, 1, 1, f);
				char nameBufferCont[MAX_FILE_NAME_CONT];
				strncpy(nameBufferCont, name, MAX_FILE_NAME_CONT);
				toWrite = length < MAX_FILE_NAME_CONT ? length : MAX_FILE_NAME_CONT;	
				fwrite(nameBufferCont, 1, toWrite, f);
			}
			if(clusterOfCurrentDir == 0){
				fclose(f);
				return 0;
			}
			break;
		}
		fseek(f, clusterOfCurrentDir * FAT_ENTRY_SIZE, SEEK_SET);
		unsigned int next; 
		fread(&next, FAT_ENTRY_SIZE, 1, f);
		if(next != CLUSTER_FREE && next != CLUSTER_END){
			clusterOfCurrentDir = next;
			continue;
		}
		unsigned char flag = 0;
		for(unsigned int i = 1; i < clustersAmount; ++i){
			fseek(f, i * FAT_ENTRY_SIZE, SEEK_SET);
			fread(&next, FAT_ENTRY_SIZE, 1, f);
			if(next == CLUSTER_FREE){
				fseek(f, clusterOfCurrentDir * FAT_ENTRY_SIZE, SEEK_SET);
				fwrite(&i, FAT_ENTRY_SIZE, 1, f);
				fseek(f, i * FAT_ENTRY_SIZE, SEEK_SET);
				unsigned int end = CLUSTER_END;
				fwrite(&end, FAT_ENTRY_SIZE, 1, f);
				--clustersFree;
				clusterOfCurrentDir = i;
				flag = 1;
				break;
			}
		}
		if(flag == 1)
			continue;
		return 1;
	}
	fclose(f);
	return 0;	
}
int copyToVirtual(char *sourceName, char *destName){
	FILE *s = fopen(sourceName, "rb");
	fseek(s, 0, SEEK_END);
	unsigned int sourceSize = ftell(s);
	rewind(s);
	if(sourceSize == 0){
		fclose(s);
		return addToDir(destName, sourceSize, 0);
	}
	unsigned int clustersNeeded = sourceSize == 0 ? 0 : (sourceSize - 1) / CLUSTER_SIZE + 1;
	if(deleteFile(destName) == 0)
		printf("Deleted previous version of %s\n", destName);
	if(clustersNeeded > clustersFree){
		fclose(s);
		return 1;
	}
	FILE *f = fopen(driveName, "r+b");
	unsigned int previousClusterNumber = 0;
	for(unsigned int i = 0; i < clustersNeeded; ++i){
		unsigned int clusterNumber = 1;
		for(; clusterNumber < clustersAmount; ++clusterNumber){
			unsigned int next;
			fseek(f, clusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
			fread(&next, FAT_ENTRY_SIZE, 1, f);
			if(next == CLUSTER_FREE)
				break;
		}
		if(i == 0){
			fclose(f);
			if(addToDir(destName, sourceSize, clusterNumber))
				return 1;
			f = fopen(driveName, "r+b");
		}
		unsigned char buffer[CLUSTER_SIZE];
		unsigned int toRead = sourceSize < CLUSTER_SIZE ? sourceSize : CLUSTER_SIZE;
		fread(buffer, 1, toRead, s);
		fseek(f, firstClusterAddress + CLUSTER_SIZE * clusterNumber, SEEK_SET);
		fwrite(buffer, 1, toRead, f);
		if(previousClusterNumber != 0){
			fseek(f, previousClusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
			fwrite(&clusterNumber, FAT_ENTRY_SIZE, 1, f);
		}
		fseek(f, clusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
		unsigned int end = CLUSTER_END;
		fwrite(&end, FAT_ENTRY_SIZE, 1, f);
		--clustersFree;
		previousClusterNumber = clusterNumber;
	}
	fclose(s);
	fclose(f);
	return 0;
}	
int findFile(char *name, unsigned int *fileSize, unsigned int *fileBegin, unsigned int *dirCluster, unsigned int *dirEntryOffset){
	FILE *f = fopen(driveName, "rb");
	unsigned int dirClusterNumber = 0;
	while(1){
		for(unsigned char i = 0; i < CLUSTER_SIZE / DIR_ENTRY_SIZE; ++i){
			fseek(f, firstClusterAddress + dirClusterNumber * CLUSTER_SIZE + i * DIR_ENTRY_SIZE, SEEK_SET);
			char dirEntry[DIR_ENTRY_SIZE];
			fread(dirEntry, 1, DIR_ENTRY_SIZE, f);
			if(dirEntry[0] != DIR_FIRST)
				continue;
			char ifNameMatches = 1;
			if(strncmp(name, dirEntry + FILE_NAME_OFFSET_FIRST, MAX_FILE_NAME_FIRST) == 0){
				for(unsigned char j = 0 ;; ++j){
					unsigned char dirState;
					fread(&dirState, 1, 1, f);
					if(dirState == DIR_CONT){
						char nameCont[MAX_FILE_NAME_CONT];
						fread(nameCont, 1, MAX_FILE_NAME_CONT, f);
						if(strncmp(name + MAX_FILE_NAME_FIRST + j * MAX_FILE_NAME_CONT, nameCont, MAX_FILE_NAME_CONT) != 0){
							ifNameMatches = 0;
							break;
						}
					}
					else
						break;
				}
			}
			else
				ifNameMatches = 0;
			if(ifNameMatches){
				fseek(f, firstClusterAddress + dirClusterNumber * CLUSTER_SIZE + i * DIR_ENTRY_SIZE + 1, SEEK_SET);
				unsigned int s, b;
				fread(&s, 4, 1, f);
				fread(&b, FAT_ENTRY_SIZE, 1, f);
				*fileSize = s;
				*fileBegin = b;
				*dirCluster = dirClusterNumber;
				*dirEntryOffset = i;	
				fclose(f);
				return 0;
			}
		}
		fseek(f, dirClusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
		unsigned int next;
		fread(&next, FAT_ENTRY_SIZE, 1, f);
		if(next == CLUSTER_END || next == CLUSTER_FREE){
			fclose(f);
			return 1;
		}
		dirClusterNumber = next;
	}
}
int copyFromVirtual(char *sourceName, char *destName){
	unsigned int dirClusterNumber = 0;
	unsigned int sourceSize = 0;
	unsigned int dataClusterNumber = 0;
	unsigned int dirEntryOffset = 0;
	if(findFile(sourceName, &sourceSize, &dataClusterNumber, &dirClusterNumber, &dirEntryOffset))
		return 1;
	unsigned int clustersUsed = sourceSize == 0 ? 0 : (sourceSize - 1) / CLUSTER_SIZE + 1;
	FILE *d = fopen(destName, "wb");
	FILE *f = fopen(driveName, "rb");
	for(unsigned int i = 0; i < clustersUsed; ++i){
		fseek(f, firstClusterAddress + dataClusterNumber * CLUSTER_SIZE, SEEK_SET);
		char buffer[CLUSTER_SIZE];
		unsigned int toRead = sourceSize < CLUSTER_SIZE ? sourceSize : CLUSTER_SIZE;
		fread(buffer, 1, toRead, f);
		fwrite(buffer, 1, toRead, d);
		sourceSize -= CLUSTER_SIZE;
		fseek(f, dataClusterNumber * FAT_ENTRY_SIZE , SEEK_SET);
		fread(&dataClusterNumber, FAT_ENTRY_SIZE, 1, f);
	}
	fclose(f);
	fclose(d);
	return 0;
}
int deleteFile(char *name){
	unsigned int fileSize, dataClusterNumber, dirClusterNumber, dirEntryOffset;
	if(findFile(name, &fileSize, &dataClusterNumber, &dirClusterNumber, &dirEntryOffset))
		return 1;
	FILE *f = fopen(driveName, "r+b");
	while(1){
		fseek(f, firstClusterAddress + dirClusterNumber * CLUSTER_SIZE + dirEntryOffset * DIR_ENTRY_SIZE, SEEK_SET);
		char state = DIR_FREE;
		fwrite(&state, 1, 1, f);
		++dirEntryOffset;
		fseek(f, MAX_FILE_NAME_CONT, SEEK_CUR);
		fread(&state, 1, 1, f);
		if(state != DIR_CONT)
			break;
	}	
	if(fileSize == 0){
		fclose(f);
		return 0;
	}
	while(1){
		fseek(f, dataClusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
	        unsigned int next;
		fread(&next, FAT_ENTRY_SIZE, 1, f);
		unsigned int empty = CLUSTER_FREE;
		fseek(f, -FAT_ENTRY_SIZE, SEEK_CUR);
		fwrite(&empty, FAT_ENTRY_SIZE, 1, f);
		++clustersFree;
		if(next == CLUSTER_END){
			fclose(f);
			return 0;
		}
		dataClusterNumber = next;
	}
}	
void printDrive(char ifShowAll){
	FILE *f = fopen(driveName, "rb");
	unsigned int dirClusterNumber = 0;
	while(1){
		fseek(f, firstClusterAddress + dirClusterNumber * CLUSTER_SIZE, SEEK_SET);
		char fileName[MAX_FILE_NAME];
		unsigned int fileSize;
		unsigned int fileBegin;
		char newFile = 1;
		unsigned int offset = 0;
		for(unsigned char i = 0; i < CLUSTER_SIZE / DIR_ENTRY_SIZE; ++i){
			char dirEntry[DIR_ENTRY_SIZE];
			fread(dirEntry, 1, DIR_ENTRY_SIZE, f);
			if((unsigned char)dirEntry[0] == DIR_FREE){
				if(newFile == 0){
					if(ifShowAll || fileName[0] != '.'){
						printf("%s", fileName);
						for(unsigned int j = 0; j < 64 - strlen(fileName); ++j)
							printf(" ");
						printf("%d\t0x%x\n", fileSize, firstClusterAddress + CLUSTER_SIZE * fileBegin);
					}
					newFile = 1;
				}
				continue;
			}
			else if(dirEntry[0] == DIR_FIRST){
				if(newFile == 0 && (ifShowAll || fileName[0] != '.')){
					printf("%s", fileName);
					for(unsigned int j = 0; j < 64 - strlen(fileName); ++j)
						printf(" ");
					printf("%d\t0x%x\n", fileSize, firstClusterAddress + CLUSTER_SIZE * fileBegin);
				}
				fseek(f, -DIR_ENTRY_SIZE + 1, SEEK_CUR);
				fread(&fileSize, 4, 1, f);
				fread(&fileBegin, FAT_ENTRY_SIZE, 1, f);
				strncpy(fileName, dirEntry + FILE_NAME_OFFSET_FIRST, MAX_FILE_NAME_FIRST);
				offset += MAX_FILE_NAME_FIRST;
				newFile = 0;
				fseek(f, MAX_FILE_NAME_FIRST, SEEK_CUR);
			}
			else{
				strncpy(fileName + offset, dirEntry + 1, MAX_FILE_NAME_CONT);
				offset += MAX_FILE_NAME_CONT;
			}
		}
		fseek(f, dirClusterNumber * FAT_ENTRY_SIZE, SEEK_SET);
		unsigned int next;
		fread(&next, FAT_ENTRY_SIZE, 1, f);
		if(next == CLUSTER_END){
			fclose(f);
			return;
		}
		dirClusterNumber = next;
	}
}
void printMap(){
	printf("Number\tAddress\tType\tSize\tState\n");
	printf("-----\t0x0\tFAT\t%d\t-----\n", firstClusterAddress);
	FILE *f = fopen(driveName, "rb");
	unsigned int lastDir = 0;
	for(unsigned int i = 0; i < clustersAmount; ++i){
		unsigned int address = firstClusterAddress + i * CLUSTER_SIZE;
		unsigned int state;
		char type[5];
		fread(&state, FAT_ENTRY_SIZE, 1, f);
		if(i == lastDir){
			strcpy(type, "DIR");	
			if(state != CLUSTER_END)
				lastDir = state;
		}
		else
			strcpy(type, "DATA");
		if(state == CLUSTER_FREE)
			printf("0x%x\t0x%x\t%s\t%d\tFREE\n", i, address, type, CLUSTER_SIZE);
		else if(state == CLUSTER_END)
			printf("0x%x\t0x%x\t%s\t%d\tLAST\n", i, address, type, CLUSTER_SIZE);
		else
			printf("0x%x\t0x%x\t%s\t%d\t->0x%x\n", i, address, type, CLUSTER_SIZE, state);
	}
}
int main(int argc, char **argv){
	if(argc < 3)
		return 1;
	driveName = malloc(strlen(argv[1]) + 1);
	strcpy(driveName, argv[1]);
	size = atoi(argv[2]);
	initDrive();
	char n[] = "This is a very long name to test stuff";
	copyToVirtual("test", n);
	copyFromVirtual(n, "test2");
	copyToVirtual("test3", n);
	copyToVirtual("test", "test");
	deleteFile(n);
	copyToVirtual("test", "t2");
	copyToVirtual("a.txt", "a");
	printDrive(0);
	printMap();
	return 0;
}
