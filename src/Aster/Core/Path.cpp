#include "Path.h"
#include <regex>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace aster
{

std::string getFileName(const std::string &filePath)
{
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash == filePath.size() - 1)
    return "";
  return filePath.substr(lastSlash + 1);
}

std::string getFileExtension(const std::string &filePath)
{
  size_t dot = filePath.find_last_of(".");
  if (dot == filePath.size() - 1)
    return "";
  return filePath.substr(dot + 1);
}

std::string getDirectory(const std::string &filePath)
{
  size_t lastSlash = filePath.find_last_of("/\\");
  if (lastSlash == filePath.size() - 1)
    return "";

  if (filePath.substr(lastSlash + 1, filePath.size()).find('.') != std::string::npos)
    return filePath.substr(0, lastSlash + 1);
  else
    return filePath;
}

std::string join(const std::string &dir, const std::string &fileName)
{
  if (dir.empty() || dir.back() == '/' || dir.back() == '\\')
  {
    return dir + fileName;
  }
  else
  {
    return dir + "/" + fileName;
  }
}

bool MakeDir(const std::string &dir)
{
  fs::path dir_path(dir);
  if (fs::exists(dir_path))
  {
    return fs::is_directory(dir_path);
  }
  if (dir.find('.') != std::string::npos)
    return false;
  try
  {
    return fs::create_directories(dir_path);
  }
  catch (const fs::filesystem_error &e)
  {
    std::cerr << "Filesystem error: " << e.what() << std::endl;
    return false;
  }
}

Path::Path(const std::string &p)
{
  path = p;
  std::replace(path.begin(), path.end(), '\\', '/');
  path = std::regex_replace(path, std::regex("/+"), "/");
  if (path.length() > 1 && path.back() == '/')
  {
    path.pop_back();
  }
}

std::string Path::filenameNoExtension() const
{
  std::string p = filename();
  size_t dot = p.find_last_of(".");
  if (dot == p.size())
    return "";
  return p.substr(0, dot);
}

bool Path::exist()
{
  try
  {
    fs::path dir_path(path);
    if (fs::exists(dir_path))
    {
      return true;
    }
    return false;
  }
  catch (const fs::filesystem_error &e)
  {
    std::cerr << "Filesystem error: " << e.what() << std::endl;
    return false;
  }
}

bool Path::isDirectory()
{
  try
  {
    fs::path dir_path(path);
    if (fs::exists(dir_path))
    {
      return fs::is_directory(dir_path);
    }
    return false;
  }
  catch (const fs::filesystem_error &e)
  {
    std::cerr << "Filesystem error: " << e.what() << std::endl;
    return false;
  }
}

} // namespace aster
