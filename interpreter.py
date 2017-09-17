import sys
import csv
import math

# Exit codes
SUCCESS       = 0 # Successful execution, no inconsistencies found
FAILURE       = 1 # Unsuccessful execution, bad parameters, system call failure
INCONSISTENCY = 2 # Successful execution, inconsistencies found

# Lists that hold different types of entries
SUPERBLOCK = []
GROUP      = []
BFREE      = []
IFREE      = []
INODE      = []
DIRENT     = []
INDIRECT   = []

# Constants relative to the data blocks
# Cited from: ext2_fs.h
# Copyright (C) 1991, 1992  Linus Torvalds
EXT2_NDIR_BLOCKS = 12
EXT2_IND_BLOCK	 = EXT2_NDIR_BLOCKS
EXT2_DIND_BLOCK	 = EXT2_IND_BLOCK + 1
EXT2_TIND_BLOCK	 = EXT2_DIND_BLOCK + 1
EXT2_N_BLOCKS	 = EXT2_TIND_BLOCK + 1

def read_rows_into_lists(reader):
    try:
        for row in reader:
            if row[0] == 'SUPERBLOCK':
                SUPERBLOCK.append(row)
            elif row[0] == 'GROUP':
                GROUP.append(row)
            elif row[0] == 'BFREE':
                BFREE.append(row)
            elif row[0] == 'IFREE':
                IFREE.append(row)
            elif row[0] == 'INODE':
                INODE.append(row)
            elif row[0] == 'DIRENT':
                DIRENT.append(row)
            elif row[0] == 'INDIRECT':
                INDIRECT.append(row)
    except csv.Error:
        print >> sys.stderr, "Unable to read file"
        sys.exit(FAILURE)

def set_constants():    
    global NUMBER_OF_BLOCKS
    global NUMBER_OF_INODES
    global BLOCK_SIZE
    global INODE_SIZE
    global FIRST_INODE
    global ROOT_INODE
    sb = SUPERBLOCK[0]
    NUMBER_OF_BLOCKS = int(sb[1])
    NUMBER_OF_INODES = int(sb[2])
    BLOCK_SIZE       = int(sb[3])
    INODE_SIZE       = int(sb[4])
    FIRST_INODE      = int(sb[7])
    ROOT_INODE       = 2

def print_block_consistency(error, block_number, inode_number, offset, index):
    sys.stdout.write(error)
    if index == EXT2_IND_BLOCK:
        sys.stdout.write("INDIRECT ")
    elif index == EXT2_DIND_BLOCK:
        sys.stdout.write("DOUBLE INDIRECT ")
    elif index == EXT2_TIND_BLOCK:
        sys.stdout.write("TRIPPLE INDIRECT ")
    sys.stdout.write("BLOCK %d IN INODE %d AT OFFSET %d\n"
                     % (block_number, inode_number, offset))
    
def get_offset(index):
    if index >= 0 and index <= EXT2_IND_BLOCK:
        return index
    else:
        nind_entries = BLOCK_SIZE >> 2
        if index == EXT2_DIND_BLOCK:
            return EXT2_IND_BLOCK + nind_entries
        elif index == EXT2_TIND_BLOCK:
            return EXT2_IND_BLOCK + nind_entries * (1 + nind_entries)

def get_first_data_block():
    inodes_per_block = BLOCK_SIZE / INODE_SIZE
    first_block_of_inodes = int(GROUP[0][8])
    inode_blocks = math.ceil(NUMBER_OF_INODES / inodes_per_block)
    return int(first_block_of_inodes + inode_blocks)

def check_invalid_data_blocks():
    inconsistencies_found = 0
    for row in INODE:
        for i in range (0, EXT2_N_BLOCKS):
            block_number = int(row[EXT2_IND_BLOCK + i])
            if block_number < 0 or block_number > NUMBER_OF_BLOCKS:
                inconsistencies_found = 1
                offset = get_offset(i)
                print_block_consistency("INVALID ", block_number, int(row[1]),
                                        offset, i)
    return inconsistencies_found

