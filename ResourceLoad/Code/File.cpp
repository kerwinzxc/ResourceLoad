//
//  File.cpp
//  ResourceLoad
//
//  Created by 张海军 on 8/2/19.
//  Copyright © 2019 张海军. All rights reserved.
//

#include <sys/errno.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <mutex>

#include "File.hpp"
#include "OSGlobalEnums.h"

#define INVALID_FILE_HANDLE     (FileHandle*)-1;

namespace EasyLoader {
static FileHandle* s_fileHandleHead = NULL;
static FileHandle* s_fileHandleTail = NULL;
static std::mutex m_s_fileHandleFindMutex;
static std::mutex m_s_fileHandleAddMutex;
static std::mutex m_s_fileHandleRemoveMutex;

static const FileHandle* FindFileHandle(const struct stat& statBuf)
{
    m_s_fileHandleFindMutex.lock();
    
    const dev_t device = statBuf.st_dev;
    const ino_t inode = statBuf.st_ino;
    
    for (FileHandle *handle = s_fileHandleHead; handle != NULL; handle = handle->next)
    {
        if (handle->device == device && handle->inode == inode)
        {
            m_s_fileHandleFindMutex.unlock();
            return handle;
        }
    }
      m_s_fileHandleFindMutex.unlock();
    return NULL;
}
static void RemoveFileHandle(FileHandle *fileHandle)
{
    m_s_fileHandleRemoveMutex.lock();
    
    if (s_fileHandleHead == fileHandle)
        s_fileHandleHead = fileHandle->next;
    
    if (s_fileHandleTail == fileHandle)
        s_fileHandleTail = fileHandle->prev;
    
    if (fileHandle->prev)
        fileHandle->prev->next = fileHandle->next;
    
    if (fileHandle->next)
        fileHandle->next->prev = fileHandle->prev;
    m_s_fileHandleRemoveMutex.unlock();
}

static int ConvertFlags(int fileaccess, int createmode)
{
    int flags;
    
    switch (fileaccess)
    {
        case kFileAccessRead:
            flags = O_RDONLY;
            break;
        case kFileAccessWrite:
            flags = O_WRONLY;
            break;
        case kFileAccessReadWrite:
            flags = O_RDWR;
            break;
        default:
            flags = 0;
            break;
    }
    
    switch (createmode)
    {
        case kFileModeCreateNew:
            flags |= O_CREAT | O_EXCL;
            break;
        case kFileModeCreate:
            flags |= O_CREAT | O_TRUNC;
            break;
        case kFileModeOpen:
            break;
        case kFileModeOpenOrCreate:
        case kFileModeAppend:
            flags |= O_CREAT;
            break;
        case kFileModeTruncate:
            flags |= O_TRUNC;
            break;
        default:
            flags = 0;
            break;
    }
    
    return flags;
}

static bool ShareAllowOpen(const struct stat& statBuf, int shareMode, int accessMode)
{
    const FileHandle *fileHandle = FindFileHandle(statBuf);
    
    if (fileHandle == NULL) // File is not open
        return true;
    
    if (fileHandle->shareMode == kFileShareNone)
        return false;
    
    if (((fileHandle->shareMode == kFileShareRead)  && (accessMode != kFileAccessRead)) ||
        ((fileHandle->shareMode == kFileShareWrite) && (accessMode != kFileAccessWrite)))
    {
        return false;
    }
    
    if (((fileHandle->accessMode & kFileAccessRead)  && !(shareMode & kFileShareRead)) ||
        ((fileHandle->accessMode & kFileAccessWrite) && !(shareMode & kFileShareWrite)))
    {
        return false;
    }
    
    return true;
}

static void AddFileHandle(FileHandle *fileHandle)
{
    m_s_fileHandleAddMutex.lock();
    
    if (s_fileHandleHead == NULL)
    {
        assert(s_fileHandleTail == NULL);
        
        s_fileHandleHead = fileHandle;
        s_fileHandleTail = fileHandle;
    }
    else
    {
        assert(s_fileHandleTail != NULL);
        assert(s_fileHandleTail->next == NULL);
        
        s_fileHandleTail->next = fileHandle;
        fileHandle->prev = s_fileHandleTail;
        s_fileHandleTail = fileHandle;
    }
    m_s_fileHandleAddMutex.unlock();
}

int64_t File::GetLength(FileHandle* handle, int *error)
{
    if (handle->type != kFileTypeDisk)
    {
        *error = kErrorCodeInvalidHandle;
        return false;
    }
    
    struct stat statbuf;
    
    const int ret = fstat(handle->fd, &statbuf);
    
    if (ret == -1)
    {
        *error = File::FileErrnoToErrorCode(errno);
        return -1;
    }
    
    *error = kErrorCodeSuccess;
    
    return statbuf.st_size;
    }
ErrorCode File::FileErrnoToErrorCode(int32_t code)
{
    ErrorCode ret;
    /* mapping ideas borrowed from wine. they may need some work */
    
    switch (code)
    {
        case EACCES: case EPERM: case EROFS:
            ret = kErrorCodeAccessDenied;
            break;
            
        case EAGAIN:
            ret = kErrorCodeSharingViolation;
            break;
            
        case EBUSY:
            ret = kErrorCodeLockViolation;
            break;
            
        case EEXIST:
            ret = kErrorCodeFileExists;
            break;
            
        case EINVAL: case ESPIPE:
            ret = kErrorSeek;
            break;
            
        case EISDIR:
            ret = kErrorCodeCannotMake;
            break;
            
        case ENFILE: case EMFILE:
            ret = kErrorCodeTooManyOpenFiles;
            break;
            
        case ENOENT: case ENOTDIR:
            ret = kErrorCodeFileNotFound;
            break;
            
        case ENOSPC:
            ret = kErrorCodeHandleDiskFull;
            break;
            
        case ENOTEMPTY:
            ret = kErrorCodeDirNotEmpty;
            break;
            
        case ENOEXEC:
            ret = kErrorBadFormat;
            break;
            
        case ENAMETOOLONG:
            ret = kErrorCodeFileNameExcedRange;
            break;
            
#ifdef EINPROGRESS
        case EINPROGRESS:
            ret = kErrorIoPending;
            break;
#endif
            
        case ENOSYS:
            ret = kErrorNotSupported;
            break;
            
        case EBADF:
            ret = kErrorCodeInvalidHandle;
            break;
            
        case EIO:
            ret = kErrorCodeInvalidHandle;
            break;
            
        case EINTR:
            ret = kErrorIoPending;
            break;
            
        case EPIPE:
            ret = kErrorCodeWriteFault;
            break;
            
        default:
            ret = kErrorCodeGenFailure;
            break;
    }
    
    return ret;
    }

