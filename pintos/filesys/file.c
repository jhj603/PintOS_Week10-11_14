#include "filesys/file.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include <debug.h>

/* 열린 파일. */
struct file
{
    struct inode *inode; /* 파일의 inode. */
    off_t pos;           /* 현재 위치. */
    bool deny_write;     /* file_deny_write()가 호출되었는가? */
};

/* 주어진 INODE에 대해 파일을 열고 소유권을 가지며,
 * 새 파일을 반환한다.
 * INODE가 null이거나 메모리 할당이 실패하면 null 포인터를 반환한다. */
struct file *file_open(struct inode *inode)
{
    struct file *file = calloc(1, sizeof *file);
    if (inode != NULL && file != NULL)
    {
        file->inode = inode;
        file->pos = 0;
        file->deny_write = false;
        return file;
    }
    else
    {
        inode_close(inode);
        free(file);
        return NULL;
    }
}

/* FILE과 같은 inode를 위한 새 파일을 열어 반환한다.
 * 실패하면 null 포인터를 반환한다. */
struct file *file_reopen(struct file *file)
{
    return file_open(inode_reopen(file->inode));
}

/* FILE 객체를 속성까지 포함하여 복제하고,
 * 같은 inode를 위한 새 파일을 반환한다.
 * 실패하면 null 포인터를 반환한다. */
struct file *file_duplicate(struct file *file)
{
    struct file *nfile = file_open(inode_reopen(file->inode));
    if (nfile)
    {
        nfile->pos = file->pos;
        if (file->deny_write)
            file_deny_write(nfile);
    }
    return nfile;
}

/* FILE을 닫는다. */
void file_close(struct file *file)
{
    if (file != NULL)
    {
        file_allow_write(file);
        inode_close(file->inode);
        free(file);
    }
}

/* FILE이 캡슐화하고 있는 inode를 반환한다. */
struct inode *file_get_inode(struct file *file)
{
    return file->inode;
}

/* FILE에서 SIZE 바이트를 BUFFER로 읽어온다,
 * 파일의 현재 위치에서 시작한다.
 * 실제로 읽은 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있다.
 * FILE의 현재 위치는 읽은 바이트 수만큼 앞으로 이동한다. */
off_t file_read(struct file *file, void *buffer, off_t size)
{
    off_t bytes_read = inode_read_at(file->inode, buffer, size, file->pos);
    file->pos += bytes_read;
    return bytes_read;
}

/* FILE에서 SIZE 바이트를 BUFFER로 읽어온다,
 * 파일의 FILE_OFS 오프셋에서 시작한다.
 * 실제로 읽은 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있다.
 * FILE의 현재 위치는 영향을 받지 않는다. */
off_t file_read_at(struct file *file, void *buffer, off_t size, off_t file_ofs)
{
    return inode_read_at(file->inode, buffer, size, file_ofs);
}

/* BUFFER에서 FILE로 SIZE 바이트를 쓴다,
 * 파일의 현재 위치에서 시작한다.
 * 실제로 쓴 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있다.
 * (보통은 그 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않음.)
 * FILE의 현재 위치는 쓴 바이트 수만큼 앞으로 이동한다. */
off_t file_write(struct file *file, const void *buffer, off_t size)
{
    off_t bytes_written = inode_write_at(file->inode, buffer, size, file->pos);
    file->pos += bytes_written;
    return bytes_written;
}

/* BUFFER에서 FILE로 SIZE 바이트를 쓴다,
 * 파일의 FILE_OFS 오프셋에서 시작한다.
 * 실제로 쓴 바이트 수를 반환하며,
 * 파일 끝에 도달하면 SIZE보다 적을 수 있다.
 * (보통은 그 경우 파일을 확장하지만, 파일 확장은 아직 구현되지 않음.)
 * FILE의 현재 위치는 영향을 받지 않는다. */
off_t file_write_at(struct file *file, const void *buffer, off_t size,
                    off_t file_ofs)
{
    return inode_write_at(file->inode, buffer, size, file_ofs);
}

/* FILE의 하부 inode에 대한 쓰기 작업을,
 * file_allow_write()가 호출되거나 FILE이 닫힐 때까지 금지한다. */
void file_deny_write(struct file *file)
{
    ASSERT(file != NULL);
    if (!file->deny_write)
    {
        file->deny_write = true;
        inode_deny_write(file->inode);
    }
}

/* FILE의 하부 inode에 대한 쓰기 작업을 다시 허용한다.
 * (다른 파일이 동일한 inode를 열고 쓰기를 금지하고 있으면
 * 여전히 쓰기 금지일 수 있다.) */
void file_allow_write(struct file *file)
{
    ASSERT(file != NULL);
    if (file->deny_write)
    {
        file->deny_write = false;
        inode_allow_write(file->inode);
    }
}

/* FILE의 크기를 바이트 단위로 반환한다. */
off_t file_length(struct file *file)
{
    ASSERT(file != NULL);
    return inode_length(file->inode);
}

/* FILE의 현재 위치를 NEW_POS 바이트로 설정한다,
 * 파일 시작 지점으로부터 NEW_POS 바이트 떨어진 위치. */
void file_seek(struct file *file, off_t new_pos)
{
    ASSERT(file != NULL);
    ASSERT(new_pos >= 0);
    file->pos = new_pos;
}

/* FILE의 현재 위치를,
 * 파일 시작 지점으로부터 바이트 단위 오프셋으로 반환한다. */
off_t file_tell(struct file *file)
{
    ASSERT(file != NULL);
    return file->pos;
}