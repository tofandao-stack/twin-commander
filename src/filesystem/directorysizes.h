#pragma once
#include "filesystem/fileanalysis.h"
class QWidget;
namespace DirectorySizes {
FileAnalysis::Statistics calculate(const std::vector<std::filesystem::path> &paths,
                                   const std::shared_ptr<LocalFiles::Control> &control);
bool confirmPreflight(const std::vector<std::filesystem::path> &paths, QWidget *parent, QString &summary);
} // namespace DirectorySizes
