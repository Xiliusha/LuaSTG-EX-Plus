﻿#include <set>
#include <string>
#include "E2DFileManager.hpp"
#include "E2DCodePage.hpp"
#include "E2DLogSystem.hpp"
#include "zip.h"

using namespace std;
using namespace Eyes2D::IO;

//======================================

struct Archive::Impl {
	zip_t* ZipFile;
	string path;//路径，可能不是绝对路径
	string password;//备用
	Archive::Impl() {
		ZipFile = nullptr;
		path = "";
		password = "";
	}
};

Archive::Archive(unsigned int uid) {
	m_Impl = new Archive::Impl;
	sort.priority = 0;
	sort.uid = uid;
}

void Archive::class_init(const char* path, const char* password) {
	//检查路径
	if (path == nullptr) {
		throw E2DException(0, 0, L"Eyes2D::IO::Archive::Archive", L"File path is null.");
		return;
	}
	string frompath = path;
	if (frompath.length() < 1) {
		throw E2DException(0, 0, L"Eyes2D::IO::Archive::Archive", L"File path is blank.");
		return;
	}
	m_Impl->path = frompath;
	//开始加载
	bool load = false;//已经完成加载，失败则为false
	m_Impl->ZipFile = nullptr;
	int err; m_Impl->ZipFile = zip_open(frompath.c_str(), ZIP_RDONLY, &err);//默认视作ANSI
	if (m_Impl->ZipFile == nullptr) {
		string topath = std::move(Eyes2D::String::UTF8ToANSI(frompath));
		m_Impl->ZipFile = zip_open(topath.c_str(), ZIP_RDONLY, &err);//加载错误，可能是UTF8，需要转换为ANSI
		if (m_Impl->ZipFile != nullptr) {
			load = true;
		}
	}
	else {
		load = true;
	}
	//检查结果
	if (load) {
		if (password != nullptr) {
			//密码内一般不会带中文，所以直接设置了
			//内存不足不用管，出错不要管，反正出错也没什么问题.jpg
			zip_set_default_password(m_Impl->ZipFile, password);
			m_Impl->password = password;
		}
	}
	else {
		zip_error_t error;
		zip_error_init_with_code(&error, err);
		string errstr = zip_error_strerror(&error);
		wstring errwstr = std::move(Eyes2D::String::ANSIToUTF16(errstr));
		zip_error_fini(&error);
		throw E2DException(0, 0, L"Eyes2D::IO::Archive::Archive", wstring(L"Failed to open archive file : ") + errwstr);
	}
}

Archive::Archive(const char* path, unsigned int uid) {
	m_Impl = new Archive::Impl;
	sort.priority = 0;
	sort.uid = uid;
	class_init(path, nullptr);
}

Archive::Archive(const char* path, int priority, unsigned int uid) {
	m_Impl = new Archive::Impl;
	sort.priority = priority;
	sort.uid = uid;
	class_init(path, nullptr);
}

Archive::Archive(const char* path, const char* password, unsigned int uid) {
	m_Impl = new Archive::Impl;
	sort.priority = 0;
	sort.uid = uid;
	class_init(path, password);
}

Archive::Archive(const char* path, int priority, const char* password, unsigned int uid) {
	m_Impl = new Archive::Impl;
	sort.priority = priority;
	sort.uid = uid;
	class_init(path, password);
}

void Archive::class_del() {
	if (m_Impl->ZipFile != nullptr) {
		zip_discard(m_Impl->ZipFile);//只读模式打开，所以关闭的时候不作任何更改
	}
}

Archive::~Archive() {
	class_del();//一定要放在前面
	delete m_Impl;
}

long long Archive::file_precheck(const char* filepath) {
	//检查路径
	if (filepath == nullptr) {
		return -1;
	}
	string frompath = filepath;
	if (frompath.length() < 1) {
		return -1;
	}
	//查找文件
	bool find = false;
	zip_int64_t index = zip_name_locate(m_Impl->ZipFile, frompath.c_str(), ZIP_FL_ENC_GUESS);
	if (index < 0) {
		string topath = std::move(Eyes2D::String::UTF8ToANSI(frompath));
		index = zip_name_locate(m_Impl->ZipFile, topath.c_str(), ZIP_FL_ENC_GUESS);
		if (index >= 0) {
			find = true;
		}
	}
	else {
		find = true;
	}
	//检查结果
	if (find) {
		return index;
	}
	else {
		return -1;
	}
}

const char* Archive::GetArchivePath() {
	return m_Impl->path.c_str();
}

bool Archive::FileExist(const char* filepath) {
	return file_precheck(filepath) >= 0;
}

fcyStream* Archive::LoadFile(const char* filepath) {
	//检查路径，查找文件
	zip_int64_t index = file_precheck(filepath);
	if (index < 0) {
		return nullptr;
	}
	//加载
	{
		//获取文件信息
		zip_stat_t zs;
		if (zip_stat_index(m_Impl->ZipFile, index, ZIP_STAT_SIZE, &zs) != 0) {
			return nullptr;
		}
		//打开压缩包内文件
		zip_file_t* zf = zip_fopen_index(m_Impl->ZipFile, index, ZIP_FL_ENC_GUESS);
		if (zf == nullptr) {
			return nullptr;
		}
		//申请内存
		fcyRefPointer<fcyMemStream> stream;
		try {
			stream.DirectSet(new fcyMemStream(nullptr, zs.size, true, false));//引用计数1
		}
		catch (const bad_alloc&) {
			zip_fclose(zf);//关闭过程中可能会出错，但是关我pi事
			return nullptr;
		}
		//读取内容
		zip_int64_t read = zip_fread(zf, (void*)stream->GetInternalBuffer(), zs.size);
		zip_fclose(zf);//先关掉再说
		if (read != zs.size) {
			return nullptr;
		}
		else {
			stream->AddRef();//引用计数2，这里加一次计数是因为一会返回的时候会自动释放一次……智能指针，智障指针
			return *stream;//返回指针
		}
	}
}

