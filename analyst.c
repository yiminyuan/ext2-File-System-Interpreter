#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ext2_fs.h"

#define BITS_PER_BYTE        8     // 8 bits per byte
#define BYTE_SHIFT_AMT       3     // x << 3 equals x * 8
#define READ_BUFFER_SIZE     1024  // Read 1024 bytes at a time
#define TIME_BUFFER_SIZE     18    // mm/dd/yy HH:MM:SS
#define DIR_ENTRY_SIZE       sizeof(struct ext2_dir_entry)

/* Exit code */
#define SUCCESS      0
#define BAD_ARGUMENT 1
#define OTHER_ERRORS 2

static __u32 block_size;
static __u32 num_indirect_entry;

/* Wrapper of pread() */
void
my_pread(int fd, void *buf, size_t count, off_t offset)
{
  if(pread(fd, buf, count, offset) < 0)
    {
      fprintf(stderr,
	      "Error occurred while reading from the file system image: %s\n",
	      strerror(errno));
      exit(OTHER_ERRORS);
    }
}

/* Wrapper of write() */
void
my_write(int fd, const void *buf, size_t count)
{
  if(write(STDOUT_FILENO, buf, count) < 0)
    {
      fprintf(stderr,
	      "Error occurred while writing to the standard output: %s\n",
	      strerror(errno));
      exit(OTHER_ERRORS);
    }
}

char
file_type(__u16 i_mode)
{
  switch(i_mode & 0xF000)
    {
    case 0x4000:
      return 'd';
    case 0x8000:
      return 'f';
    case 0xA000:
      return 's';
    default:
      return '?';
    }
}

void
convert_time(__u32 i_time, char *time_buffer)
{
  time_t time = i_time;
  struct tm *tmp = gmtime(&time);
  
  if(tmp == NULL)
    {
      fprintf(stderr, "Error occurred while converting the time\n");
      exit(OTHER_ERRORS);
    }

  if(strftime(time_buffer, TIME_BUFFER_SIZE, "%m/%d/%y %H:%M:%S", tmp) == 0)
    {
      fprintf(stderr, "Error occurred while setting the time buffer\n");
      exit(OTHER_ERRORS);
    }
}

void
read_superblock(int fd, struct ext2_super_block *sb)
{
  off_t sb_offset = 1024;
  
  my_pread(fd, sb, sizeof(struct ext2_super_block), sb_offset);

  block_size = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;

  if(block_size < EXT2_MIN_BLOCK_SIZE || block_size > EXT2_MAX_BLOCK_SIZE)
    {
      fprintf(stderr, "Block size of %u bytes is not supported\n", block_size);
      exit(OTHER_ERRORS);
    }

  char read_buffer[READ_BUFFER_SIZE];
  int nchars;
  
  nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
		    "SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n",
		    sb->s_blocks_count,
		    sb->s_inodes_count,
		    (int)block_size,
		    sb->s_inode_size,
		    sb->s_blocks_per_group,
		    sb->s_inodes_per_group,
		    sb->s_first_ino);

  my_write(STDOUT_FILENO, read_buffer, nchars);
}

void
read_free_block_entries(int fd, off_t offset,
			unsigned group_number,
			unsigned blocks_per_group,
			unsigned blocks_in_this_group)
{
  char block_bitmap_buffer[block_size], read_buffer[READ_BUFFER_SIZE];
  int nchars;

  my_pread(fd, block_bitmap_buffer, block_size, offset);

  unsigned i, j;
  for(i = 0; i < blocks_in_this_group; i += BITS_PER_BYTE)
    {
      unsigned index = i >> BYTE_SHIFT_AMT;
      char ichar = block_bitmap_buffer[index];
      for(j = 1; j <= BITS_PER_BYTE; ++j)
	{
	  if((ichar & 1) == 0)
	    {
	      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
				"BFREE,%d\n",
				i + j + group_number * blocks_per_group);
	      
	      my_write(STDOUT_FILENO, read_buffer, nchars);
	    }
	  
	  ichar >>= 1;
	}
    }
}

void
read_free_inode_entries(int fd, off_t offset,
			unsigned group_number,
			unsigned inodes_per_group,
			unsigned inodes_in_this_group)
{
  char inode_bitmap_buffer[block_size], read_buffer[READ_BUFFER_SIZE];
  int nchars;

  my_pread(fd, inode_bitmap_buffer, block_size, offset);

  unsigned i, j;
  for(i = 0; i < inodes_in_this_group; i += BITS_PER_BYTE)
    {
      unsigned index = i >> BYTE_SHIFT_AMT;
      char ichar = inode_bitmap_buffer[index];
      for(j = 1; j <= BITS_PER_BYTE; ++j)
	{
	  if((ichar & 1) == 0)
	    {
	      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
				"IFREE,%d\n",
				i + j + group_number * inodes_per_group);
	      
	      my_write(STDOUT_FILENO, read_buffer, nchars);
	    }
	  
	  ichar >>= 1;
	}
    }
}

