

#include "UserInterface.h"

UserInterface *UserInterface::instance = nullptr;

UserInterface *UserInterface::getInstance()
{
    if (!instance)
    {
        instance = new UserInterface;
    }
    return instance;
}

UserInterface::UserInterface()
{
    fileSystem = FileSystem::getInstance();
}

UserInterface::~UserInterface()
{
    FileSystem::revokeInstance();
}

void UserInterface::revokeInstance()
{
    delete instance;
    instance = nullptr;
}

void UserInterface::initialize()
{
    // 如果挂载失败,先格式化
    if (!fileSystem->mount())
    {
        std::cout << "mount failed!" << std::endl
                  << "begin format!" << std::endl;
        // 如果格式化失败,创建新磁盘
        if (!fileSystem->format(BLOCK_SIZE / 8))
        {
            std::cout << "format failed because there is no disk.\nStart creating a disk, please input disk size(MB):"
                      << std::flush;
            uint32_t disk_size;
            std::cin >> disk_size;
            fileSystem->createDisk(disk_size * 1024 * 1024);
            std::cout << "disk create success!" << std::endl;
            fileSystem->mount();
            fileSystem->format(BLOCK_SIZE / 8);
        }
        std::cout << "format success!" << std::endl;
    }
    // 读入根节点所在磁盘块
    uint32_t root_disk = fileSystem->getRootLocation();
    // 根节点
    INode rootInode{};
    // 从根节点所在磁盘块读入根节点信息
    fileSystem->read(root_disk, 0, reinterpret_cast<char *>(&rootInode), sizeof(rootInode));
    // 将根目录信息写入当前目录
    fileSystem->read(rootInode.bno, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    nowDiretoryDisk = rootInode.bno;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        fileOpenTable[i].fileNumber = 0;
        fileOpenTable[i].cursor = 0;
    }
}

void UserInterface::mkdir(uint8_t uid, std::string directoryName)
{
    int directoryIndex = -1;
    // 遍历所有目录项,找到空闲目录项
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        if (directory.item[i].inodeIndex == 0)
        {
            directoryIndex = i;
            break;
        }
    }
    // 目录项满了
    if (directoryIndex == -1)
    {
        std::cout << "directory full!" << std::endl;
        return;
    }

    // 重复文件检测
    if (duplicateDetection(directoryName))
    {
        std::cout << "mkdir:" << YELLOW << "cannot " << RESET << "create directory '" << directoryName << "':"
                  << "File exists" << std::endl;
        return;
    }

    // 给新目录的i结点分配空闲磁盘块
    uint32_t directoryInodeDisk = fileSystem->blockAllocate();
    // 给新目录的目录项信息分配空闲磁盘块
    uint32_t directoryDisk = fileSystem->blockAllocate();
    Directory newDirectory{};
    // 目录的第一项为本目录的信息
    newDirectory.item[0].inodeIndex = directoryInodeDisk;
    // 设置本目录./
    strcpy(newDirectory.item[0].name, ".");
    // 目录的第二项为上级目录的信息
    newDirectory.item[1].inodeIndex = directory.item[0].inodeIndex;
    // 设置上级目录../
    strcpy(newDirectory.item[1].name, "..");
    // 设置结束标记
    newDirectory.item[2].inodeIndex = 0;
    // 将目录项信息写入磁盘
    fileSystem->write(directoryDisk, 0, reinterpret_cast<char *>(&newDirectory), sizeof(newDirectory));

    // 新目录i节点
    INode directoryInode{};

    directoryInode.bno = directoryDisk;
    directoryInode.flag = 0x7f; // 01 111 111b
    directoryInode.uid = uid;
    fileSystem->write(directoryInodeDisk, 0, reinterpret_cast<char *>(&directoryInode), sizeof(directoryInode));

    // 更新当前目录目录项
    strcpy(directory.item[directoryIndex].name, directoryName.c_str());
    directory.item[directoryIndex].inodeIndex = directoryInodeDisk;

    // 将更新后的当前目录信息写入磁盘
    fileSystem->write(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));

    // 更新已分配磁盘块
    fileSystem->update();
}

void UserInterface::ls()
{
    // 遍历目录项数组，目录项总数上限为 DIRECTORY_NUMS
    for (int i = 0; i < DIRECTORY_NUMS && directory.item[i].inodeIndex != 0; i++)
    {
        // 如果当前目录项对应的 inodeIndex 可访问（judge 返回 true 表示可访问或高亮显示）
        if (judge(directory.item[i].inodeIndex))
        {
            // 打印文件/目录名，并用蓝色高亮显示，后面加一个制表符
            std::cout << BLUE << directory.item[i].name << RESET << "\t";
        }
        else
        {
            // 普通打印文件/目录名，不做高亮，后面加一个制表符
            std::cout << directory.item[i].name << "\t";
        }
    }
    // 输出换行，表示 ls 列表结束
    std::cout << std::endl;
}

void UserInterface::touch(uint8_t uid, std::string fileName)
{
    int directoryIndex = -1;
    // 遍历所有目录项,找到空闲目录项
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        if (directory.item[i].inodeIndex == 0)
        {
            directoryIndex = i;
            break;
        }
    }
    // 目录项满了
    if (directoryIndex == -1)
    {
        std::cout << "directory full!" << std::endl;
        return;
    }

    // 重复文件检测
    if (duplicateDetection(fileName))
    {
        std::cout << "touch:" << YELLOW << "cannot " << RESET << "create file '" << fileName << "':" << "File exists"
                  << std::endl;
        return;
    }
    // 给文件索引表分配空闲磁盘块
    uint32_t fileIndexDisk = fileSystem->blockAllocate();
    // 新文件i节点
    INode fileInode{};
    // 给新文件的i结点分配空闲磁盘块
    uint32_t fileInodeDisk = fileSystem->blockAllocate();
    fileInode.bno = fileIndexDisk;
    fileInode.flag = 0x3f; // 00 111 111b
    fileInode.uid = uid;
    // 把i结点写入磁盘
    fileSystem->write(fileInodeDisk, 0, reinterpret_cast<char *>(&fileInode), sizeof(fileInode));
    // 更新目录项信息
    strcpy(directory.item[directoryIndex].name, fileName.c_str());
    directory.item[directoryIndex].inodeIndex = fileInodeDisk;

    // 将更新后的当前目录信息写入磁盘
    fileSystem->write(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    FileIndex fileIndex{};   //文件索引表
    // 给文件分配空闲磁盘块
    uint32_t fileDisk = fileSystem->blockAllocate();
    fileIndex.index[0] = fileDisk;
    fileIndex.index[1] = 0;
    fileIndex.next = 0;

    // 把文件索引表写入磁盘
    fileSystem->write(fileIndexDisk, 0, reinterpret_cast<char *>(&fileIndex), sizeof(fileIndex));

    fileSystem->update();
}

bool UserInterface::duplicateDetection(std::string name)
{
    // 初始化目录项索引变量为 -1，表示暂时未找到重名项
    int dirLocation = -1;

    // 遍历当前目录的所有目录项，最大项数由 DIRECTORY_NUMS 决定
    for (int i = 0; i < DIRECTORY_NUMS; ++i)
    {
        // 如果当前目录项的 inodeIndex 为 0，说明到达空闲或结束位置，跳出循环
        if (directory.item[i].inodeIndex == 0)
        {
            break;
        }
        // 将目录项的名字与传入的 name 做字符串比较
        // strcmp 返回 0 表示两者相同
        if (std::strcmp(directory.item[i].name, name.c_str()) == 0)
        {
            // 找到重名项，将索引记录在 dirLocation 中并跳出循环
            dirLocation = i;
            break;
        }
    }

    // 如果 dirLocation 仍为 -1，说明未找到重名文件/目录，返回 false；否则返回 true
    if (dirLocation == -1)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool UserInterface::cd(std::string directoryName)
{
    int dirLocation = -1;

    // 遍历当前目录项数组，寻找与 directoryName 同名且可访问的目录项
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        // 如果 inodeIndex 为 0，表示该位置没有更多有效目录项，退出遍历
        if (directory.item[i].inodeIndex == 0)
        {
            break;
        }
        // 比较目录项名称与传入的 directoryName，同时调用 judge() 确认该目录项是可访问的目录
        if (std::strcmp(directory.item[i].name, directoryName.c_str()) == 0 &&
            judge(directory.item[i].inodeIndex))
        {
            // 找到匹配的目录项，记录其索引并跳出循环
            dirLocation = i;
            break;
        }
    }

    // 如果没有找到有效的同名目录项，输出错误提示并返回 false
    if (dirLocation == -1)
    {
        std::cout << "cd: " << directoryName << ": No such directory" << std::endl;
        return false;
    }

    // 获取要进入目录对应的 inode 索引（磁盘上该目录的 i-node 号）
    uint32_t directoryInodeDisk = directory.item[dirLocation].inodeIndex;

    // 从磁盘读取该目录的 INode 信息到 iNode 结构体
    INode iNode{};
    fileSystem->read(directoryInodeDisk, 0, reinterpret_cast<char *>(&iNode), sizeof(iNode));

    // 将当前目录磁盘块号更新为 iNode.bno（该目录在磁盘上存放的首数据块）
    nowDiretoryDisk = iNode.bno;

    // 从磁盘读取新的目录结构（directory 结构体）到内存
    fileSystem->read(iNode.bno, 0, reinterpret_cast<char *>(&directory), sizeof(directory));

    return true;
}