fcyStream* Archive::LoadEncryptedFile(const char* filepath, const char* password) {
	//检查路径，查找文件
	zip_int64_t index = file_precheck(filepath);
	if (index < 0) {
		return nullptr;
	}
	//加载
	{
		//获取文件信息
		zip_stat_t zs;
		if (zip_stat_index(m_Impl->ZipFile, index, ZIP_STAT_SIZE, &zs) != 0) {
			return nullptr;
		}
		//打开压缩包内文件，密码不使用中文
		zip_file_t* zf = zip_fopen_index_encrypted(m_Impl->ZipFile, index, ZIP_FL_ENC_GUESS, password);
		if (zf == nullptr) {
			return nullptr;
		}
		//申请内存
		fcyRefPointer<fcyMemStream> stream;
		try {
			stream.DirectSet(new fcyMemStream(nullptr, zs.size, true, false));//引用计数1
		}
		catch (const bad_alloc&) {
			zip_fclose(zf);//关闭过程中可能会出错，但是关我pi事
			return nullptr;
		}
		//读取内容
		zip_int64_t read = zip_fread(zf, (void*)stream->GetInternalBuffer(), zs.size);
		zip_fclose(zf);//先关掉再说
		if (read != zs.size) {
			return nullptr;
		}
		else {
			stream->AddRef();//引用计数2，这里加一次计数是因为一会返回的时候会自动释放一次……智能指针，智障指针
			return *stream;//返回指针
		}
	}
}

long long Archive::GetFileCount() {
	return zip_get_num_entries(m_Impl->ZipFile, ZIP_FL_UNCHANGED);
}

const char* Archive::GetFileName(long long index) {
	if ((index < 0) || (index >= GetFileCount())) {
		return nullptr;
	}
	return zip_get_name(m_Impl->ZipFile, index, ZIP_FL_ENC_RAW | ZIP_FL_ENC_GUESS);
}

void Archive::ListFile() {
	zip_int64_t count = zip_get_num_entries(m_Impl->ZipFile, ZIP_FL_UNCHANGED);
	for (zip_int64_t index = 0; index < count; index++) {
		const char* pname = zip_get_name(m_Impl->ZipFile, index, ZIP_FL_ENC_RAW | ZIP_FL_ENC_GUESS);
		if (pname != nullptr) {
			Eyes2D::EYESINFO(pname);
		}
		else {
			Eyes2D::EYESERROR("NULL");
		}
	}
}

//======================================

struct FileManager::Impl {
	set<Archive*, Archive::ArchiveSort> ArchiveSet;
};

FileManager::FileManager() {
	m_Impl = new FileManager::Impl;
	m_ArchiveUID = 0;
}

FileManager::~FileManager() {
	UnloadAllArchive();
	delete m_Impl;
}

bool FileManager::LoadArchive(const char* name, int priority, const char* password) {
	Archive* zip;
	try {
		if (password != nullptr) {
			zip = new Archive(name, priority, password, m_ArchiveUID);
		}
		else {
			zip = new Archive(name, priority, m_ArchiveUID);
		}
	}
	catch (E2DException& e) {
		Eyes2D::EYESERROR(e);
		return false;
	}
	m_Impl->ArchiveSet.insert(zip);
	m_ArchiveUID++;
	Eyes2D::EYESDEBUG(string(string("Archive : ") + string(name) + string(" was loaded.")).c_str());
	return true;
}

Archive* FileManager::GetArchive(const char* name) {
	string frompath = name;
	string topath;
	for (auto i : m_Impl->ArchiveSet) {
		topath = i->GetArchivePath();
		if (frompath == topath) {
			return i;
		}
	}
	Eyes2D::EYESERROR(string(string("Can't find archive : ") + string(name)).c_str());
	return nullptr;
}

Archive* FileManager::GetArchive(unsigned int pos) {
	if ((pos < 0) || (pos > m_Impl->ArchiveSet.size())) {
		return nullptr;
	}
	unsigned int cur = 0;
	for (auto i : m_Impl->ArchiveSet) {
		if (cur == pos) {
			return i;
		}
		cur++;
	}
	return nullptr;
}

bool FileManager::ArchiveExist(const char* name) {
	return GetArchive(name) != nullptr;
}

unsigned int FileManager::GetArchiveCount() {
	return m_Impl->ArchiveSet.size();
}

void FileManager::UnloadArchive(const char* name) {
	string frompath = name;
	string topath;
	for (auto it = m_Impl->ArchiveSet.begin(); it != m_Impl->ArchiveSet.end();) {
		topath = (*it)->GetArchivePath();
		if (frompath == topath) {
			Archive* p = *it;
			it = m_Impl->ArchiveSet.erase(it);
			delete p;//只会擦除元素，不会将指针指向的内存释放
			return;
		}
		++it;
	}
	Eyes2D::EYESWARN(string(string("Can't find archive : ") + string(name)).c_str());
}

void FileManager::UnloadAllArchive() {
	for (auto it = m_Impl->ArchiveSet.begin(); it != m_Impl->ArchiveSet.end();) {
		Archive* p = *it;
		it = m_Impl->ArchiveSet.erase(it);//这个操作会自动移动到下一个迭代器
		delete p;//只会擦除元素，不会将指针指向的内存释放
	}
}