def check_reserved_data_blocks():
    inconsistencies_found = 0
    first_data_block = get_first_data_block()
    for row in INODE:
        for i in range (0, EXT2_N_BLOCKS):
            block_number = int(row[EXT2_IND_BLOCK + i])
            if block_number > 0 and block_number < first_data_block:
                inconsistencies_found = 1
                offset = get_offset(i)
                print_block_consistency("RESERVED ", block_number,
                                        int(row[1]), offset, i)
    return inconsistencies_found

def check_duplicate_data_blocks():
    inconsistencies_found = 0
    d = dict()
    for row in INODE:
        for i in range (0, EXT2_N_BLOCKS):
            block_number = int(row[EXT2_IND_BLOCK + i])
            if block_number > 0:
                offset = get_offset(i)
                if block_number not in d:
                    d[block_number] = [block_number, int(row[1]), offset, i, 0]
                else:
                    if d[block_number][4] == 0:
                        inconsistencies_found = 1
                        print_block_consistency("DUPLICATE ",
                                                d[block_number][0],
                                                d[block_number][1],
                                                d[block_number][2],
                                                d[block_number][3])
                        d[block_number][4] = 1
                        print_block_consistency("DUPLICATE ", block_number,
                                                int(row[1]), offset, i)
    return inconsistencies_found

def check_allocated_blocks():
    inconsistencies_found = 0
    free_blocks = set()
    for row in BFREE:
        free_blocks.add(int(row[1]))
    for row in INODE:
        for i in range (0, EXT2_N_BLOCKS):
            block_number = int(row[EXT2_IND_BLOCK + i])
            if block_number in free_blocks:
                inconsistencies_found = 1
                sys.stdout.write("ALLOCATED BLOCK %d ON FREELIST\n"
                                 % (block_number))
    return inconsistencies_found

def check_unreferenced_blocks():
    inconsistencies_found = 0
    legal_blocks = set()
    for row in BFREE:
        legal_blocks.add(int(row[1]))
    for row in INODE:
        for i in range (0, EXT2_N_BLOCKS):
            block_number = int(row[EXT2_IND_BLOCK + i])
            legal_blocks.add(block_number)
    for row in INDIRECT:
        legal_blocks.add(int(row[4]))
        legal_blocks.add(int(row[5]))
    first_data_block = get_first_data_block()
    for i in range(first_data_block, NUMBER_OF_BLOCKS):
        if i not in legal_blocks:
            inconsistencies_found = 1
            sys.stdout.write("UNREFERENCED BLOCK %d\n" % (i))
    return inconsistencies_found
    
def check_block_consistencies():
    inconsistencies_found = 0
    # Check if every block pointer in an I-node is valid
    inconsistencies_found += check_invalid_data_blocks()
    # Check if there is a block pointer points to a reserved data block
    inconsistencies_found += check_reserved_data_blocks()
    # Check if there is a block that is referenced by multiple files
    inconsistencies_found += check_duplicate_data_blocks()
    # Check if there is an allocated block appears on the free list
    inconsistencies_found += check_allocated_blocks()
    # Check if there is a block that does not appear both on the free list
    # and in an I-node
    inconsistencies_found += check_unreferenced_blocks()
    return inconsistencies_found

def check_inode_allocation():
    inconsistencies_found = 0
    free_inodes = set()
    legal_inodes = set()
    for row in IFREE:
        inode_number = int(row[1])
        free_inodes.add(inode_number)
        legal_inodes.add(inode_number)
    for row in INODE:
        inode_number = int(row[1])
        if inode_number in free_inodes:
            inconsistencies_found = 1
            sys.stdout.write("ALLOCATED INODE %d ON FREELIST\n"
                             % (inode_number))
        else:
            legal_inodes.add(inode_number)
    for i in range(FIRST_INODE, NUMBER_OF_INODES):
        if i not in legal_inodes:
            inconsistencies_found = 1
            sys.stdout.write("UNALLOCATED INODE %d NOT ON FREELIST\n" % (i))
    return inconsistencies_found