void
read_directory_entries(int fd, unsigned inode_number,
		       const struct ext2_inode *inode)
{
  struct ext2_dir_entry dir_entry;
  char read_buffer[READ_BUFFER_SIZE];
  int nchars;
  unsigned i;
  
  for(i = 0; i < EXT2_NDIR_BLOCKS; ++i)
    {
      unsigned block_offset = inode->i_block[i] * block_size;
      unsigned next_block_offset = block_offset + block_size;
      unsigned current_offset = block_offset;

      while(current_offset < next_block_offset)
	{
	  my_pread(fd, &dir_entry, DIR_ENTRY_SIZE, current_offset);

	  if(dir_entry.inode == 0)
	    {
	      break;
	    }

	  nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
			    "DIRENT,%d,%d,%d,%d,%d,'%s'\n",
			    inode_number,
			    current_offset - block_offset,
			    dir_entry.inode,
			    dir_entry.rec_len,
			    dir_entry.name_len,
			    dir_entry.name);

	  my_write(STDOUT_FILENO, read_buffer, nchars);

	  current_offset += dir_entry.rec_len;
	}
    }
}

void
read_indirect_blocks_helper(int fd, off_t offset,
			    unsigned level,
			    unsigned inode_number,
			    unsigned block_number)
{
  __u32 indirect_blocks[num_indirect_entry];
  char read_buffer[READ_BUFFER_SIZE];
  int nchars;
  off_t logical_offset;

  if(block_number != 0)
    {
      my_pread(fd, indirect_blocks, block_size, block_number * block_size);

      unsigned i;
      for(i = 0; i < num_indirect_entry; ++i)
	{
	  if(indirect_blocks[i] != 0)
	    {
	      logical_offset = offset + i;
	      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
				"INDIRECT,%d,%d,%d,%d,%d\n",
				inode_number,
				level,
				(int)logical_offset,
				block_number,
				indirect_blocks[i]);

	      my_write(STDOUT_FILENO, read_buffer, nchars);

	      if(level > 1)
		{
		  read_indirect_blocks_helper(fd, logical_offset, level - 1,
					      inode_number,
					      indirect_blocks[i]);
		}
	    }
	}
    }
}

void
read_indirect_blocks(int fd, unsigned level,
		     unsigned inode_number, unsigned block_number)
{
  off_t offset = 0;
  num_indirect_entry = block_size >> 2;
  
  switch(level)
    {
    case 1:
      offset = EXT2_IND_BLOCK;
      break;
    case 2:
      offset = EXT2_IND_BLOCK + num_indirect_entry;
      break;
    case 3:
      offset = EXT2_IND_BLOCK + num_indirect_entry +
	(num_indirect_entry << BITS_PER_BYTE);
      break;
    default:
      fprintf(stderr, "Invalid level of indirection\n");
      exit(OTHER_ERRORS);
    }

  read_indirect_blocks_helper(fd, offset, level, inode_number, block_number);
}