bool UserInterface::judge(uint32_t disk)
{
    INode iNode{};
    fileSystem->read(disk, 0, reinterpret_cast<char *>(&iNode), sizeof(iNode));
    if ((iNode.flag & 0xC0) == 0x40)
    { // 是目录，01,xxx,xxx & 11,000,000 == 01,000,000
        return true;
    }
    else if ((iNode.flag & 0xC0) == 0)
    { // 是文件，00,xxx,xxx & 11,000,000 == 0
        return false;
    }
    return false;
}

void UserInterface::rm(uint8_t uid, std::string fileName)
{
    int fileLocation = -1;
    // 遍历当前目录的目录项数组，查找名称匹配且不可再进入（即是文件而非目录）的项目
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        // 如果遇到 inodeIndex 为 0，说明后面没有更多有效项目，停止遍历
        if (directory.item[i].inodeIndex == 0)
        {
            break;
        }
        // strcmp 比较目录项名称与给定 fileName，并用 judge() 判断该 inode 代表的不是目录
        if (strcmp(directory.item[i].name, fileName.c_str()) == 0 && !judge(directory.item[i].inodeIndex))
        {
            // 找到匹配的文件项，将索引记下并退出循环
            fileLocation = i;
            break;
        }
    }

    // 如果遍历结束仍未找到对应文件，输出错误提示并返回
    if (fileLocation == -1)
    {
        std::cout << "rm: " << YELLOW << "cannot" << RESET << " remove '" << fileName << "': "
                  << "No such file" << std::endl;
        return;
    }

    // 找到文件对应的 INode，准备释放其所有占用的磁盘空间
    INode fileIndexInode{};
    fileSystem->read(
        directory.item[fileLocation].inodeIndex, // 传入找到的 inode 索引
        0,
        reinterpret_cast<char *>(&fileIndexInode), // 将磁盘上的 INode 数据读到 fileIndexInode
        sizeof(fileIndexInode));

    // 循环释放该文件的所有数据块或索引块，直到 fileIndexBlockFree 返回 0 表示没有下一个索引块
    uint32_t next = fileIndexBlockFree(fileIndexInode.bno);
    while (next != 0)
        next = fileIndexBlockFree(next);

    // 释放保存文件索引表的 INode 本身
    fileSystem->blockFree(directory.item[fileLocation].inodeIndex);

    // 从目录中移除该文件项：将后续目录项依次前移覆盖当前位置，以保持目录项数组连续
    wholeDirItemsMove(fileLocation);

    // 将修改后的目录结构写回到当前目录所在的磁盘块
    fileSystem->write(
        nowDiretoryDisk, // 当前目录所在磁盘块号
        0,
        reinterpret_cast<char *>(&directory), // 目录结构的新内容
        sizeof(directory));

    // 更新超级块等元信息，将本次删除操作的修改写回磁盘
    fileSystem->update();
}

uint32_t UserInterface::fileIndexBlockFree(uint32_t disk)
{
    // 读取磁盘上位于 'disk' 块号的 FileIndex 结构
    FileIndex fileIndex{};
    fileSystem->read(
        disk,                                 // 要读取的磁盘块号
        0,                                    // 块内偏移为 0（从块头开始）
        reinterpret_cast<char *>(&fileIndex), // 将数据读入 fileIndex 结构
        sizeof(fileIndex));                   // 读取整个 FileIndex 大小

    // 遍历 fileIndex.index 数组，直到遇到 0 或遍历完 FILE_INDEX_SIZE 项
    for (int i = 0; i < FILE_INDEX_SIZE && fileIndex.index[i] != 0; i++)
    {
        // 回收该文件数据块：fileIndex.index[i] 存储了一个数据块号
        fileSystem->blockFree(fileIndex.index[i]);
        // 将索引置 0，表示该数据块已经释放
        fileIndex.index[i] = 0;
    }

    // 保留下一块索引块的块号，以便递归释放
    uint32_t next = fileIndex.next;

    // 释放当前的索引块本身
    fileSystem->blockFree(disk);

    // 返回下一个索引块的块号（如果为 0，则表示没有后续索引块）
    return next;
}

