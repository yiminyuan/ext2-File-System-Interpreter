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