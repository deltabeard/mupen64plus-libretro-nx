/*
 * Texture Filtering
 * Version:  1.0
 *
 * Copyright (C) 2007  Hiroshi Morii   All Rights Reserved.
 * Email koolsmoky(at)users.sourceforge.net
 * Web   http://www.3dfxzone.it/koolsmoky
 *
 * this is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * this is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __MSC__
#pragma warning(disable: 4786)
#endif

#include <assert.h>
#include <fstream>
#include <memory.h>
#include <stdlib.h>
#include <unordered_map>
#include <zlib.h>

#include "TxCache.h"
#include "TxDbg.h"

class TxCacheImpl
{
public:
	virtual ~TxCacheImpl() = default;

	virtual bool add(Checksum checksum, GHQTexInfo *info, int dataSize = 0) = 0;
	virtual bool get(Checksum checksum, GHQTexInfo *info) = 0;
	virtual bool save(const wchar_t *path, const wchar_t *filename, const int config) = 0;
	virtual bool load(const wchar_t *path, const wchar_t *filename, const int config, bool force) = 0;
	virtual bool del(Checksum checksum) = 0;
	virtual bool isCached(Checksum checksum) = 0;
	virtual void clear() = 0;
	virtual bool empty() const = 0;
	virtual uint32 getOptions() const = 0;
	virtual void setOptions(uint32 options) = 0;

	virtual uint64 size() const = 0;
	virtual uint64 totalSize() const = 0;
	virtual uint64 cacheLimit() const = 0;
};


/************************** TxMemoryCache *************************************/

class TxMemoryCache : public TxCacheImpl
{
public:
	TxMemoryCache(uint32 _options, uint64 cacheLimit);
	~TxMemoryCache();

	bool add(Checksum checksum, GHQTexInfo *info, int dataSize = 0) override;
	bool get(Checksum checksum, GHQTexInfo *info) override;

	bool save(const wchar_t *path, const wchar_t *filename, const int config) override;
	bool load(const wchar_t *path, const wchar_t *filename, const int config, bool force) override;
	bool del(Checksum checksum) override;
	bool isCached(Checksum checksum) override;
	void clear() override;
	bool empty() const  override { return _cache.empty(); }

	uint64 size() const  override { return _cache.size(); }
	uint64 totalSize() const  override { return _totalSize; }
	uint64 cacheLimit() const  override { return _cacheLimit; }
	uint32 getOptions() const override { return _options; }
	void setOptions(uint32 options) override { _options = options; }

private:
	struct TXCACHE {
		int size;
		GHQTexInfo info;
		std::list<uint64>::iterator it;
	};

	uint32 _options;
	uint64 _cacheLimit;
	uint64 _totalSize;

	std::map<uint64, TXCACHE*> _cache;
	std::list<uint64> _cachelist;

	uint8 *_gzdest0 = nullptr;
	uint8 *_gzdest1 = nullptr;
	uint32 _gzdestLen = 0;
};

TxMemoryCache::TxMemoryCache(uint32 options,
	uint64 cacheLimit)
	: _options(options)
	, _cacheLimit(cacheLimit)
	, _totalSize(0U)
{
	return;
}

TxMemoryCache::~TxMemoryCache()
{
	/* free memory, clean up, etc */
	clear();
}