void UserInterface::rmdir(uint8_t uid, std::string dirName)
{
    // 先查找对应目录
    int dirLocation = -1;
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        if (directory.item[i].inodeIndex == 0)
            break;
        if (strcmp(directory.item[i].name, dirName.c_str()) == 0 && judge(directory.item[i].inodeIndex))
        {
            dirLocation = i;
            break;
        }
    }
    if (dirLocation == -1)
    {
        std::cout << "rmdir: " << RED << "failed" << RESET << " to remove '" << dirName << "': " << RED << "No" << RESET
                  << " such directory" << std::endl;
        return;
    }
    // 保存当前目录所在磁盘块
    uint32_t nowDisk = nowDiretoryDisk;
    // 进入指定目录
    INode dirInode1{};
    fileSystem->read(directory.item[dirLocation].inodeIndex, 0, reinterpret_cast<char *>(&dirInode1),
                     sizeof(dirInode1));
    fileSystem->read(dirInode1.bno, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    nowDiretoryDisk = dirInode1.bno;

    // 一个目录的目录项如果只有前两项或者前一项(根目录),那么该层目录递归结束
    if (directory.item[1].inodeIndex == 0 || directory.item[2].inodeIndex == 0)
    {
        // 回收本层目录所有磁盘块
        fileSystem->blockFree(nowDiretoryDisk);
        //        return;
    }
    else
    {
        // 还有其他项
        // 如果是文件,就使用rm接口;如果是目录,就递归调用rmdir
        for (int i = 0; i < DIRECTORY_NUMS; i++)
        {
            if (directory.item[i].inodeIndex == 0)
                break;
            // 文件
            if (!judge(directory.item[i].inodeIndex))
                rm(uid, directory.item[i].name);
            // 目录
            else if (strcmp(directory.item[i].name, ".") != 0 && strcmp(directory.item[i].name, "..") != 0)
            {
                // 保存当前目录
                uint32_t nowDiretoryDisk1 = nowDiretoryDisk;
                INode dirInode{};
                fileSystem->read(directory.item[i].inodeIndex, 0, reinterpret_cast<char *>(&dirInode),
                                 sizeof(dirInode));
                // 进入下一目录
                fileSystem->read(dirInode.bno, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
                nowDiretoryDisk = dirInode.bno;
                // 回收该i结点
                fileSystem->blockFree(directory.item[i].inodeIndex);
                directory.item[i].inodeIndex = 0;
                // 递归删除
                rmdir(uid, directory.item[i].name);
                // 递归返回时重置当前目录
                fileSystem->read(nowDiretoryDisk1, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
                nowDiretoryDisk = nowDiretoryDisk1;
            }
        }
    }

    // 回收指定目录的i结点所在磁盘块
    fileSystem->blockFree(directory.item[dirLocation].inodeIndex);
    directory.item[dirLocation].inodeIndex = 0;
    // 重置当前目录
    fileSystem->read(nowDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    nowDiretoryDisk = nowDisk;
    // 更新目录项
    wholeDirItemsMove(dirLocation);
    // 将新的目录项写入磁盘
    fileSystem->write(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    fileSystem->update();
}

void UserInterface::wholeDirItemsMove(int itemLocation)
{
    // 如果恰好是最后一项
    if (itemLocation == DIRECTORY_NUMS - 1)
    {
        directory.item[itemLocation].inodeIndex = 0;
        return;
    }
    // 目录项整体前移
    for (int i = itemLocation; i < DIRECTORY_NUMS; i++)
    {
        if (directory.item[i].inodeIndex == 0)
            break;
        strcpy(directory.item[i].name, directory.item[i + 1].name);
        directory.item[i].inodeIndex = directory.item[i + 1].inodeIndex;
    }
}

void UserInterface::mv(std::vector<std::string> src, std::vector<std::string> des)
{
    /*查找源文件或者目录的i结点*/

    // 被移动的文件或者目录的i结点所在磁盘块号
    uint32_t srcInodeIndex = 0;
    Directory tmpDirSrc{};
    uint32_t tmpDirDiskSrc;
    int srcIndex;
    // 查找源文件所在的目录所在的磁盘块号以及对应目录项编号
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find src" << std::endl;
        return;
    }
    tmpDirDiskSrc = findRes.first;
    fileSystem->read(tmpDirDiskSrc, 0, reinterpret_cast<char *>(&tmpDirSrc), sizeof(tmpDirSrc));
    srcIndex = findRes.second;
    srcInodeIndex = tmpDirSrc.item[srcIndex].inodeIndex;
    if (srcInodeIndex == 0)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find src" << std::endl;
        return;
    }

    /*查找目的目录*/
    // 查找目的目录所在的目录所在的磁盘块号以及对应目录项编号
    auto findResDes = findDisk(des);
    Directory tmpDir{};
    uint32_t tmpDirDisk = findResDes.first;
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    uint32_t desInodeIndex = tmpDir.item[findResDes.second].inodeIndex;
    if (desInodeIndex == 0)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find des" << std::endl;
        return;
    }
    INode desInode{};
    fileSystem->read(desInodeIndex, 0, reinterpret_cast<char *>(&desInode), sizeof(desInode));
    tmpDirDisk = desInode.bno;
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    // 在目标目录查找空目录项
    int location = -1;
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        if (tmpDir.item[i].inodeIndex == 0)
        {
            location = i;
            break;
        }
    }
    if (location == -1)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":des directory full" << std::endl;
        return;
    }

    // 保存当前目录,设置当前目录为被移动的文件所在的目录
    std::swap(directory, tmpDirSrc);
    std::swap(nowDiretoryDisk, tmpDirDiskSrc);
    // 更新被移动的文件所在的目录
    wholeDirItemsMove(srcIndex);
    // 将新的目录项写入磁盘
    fileSystem->write(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    fileSystem->update();
    //    std::swap(directory, tmpDirSrc);
    std::swap(nowDiretoryDisk, tmpDirDiskSrc);
    fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
    strcpy(tmpDir.item[location].name, src.back().c_str());
    tmpDir.item[location].inodeIndex = srcInodeIndex;
    fileSystem->write(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    fileSystem->update();
}

std::pair<uint32_t, int> UserInterface::findDisk(std::vector<std::string> src)
{
    // 获取名
    std::string srcName = src.back();
    src.pop_back();
    uint32_t tmpDirectoryDisk = nowDiretoryDisk;
    bool ok = true;      // 能否找到目录
    std::string dirName; // 输出错误信息用
    // 在此次直接调用cd函数来寻找
    for (std::string &item : src)
    {
        if (item == "")
        {
            // 空，直接寻找根目录
            goToRoot();
        }
        else if (item == "..")
        {
            // 上级
            ok = cd(item);
        }
        else if (item == ".")
        {
            // 当前
            continue;
        }
        else
        {
            ok = cd(item);
        }
        if (!ok)
        {
            dirName = item; // 记录哪一步出错了
            break;
        }
    }
    if (!ok)
    {
        std::cout << RED << "failed: " << RESET << "'" << dirName << "' No such directory" << std::endl;
        // 还原现场
        nowDiretoryDisk = tmpDirectoryDisk;
        fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof directory);
        return std::make_pair(-1, -1);
    }
    int location = -1;
    if (srcName != "")
    {
        // 找到对应i结点
        for (int i = 0; i < DIRECTORY_NUMS; i++)
        {
            if (directory.item[i].inodeIndex == 0)
                break;
            if (strcmp(directory.item[i].name, srcName.c_str()) == 0)
            {
                location = i;
                break;
            }
        }
        if (location == -1)
        {
            std::cout << RED << "failed " << RESET << "'" << srcName << "' No such directory or file" << std::endl;
            // 还原现场
            nowDiretoryDisk = tmpDirectoryDisk;
            fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof directory);
            return std::make_pair(-1, -1);
        }
    }
    else
    {
        goToRoot();
        location = 0;
    }
    // 记录下需要返回的数据
    std::pair<uint32_t, int> ret = std::make_pair(nowDiretoryDisk, location);
    // 还原现场
    nowDiretoryDisk = tmpDirectoryDisk;
    fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof directory);
    return ret;
}

void UserInterface::rename(std::vector<std::string> src, std::string newName)
{
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        std::cout << "rename: " << RED << "failed" << RESET << ":cannot find src" << std::endl;
        return;
    }
    // 找到需要被改名的文件或者目录所在的目录,和其对应的目录项编号
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    int dirLocation = findRes.second;
    strcpy(tmpDir.item[dirLocation].name, newName.c_str());
    fileSystem->write(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
}

uint8_t UserInterface::userVerify(std::string &username, std::string &password)
{
    return fileSystem->userVerify(username, password);
}

void UserInterface::getUser(uint8_t uid, User *user)
{
    fileSystem->getUser(uid, user);
}

void UserInterface::format()
{
    // 调用底层文件系统的 format 方法，传入块数量（BLOCK_SIZE/8 表示每个块能存放的 INode 数量或类似含义）
    fileSystem->format(BLOCK_SIZE / 8);

    // 更新超级块等元数据，将格式化操作写回磁盘
    fileSystem->update();

    // 读取根目录的 INode（超级块里的 rootLocation 指定了根目录 INode 所在位置）
    INode iNode{};
    fileSystem->read(
        fileSystem->getRootLocation(),    // 根目录 INode 在磁盘上的块号
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 将读取结果写入 iNode 结构
        sizeof(iNode));                   // 读取整个 INode 大小

    // 将当前目录指针设置为根目录 INode 所指向的数据块（目录内容所在磁盘块）
    nowDiretoryDisk = iNode.bno;

    // 从磁盘读取根目录的数据到 directory 结构中，以便后续 ls、cd 等操作使用
    fileSystem->read(
        nowDiretoryDisk,                      // 根目录内容所在磁盘块号
        0,                                    // 块内偏移 0
        reinterpret_cast<char *>(&directory), // 将读取结果写入 directory 结构
        sizeof(directory));                   // 读取整个目录结构大小
}

void UserInterface::chmod(std::string who, std::string how, std::vector<std::string> src)
{
    bool hasU = false, hasO = false, hasA = false;
    if (who.find('a') != std::string::npos)
        hasA = true;
    if (who.find('o') != std::string::npos)
        hasO = true;
    if (who.find('u') != std::string::npos)
        hasU = true;
    bool hasR = false, hasW = false, hasX = false;
    if (how.find('r') != std::string::npos)
        hasR = true;
    if (how.find('w') != std::string::npos)
        hasW = true;
    if (how.find('x') != std::string::npos)
        hasX = true;
    uint8_t rwxResult = 0xc0; // 11 000 000
    uint8_t a_r = 0xe4;       // r-- 11 100 100
    uint8_t a_w = 0xd2;       //-w- 11 010 010
    uint8_t a_x = 0xc9;       //--r 11 001 001
    uint8_t u_r = 0xE7;       // r-- 11 100 111
    uint8_t u_w = 0xD7;       //-w- 11 010 111
    uint8_t u_x = 0xCF;       //--x 11 001 111
    uint8_t o_r = 0xFC;       // r-- 11 111 100
    uint8_t o_w = 0xFA;       //-w- 11 111 010
    uint8_t o_x = 0xF9;       //--x 11 111 001
    if (hasA)
    {
        if (hasR)
            rwxResult |= a_r;
        if (hasW)
            rwxResult |= a_w;
        if (hasX)
            rwxResult |= a_x;
    }
    else
    {
        if (hasU)
        {
            if (hasR)
                rwxResult |= u_r;
            if (hasW)
                rwxResult |= u_w;
            if (hasX)
                rwxResult |= u_x;
        }
        if (hasO)
        {
            if (hasR)
                rwxResult |= o_r;
            if (hasW)
                rwxResult |= o_w;
            if (hasX)
                rwxResult |= o_x;
        }
    }
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        std::cout << "chmod: " << RED << "failed" << RESET << ":no such file" << std::endl;
        return;
    }
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    fileSystem->write(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    INode iNode{};
    fileSystem->read(tmpDir.item[findRes.second].inodeIndex, 0, reinterpret_cast<char *>(&iNode), sizeof(iNode));
    iNode.flag &= rwxResult;
    fileSystem->write(tmpDir.item[findRes.second].inodeIndex, 0, reinterpret_cast<char *>(&iNode), sizeof(iNode));
}

