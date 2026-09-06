#include "pch.h"
#include "TextFile.h"

#include <fstream>
#include <ios>
#include <iterator>

namespace GameEngine::Platform
{

std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::string contents{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    // 스트림이 읽다 실패했는지는 다 읽은 뒤에야 알 수 있다. 여기서 묻지 않으면 잘린 내용을
    // 온전한 파일로 넘기게 된다.
    if (stream.bad())
    {
        return std::nullopt;
    }
    return contents;
}

std::string_view FileWriteResult::Describe() const
{
    switch (error)
    {
    case FileWriteError::None:
        return "the file was written";
    case FileWriteError::WriteFailed:
        return "the file could not be written, and what was there is unchanged";
    case FileWriteError::ReplaceFailed:
        return "the new contents were written but could not take the file's place, so the file "
               "still holds what it did";
    }
    return "unknown";
}

FileWriteResult WriteTextFile(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    return stream ? FileWriteResult{} : FileWriteResult{ FileWriteError::WriteFailed, {} };
}

FileWriteResult WriteTextFileAtomically(
    const std::filesystem::path& path, const std::string_view text)
{
    std::filesystem::path temporaryPath = path;
    temporaryPath += ".tmp";

    if (const FileWriteResult written = WriteTextFile(temporaryPath, text); !written)
    {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        return written;
    }

    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (error)
    {
        // 임시 파일을 치운다. 두고 가면 다음 쓰기가 「자리에 뭔가 있다」로 막히고, 그 이유가
        // 지금의 실패와 이어져 있다는 것을 아무도 모른다.
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        return { FileWriteError::ReplaceFailed, error };
    }
    return {};
}

}