bool TxMemoryCache::add(Checksum checksum, GHQTexInfo *info, int dataSize)
{
	/* NOTE: dataSize must be provided if info->data is zlib compressed. */

	if (!checksum || !info->data || _cache.find(checksum) != _cache.end())
		return false;

	uint8 *dest = info->data;
	uint32 format = info->format;

	if (dataSize == 0) {
		dataSize = TxUtil::sizeofTx(info->width, info->height, info->format);

		if (!dataSize)
			return false;
	}

	/* if cache size exceeds limit, remove old cache */
	if (_cacheLimit != 0) {
		_totalSize += dataSize;
		if ((_totalSize > _cacheLimit) && !_cachelist.empty()) {
			/* _cachelist is arranged so that frequently used textures are in the back */
			std::list<uint64>::iterator itList = _cachelist.begin();
			while (itList != _cachelist.end()) {
				/* find it in _cache */
				auto itMap = _cache.find(*itList);
				if (itMap != _cache.end()) {
					/* yep we have it. remove it. */
					_totalSize -= (*itMap).second->size;
					free((*itMap).second->info.data);
					delete (*itMap).second;
					_cache.erase(itMap);
				}
				itList++;

				/* check if memory cache has enough space */
				if (_totalSize <= _cacheLimit)
					break;
			}
			/* remove from _cachelist */
			_cachelist.erase(_cachelist.begin(), itList);

			DBG_INFO(80, wst("+++++++++\n"));
		}
		_totalSize -= dataSize;
	}

	/* cache it */
	uint8 *tmpdata = (uint8*)malloc(dataSize);
	if (tmpdata == nullptr)
		return false;

	TXCACHE *txCache = new TXCACHE;
	/* we can directly write as we filter, but for now we get away
	* with doing memcpy after all the filtering is done.
	*/
	memcpy(tmpdata, dest, dataSize);

	/* copy it */
	memcpy(&txCache->info, info, sizeof(GHQTexInfo));
	txCache->info.data = tmpdata;
	txCache->info.format = format;
	txCache->size = dataSize;

	/* add to cache */
	if (_cacheLimit != 0) {
		_cachelist.push_back(checksum);
		txCache->it = --(_cachelist.end());
	}
	/* _cache[checksum] = txCache; */
	_cache.insert(std::map<uint64, TXCACHE*>::value_type(checksum, txCache));

#ifdef DEBUG
	DBG_INFO(80, wst("[%5d] added!! crc:%08X %08X %d x %d gfmt:%x total:%.02fmb\n"),
		_cache.size(), checksum._hi, checksum._low,
		info->width, info->height, info->format & 0xffff, (double)_totalSize / 1000000);

	if (_cacheLimit != 0) {
		DBG_INFO(80, wst("cache max config:%.02fmb\n"), (double)_cacheLimit / 1000000);

		if (_cache.size() != _cachelist.size()) {
			DBG_INFO(80, wst("Error: cache/cachelist mismatch! (%d/%d)\n"), _cache.size(), _cachelist.size());
		}
	}
#endif

	/* total cache size */
	_totalSize += dataSize;

	return true;
}

bool TxMemoryCache::get(Checksum checksum, GHQTexInfo *info)
{
	if (!checksum || _cache.empty())
		return false;

	/* find a match in cache */
	auto itMap = _cache.find(checksum);
	if (itMap != _cache.end()) {
		/* yep, we've got it. */
		memcpy(info, &(((*itMap).second)->info), sizeof(GHQTexInfo));

		/* push it to the back of the list */
		if (_cacheLimit != 0) {
			_cachelist.erase(((*itMap).second)->it);
			_cachelist.push_back(checksum);
			((*itMap).second)->it = --(_cachelist.end());
		}

		/* zlib decompress it */
		if (info->format & GL_TEXFMT_GZ) {
			uLongf destLen = _gzdestLen;
			uint8 *dest = (_gzdest0 == info->data) ? _gzdest1 : _gzdest0;
			if (uncompress(dest, &destLen, info->data, ((*itMap).second)->size) != Z_OK) {
				DBG_INFO(80, wst("Error: zlib decompression failed!\n"));
				return false;
			}
			info->data = dest;
			info->format &= ~GL_TEXFMT_GZ;
			DBG_INFO(80, wst("zlib decompressed: %.02fkb->%.02fkb\n"), (float)(((*itMap).second)->size) / 1000, (float)destLen / 1000);
		}

		return true;
	}

	return false;
}

bool TxMemoryCache::save(const wchar_t *path, const wchar_t *filename, int config)
{
	return false;
}

