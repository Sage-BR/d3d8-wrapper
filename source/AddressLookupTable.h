#pragma once

#include <unordered_map>
#include <mutex>

constexpr UINT MaxIndex = 11;

template <typename D>
class AddressLookupTable
{
public:
	explicit AddressLookupTable(D *pDevice) : pDevice(pDevice) {}
	~AddressLookupTable()
	{
		bIsBeingDestroyed = true;

		for (const auto& cache : g_map)
		{
			for (const auto& entry : cache)
			{
				entry.second->DeleteMe();
			}
		}
	}

	template <typename T>
	struct AddressCacheIndex { static constexpr UINT CacheIndex = 0; };
	template <>
	struct AddressCacheIndex<m_IDirect3D8> { static constexpr UINT CacheIndex = 1; };
	template <>
	struct AddressCacheIndex<m_IDirect3DDevice8> { static constexpr UINT CacheIndex = 2; };
	template <>
	struct AddressCacheIndex<m_IDirect3DCubeTexture8> { static constexpr UINT CacheIndex = 3; };
	template <>
	struct AddressCacheIndex<m_IDirect3DIndexBuffer8> { static constexpr UINT CacheIndex = 4; };
	template <>
	struct AddressCacheIndex<m_IDirect3DSurface8> { static constexpr UINT CacheIndex = 5; };
	template <>
	struct AddressCacheIndex<m_IDirect3DSwapChain8> { static constexpr UINT CacheIndex = 6; };
	template <>
	struct AddressCacheIndex<m_IDirect3DTexture8> { static constexpr UINT CacheIndex = 7; };
	template <>
	struct AddressCacheIndex<m_IDirect3DVertexBuffer8> { static constexpr UINT CacheIndex = 8; };
	template <>
	struct AddressCacheIndex<m_IDirect3DVolume8> { static constexpr UINT CacheIndex = 9; };
	template <>
	struct AddressCacheIndex<m_IDirect3DVolumeTexture8> { static constexpr UINT CacheIndex = 10; };

	template <typename T>
	T *FindAddress(void *Proxy)
	{
		if (!Proxy)
		{
			return nullptr;
		}

		constexpr UINT CacheIndex = AddressCacheIndex<T>::CacheIndex;
		
		std::lock_guard<std::recursive_mutex> lock(TableMutex);

		auto it = g_map[CacheIndex].find(Proxy);

		if (it != std::end(g_map[CacheIndex]))
		{
			return static_cast<T *>(it->second);
		}

		T* wrapper = new T(static_cast<T *>(Proxy), pDevice);
		return wrapper;
	}

	template <typename T>
	void SaveAddress(T *Wrapper, void *Proxy)
	{
		constexpr UINT CacheIndex = AddressCacheIndex<T>::CacheIndex;
		if (Wrapper && Proxy)
		{
			std::lock_guard<std::recursive_mutex> lock(TableMutex);
			g_map[CacheIndex][Proxy] = Wrapper;
		}
	}

	template <typename T>
	void DeleteAddress(T *Wrapper)
	{
		if (!Wrapper || bIsBeingDestroyed)
		{
			return;
		}

		constexpr UINT CacheIndex = AddressCacheIndex<T>::CacheIndex;
		std::lock_guard<std::recursive_mutex> lock(TableMutex);
		g_map[CacheIndex].erase(Wrapper->GetProxyInterface());
	}

private:
	bool bIsBeingDestroyed = false;
	D *const pDevice;
	std::recursive_mutex TableMutex;
	std::unordered_map<void*, class AddressLookupTableObject*> g_map[MaxIndex];
};

class AddressLookupTableObject
{
public:
	virtual ~AddressLookupTableObject() { }

	void DeleteMe()
	{
		delete this;
	}
};
