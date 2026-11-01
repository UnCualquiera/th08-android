#pragma once

namespace th08
{
namespace modern
{

bool ConfigureDataDirectory();
void InstallCrashReporter();
void LogArchiveRequest(const char *path);
void WriteAndroidBootStage(const char *status, const char *stage);

} // namespace modern
} // namespace th08
