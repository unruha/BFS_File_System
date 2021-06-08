// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {

  // get to inum of the file to be read
  i32 inum = bfsFdToInum(fd);

  // get cursor position of file
  i32 cursor = bfsTell(fd);

  // determine the FBN that holds the offset
  i32 firstFBN = cursor / BYTESPERBLOCK;

  // determine the last FBN we need to access
  i32 totalBytes = cursor + numb;
  i32 lastFBN = totalBytes / BYTESPERBLOCK;

  // current offset in the return buffer
  i32 bufOffset = 0;

  // loop through each block and add to the return buffer
  for (int i = firstFBN; i <= lastFBN; i++)
  {
    // allocate temporary buffer
    i8* bioBuf = malloc(BYTESPERBLOCK);

    // case for when the FBN is the first FBN to be read
    if (i == firstFBN) {

      // read contents of the FBN into buffer
      bfsRead(inum, i, bioBuf);

      // determine the offset within the FBN
      i32 blockOffset = cursor % BYTESPERBLOCK;

      // determine total num bytes to allocate
      i32 numAllocations = BYTESPERBLOCK - blockOffset;

      // copy the data at the offset into the buffer
      memcpy(buf, bioBuf + blockOffset, numAllocations);

      // update the buffer offset
      bufOffset += numAllocations;
    }
    // it is one of the FBN's in the middle, therefore we read the entire block
    else {

      // read contents of the FBN into buffer
      bfsRead(inum, i, bioBuf);

      // copy the data at the offset into the buffer
      memcpy(buf + bufOffset, bioBuf, BYTESPERBLOCK);

      // update buffer offset
      bufOffset += BYTESPERBLOCK;
    }

    // free the bioBuf
    free(bioBuf);
  }

  // set the new cursor
  bfsSetCursor(inum, cursor + numb);
                                    
  return numb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {

  // get to inum of the file to be read
  i32 inum = bfsFdToInum(fd);

  // get cursor position of file
  i32 cursor = bfsTell(fd);

  // get file size
  i32 fSize = bfsGetSize(inum);

  // file size after writing the additional bytes
  i32 newfSize = cursor + numb;

  // case for when allocation will be greater than current file length 
  // must determine whether the remaining memory in the last block is enough
  // otherwise, we need to allocate new blocks
  if (fSize < newfSize) {

    // determine current number of blocks that the file has
    i32 fCurrentBlocks = 0;
    // case for when the file fills up its last block
    if (fSize % BYTESPERBLOCK == 0) {
      fCurrentBlocks = fSize / BYTESPERBLOCK;
    }
    // case for when the last block has leftover
    else if(fSize % BYTESPERBLOCK != 0) {
      fCurrentBlocks = fSize / BYTESPERBLOCK + 1;
    }
    else {
      printf("ERROR: calculating number of current file blocks");
    }

    // determine if the available space is enough
    i32 fTotalAvailable = fCurrentBlocks * BYTESPERBLOCK;

    // allocate new blocks if we need to
    if (fTotalAvailable < newfSize) {

      // determine how many new blocks to allocate
      i32 memNeeded = newfSize - fTotalAvailable;
      i32 blocksNeeded = 0;

      // case for when we will to fill up newly allocated blocks
      if (memNeeded % BYTESPERBLOCK == 0) {
        blocksNeeded = memNeeded / BYTESPERBLOCK;
      }
      // case for when there will be remaining space on the last new allocated block
      else if (memNeeded % BYTESPERBLOCK != 0) {
        blocksNeeded = memNeeded / BYTESPERBLOCK + 1;
      }
      else {
        printf("ERROR: calculating number of blocks needed");
      }
      
      // determine new FBN size
      i32 fbnSize = fCurrentBlocks + blocksNeeded;
      // allocate new blocks
      bfsExtend(inum, fbnSize);
    }

    // update the size of the file
    bfsSetSize(inum, newfSize);
    
  }

  // store the first fbn number to write to
  i32 fbnStart = cursor / BYTESPERBLOCK;

  // store the last fbn number to write to
  i32 fbnEnd = (cursor + numb) / BYTESPERBLOCK;

  // store total number of blocks to write to
  i32 totalBlocks = fbnStart - fbnEnd + 1;

  // keep track of current buf offset
  i32 bufOffset = 0;

  // loop through and write to each block
  for (int i = fbnStart; i <= fbnEnd; i++) {

    // temporary block buffer
    i8* bioBuf = malloc(BYTESPERBLOCK);

    // case for if we are reading the first block
    // so need to read in the block before writing
    if (i == fbnStart) {

      // read the contents of the block
      bfsRead(inum, i, bioBuf);

      // determine first blockOffset
      i32 firstBlockOffset = cursor % BYTESPERBLOCK;

      /* // determine number of bytes to write to first block
      i32 firstBlockBytes = BYTESPERBLOCK - firstBlockOffset; */

      i32 firstBlockBytes = -1;

      // handle small write (within the same block)
      if (fbnStart == fbnEnd) {
        firstBlockBytes = numb;
      }
      else if (fbnEnd > fbnStart) {
        // determine number of bytes to write to first block
        firstBlockBytes = BYTESPERBLOCK - firstBlockOffset;
      }

      // overwrite the portion of the buf that is being written to
      memcpy(bioBuf + firstBlockOffset, buf, firstBlockBytes);

      // determine the DBN of the FBN
      i32 dbn = bfsFbnToDbn(inum, i);

      // write the block to the disk
      bioWrite(dbn, bioBuf);

      bufOffset += firstBlockBytes;
    }
    // case for if we are writing to the last block
    // so need to read in the block so we dont overwrite existing data
    else if (i == fbnEnd) {

      // read the contents of the block
      bfsRead(inum, i, bioBuf);

      // determine number of bytes to write to last block
      i32 lastBlockStart = BYTESPERBLOCK * fbnEnd;
      i32 lastBlockBytes = (cursor + numb) - lastBlockStart;

      // overwrite the portion of the buf that is being written to
      memcpy(bioBuf, buf + (numb - lastBlockBytes), lastBlockBytes);

      // determine the DBN of the FBN
      i32 dbn = bfsFbnToDbn(inum, i);

      // write the block to the disk
      bioWrite(dbn, bioBuf);

      bufOffset += lastBlockBytes;
    }
    // case when it is one of the middle blocks
    else {

      // write to the buf
      memcpy(bioBuf, buf + bufOffset, BYTESPERBLOCK);

      // determine the DBN of the FBN
      i32 dbn = bfsFbnToDbn(inum, i);

      // write to the disk
      bioWrite(dbn, bioBuf);

      bufOffset += BYTESPERBLOCK;
    }

    free(bioBuf);
  }

  // set new cursor
  bfsSetCursor(inum, cursor + numb);

  return 0;
}
