#include "pch.h"
#include "Win32ComApartment.h"

#include <windows.h>

#include "Win32Diagnostics.h"

namespace GameEngine::Platform::Win32
{

ComApartment::~ComApartment()
{
    if (mUninitializeRequired)
    {
        CoUninitialize();
    }
}

bool ComApartment::Initialize()
{
    if (mReady)
    {
        return true;
    }

    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(result))
    {
        mReady = true;
        mUninitializeRequired = true;
        return true;
    }
    if (result == RPC_E_CHANGED_MODE)
    {
        // Another component already joined this thread to a single-threaded apartment. COM is usable,
        // but this object must not uninitialize an apartment it did not enter.
        mReady = true;
        return true;
    }

    LogHResult("COM apartment initialization", result);
    return false;
}

}
