# ext2 File System Interpreter
A program that reads an ext2 file system image and reports the inconsistencies to standard output.
## Usage
You can compile the code by simply running:
```
$ make
```
and delete the executable programs by using:
```
$ make clean
```
### Running
Assume the name of the testing file system image is foo.img. You can use the following command to read the image and get a summary in CSV format:
```
$ ./analyst foo.img
```
By default, the summary is printed to standard output. You can redirect it to a regular file by using this command:
```
$ ./analyst foo.img > foo.csv
```
(Optional) If you want to find inconsistencies of the file system image, the second command with redirection in above is needed. Then run the following command to analyze the CSV file:
```
$ ./interpreter foo.csv
```
### CSV Summaries
**Superblock summary**
1. SUPERBLOCK
2. total number of blocks
3. total number of i-nodes
4. block size
5. i-node size
6. blocks per group
7. i-nodes per group
8. first non-reserved i-node
**Group summary**
1. GROUP
2. group number
3. total number of blocks in this group
4. total number of i-nodes in this group
5. number of free blocks
6. number of free i-nodes
7. block number of free block bitmap for this group
8. block number of free i-node bitmap for this group
9. block number of first block of i-nodes in this group
**Free block entries**
1. BFREE
2. number of the free block
**Free I-node entries**
1. IFREE
2. number of the free I-node
**I-node summary**
1. INODE
2. inode number
3. file type
4. mode
5. owner
6. group
7. link count
8. time of last I-node change
9. modification time
10. time of last access
11. file size
12. number of blocks
**Directory entries**
1. DIRENT
2. parent inode number
3. logical byte offset
4. inode number of the referenced file
5. entry length
6. name length
7. name
**Indirect block references**
1. INDIRECT
2. I-node number of the owning file
3. level of indirection for the block being scanned
4. logical block offset represented by the referenced block
5. block number of the indirect block being scanned
6. block number of the referenced block