void UserInterface::cd(std::vector<std::string> src)
{
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        std::cout << "cd: " << RED << "failed" << RESET << ":no such directory" << std::endl;
        return;
    }
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    uint32_t InodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode dirInode{};
    fileSystem->read(InodeDisk, 0, reinterpret_cast<char *>(&dirInode), sizeof(dirInode));
    nowDiretoryDisk = dirInode.bno;
    fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
}

void UserInterface::mkdir(uint8_t uid, std::vector<std::string> src, std::string dirName)
{
    // 根据传入的路径 src（各级目录名），查找对应的磁盘块和目录项索引
    // findRes.first  = 父目录所在的磁盘块号
    // findRes.second = 在该目录中对应 src 最后一个元素的目录项索引
    auto findRes = findDisk(std::move(src));
    if (findRes.first == -1)
    {
        // 如果未找到对应目录，输出错误并返回
        std::cout << "mkdir: " << RED << "failed" << RESET << ": no such directory" << std::endl;
        return;
    }

    // 首先将 findRes.first 作为临时目录块号读取该目录结构
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    fileSystem->read(
        tmpDirDisk,                        // 读取该磁盘块
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 结构中
        sizeof(tmpDir));                   // 读取整个目录结构大小

    // 从刚才读取的目录中，根据 findRes.second 获取目标子目录的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;

    // 读取该 inode，拿到它的 bno，即“该子目录内容所在的数据块号”
    INode iNode{};
    fileSystem->read(
        inodeDisk,                        // 子目录对应的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 将读取结果写入 iNode 结构
        sizeof(iNode));                   // 读取整个 INode 大小

    // 将 tmpDirDisk 更新为子目录数据块号（iNode.bno 指向实际的目录内容块）
    tmpDirDisk = iNode.bno;

    // 读取子目录对应的数据块，将该目录内容加载到 tmpDir 中
    fileSystem->read(
        tmpDirDisk,                        // 子目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 将内容读入 tmpDir
        sizeof(tmpDir));                   // 读取整个目录结构大小

    // 交换当前目录与 tmpDir，使得后续调用 mkdir(uid, dirName) 时，当前目录被切换到目标父目录
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);

    // 在刚才切换到的目录下创建名为 dirName 的新目录
    // 这里调用了重载的 mkdir(uid, dirName)，它只需在“当前目录”下建目录
    mkdir(uid, std::move(dirName));

    // 创建完成后，再将 directory 和 nowDiretoryDisk 交换回去，恢复原来的“当前目录”状态
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);
}

void UserInterface::ls(std::vector<std::string> src)
{
    // 首先通过 findDisk 查找传入路径 src 对应的目录块和目录项索引
    // findRes.first  = 目录所在磁盘块号
    // findRes.second = 该目录在目录块中的目录项索引
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        // 如果未找到对应目录，输出错误提示并返回
        std::cout << "ls: " << RED << "failed" << RESET << ": no such directory" << std::endl;
        return;
    }

    // 将找到的目录块号保存到临时变量 tmpDirDisk
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    // 读取该目录块内容到 tmpDir 结构中
    fileSystem->read(
        tmpDirDisk,                        // 目标目录块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 中
        sizeof(tmpDir));                   // 大小为整个目录结构

    // 根据 findRes.second 获取该目录项对应的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode iNode{};
    // 读取该 inode，拿到其 bno（目录内容所在的数据块号）
    fileSystem->read(
        inodeDisk,                        // 目录项对应的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 读取到 iNode 中
        sizeof(iNode));                   // 大小为整个 INode 结构

    // 将 tmpDirDisk 更新为子目录实际存放内容的块号
    tmpDirDisk = iNode.bno;
    // 读取子目录内容到 tmpDir 中
    fileSystem->read(
        tmpDirDisk,                        // 子目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 中
        sizeof(tmpDir));                   // 大小为整个目录结构

    // 交换当前目录和 tmpDir，使得后续调用 ls() 时在目标目录下执行
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);

    // 在刚才切换到的目录下执行 ls，无参版本会列出当前目录的所有项
    ls();

    // ls 完成后，再将目录恢复为原来的“当前目录”
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);
}

void UserInterface::touch(uint8_t uid, std::vector<std::string> src, std::string fileName)
{
    // 使用 findDisk 查找传入路径 src 对应的目录块号和目录项索引
    // findRes.first  = 目录所在的磁盘块号
    // findRes.second = 该目录在目录块中的目录项索引
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        // 未找到对应目录，输出错误提示并返回
        std::cout << "touch: " << RED << "failed" << RESET << ": no such directory" << std::endl;
        return;
    }

    // 将找到的父目录块号保存到临时变量 tmpDirDisk
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    // 从磁盘读取该目录结构到 tmpDir
    fileSystem->read(
        tmpDirDisk,                        // 目标目录块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 结构
        sizeof(tmpDir));                   // 读取整个目录结构大小

    // 根据 findRes.second 获取该目录项对应的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode iNode{};
    // 读取该 inode，拿到其 bno 字段（指向目录内容所在的数据块号）
    fileSystem->read(
        inodeDisk,                        // 目录项对应的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 将数据读到 iNode 结构
        sizeof(iNode));                   // 读取整个 INode 大小

    // 更新 tmpDirDisk 为子目录实际存放内容的块号（iNode.bno）
    tmpDirDisk = iNode.bno;
    // 从磁盘读取子目录内容到 tmpDir
    fileSystem->read(
        tmpDirDisk,                        // 子目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 结构
        sizeof(tmpDir));                   // 读取整个目录结构大小

    // 将当前目录与 tmpDir 交换，使得下面调用无参 touch(uid, fileName) 时
    // 作用于刚才找到的目标目录
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);

    // 在已经切换到的目标目录下调用无参版本的 touch，创建或更新文件
    touch(uid, fileName);

    // 创建完成后，将目录状态恢复到原来的“当前目录”
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);
}

void UserInterface::rm(uint8_t uid, std::vector<std::string> src, std::string fileName)
{
    // 使用 findDisk 查找 src 指定路径对应的目录块号和目录项索引
    // findRes.first  = 目录所在的磁盘块号
    // findRes.second = 该目录在目录块中的目录项索引
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        // 若未找到对应目录，提示错误并返回
        std::cout << "rm: " << RED << "failed" << RESET << ": no such directory" << std::endl;
        return;
    }

    // 将找到的父目录块号保存到临时变量 tmpDirDisk
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    // 从磁盘读取该父目录内容到 tmpDir 结构
    fileSystem->read(
        tmpDirDisk,                        // 父目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 将内容读入 tmpDir
        sizeof(tmpDir));                   // 大小为整个目录结构

    // 根据 findRes.second 得到目标子目录或文件所在的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode iNode{};
    // 读取该 inode，获取其 bno 字段（指向子目录内容所在的数据块号）
    fileSystem->read(
        inodeDisk,                        // 目标条目对应的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 将数据读入 iNode
        sizeof(iNode));                   // 大小为整个 INode 结构

    // 更新 tmpDirDisk 为子目录实际存放内容的块号
    tmpDirDisk = iNode.bno;
    // 从磁盘读取子目录内容到 tmpDir
    fileSystem->read(
        tmpDirDisk,                        // 子目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 将内容读入 tmpDir
        sizeof(tmpDir));                   // 大小为整个目录结构

    // 交换当前目录与 tmpDir，使得随后调用无参版本 rm(uid, fileName) 时
    // 实际在刚才找到的目标目录下执行删除操作
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);

    // 在切换到的目标目录下执行 rm(uid, fileName)，删除指定文件
    rm(uid, fileName);

    // 删除完成后，将目录状态恢复到原来的“当前目录”
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);
}