void
read_valid_inodes(int fd, off_t offset,
		  unsigned group_number,
		  unsigned inodes_per_group,
		  unsigned inodes_in_this_group)
{
  struct ext2_inode inodes[inodes_in_this_group];
  char atime_buffer[TIME_BUFFER_SIZE];
  char ctime_buffer[TIME_BUFFER_SIZE];
  char mtime_buffer[TIME_BUFFER_SIZE];
  char read_buffer[READ_BUFFER_SIZE];
  int nchars;

  unsigned i, j;

  my_pread(fd, inodes,
	   inodes_in_this_group * sizeof(struct ext2_inode), offset);

  for(i = 0; i < inodes_in_this_group; ++i)
    {
      unsigned inode_number = i + 1 + group_number * inodes_per_group;
      
      if(inodes[i].i_mode != 0 && inodes[i].i_links_count != 0)
	{
	  convert_time(inodes[i].i_atime, atime_buffer);
	  convert_time(inodes[i].i_ctime, ctime_buffer);
	  convert_time(inodes[i].i_mtime, mtime_buffer);

	  nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
			    "INODE,%d,%c,%o,%d,%d,%d,%s,%s,%s,%d,%d,",
			    inode_number,
			    file_type(inodes[i].i_mode),
			    inodes[i].i_mode & 0x0FFF,
			    inodes[i].i_uid,
			    inodes[i].i_gid,
			    inodes[i].i_links_count,
			    ctime_buffer,
			    mtime_buffer,
			    atime_buffer,
			    inodes[i].i_size,
			    inodes[i].i_blocks);

	  my_write(STDOUT_FILENO, read_buffer, nchars);

	  unsigned num_blocks = EXT2_N_BLOCKS;
	  unsigned tmp = num_blocks - 1;
	  for(j = 0; j < num_blocks; ++j)
	    {
	      if(inodes[i].i_blocks == 0 && inodes[i].i_block[j] != 0)
		{
		  nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
				    "%d\n", inodes[i].i_block[j]);
		  my_write(STDOUT_FILENO, read_buffer, nchars);
		  break;
		}
	      else
		{
		  if(j == tmp)
		    {
		      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
					"%d\n", inodes[i].i_block[j]);
		    }
		  else
		    {
		      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
					"%d,", inodes[i].i_block[j]);
		    }
		}
		
	      my_write(STDOUT_FILENO, read_buffer, nchars);
	    }
	}

      /* Read directory entries */
      if(file_type(inodes[i].i_mode) == 'd')
	{
	  read_directory_entries(fd, inode_number, &inodes[i]);
	}

      /* Read single indirect blocks */
      read_indirect_blocks(fd, 1, inode_number,
			   inodes[i].i_block[EXT2_IND_BLOCK]);

      /* Read double indirect blocks */
      read_indirect_blocks(fd, 2, inode_number,
			   inodes[i].i_block[EXT2_DIND_BLOCK]);
      
      /* Read triple indirect blocks */
      read_indirect_blocks(fd, 3, inode_number,
			   inodes[i].i_block[EXT2_TIND_BLOCK]);
    }
}

void
read_block_groups(int fd, const struct ext2_super_block *sb)
{
  off_t bg_offset = 2048;
  char read_buffer[READ_BUFFER_SIZE];
  int nchars;
  unsigned i;
  unsigned group_count = 1 + (sb->s_blocks_count - 1) / sb->s_blocks_per_group;
  unsigned last_group_number = group_count - 1;
  unsigned desc_list_size = group_count * sizeof(struct ext2_group_desc);
  struct ext2_group_desc bgs[group_count];

  my_pread(fd, bgs, desc_list_size, bg_offset);

  for(i = 0; i < group_count; ++i)
    {
      unsigned blocks_in_this_group = (i == last_group_number) ?
	sb->s_blocks_count - sb->s_blocks_per_group * last_group_number :
	sb->s_blocks_per_group;

      unsigned inodes_in_this_group = (i == last_group_number) ?
	sb->s_inodes_count - sb->s_inodes_per_group * last_group_number :
	sb->s_inodes_per_group;
	
      nchars = snprintf(read_buffer, READ_BUFFER_SIZE,
			"GROUP,%d,%d,%d,%d,%d,%d,%d,%d\n",
			i,
			blocks_in_this_group,
			inodes_in_this_group,
			bgs[i].bg_free_blocks_count,
			bgs[i].bg_free_inodes_count,
			bgs[i].bg_block_bitmap,
			bgs[i].bg_inode_bitmap,
			bgs[i].bg_inode_table);

      my_write(STDOUT_FILENO, read_buffer, nchars);

      /* Read free block entries */
      read_free_block_entries(fd,
			      bgs[i].bg_block_bitmap * block_size,
			      i,
			      sb->s_blocks_per_group,
			      blocks_in_this_group);

      /* Read free inode entries */
      read_free_inode_entries(fd,
			      bgs[i].bg_inode_bitmap * block_size,
			      i,
			      sb->s_inodes_per_group,
			      inodes_in_this_group);

      /* Read valid inodes */
      read_valid_inodes(fd,
			bgs[i].bg_inode_table * block_size,
			i,
			sb->s_inodes_per_group,
			inodes_in_this_group);
    }
}

void
read_file_system(int fd)
{
  struct ext2_super_block sb;

  /* Read superblock */
  read_superblock(fd, &sb);

  /* Read block groups */
  read_block_groups(fd, &sb);
}

int
main(int argc, char **argv)
{
  /* Check number of arguments */
  if(argc != 2)
    {
      fprintf(stderr, "The number of arguments has to be one\n");
      exit(BAD_ARGUMENT);
    }

  int fd;
  
  /* Open file system image */
  if((fd = open(argv[1], O_RDONLY)) < 0)
    {
      fprintf(stderr, "Error occurred while opening file: %s\n",
	      strerror(errno));
      exit(OTHER_ERRORS);
    }

  /* Read file system */
  read_file_system(fd);

  exit(SUCCESS);
}