bool TxMemoryCache::load(const wchar_t *path, const wchar_t *filename, int config, bool force)
{
	/* find it on disk */
	char cbuf[MAX_PATH];

#ifdef OS_WINDOWS
	wchar_t curpath[MAX_PATH];
	GETCWD(MAX_PATH, curpath);
	CHDIR(path);
#else
	char curpath[MAX_PATH];
	GETCWD(MAX_PATH, curpath);
	wcstombs(cbuf, path, MAX_PATH);
	CHDIR(cbuf);
#endif

	wcstombs(cbuf, filename, MAX_PATH);

	gzFile gzfp = gzopen(cbuf, "rb");
	DBG_INFO(80, wst("gzfp:%x file:%ls\n"), gzfp, filename);
	if (gzfp) {
		/* yep, we have it. load it into memory cache. */
		int dataSize;
		uint64 checksum;
		int tmpconfig;
		/* read header to determine config match */
		gzread(gzfp, &tmpconfig, 4);

		if (tmpconfig == config || force) {
			do {
				GHQTexInfo tmpInfo;

				gzread(gzfp, &checksum, 8);

				gzread(gzfp, &tmpInfo.width, 4);
				gzread(gzfp, &tmpInfo.height, 4);
				gzread(gzfp, &tmpInfo.format, 4);
				gzread(gzfp, &tmpInfo.texture_format, 2);
				gzread(gzfp, &tmpInfo.pixel_type, 2);
				gzread(gzfp, &tmpInfo.is_hires_tex, 1);

				gzread(gzfp, &dataSize, 4);

				tmpInfo.data = (uint8*)malloc(dataSize);
				if (tmpInfo.data) {
					gzread(gzfp, tmpInfo.data, dataSize);

					/* add to memory cache */
					add(checksum, &tmpInfo, (tmpInfo.format & GL_TEXFMT_GZ) ? dataSize : 0);

					free(tmpInfo.data);
				} else {
					gzseek(gzfp, dataSize, SEEK_CUR);
				}
			} while (!gzeof(gzfp));
			gzclose(gzfp);
		}
	}

	CHDIR(curpath);

	return !_cache.empty();
}

bool TxMemoryCache::del(Checksum checksum)
{
	if (!checksum || _cache.empty())
		return false;

	auto itMap = _cache.find(checksum);
	if (itMap != _cache.end()) {

		/* for texture cache (not hi-res cache) */
		if (!_cachelist.empty())
			_cachelist.erase(((*itMap).second)->it);

		/* remove from cache */
		free((*itMap).second->info.data);
		_totalSize -= (*itMap).second->size;
		delete (*itMap).second;
		_cache.erase(itMap);

		DBG_INFO(80, wst("removed from cache: checksum = %08X %08X\n"), checksum._low, checksum._hi);

		return true;
	}

	return false;
}

bool TxMemoryCache::isCached(Checksum checksum)
{
	return _cache.find(checksum) != _cache.end();
}

void TxMemoryCache::clear()
{
	if (!_cache.empty()) {
		auto itMap = _cache.begin();
		while (itMap != _cache.end()) {
			free((*itMap).second->info.data);
			delete (*itMap).second;
			itMap++;
		}
		_cache.clear();
	}

	if (!_cachelist.empty())
		_cachelist.clear();

	_totalSize = 0;
}

/************************** TxFileCache *************************************/

class TxFileStorage : public TxCacheImpl
{
public:
	TxFileStorage(uint32 _options, const wchar_t *cachePath);
	~TxFileStorage() = default;

	bool add(Checksum checksum, GHQTexInfo *info, int dataSize = 0) override;
	bool get(Checksum checksum, GHQTexInfo *info) override;

	bool save(const wchar_t *path, const wchar_t *filename, const int config) override;
	bool load(const wchar_t *path, const wchar_t *filename, const int config, bool force) override;
	bool del(Checksum checksum) override { return false; }
	bool isCached(Checksum checksum) override;
	void clear() override;
	bool empty() const override { return _storage.empty(); }

