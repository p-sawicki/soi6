#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_NAME	"disk"

#define CLUSTER_FREE 	0xFFFFFFFF
#define CLUSTER_END  	0x00000000
#define CLUSTER_SIZE	4096
#define DIR_FIRST	0x01
#define DIR_CONT	0x02
#define DIR_FREE	0xFF
#define DIR_ENTRY_SIZE	32	

unsigned int size;
unsigned int clustersAmount;
unsigned int clustersFree;
unsigned int firstClusterAddress;
unsigned int *fat;

void init(){
	FILE *f = fopen(FILE_NAME, "wb");
	unsigned char filler = 0xFF;
	for(unsigned int i = 0; i < size; ++i)
		fwrite(&filler, 1, 1, f);
	clustersAmount = size / (CLUSTER_SIZE + 4);
	clustersFree = clustersAmount - 1;
	firstClusterAddress = clustersAmount * 4;
	fat = malloc(clustersAmount * sizeof(clustersAmount));
	fat[0] = CLUSTER_END;
	for(unsigned int i = 1; i < clustersAmount; ++i)
		fat[i] = CLUSTER_FREE; 
       	fseek(f, 0, SEEK_SET);
	fwrite(fat, 4, 1, f);
	fclose(f);
}
void updateFat(){
	FILE *f = fopen(FILE_NAME, "r+b");
	fseek(f, 0, SEEK_SET);
	fwrite(fat, 4, clustersAmount, f);
	fclose(f);
}
int addToDir(char *name, unsigned int size){
	char *n = strrchr(name, '/');
	if(n)
		name = n + 1;
	unsigned int length = strlen(name) + 1;
	unsigned char entriesNeeded = 1;
	if(length > 27)
		entriesNeeded += (length - 27 - 1) / 31 + 1;
	FILE *f = fopen(FILE_NAME, "r+b");
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
			char nameBuffer[27];
			strncpy(nameBuffer, name, 27);
			fwrite(nameBuffer, 1, 27, f);
			for(unsigned char i = 1; i < entriesNeeded; ++i){
				name = i == 1 ? name + 27 : name + 31;
				entryState = DIR_CONT;
				fwrite(&entryState, 1, 1, f);
				char nameBufferCont[31];
				strncpy(nameBufferCont, name, 31);
				fwrite(nameBufferCont, 1, 31, f);
			}
			if(clusterOfCurrentDir == 0){
				fclose(f);
				return 0;
			}
			break;
		}
		unsigned int next = fat[clusterOfCurrentDir];
		if(next != CLUSTER_FREE && next != CLUSTER_END){
			clusterOfCurrentDir = next;
			continue;
		}
		unsigned char flag = 0;
		for(unsigned int i = 1; i < clustersAmount; ++i){
			if(fat[i] == CLUSTER_FREE){
				fat[clusterOfCurrentDir] = i;
				fat[i] = CLUSTER_END;
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
	updateFat();
	return 0;	
}
int copyToVirtual(char *sourceName, char *destName){
	FILE *s = fopen(sourceName, "rb");
	fseek(s, 0, SEEK_END);
	unsigned int sourceSize = ftell(s);
	fclose(s);
	if(addToDir(destName, sourceSize))
		return 1;
	if(sourceSize == 0)
		return 0;
	unsigned int clustersNeeded = (sourceSize - 1) / CLUSTER_SIZE + 1;
	if(clustersNeeded > clustersFree)
		return 1;
	FILE *f = fopen(FILE_NAME, "r+b");
	s = fopen(sourceName, "rb");
	unsigned int previousClusterNumber = 0;
	for(unsigned int i = 0; i < clustersNeeded; ++i){
		unsigned int clusterNumber = 1;
		for(; clusterNumber < clustersAmount; ++clusterNumber){
			if(fat[clusterNumber] == CLUSTER_FREE)
				break;
		}
		unsigned char buffer[CLUSTER_SIZE];
		unsigned int toRead = sourceSize < CLUSTER_SIZE ? sourceSize : CLUSTER_SIZE;
		fread(buffer, 1, toRead, s);
		fseek(f, firstClusterAddress + CLUSTER_SIZE * clusterNumber, SEEK_SET);
		fwrite(buffer, 1, toRead, f);
		if(previousClusterNumber != 0)
			fat[previousClusterNumber] = clusterNumber;
		fat[clusterNumber] = CLUSTER_END;
		--clustersFree;
		previousClusterNumber = clusterNumber;
	}
	fclose(s);
	fclose(f);
	updateFat();
	return 0;
}	
int main(int argc, char **argv){
	if(argc < 2)
		return 1;
	size = atoi(argv[1]);
	init();
	copyToVirtual("test", "t");
	return 0;
}
