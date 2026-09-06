#pragma once

#include <vector>

#include <d3d12.h>

namespace GameEngine::Rendering::D3D12
{

/// <summary>
/// 고정 용량의 descriptor 인덱스 풀이다. 퇴거된 텍스처 바인딩이 놓아준 슬롯이 재사용되므로,
/// 크기가 제한된 바인딩 캐시는 descriptor 힙을 소진하는 대신 무한정 돌 수 있다.
/// </summary>
class D3D12DescriptorIndexAllocator final
{
public:
    void Configure(const UINT capacity)
    {
        mCapacity = capacity;
        mNextIndex = 0;
        mFreeIndices.clear();
    }

    [[nodiscard]] UINT GetCapacity() const { return mCapacity; }

    [[nodiscard]] UINT GetUsedCount() const
    {
        return mNextIndex - static_cast<UINT>(mFreeIndices.size());
    }

    [[nodiscard]] bool HasFreeSlot() const
    {
        return !mFreeIndices.empty() || mNextIndex < mCapacity;
    }

    [[nodiscard]] bool TryAllocate(UINT& index)
    {
        if (!mFreeIndices.empty())
        {
            index = mFreeIndices.back();
            mFreeIndices.pop_back();
            return true;
        }
        if (mNextIndex >= mCapacity)
        {
            return false;
        }
        index = mNextIndex++;
        return true;
    }

    /// <summary>슬롯을 풀에 돌려준다. 설정된 범위 밖의 인덱스는 무시된다.</summary>
    void Release(const UINT index)
    {
        if (index < mNextIndex)
        {
            mFreeIndices.push_back(index);
        }
    }

private:
    UINT mCapacity = 0;
    UINT mNextIndex = 0;
    std::vector<UINT> mFreeIndices;
};

/// <summary>
/// descriptor 슬롯 하나를 소유하고 파괴될 때 풀에 돌려준다. 그래서 캐시된 바인딩을 퇴거하면
/// 퇴거 지점에 아무 장부 없이도 그 descriptor가 풀려난다.
/// </summary>
class D3D12DescriptorAllocation final
{
public:
    D3D12DescriptorAllocation() = default;

    D3D12DescriptorAllocation(D3D12DescriptorIndexAllocator& allocator, const UINT index)
        : mAllocator(&allocator), mIndex(index)
    {
    }

    ~D3D12DescriptorAllocation() { Release(); }

    D3D12DescriptorAllocation(const D3D12DescriptorAllocation&) = delete;
    D3D12DescriptorAllocation& operator=(const D3D12DescriptorAllocation&) = delete;

    D3D12DescriptorAllocation(D3D12DescriptorAllocation&& other) noexcept
        : mAllocator(other.mAllocator), mIndex(other.mIndex)
    {
        other.mAllocator = nullptr;
    }

    D3D12DescriptorAllocation& operator=(D3D12DescriptorAllocation&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            mAllocator = other.mAllocator;
            mIndex = other.mIndex;
            other.mAllocator = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const { return mAllocator != nullptr; }
    [[nodiscard]] UINT GetIndex() const { return mIndex; }

private:
    void Release()
    {
        if (mAllocator)
        {
            mAllocator->Release(mIndex);
            mAllocator = nullptr;
        }
    }

    D3D12DescriptorIndexAllocator* mAllocator = nullptr;
    UINT mIndex = 0;
};

/// <summary>descriptor 핸들 계산을 위해 인덱스 풀을 shader-visible 힙에 묶는다.</summary>
class D3D12DescriptorHeapAllocator final
{
public:
    void Configure(ID3D12DescriptorHeap& heap, const UINT descriptorSize, const UINT capacity)
    {
        mHeap = &heap;
        mDescriptorSize = descriptorSize;
        mIndices.Configure(capacity);
    }

    /// <summary>Number of slots this heap was created with.</summary>
    [[nodiscard]] UINT GetCapacity() const { return mIndices.GetCapacity(); }

    /// <summary>Reserves one slot. The result is invalid when every descriptor is in use.</summary>
    [[nodiscard]] D3D12DescriptorAllocation Allocate()
    {
        UINT index = 0;
        if (!mHeap || !mIndices.TryAllocate(index))
        {
            return {};
        }
        return { mIndices, index };
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(const UINT index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = mHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * mDescriptorSize;
        return handle;
    }

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(const UINT index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = mHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * mDescriptorSize;
        return handle;
    }

private:
    ID3D12DescriptorHeap* mHeap = nullptr;
    UINT mDescriptorSize = 0;
    D3D12DescriptorIndexAllocator mIndices;
};

}