	uint64 size() const override { return _storage.size(); }
	uint64 totalSize() const override { return _totalSize; }
	uint64 cacheLimit() const override { return 0UL; }
	uint32 getOptions() const override { return _options; }
	void setOptions(uint32 options) override { _options = options; }

private:
	bool open(bool forRead);
	bool readData(GHQTexInfo & info);
	void buildFullPath();

	uint32 _options;
	tx_wstring _cachePath;
	tx_wstring _filename;
	uint64 _totalSize = 0;

	using StorageMap = std::unordered_map<uint64, int64>;
	StorageMap _storage;

	uint8 *_gzdest0 = nullptr;
	uint8 *_gzdest1 = nullptr;
	uint32 _gzdestLen = 0;

	std::ifstream _infile;
	std::ofstream _outfile;
	int64 _storagePos = 0;
	bool _dirty = false;
	static const int _fakeConfig;
	static const int64 _initialPos;
};

const int TxFileStorage::_fakeConfig = -1;
const int64 TxFileStorage::_initialPos = sizeof(int64) + sizeof(int);

TxFileStorage::TxFileStorage(uint32 options,
	const wchar_t *cachePath)
	: _options(options)
{
	/* save path name */
	if (cachePath)
		_cachePath.assign(cachePath);
}

#define FWRITE(a) _outfile.write((char*)(&a), sizeof(a))
#define FREAD(a) _infile.read((char*)(&a), sizeof(a))

bool TxFileStorage::open(bool forRead)
{
	/* FIXME: Replace. */
	return false;
#if 0
	if (_infile.is_open())
		_infile.close();
	if (_outfile.is_open())
		_outfile.close();

	if (forRead) {
		/* find it on disk */
		_infile.open(_fullPath, std::ifstream::in | std::ifstream::binary);
		DBG_INFO(80, wst("file:%s %s\n"), _fullPath.c_str(), _infile.good() ? "opened for read" : "failed to open");
		return _infile.good();
	}

	if (osal_path_existsA(_fullPath.c_str()) != 0) {
		assert(_storagePos != 0L);
		_outfile.open(_fullPath, std::ofstream::out | std::ofstream::binary);
		DBG_INFO(80, wst("file:%s %s\n"), _fullPath.c_str(), _outfile.good() ? "opened for write" : "failed to open");
		return _outfile.good();
	}

	if (osal_mkdirp(_cachePath.c_str()) != 0)
		return false;

	_outfile.open(_fullPath, std::ofstream::out | std::ofstream::binary);
	DBG_INFO(80, wst("file:%s %s\n"), _fullPath.c_str(), _outfile.good() ? L"created for write" : L"failed to create");
	if (!_outfile.good())
		return false;

	FWRITE(_fakeConfig);
	_storagePos = _initialPos;
	FWRITE(_storagePos);

	return _outfile.good();
#endif
}

void TxFileStorage::clear()
{
	return;
}

bool TxFileStorage::readData(GHQTexInfo & info)
{
	FREAD(info.width);
	FREAD(info.height);
	FREAD(info.format);
	FREAD(info.texture_format);
	FREAD(info.pixel_type);
	FREAD(info.is_hires_tex);

	uint32 dataSize = 0U;
	FREAD(dataSize);
	if (dataSize == 0)
		return false;

	if (_gzdest0 == nullptr)
		return false;

	_infile.read((char*)_gzdest0, dataSize);
	if (!_infile.good())
		return false;

	/* zlib decompress it */
	if (info.format & GL_TEXFMT_GZ) {
		uLongf destLen = _gzdestLen;
		if (uncompress(_gzdest1, &destLen, _gzdest0, dataSize) != Z_OK) {
			DBG_INFO(80, wst("Error: zlib decompression failed!\n"));
			return false;
		}
		info.data = _gzdest1;
		info.format &= ~GL_TEXFMT_GZ;
		DBG_INFO(80, wst("zlib decompressed: %.02gkb->%.02gkb\n"), dataSize / 1024.0, destLen / 1024.0);
	} else {
		info.data = _gzdest0;
	}

	return true;
}

