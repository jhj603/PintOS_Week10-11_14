#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

/* 전역 락을 통한 경쟁 상태 방지를 위함 */
/* 두 프로세스가 거의 동시에 filesys_create()를 호출할 수 있고 이는 경쟁 상태 유발 */
/* 락을 사용해 동기화를 필수적으로 수행해야 함. */
#include "include/threads/synch.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

/* 파일 시스템 접근을 직렬화하기 위한 락 */
/* 파일 시스템 전체를 보호하는 전역 락으로 한 번에 단 하나의 프로세스만이 */
/* 파일 시스템 관련 작업을 수행할 수 있도록 보장해 경쟁 상태 원천 방지 가능 */
struct lock filesys_lock;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

	/* 전역 락 초기화 */
	lock_init(&filesys_lock);
	
#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	/* 락 획득으로 한 번에 하나의 프로세스만 수행 */
	lock_acquire(&filesys_lock);

	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	/* 작업이 끝났으므로 락 해제 */
	lock_release(&filesys_lock);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	/* 락 획득으로 한 번에 하나의 프로세스만 수행 */
	lock_acquire(&filesys_lock);

	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	struct file* file = file_open (inode);

	/* 작업이 끝났으므로 락 해제 */
	lock_release(&filesys_lock);

	return file;
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	/* 락 획득으로 한 번에 하나의 프로세스만 수행 */
	lock_acquire(&filesys_lock);

	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	/* 작업이 끝났으므로 락 해제 */
	lock_release(&filesys_lock);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
