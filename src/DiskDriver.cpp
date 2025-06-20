
#include "DiskDriver.h"

DiskDriver *DiskDriver::instance = nullptr;

std::string DiskDriver::diskName = "./disk.zhl";

DiskDriver *DiskDriver::getInstance() {
    if (!instance) {
        instance = new DiskDriver;
    }
    return instance;
}

DiskDriver::DiskDriver() {
    isOpen = false;
}

bool DiskDriver::open() {
    if (isOpen) {
        return true;
    }
    std::ifstream t(diskName);      //使用读入流来判断文件是否存在
    if (t.is_open()) {
        t.close();
        disk.open(diskName, std::ios::in | std::ios::out | std::ios::binary);
        isOpen = true;
        return true;
    }
    return false;
}

bool DiskDriver::close() {
    if (!isOpen) {
        return true;
    }
    disk.close();
    isOpen = false;
    return true;
}

bool DiskDriver::init(uint32_t sz) {
    if (isOpen) {
        return false;
    }
    std::ofstream c(diskName, std::ios::binary | std::ios::out);
    //填充虚拟磁盘
    c.seekp(0, std::ios::beg);
    for (uint32_t i = 0; i < sz; ++i) {
        c.write("", 1);
    }
    c.seekp(0, std::ios::beg);
    c.write(reinterpret_cast<char *>(&sz), sizeof(sz));
    int8_t ok = -1;     //未格式化标记
    c.write(reinterpret_cast<char *>(&ok), sizeof(ok));
    c.close();
    return true;
}

void DiskDriver::seekStart(uint32_t sz) {
    disk.seekp(sz, std::ios::beg);
    //std::cout<<cursor<<std::endl;
}

void DiskDriver::seekCurrent(uint32_t sz) {
    disk.seekp(sz, std::ios::cur);
    //std::cout<<cursor<<std::endl;
}

void DiskDriver::read(char *buf, uint32_t sz) {
    disk.read(buf, sz);
    disk.flush();
    disk.clear();
}

void DiskDriver::write(const char *buf, uint32_t sz) {
    disk.write(buf, sz);
    disk.flush();
    disk.clear();
}

DiskDriver::~DiskDriver() {
    if (isOpen) {
        disk.close();
    }
}

void DiskDriver::revokeInstance() {
    delete instance;
    instance = nullptr;
}