// TODO: Remove
bool TxFileStorage::add(Checksum checksum, GHQTexInfo *info, int dataSize)
{
	return true;
}

bool TxFileStorage::get(Checksum checksum, GHQTexInfo *info)
{
	if (!checksum || _storage.empty())
		return false;

	/* find a match in storage */
	auto itMap = _storage.find(checksum);
	if (itMap == _storage.end())
		return false;

	if (_outfile.is_open() || !_infile.is_open())
		if (!open(true))
			return false;

	_infile.seekg(itMap->second, std::ifstream::beg);
	return readData(*info);
}

bool TxFileStorage::save(const wchar_t *path, const wchar_t *filename, int config)
{
	return true;
}

bool TxFileStorage::load(const wchar_t *path, const wchar_t *filename, int config, bool force)
{
	return false;
#if 0
	assert(_cachePath == path);
	if (_filename.empty()) {
		_filename = filename;
		buildFullPath();
	} else
		assert(_filename == filename);

	if (_outfile.is_open() || !_infile.is_open())
		if (!open(true))
			return false;

	int tmpconfig = 0;
	/* read header to determine config match */
	_infile.seekg(0L, std::ifstream::beg);
	FREAD(tmpconfig);
	FREAD(_storagePos);
	if (tmpconfig == _fakeConfig) {
		if (_storagePos != _initialPos)
			return false;
	} else if (tmpconfig != config && !force)
		return false;

	if (_storagePos <= sizeof(config) + sizeof(_storagePos))
		return false;
	_infile.seekg(_storagePos, std::ifstream::beg);

	int storageSize = 0;
	FREAD(storageSize);
	if (storageSize <= 0)
		return false;

	uint64 key;
	int64 value;
	for (int i = 0; i < storageSize; ++i) {
		FREAD(key);
		FREAD(value);
		_storage.insert(StorageMap::value_type(key, value));
	}

	_dirty = false;
	return !_storage.empty();
#endif
}

bool TxFileStorage::isCached(Checksum checksum)
{
	return _storage.find(checksum) != _storage.end();
}

/************************** TxCache *************************************/

TxCache::~TxCache()
{
}

TxCache::TxCache(uint32 options,
	uint64 cachesize,
	const wchar_t *cachePath,
	const wchar_t *ident)
{
	/* save path name */
	if (cachePath)
		_cachePath.assign(cachePath);

	/* save ROM name */
	if (ident)
		_ident.assign(ident);

	if ((options & FILE_CACHE_MASK) == 0)
		_pImpl.reset(new TxMemoryCache(options, cachesize));
	else
		_pImpl.reset(new TxFileStorage(options, cachePath));
}

bool TxCache::add(Checksum checksum, GHQTexInfo *info, int dataSize)
{
	return _pImpl->add(checksum, info, dataSize);
}

bool TxCache::get(Checksum checksum, GHQTexInfo *info)
{
	return _pImpl->get(checksum, info);
}

uint64 TxCache::size() const
{
	return _pImpl->size();
}

uint64 TxCache::totalSize() const
{
	return _pImpl->totalSize();
}

uint64 TxCache::cacheLimit() const
{
	return _pImpl->cacheLimit();
}

bool TxCache::save()
{
	return _pImpl->save(_cachePath.c_str(), _getFileName().c_str(), _getConfig());
}

bool TxCache::load(bool force)
{
	return _pImpl->load(_cachePath.c_str(), _getFileName().c_str(), _getConfig(), force);
}

bool TxCache::del(Checksum checksum)
{
	return _pImpl->del(checksum);
}

bool TxCache::isCached(Checksum checksum)
{
	return _pImpl->isCached(checksum);
}

void TxCache::clear()
{
	_pImpl->clear();
}

bool TxCache::empty() const
{
	return _pImpl->empty();
}

uint32 TxCache::getOptions() const
{
	return _pImpl->getOptions();
}

void TxCache::setOptions(uint32 options)
{
	_pImpl->setOptions(options);
}
