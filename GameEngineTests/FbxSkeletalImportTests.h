#pragma once

#include <filesystem>

[[nodiscard]] bool RunFbxSkeletalImportTests();

/// <summary>Explicit integration check for the external 34-bone, five-clip reference model.</summary>
[[nodiscard]] bool RunExternalFbxSkeletalImportTests(const std::filesystem::path& fixture);