void UserInterface::rmdir(uint8_t uid, std::vector<std::string> src, std::string dirName)
{
    // 使用 findDisk 查找传入路径 src 对应的父目录块号和目录项索引
    // findRes.first  = 父目录所在的磁盘块号
    // findRes.second = 在该父目录中 src 对应目录项的索引
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        // 如果未找到对应目录，输出错误提示并返回
        std::cout << "rmdir: " << RED << "failed" << RESET << ": no such directory" << std::endl;
        return;
    }

    // 将找到的父目录块号保存在临时变量 tmpDirDisk
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    // 从磁盘读取该父目录的内容到临时目录结构 tmpDir 中
    fileSystem->read(
        tmpDirDisk,                        // 父目录所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 将数据读入 tmpDir
        sizeof(tmpDir));                   // 大小为整个目录结构

    // 从 tmpDir 中根据 findRes.second 获取目标子目录的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode iNode{};
    // 读取该子目录对应的 INode，获取其 bno（表示该目录内容所在的数据块号）
    fileSystem->read(
        inodeDisk,                        // 目标子目录的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 将数据读入 iNode
        sizeof(iNode));                   // 读取整个 INode 大小

    // 更新 tmpDirDisk 为子目录实际存放内容的块号（iNode.bno）
    tmpDirDisk = iNode.bno;
    // 读取子目录内容到临时目录结构 tmpDir 中
    fileSystem->read(
        tmpDirDisk,                        // 子目录内容所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 将数据读入 tmpDir
        sizeof(tmpDir));                   // 读取整个目录结构大小

    // 将当前目录与 tmpDir 交换，使得接下来调用的无参 rmdir(uid, dirName)
    // 实际作用于刚才定位到的目标子目录
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);

    // 在目标子目录下调用无参版本的 rmdir，删除指定名称的子目录
    rmdir(uid, dirName);

    // 完成后，将 directory 和 nowDiretoryDisk 恢复成原来所在的父目录状态
    std::swap(directory, tmpDir);
    std::swap(nowDiretoryDisk, tmpDirDisk);
}

int UserInterface::judge(std::vector<std::string> src)
{
    // 提取路径列表最后一个元素作为目标名称（文件或目录名）
    std::string srcName = src.back();
    src.pop_back(); // 从路径列表中移除最后一个元素

    // tmpDirectoryDisk 用来暂存当前目录所在的磁盘块号，初始为当前目录
    uint32_t tmpDirectoryDisk = nowDiretoryDisk;
    // tmpDirectory 用来临时存储每次读取的目录内容
    Directory tmpDirectory = directory;

    // 先处理路径中的中间目录部分，逐层向下遍历
    while (!src.empty())
    {
        // 取出下一层的目录名
        std::string dirName = src.front();
        src.erase(src.begin()); // 将该目录名从列表中移除

        int dirLocation = -1; // 用来记录在当前目录中找到的目录项索引

        // 从磁盘读取 tmpDirectoryDisk 指定块号的目录数据到 tmpDirectory
        fileSystem->read(
            tmpDirectoryDisk,                        // 要读取的块号
            0,                                       // 块内偏移 0
            reinterpret_cast<char *>(&tmpDirectory), // 读取到 tmpDirectory 结构中
            sizeof(tmpDirectory));                   // 读取整个目录结构大小

        // 在 tmpDirectory 中查找名称为 dirName 的目录项
        for (int i = 0; i < DIRECTORY_NUMS; i++)
        {
            // 如果遇到 inodeIndex 为 0，说明后面没有更多有效目录项，停止遍历
            if (tmpDirectory.item[i].inodeIndex == 0)
                break;

            // strcmp 比较目录项名称与 dirName，并调用重载的 judge(inodeIndex) 检查该项是否是目录
            if (strcmp(tmpDirectory.item[i].name, dirName.c_str()) == 0 &&
                judge(tmpDirectory.item[i].inodeIndex))
            {
                // 找到匹配且是目录的项目，将索引记为 dirLocation 并退出循环
                dirLocation = i;
                break;
            }
        }

        // 如果在这一层没有找到名为 dirName 的目录，打印错误并返回 0（表示路径错误）
        if (dirLocation == -1)
        {
            std::cout << RED << "failed " << RESET << "'" << dirName << "' No such directory" << std::endl;
            return 0;
        }

        // 读取该目录项对应的 INode，以获取它的 bno（即子目录内容块号）
        INode dirInode{};
        fileSystem->read(
            tmpDirectory.item[dirLocation].inodeIndex, // 目录项的 INode 索引
            0,                                         // 块内偏移 0
            reinterpret_cast<char *>(&dirInode),       // 读取到 dirInode 结构
            sizeof(dirInode));                         // 读取整个 INode 大小

        // 更新 tmpDirectoryDisk 为子目录实际存放内容的块号（dirInode.bno）
        tmpDirectoryDisk = dirInode.bno;
        // 从磁盘读取该子目录内容到 tmpDirectory，准备进入下一层循环
        fileSystem->read(
            tmpDirectoryDisk,                        // 子目录内容所在块号
            0,                                       // 块内偏移 0
            reinterpret_cast<char *>(&tmpDirectory), // 将数据读入 tmpDirectory
            sizeof(tmpDirectory));                   // 读取整个目录结构大小
    }

    // 到达了目标名称的父目录，现在在 tmpDirectory 中查找最终的文件/目录名 srcName
    int location = -1;
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        // 如果 inodeIndex 为 0，则表示没有更多有效目录项，停止遍历
        if (tmpDirectory.item[i].inodeIndex == 0)
            break;

        // 比较目录项名称与 srcName，找到后记录索引并退出循环
        if (strcmp(tmpDirectory.item[i].name, srcName.c_str()) == 0)
        {
            location = i;
            break;
        }
    }

    // 如果父目录中没有找到 srcName，打印错误并返回 0（表示该文件或目录不存在）
    if (location == -1)
    {
        std::cout << RED << "failed " << RESET << "'" << srcName << "' No such file or directory" << std::endl;
        return 0;
    }

    // 读取目标文件/目录对应的 INode，以便判断它到底是文件还是目录
    INode iNode{};
    fileSystem->read(
        tmpDirectory.item[location].inodeIndex, // 目标项的 INode 索引
        0,                                      // 块内偏移 0
        reinterpret_cast<char *>(&iNode),       // 读取到 iNode 结构
        sizeof(iNode));                         // 读取整个 INode 大小

    // 调用重载的 judge(bno) 来判断 iNode.bno 指向的块是目录还是文件
    // 如果返回 true（非 0），表示是目录，函数返回 2
    // 否则返回 1，表示是普通文件
    if (judge(iNode.bno))
        return 2;
    else
        return 1;
}

void UserInterface::goToRoot()
{
    INode iNode{};
    fileSystem->read(fileSystem->getRootLocation(), 0, reinterpret_cast<char *>(&iNode), sizeof(iNode));
    nowDiretoryDisk = iNode.bno;
    fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
}