def check_link_count():
    inconsistencies_found = 0
    for row in INODE:
        reference_count = 0
        for idir in DIRENT:
            if int(row[1]) == int(idir[3]):
                reference_count += 1
        link_count = int(row[6])
        if reference_count != link_count:
            inconsistencies_found = 1
            sys.stdout.write("INODE %d HAS %d LINKS BUT LINKCOUNT IS %d\n"
                             %(int(row[1]), reference_count, link_count))
    return inconsistencies_found

def check_inodes_of_itself_and_parent():
    inconsistencies_found = 0
    dirent_inodes = set()
    for row in DIRENT:
        dirent_inodes.add(int(row[1]))
    d = dict()
    for row in DIRENT:
        referenced_inode = int(row[3])
        if referenced_inode in dirent_inodes and row[6] not in ["'.'", "'..'"]:
            d[referenced_inode] = int(row[1])
    for row in DIRENT:
        inode_number = int(row[1])
        referenced_inode = int(row[3])
        dirent_name = row[6]
        if dirent_name == "'..'":
            if inode_number == ROOT_INODE and referenced_inode != ROOT_INODE:
                inconsistencies_found = 1
                sys.stdout.write("DIRECTORY INODE 2 NAME '..' LINK TO "
                                 "INODE %d SHOULD BE 2\n" % (referenced_inode))
            elif inode_number != ROOT_INODE \
                 and d[inode_number] != referenced_inode:
                inconsistencies_found = 1
                sys.stdout.write("DIRECTORY INODE %d NAME '..' LINK TO "
                                 "INODE %d SHOULD BE %d\n"
                                 % (inode_number, referenced_inode,
                                    d[inode_number]))
                
        elif dirent_name == "'.'":
            if inode_number != referenced_inode:
                inconsistencies_found = 1
                sys.stdout.write("DIRECTORY INODE %d NAME '.' LINK TO "
                                 "INODE %d SHOULD BE %d\n"
                                 % (inode_number, referenced_inode,
                                    inode_number))
    return inconsistencies_found

def check_directory_entries():
    inconsistencies_found = 0
    free_inodes = set()
    for row in IFREE:
        free_inodes.add(int(row[1]))
    for row in DIRENT:
        inode_number = int(row[1])
        referenced_inode = int(row[3])
        dirent_name = row[6]
        if referenced_inode > NUMBER_OF_INODES or referenced_inode < 1:
            inconsistencies_found = 1
            sys.stdout.write("DIRECTORY INODE %d NAME %s INVALID INODE %d\n"
                             % (inode_number, dirent_name, referenced_inode))
        elif referenced_inode in free_inodes \
             and dirent_name not in ["'.'", "'..'"]:
            inconsistencies_found = 1
            sys.stdout.write("DIRECTORY INODE %d NAME %s "
                             "UNALLOCATED INODE %d\n"
                             % (inode_number, dirent_name, referenced_inode))
    return inconsistencies_found
            
def check_directory_consistencies():
    inconsistencies_found = 0
    # Check if the number of reference count matches the number of link count
    inconsistencies_found += check_link_count()
    # Check inodes of itself(.) and its parent(..)
    inconsistencies_found += check_inodes_of_itself_and_parent()
    # Check if directory entries refer to valid or allocated I-nodes
    inconsistencies_found += check_directory_entries()
    return inconsistencies_found
        
def main():
    # Check the number of arguments
    if len(sys.argv) != 2:
        print >> sys.stderr, 'The number of arguments must be 2'
        sys.exit(FAILURE)

    inconsistencies_found = 0
    
    # Open the input file
    try:
        with open(sys.argv[1], 'rb') as f:
            reader = csv.reader(f)
            # Read every row into appropriate lists
            read_rows_into_lists(reader)
            # Setup of global constants
            set_constants()
            # Consistency Analysis
            inconsistencies_found += check_block_consistencies()
            inconsistencies_found += check_inode_allocation()
            inconsistencies_found += check_directory_consistencies()
    except IOError:
        print >> sys.stderr, 'Unable to open file'
        sys.exit(FAILURE)

    if inconsistencies_found > 0:
        sys.exit(INCONSISTENCY)
    else:
        sys.exit(SUCCESS)

if __name__ == '__main__':
    main()
