#include "Bookmark.h"

#include <direct.h>

#include "utils/file/FileUtils.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

Bookmark::Bookmark(const std::string& uuid, std::shared_ptr<logging::Logger> logger)
  :logger_(logger) {
  if (createUUIDDir(uuid, filePath_)) {
    filePath_ += "Bookmark.txt";
  }

  std::wstring bookmarkXml;
  if (!filePath_.empty() && !getBookmarkXmlFromFile(bookmarkXml)) {
    return;
  }

  if (bookmarkXml.empty()) {
    if (!(hBookmark_ = EvtCreateBookmark(0))) {
      logger_->log_error("!EvtCreateBookmark error: %d", GetLastError());
      return;
    }

    hasBookmarkXml_ = false;
  } else {
    if (!(hBookmark_ = EvtCreateBookmark(bookmarkXml.c_str()))) {
      logger_->log_error("!EvtCreateBookmark error: %d bookmarkXml_ '%s'", GetLastError(), bookmarkXml.c_str());

      // BookmarkXml can be corrupted - create hBookmark_, and create empty file. 
      if (!(hBookmark_ = EvtCreateBookmark(0))) {
        logger_->log_error("!EvtCreateBookmark error: %d", GetLastError());
        return;
      }

      hasBookmarkXml_ = false;

      ok_ = createEmptyBookmarkXmlFile();

      return;
    }

    hasBookmarkXml_ = true;
  }

  ok_ = true;
}

Bookmark::~Bookmark() {
  if (file_.is_open()) {
    file_.close();
  }

  if (hBookmark_) {
    EvtClose(hBookmark_);
  }
}

Bookmark::operator bool() const {
  return ok_;
}
  
bool Bookmark::hasBookmarkXml() const {
  return hasBookmarkXml_;
}

EVT_HANDLE Bookmark::bookmarkHandle() const {
  return hBookmark_;
}

bool Bookmark::saveBookmark(EVT_HANDLE hEvent)
{
  if (!EvtUpdateBookmark(hBookmark_, hEvent)) {
    logger_->log_error("!EvtUpdateBookmark error: %d.", GetLastError());
    return false;
  }

  // Render the bookmark as an XML string that can be persisted.
  DWORD bufferSize{};
  DWORD bufferUsed{};
  DWORD propertyCount{};
  if (!EvtRender(0, hBookmark_, EvtRenderBookmark, bufferSize, 0, &bufferUsed, &propertyCount)) {
    DWORD status = ERROR_SUCCESS;
    if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError())) {
      bufferSize = bufferUsed;

      std::vector<wchar_t> buf(bufferSize / 2 + 1);

      if (!EvtRender(0, hBookmark_, EvtRenderBookmark, bufferSize, &buf[0], &bufferUsed, &propertyCount)) {
        logger_->log_error("!EvtRender error: %d.", GetLastError());
        return false;
      }

      // Write new bookmark over old and in the end write '!'. Then new bookmark is read until '!'. This is faster than truncate.
      file_.seekp(std::ios::beg);

      file_ << &buf[0] << L'!';

      file_.flush();
    }
    else if (ERROR_SUCCESS != (status = GetLastError())) {
      logger_->log_error("!EvtRender error: %d.", GetLastError());
      return false;
    }
  }

  return true;
}

bool Bookmark::createEmptyBookmarkXmlFile() {
  if (file_.is_open()) {
    file_.close();
  }

  file_.open(filePath_, std::ios::out);
  if (!file_.is_open()) {
    logger_->log_error("Cannot open %s", filePath_.c_str());
    return false;
  }

  return true;
}

// Creates directory "processor_repository\ConsumeWindowsEventLog\uuid\{uuid}" under "root" directory.
bool Bookmark::createUUIDDir(const std::string& uuid, std::string& dir)
{
  // Returns root directory with backslash.
  auto getRootDirectory = [](std::string& rootDir, std::shared_ptr<logging::Logger> logger)
  {
    auto dir = utils::file::FileUtils::get_executable_dir();
    if (dir.back() == '\\') {
      dir.pop_back();
    }

    auto pBackslash = strrchr(dir.c_str(), '\\');
    if (!pBackslash) {
      logger->log_error("!pBackslash");
      return false;
    }
    auto posBackslash = pBackslash - dir.c_str();
    dir.resize(posBackslash + 1);

    rootDir = dir;

    return true;
  };

  dir.clear();

  if (!getRootDirectory(dir, logger_))
    return false;

  for (const auto& curDir : std::vector<std::string>{"processor_repository", "ConsumeWindowsEventLog", "uuid", uuid}) {
    dir += curDir + '\\';
    utils::file::FileUtils::create_dir(dir, false);
  }

  return utils::file::FileUtils::is_directory(dir.c_str());
}

bool Bookmark::getBookmarkXmlFromFile(std::wstring& bookmarkXml) {
  bookmarkXml.clear();

  std::wifstream file(filePath_);
  if (!file.is_open()) {
    return createEmptyBookmarkXmlFile();
  }

  // Generically is not efficient, but bookmarkXML is small ~100 bytes. 
  wchar_t c;
  do {
    file.read(&c, 1);
    if (!file) {
      break;
    }

    bookmarkXml += c;
  } while (true);

  file.close();

  file_.open(filePath_);
  if (!file_.is_open()) {
    logger_->log_error("Cannot open %s", filePath_.c_str());
    bookmarkXml.clear();
    return false;
  }

  if (bookmarkXml.empty()) {
    return true;
  }

  // '!' should be at the end of bookmark.
  auto pos = bookmarkXml.find(L'!');
  if (std::wstring::npos == pos) {
    logger_->log_error("No '!' in bookmarXml '%s'", bookmarkXml.c_str());
    bookmarkXml.clear();
    return createEmptyBookmarkXmlFile();
  }

  // Remove '!'.
  bookmarkXml.resize(pos);

  return true;
}

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