void UserInterface::open(std::string how, std::vector<std::string> src)
{
    // 解析打开模式，判断是否包含读（'r'）或写（'w'）标志
    bool hasR = false, hasW = false;
    if (how.find('r') != std::string::npos)
        hasR = true;
    if (how.find('w') != std::string::npos)
        hasW = true;

    // rwResult 用于记录最终的读/写权限位：
    // bit1 表示读权限，bit0 表示写权限
    uint8_t rwResult = 0x00; // 00000000
    uint8_t _r = 0x02;       // 00000010
    uint8_t _w = 0x01;       // 00000001
    if (hasR)
        rwResult |= _r; // 如果需要读，则在 rwResult 上 OR 上读位
    if (hasW)
        rwResult |= _w; // 如果需要写，则在 rwResult 上 OR 上写位

    // 根据传入的路径 src 查找对应文件所在的目录块号和目录项索引
    auto findRes = findDisk(src);
    if (findRes.first == -1)
    {
        // 如果没找到对应文件，输出错误并返回
        std::cout << "open: " << RED << "failed" << RESET << ": no such file" << std::endl;
        return;
    }

    // 先将父目录块号存于 tmpDirDisk
    uint32_t tmpDirDisk = findRes.first;
    Directory tmpDir{};
    // 读取该父目录的数据到 tmpDir
    fileSystem->read(
        tmpDirDisk,                        // 目录所在块号
        0,                                 // 块内偏移 0
        reinterpret_cast<char *>(&tmpDir), // 读取到 tmpDir 结构
        sizeof(tmpDir));                   // 读取整个目录结构

    // 获取目标文件对应的 inode 索引
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    INode iNode{};
    // 读取该 inode，获取其元信息（以便后面存入打开表）
    fileSystem->read(
        inodeDisk,                        // 文件对应的 inode 索引
        0,                                // 块内偏移 0
        reinterpret_cast<char *>(&iNode), // 读取到 iNode 结构
        sizeof(iNode));                   // 读取整个 INode 大小

    // 保存文件名，以便放入打开表中的 fileName 字段
    std::string fileName = tmpDir.item[findRes.second].name;
    // fileNumber 直接用 inodeDisk 作为该文件在打开表中的唯一标识
    uint32_t fileNumber = inodeDisk;

    // 再次读取文件索引表的 iNode，以保证 iNode 内含索引块信息等（此处与上面类似）
    fileSystem->read(
        tmpDir.item[findRes.second].inodeIndex, // 同样的 inode 索引
        0,
        reinterpret_cast<char *>(&iNode),
        sizeof(iNode));

    // 在文件打开表（fileOpenTable）中寻找一个空闲槽或已打开同一文件的冲突
    int fileLocation = -1;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        // 如果该文件已经被打开，则报错并返回
        if (fileOpenTable[i].fileNumber == fileNumber)
        {
            std::cout << "open: " << RED << "failed" << RESET
                      << ": file '" << src.back() << "' already opened" << std::endl;
            return;
        }
        // 如果该槽位空闲（fileNumber == 0），记录索引并跳出循环
        if (fileOpenTable[i].fileNumber == 0)
        {
            fileLocation = i;
            break;
        }
    }

    // 如果没有找到空闲槽，说明打开表已满，返回错误
    if (fileLocation == -1)
    {
        std::cout << "open: " << RED << "failed" << RESET << ": fileOpenTable full" << std::endl;
        return;
    }

    // 在找到的空闲槽位填充打开表项：
    // 1. 将文件名拷贝到打开表
    strcpy(fileOpenTable[fileLocation].fileName, fileName.c_str());
    // 2. 将文件编号（即 inode 索引）记录到 fileNumber
    fileOpenTable[fileLocation].fileNumber = fileNumber;
    // 3. 将读取到的 INode 复制到打开表，用于后续读写时参考
    fileOpenTable[fileLocation].iNode = iNode;
    // 4. 将游标（cursor）初始化为 0，表示从文件开头读/写
    fileOpenTable[fileLocation].cursor = 0;
    // 5. 将权限标志（flag）设置为之前计算好的 rwResult
    fileOpenTable[fileLocation].flag = rwResult;
}

// 关闭打开的文件
void UserInterface::close(std::vector<std::string> src)
{
    // 在目录中查找给定路径对应的磁盘位置和目录项索引
    auto findRes = findDisk(src);
    // 如果未找到文件（返回的磁盘编号为 -1），则提示失败并返回
    if (findRes.first == -1)
    {
        std::cout << "close: " << RED << "failed" << RESET << ":no such file" << std::endl;
        return;
    }

    // 获取文件所在的磁盘编号
    uint32_t tmpDirDisk = findRes.first;
    // 从磁盘中读取该目录的数据结构
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    // 根据目录项索引获取对应的 inode 索引 (i-node 编号)
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    // 将 inode 编号作为文件标识
    uint32_t fileNumber = inodeDisk;

    // 在文件打开表中查找该文件是否已被打开
    int fileLocation = -1;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        if (fileOpenTable[i].fileNumber == fileNumber)
        {
            fileLocation = i;
            break;
        }
    }
    // 如果没有在打开表中找到对应的条目，说明该文件未打开
    if (fileLocation == -1)
    {
        std::cout << "close: " << RED << "failed" << RESET << ":no such file opened" << std::endl;
        return;
    }

    // 如果该文件在打开时是以写标志（0x04）打开的，需要将内存中 i-node 的信息写回磁盘
    if ((fileOpenTable[fileLocation].flag & (0x04)) != 0)
    {
        fileSystem->write(
            fileOpenTable[fileLocation].fileNumber,                       // 写回的 i-node 对应的文件编号
            0,                                                            // 偏移量设为 0，写回整个 i-node 结构
            reinterpret_cast<char *>(&fileOpenTable[fileLocation].iNode), // i-node 数据
            sizeof(fileOpenTable[fileLocation].iNode)                     // i-node 大小
        );
    }

    // 清空文件打开表中对应位置的文件编号，表示该文件已被关闭
    fileOpenTable[fileLocation].fileNumber = 0;
}

// 设置文件操作的光标位置
void UserInterface::setCursor(int code, std::vector<std::string> src, uint32_t offset)
{
    // 在目录中查找给定路径对应的磁盘号和目录项索引
    auto findRes = findDisk(src);
    // 如果未找到该文件，提示失败并返回
    if (findRes.first == -1)
    {
        std::cout << "setCursor: " << RED << "failed" << RESET << ":no such file" << std::endl;
        return;
    }

    // 获取文件所在的磁盘编号
    uint32_t tmpDirDisk = findRes.first;
    // 从磁盘中读取该目录的数据结构
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    // 根据目录项索引获取对应的 i-node 编号
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    // 将 i-node 编号作为文件标识
    uint32_t fileNumber = inodeDisk;

    // 在文件打开表中查找该文件对应的打开项
    int fileLocation = -1;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        if (fileOpenTable[i].fileNumber == fileNumber)
        {
            fileLocation = i;
            break;
        }
    }
    // 如果该文件未在打开表中找到，说明文件尚未打开，提示错误并返回
    if (fileLocation == -1)
    {
        std::cout << "setCursor: " << RED << "failed" << RESET << ":no such file opened" << std::endl;
        return;
    }

    // code == 1：在当前光标位置的基础上向后移动 offset
    if (code == 1)
    {
        // 增加光标偏移
        fileOpenTable[fileLocation].cursor += offset;
        // 确保光标不超过文件容量（i-node.capacity）
        fileOpenTable[fileLocation].cursor = std::min(
            fileOpenTable[fileLocation].cursor,
            fileOpenTable[fileLocation].iNode.capacity);
    }
    // code == 2：将光标直接设置为 offset
    if (code == 2)
    {
        // 直接将光标设为 offset
        fileOpenTable[fileLocation].cursor = offset;
        // 确保光标不超过文件容量（i-node.capacity）
        fileOpenTable[fileLocation].cursor = std::min(
            fileOpenTable[fileLocation].cursor,
            fileOpenTable[fileLocation].iNode.capacity);
    }
}

void UserInterface::updateDirNow()
{
    fileSystem->read(nowDiretoryDisk, 0, reinterpret_cast<char *>(&directory), sizeof(directory));
}