 FileHandle* File::Open(const std::string& path, int mode, int accessMode, int shareMode, int options, int *error)
{
    const int flags = ConvertFlags(accessMode, mode);
    
    /* we don't use sharemode, because that relates to sharing of
     * the file when the file is open and is already handled by
     * other code, perms instead are the on-disk permissions and
     * this is a sane default.
     */
    const mode_t perms = options & kFileOptionsTemporary ? 0600 : 0666;
    
    int fd = open(path.c_str(), flags, perms);
    
    /* If we were trying to open a directory with write permissions
     * (e.g. O_WRONLY or O_RDWR), this call will fail with
     * EISDIR. However, this is a bit bogus because calls to
     * manipulate the directory (e.g. SetFileTime) will still work on
     * the directory because they use other API calls
     * (e.g. utime()). Hence, if we failed with the EISDIR error, try
     * to open the directory again without write permission.
     */
    
    // Try again but don't try to make it writable
    if (fd == -1)
    {
        if (errno == EISDIR)
        {
            fd = open(path.c_str(), flags & ~(O_RDWR | O_WRONLY), perms);
            
            if (fd == -1)
            {
                *error = File::PathErrnoToErrorCode(path, errno);
                return INVALID_FILE_HANDLE;
            }
        }
        else
        {
            *error = File::PathErrnoToErrorCode(path, errno);
            return INVALID_FILE_HANDLE;
        }
    }
    
    struct stat statbuf;
    const int ret = fstat(fd, &statbuf);
    
    if (ret == -1)
    {
        *error = FileErrnoToErrorCode(errno);
        close(fd);
        return INVALID_FILE_HANDLE;
    }
    
    if (!ShareAllowOpen(statbuf, shareMode, accessMode))
    {
        *error = kErrorCodeSharingViolation;
        close(fd);
        return INVALID_FILE_HANDLE;
    }
    
    FileHandle* fileHandle = new FileHandle();
    fileHandle->fd = fd;
    fileHandle->path = path;
    fileHandle->options = options;
    fileHandle->accessMode = accessMode;
    fileHandle->shareMode = shareMode;
    
    fileHandle->device = statbuf.st_dev;
    fileHandle->inode = statbuf.st_ino;
    
    // Add to linked list
    AddFileHandle(fileHandle);
    
#ifdef HAVE_POSIX_FADVISE
    if (options & kFileOptionsSequentialScan)
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    if (options & kFileOptionsRandomAccess)
        posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
#endif
    
    if (S_ISFIFO(statbuf.st_mode))
        fileHandle->type = kFileTypePipe;
    else if (S_ISCHR(statbuf.st_mode))
        fileHandle->type = kFileTypeChar;
    else
        fileHandle->type = kFileTypeDisk;
    
    *error = kErrorCodeSuccess;
    
    return fileHandle;
}

bool File::Close(FileHandle* handle, int *error)
{
    if (handle->type == kFileTypeDisk && handle->options & kFileOptionsDeleteOnClose)
        unlink(handle->path.c_str());
    
    close(handle->fd);
    
    // Remove from linked list
    RemoveFileHandle(handle);
    
    delete handle;
    
    *error = kErrorCodeSuccess;
    
    return true;
}

ErrorCode File::PathErrnoToErrorCode(const std::string& path, int32_t code)
{
    if (code == ENOENT)
    {
        const std::string dirname(path);//暂时不考虑路径
        if(access(dirname.c_str(),F_OK) == 0)
        {
            return kErrorCodeFileNotFound;
        }
    }
    else
    {
        return FileErrnoToErrorCode(code);
    }
     return kErrorCodeFileNotFound;
}

}

