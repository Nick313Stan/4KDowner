#pragma once

#include <filesystem>

bool SetWorkingDirectoryToExecutable();

// Documents/4KDownerTemp path (does not create the directory).
std::filesystem::path GetDocuments4KDownerTempPath();

// Walk up from cwd looking for relativePath; returns absolute path if found, else relativePath.
std::filesystem::path FindAssetPath(const std::filesystem::path& relativePath);