// 从已打开的文件中读取数据
void UserInterface::read(uint8_t uid, std::vector<std::string> src, char *buf, uint16_t sz)
{
    // 先将缓冲区首字符设置为终止符，防止之前内容干扰
    buf[0] = '\0';

    // 在目录中查找给定路径 src 对应的磁盘编号和目录项索引
    auto findRes = findDisk(src);
    // 如果未找到该文件，打印错误信息并返回
    if (findRes.first == -1)
    {
        std::cout << "read: " << RED << "failed" << RESET << ":no such file" << std::endl;
        return;
    }

    // 获取找到的目录所在的磁盘编号
    uint32_t tmpDirDisk = findRes.first;
    // 从磁盘中读取该目录的数据结构
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    // 根据目录项索引取得对应的 i-node 编号（inodeIndex）
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    // 将 i-node 编号作为文件唯一标识（fileNumber）
    uint32_t fileNumber = inodeDisk;

    // 在文件打开表中查找该文件是否已经打开，找出它在打开表中的索引 fileLocation
    int fileLocation = -1;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        if (fileOpenTable[i].fileNumber == fileNumber)
        {
            fileLocation = i;
            break;
        }
    }
    // 如果没有找到，说明该文件尚未打开，打印错误并返回
    if (fileLocation == -1)
    {
        std::cout << "read: " << RED << "failed" << RESET << ":no such file opened" << std::endl;
        return;
    }

    // 获取该文件的总容量和当前光标位置的引用
    auto &capacity = fileOpenTable[fileLocation].iNode.capacity;
    auto &cursor = fileOpenTable[fileLocation].cursor;
    // 如果请求读取的字节数超过剩余容量，则调整 sz 为剩余可读字节数
    if (cursor + sz > capacity)
    {
        sz = capacity - cursor;
    }
    // 在目标缓冲区中为字符串结尾留出空间
    buf[sz] = '\0';

    // 读取该文件的第一个索引表（FileIndex 结构），它存储了该文件各数据块的块号
    FileIndex fileIndexTable{};
    fileSystem->read(
        fileOpenTable[fileLocation].iNode.bno,     // 索引表所在的块号
        0,                                         // 从块内偏移 0 开始读取
        reinterpret_cast<char *>(&fileIndexTable), // 读取到内存中的 fileIndexTable
        sizeof(fileIndexTable)                     // 读取大小为索引表结构体大小
    );

    // 当前光标位置
    uint32_t nowCursor = fileOpenTable[fileLocation].cursor;
    // 计算光标越过了多少个完整的索引表（每个索引表能存储 FILE_INDEX_SIZE * BLOCK_SIZE_BYTE 字节的信息）
    int fileIndexTableNums = nowCursor / (FILE_INDEX_SIZE * BLOCK_SIZE_BYTE);
    // 计算在当前索引表中对应的是第几个数据块（索引表的下标）
    int fileIndexNums = (nowCursor % (FILE_INDEX_SIZE * BLOCK_SIZE_BYTE)) / BLOCK_SIZE_BYTE;

    // 如果光标不在第一个索引表，则不断跳到下一个索引表中，直到找到光标所在的那个索引表
    while (fileIndexTableNums > 0)
    {
        uint32_t nextFileIndexTableBlock = fileIndexTable.next; // 下一个索引表所在的块号
        fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
        fileIndexNums--; // 因为已经越过了一个完整的索引表，索引下标要往前递减
        fileIndexTableNums--;
    }

    // 文件当前光标所在的数据块号
    uint32_t cursorBlock = fileIndexTable.index[fileIndexNums];
    // 计算光标在当前数据块内的偏移量
    uint16_t offset = nowCursor % BLOCK_SIZE_BYTE;
    // 计算当前数据块从光标位置到块末尾还剩余多少字节
    uint16_t resBlockSz = BLOCK_SIZE_BYTE - offset;

    // 已经读取的字节数
    uint16_t readByte = 0;

    // 如果请求读取的大小 sz 小于等于当前块的剩余字节数
    if (sz <= resBlockSz)
    {
        // 直接从当前块的 offset 位置读取 sz 个字节到 buf
        fileSystem->read(cursorBlock, offset, buf, sz);
        readByte = sz;                                  // 本次读取的字节数就是 sz
        buf += readByte;                                // 更新 buf 指针，后续可在 buf 后继续写入
        fileOpenTable[fileLocation].cursor += readByte; // 更新文件的光标位置
        return;                                         // 读取完成后直接返回
    }

    // 否则，先把当前块剩余的 resBlockSz 字节读完
    fileSystem->read(cursorBlock, offset, buf, resBlockSz);
    readByte = resBlockSz;                          // 已读字节数等于当前块剩余字节数
    buf += readByte;                                // 更新 buf 指针
    fileOpenTable[fileLocation].cursor += readByte; // 更新光标位置

    // 计算剩余还需要读取的字节数 ResByte（超过当前块的部分）
    uint16_t ResByte = sz - resBlockSz;

    // 计算剩余字节数需要多少个完整的数据块
    int blocksToRead = ResByte / BLOCK_SIZE_BYTE;
    // 计算最后一个不完整块需要读取的字节偏移量
    int lastBlockToReadOffset = ResByte % BLOCK_SIZE_BYTE;

    // 然后逐块读取所有完整的数据块
    while (blocksToRead--)
    {
        fileIndexNums++; // 在当前索引表中下一个数据块的下标
        // 如果索引下标已经超出当前索引表的范围，需要加载下一个索引表
        if (fileIndexNums == FILE_INDEX_SIZE)
        {
            uint32_t nextFileIndexTableBlock = fileIndexTable.next;
            fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
            fileIndexNums = 0; // 新索引表要从 0 开始
        }
        // 从 fileIndexTable.index[fileIndexNums] 块开始读取整整一个块的数据到 buf
        fileSystem->read(fileIndexTable.index[fileIndexNums], 0, buf, BLOCK_SIZE_BYTE);
        buf += BLOCK_SIZE_BYTE;                                // 更新 buf 指针
        fileOpenTable[fileLocation].cursor += BLOCK_SIZE_BYTE; // 更新光标位置
    }

    // 最后，将最后一个不完整块的剩余字节也读取到 buf
    fileIndexNums++;
    if (fileIndexNums == FILE_INDEX_SIZE)
    {
        // 如果已经读完当前索引表，则先读取下一个索引表
        uint32_t nextFileIndexTableBlock = fileIndexTable.next;
        fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
        fileIndexNums = 0;
    }
    // 从当前数据块读取最后 lastBlockToReadOffset 个字节到 buf
    fileSystem->read(fileIndexTable.index[fileIndexNums], 0, buf, lastBlockToReadOffset);
    // 最后无需手动更新 cursor，因为读取操作结束后不会再次使用光标
}

