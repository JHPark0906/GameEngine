#include "pch.h"
#include "ApplicationContent.h"

#include "DirectoryContentSource.h"
#include "PackedContentSource.h"
#include "PlatformServices.h"
#include "../Diagnostics/Debug.h"

namespace GameEngine::Platform
{

namespace
{
    /// <summary>
    /// 실행 파일에 packed된 콘텐츠가 옆의 파일들을 이긴다.
    ///
    /// 배포된 게임은 팩이 있고 그 밖에 찾을 것이 없다. 개발 빌드는 팩이 없어 디렉터리를 읽으므로,
    /// 텍스처를 고치고 다시 실행하는 데 패킹 단계가 필요 없다. 팩을 우선하면 packed 실행 파일이
    /// 자기가 복사된 폴더에 우연히 놓인 것들을 무시하게 되기도 하는데, 그것이 파일 하나로
    /// 배포하는 목적이다.
    /// </summary>
    class ApplicationContent final
    {
    public:
        ApplicationContent()
            : mPacked(PlatformServices::GetExecutablePath())
            , mDirectory(PlatformServices::GetExecutableDirectory())
        {
            if (mPacked.IsValid())
            {
                return;
            }
            if (!mDirectory.IsValid())
            {
                Diagnostics::Debug::LogError(
                    "This application has neither packed content nor a content directory. path=",
                    mDirectory.GetRootPath().string());
            }
        }

        [[nodiscard]] const IContentSource& Get() const
        {
            return mPacked.IsValid() ? static_cast<const IContentSource&>(mPacked) : mDirectory;
        }

        [[nodiscard]] bool IsPacked() const { return mPacked.IsValid(); }

    private:
        PackedContentSource mPacked;
        DirectoryContentSource mDirectory;
    };
}

namespace
{
    /// <summary>
    /// 정적 초기화 시점이 아니라 처음 쓰일 때 만들어진다: 플랫폼에 실행 파일이 어디 있는지
    /// 묻는데, 다른 번역 단위의 초기화가 돌기 전에 플랫폼 계층이 준비되어 있다는 보장이 없다.
    /// </summary>
    [[nodiscard]] const ApplicationContent& GetContent()
    {
        static const ApplicationContent content;
        return content;
    }
}

const IContentSource& GetApplicationContent()
{
    return GetContent().Get();
}

bool IsApplicationContentPacked()
{
    return GetContent().IsPacked();
}

}