// 向已打开的文件中写入数据
void UserInterface::write(uint8_t uid, std::vector<std::string> src, const char *buf, uint16_t sz)
{
    // 在目录中查找给定路径 src 对应的磁盘号和目录项索引
    auto findRes = findDisk(std::move(src));
    // 如果未找到该文件，打印错误信息并返回
    if (findRes.first == -1)
    {
        std::cout << "write: " << RED << "failed" << RESET << ":no such file" << std::endl;
        return;
    }

    // 获取文件所在目录的磁盘编号
    uint32_t tmpDirDisk = findRes.first;
    // 从磁盘中读取该目录的数据结构
    Directory tmpDir{};
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    // 根据目录项索引获取对应的 i-node 编号（inodeIndex）
    uint32_t inodeDisk = tmpDir.item[findRes.second].inodeIndex;
    // 将 i-node 编号作为文件唯一标识（fileNumber）
    uint32_t fileNumber = inodeDisk;

    // 在文件打开表中查找该文件是否已打开，记录其在表中的位置 fileLocation
    int fileLocation = -1;
    for (int i = 0; i < FILE_OPEN_MAX_NUM; i++)
    {
        if (fileOpenTable[i].fileNumber == fileNumber)
        {
            fileLocation = i;
            break;
        }
    }
    // 如果没有在打开表中找到该文件，说明文件尚未打开，打印错误并返回
    if (fileLocation == -1)
    {
        std::cout << "write: " << RED << "failed" << RESET << ":no such file opened" << std::endl;
        return;
    }

    // 引用文件的当前容量和光标位置
    auto &capacity = fileOpenTable[fileLocation].iNode.capacity;
    auto &cursor = fileOpenTable[fileLocation].cursor;
    // 如果写入的新数据会超过当前容量，则扩展容量并标记该文件已被修改
    if (cursor + sz > capacity)
    {
        capacity = cursor + sz;
        fileOpenTable[fileLocation].flag |= (0x04); // 设置写回标志
    }

    // 读取当前文件的第一个索引表（FileIndex 结构），用于定位数据块
    FileIndex fileIndexTable{};
    uint32_t fileIndexTableBlock = fileOpenTable[fileLocation].iNode.bno;
    fileSystem->read(fileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));

    // 获取当前光标位置
    uint32_t nowCursor = fileOpenTable[fileLocation].cursor;
    // 计算光标跨越了多少个完整的索引表
    int fileIndexTableNums = nowCursor / (FILE_INDEX_SIZE * BLOCK_SIZE_BYTE);
    // 计算光标在当前索引表中的下标（代表第几个数据块）
    int fileIndexNums = (nowCursor % (FILE_INDEX_SIZE * BLOCK_SIZE_BYTE)) / BLOCK_SIZE_BYTE;

    // 如果光标不在第一个索引表，则循环读取下一个索引表，直到定位到光标所在的索引表
    while (fileIndexTableNums > 0)
    {
        uint32_t nextFileIndexTableBlock = fileIndexTable.next;
        fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
        fileIndexNums--;      // 调整在新索引表中的块下标
        fileIndexTableNums--; // 已跳过一个索引表
    }

    // 定位到当前光标所在的数据块号
    uint32_t cursorBlock = fileIndexTable.index[fileIndexNums];
    // 计算光标在该数据块内的偏移量
    uint16_t offset = nowCursor % BLOCK_SIZE_BYTE;
    // 计算当前块从光标位置到块末尾剩余的可写字节数
    uint16_t resBlockSz = BLOCK_SIZE_BYTE - offset;

    // 记录已经写入的字节数
    uint16_t writeByte = 0;

    // 如果待写入数据 sz 小于等于当前块剩余空间，则直接写入并更新光标，返回
    if (sz <= resBlockSz)
    {
        fileSystem->write(cursorBlock, offset, buf, sz);
        writeByte = sz;
        buf += writeByte;                                // 更新 buf 指针到下一个待写位置
        fileOpenTable[fileLocation].cursor += writeByte; // 更新光标位置
        return;
    }

    // 否则，先将当前块剩余空间写满
    fileSystem->write(cursorBlock, offset, buf, resBlockSz);
    writeByte = resBlockSz;
    buf += writeByte;                                // 更新 buf 指针
    fileOpenTable[fileLocation].cursor += writeByte; // 更新光标位置

    // 计算剩余还需写入的字节数（超出当前块部分）
    uint16_t ResByte = sz - resBlockSz;
    // 计算剩余字节数可以完整填满多少个数据块
    int blocksToWrite = ResByte / BLOCK_SIZE_BYTE;
    // 计算最后一个不完整块需要写入的字节数
    int lastBlockToWriteOffset = ResByte % BLOCK_SIZE_BYTE;

    // 写入所有能够完整填充的块
    while (blocksToWrite--)
    {
        fileIndexNums++; // 进入下一个块下标
        // 如果当前索引表已满，则需要新建下一个索引表
        if (fileIndexNums == FILE_INDEX_SIZE)
        {
            // 为新的索引表分配一个磁盘块
            uint32_t nextFileIndexTableBlock = fileSystem->blockAllocate();
            // 更新旧索引表的 next 指针指向新块
            fileIndexTable.next = nextFileIndexTableBlock;
            // 将更新后的索引表写回磁盘
            fileSystem->write(fileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
            fileSystem->update(); // 提交元数据更改

            // 读取新索引表，开始操作
            fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
            fileIndexNums = 0; // 在新索引表中从第 0 个下标开始
        }

        // 如果当前索引项尚未分配数据块，则为其分配一个新块
        if (fileIndexTable.index[fileIndexNums] == 0)
        {
            uint32_t newBlock = fileSystem->blockAllocate();
            fileIndexTable.index[fileIndexNums] = newBlock;
            fileSystem->update(); // 提交元数据更改
        }

        // 将一个整块数据写入到分配好的块
        fileSystem->write(fileIndexTable.index[fileIndexNums], 0, buf, BLOCK_SIZE_BYTE);
        buf += BLOCK_SIZE_BYTE;                                // 更新 buf 指针
        fileOpenTable[fileLocation].cursor += BLOCK_SIZE_BYTE; // 更新光标位置
    }

    // 写入最后一个不足一整块的数据
    fileIndexNums++;
    // 如果当前索引表已满，需要新建并跳转到下一个索引表
    if (fileIndexNums == FILE_INDEX_SIZE)
    {
        // 分配新索引表块
        uint32_t nextFileIndexTableBlock = fileSystem->blockAllocate();
        // 更新并写回当前索引表
        fileIndexTable.next = nextFileIndexTableBlock;
        fileSystem->write(fileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
        fileSystem->update();

        // 读取新索引表并重置索引下标
        fileSystem->read(nextFileIndexTableBlock, 0, reinterpret_cast<char *>(&fileIndexTable), sizeof(fileIndexTable));
        fileIndexNums = 0;
    }

    // 如果分配给最后一段写入的块尚未分配，则先分配一个块
    if (fileIndexTable.index[fileIndexNums] == 0)
    {
        uint32_t newBlock = fileSystem->blockAllocate();
        fileIndexTable.index[fileIndexNums] = newBlock;
        fileSystem->update();
    }

    // 将最后一个零碎部分写入到对应块
    fileSystem->write(fileIndexTable.index[fileIndexNums], 0, buf, lastBlockToWriteOffset);

    // 标记该文件已被修改，需要在关闭时将 i-node 写回磁盘
    fileOpenTable[fileLocation].flag |= (0x04);
}

// 复制文件或目录：在目标目录下创建源文件/目录的硬链接（仅复制目录项及分配新的 i-node，未复制实际数据）
void UserInterface::cp(std::vector<std::string> src, std::vector<std::string> des)
{
    /*==================== 查找源文件或目录的 i-node ====================*/

    // 存储被复制的文件或目录的 i-node 块号
    uint32_t srcInodeIndex = 0;
    // 临时目录结构，用于读取源文件所在目录的信息
    Directory tmpDirSrc{};
    // 存储源文件所在目录所在的磁盘块号
    uint32_t tmpDirDiskSrc;
    // 存储源文件在目录中的项索引
    int srcIndex;

    // 在文件系统中查找 src 路径，对应的目录块号和目录项索引
    auto findRes = findDisk(src);
    // 如果未找到源文件或目录，打印错误并返回
    if (findRes.first == -1)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find src" << std::endl;
        return;
    }

    // 获取源文件所在目录的磁盘块号
    tmpDirDiskSrc = findRes.first;
    // 从磁盘上读取该目录结构到 tmpDirSrc 中
    fileSystem->read(tmpDirDiskSrc, 0, reinterpret_cast<char *>(&tmpDirSrc), sizeof(tmpDirSrc));

    // 获取源文件在目录中的索引
    srcIndex = findRes.second;
    // 根据目录项索引，获取源文件的 i-node 块号
    srcInodeIndex = tmpDirSrc.item[srcIndex].inodeIndex;

    // 如果 i-node 块号为 0，则说明找不到源文件，打印错误并返回
    if (srcInodeIndex == 0)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find src" << std::endl;
        return;
    }

    /*==================== 查找目标目录 ====================*/

    // 在文件系统中查找 des 路径，对应的目录块号和目录项索引
    auto findResDes = findDisk(des);
    // 用于读取目标目录或其父目录的信息
    Directory tmpDir{};
    // 获取目标路径所在的目录块号
    uint32_t tmpDirDisk = findResDes.first;
    // 从磁盘中读取该目录结构到 tmpDir 中
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    // 获取目标目录项对应的 i-node 块号
    uint32_t desInodeIndex = tmpDir.item[findResDes.second].inodeIndex;
    // 如果 i-node 块号为 0，则目标不存在，打印错误并返回
    if (desInodeIndex == 0)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":cannot find des" << std::endl;
        return;
    }

    // 读取目标目录自身的 i-node，获取该目录在磁盘上的数据块号（bno）
    INode desInode{};
    fileSystem->read(desInodeIndex, 0, reinterpret_cast<char *>(&desInode), sizeof(desInode));
    // 目标目录的数据块号（bno）存储了该目录的实际目录项表
    tmpDirDisk = desInode.bno;
    // 读取目标目录的实际目录结构到 tmpDir 中
    fileSystem->read(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));

    /* 在目标目录中查找第一个空闲的目录项位置 */
    int location = -1;
    for (int i = 0; i < DIRECTORY_NUMS; i++)
    {
        if (tmpDir.item[i].inodeIndex == 0)
        {
            location = i;
            break;
        }
    }
    // 如果目标目录已满，没有空闲目录项，则打印错误并返回
    if (location == -1)
    {
        std::cout << "mv: " << RED << "failed" << RESET << ":des directory full" << std::endl;
        return;
    }

    /*==================== 在目标目录中创建新目录项 ====================*/

    // 将源文件/目录的名称（src 的最后一个路径组件）复制到目标目录项的 name 字段
    strcpy(tmpDir.item[location].name, src.back().c_str());
    // 为目标目录项分配一个新的 i-node 块号，用于“复制”源文件/目录的 i-node（目前仅分配，未写入具体 i-node 数据）
    tmpDir.item[location].inodeIndex = fileSystem->blockAllocate();

    // 将修改后的目标目录结构写回磁盘
    fileSystem->write(tmpDirDisk, 0, reinterpret_cast<char *>(&tmpDir), sizeof(tmpDir));
    // 更新文件系统元数据（如位图、空闲块信息等）
    fileSystem->update();

    // 注意：此处仅完成了目录项的创建和新的 i-node 分配，真正的文件内容或子目录内容未被复制。
    // 若需实现深度复制，还需递归地复制源文件/目录的所有数据块与子目录项，并写入到新的 i-node 和数据块中。
}

void UserInterface::logOut()
{
    // 关闭所有未关闭的文件
    for (int i = 0; i < FILE_OPEN_MAX_NUM; ++i)
    {
        if (fileOpenTable[i].fileNumber != 0 && 0 != (fileOpenTable[i].flag & 0x04))
        {
            fileSystem->write(fileOpenTable[i].fileNumber, 0, reinterpret_cast<char *>(&fileOpenTable[i].iNode),
                              sizeof(fileOpenTable[i].iNode));
        }
    }
    // 更新信息
    fileSystem->update();
}
