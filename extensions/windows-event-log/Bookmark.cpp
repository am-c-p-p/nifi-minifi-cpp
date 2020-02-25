#include "Bookmark.h"

#include <direct.h>

#include "utils/file/FileUtils.h"
#include "utils/ScopeGuard.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

Bookmark::Bookmark(const std::wstring& channel, const std::wstring& query, const std::string& bookmarkRootDir, const std::string& uuid, std::shared_ptr<logging::Logger> logger)
  :logger_(logger) {
  if (!createUUIDDir(bookmarkRootDir, uuid, filePath_))
    return;

  filePath_ += "Bookmark.txt";

  if (!getBookmarkXmlFromFile(bookmarkXml_)) {
    return;
  }

  if (!bookmarkXml_.empty()) {
    if (hBookmark_ = EvtCreateBookmark(bookmarkXml_.c_str())) {
      ok_ = true;
      return;
    }

    logger_->log_error("!EvtCreateBookmark error: %d.", GetLastError());

    bookmarkXml_.clear();
    if (!createEmptyBookmarkXmlFile()) {
      return;
    }
  }

  if (!(hBookmark_ = EvtCreateBookmark(0))) {
    logger_->log_error("!EvtCreateBookmark error: %d.", GetLastError());
    return;
  }

  const auto hEventResults = EvtQuery(0, channel.c_str(), query.c_str(), EvtQueryChannelPath);
  if (!hEventResults) {
    logger_->log_error("!EvtQuery error: %d.", GetLastError());
    return;
  }
  const utils::ScopeGuard guard_hEventResults([hEventResults]() { EvtClose(hEventResults); });

  if (!EvtSeek(hEventResults, 0, 0, 0, EvtSeekRelativeToLast)) {
    logger_->log_error("!EvtSeek error: %d.", GetLastError());
    return;
  }

  DWORD dwReturned{};
  EVT_HANDLE hEvent{};
  if (!EvtNext(hEventResults, 1, &hEvent, INFINITE, 0, &dwReturned)) {
    logger_->log_error("!EvtNext error: %d.", GetLastError());
    return;
  }

  ok_ = saveBookmark(hEvent);
}

Bookmark::~Bookmark() {
  if (hBookmark_) {
    EvtClose(hBookmark_);
  }
}

Bookmark::operator bool() const {
  return ok_;
}
  
EVT_HANDLE Bookmark::getBookmarkHandleFromXML() {
  if (hBookmark_) {
    EvtClose(hBookmark_);
    hBookmark_ = 0;
  }

  hBookmark_ = EvtCreateBookmark(bookmarkXml_.c_str());
  if (!(hBookmark_ = EvtCreateBookmark(bookmarkXml_.c_str()))) {
    logger_->log_error("!EvtCreateBookmark error: %d.", GetLastError());
    return 0;
  }

  return hBookmark_;
}

bool Bookmark::saveBookmark(EVT_HANDLE hEvent)
{
  std::wstring bookmarkXml;
  if (!getNewBookmarkXml(hEvent, bookmarkXml)) {
    return false;
  }

  saveBookmarkXml(bookmarkXml);

  return true;
}

bool Bookmark::getNewBookmarkXml(EVT_HANDLE hEvent, std::wstring& bookmarkXml) {
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

      bookmarkXml = &buf[0];

      return true;
    }
    if (ERROR_SUCCESS != (status = GetLastError())) {
      logger_->log_error("!EvtRender error: %d.", GetLastError());
      return false;
    }
  }

  return false;
}

void Bookmark::saveBookmarkXml(const std::wstring& bookmarkXml) {
  bookmarkXml_ = bookmarkXml;

  // Write new bookmark over old and in the end write '!'. Then new bookmark is read until '!'. This is faster than truncate.
  file_.seekp(std::ios::beg);

  file_ << bookmarkXml << L'!';

  file_.flush();
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

bool Bookmark::createUUIDDir(const std::string& bookmarkRootDir, const std::string& uuid, std::string& dir)
{
  if (bookmarkRootDir.empty()) {
    dir.clear();
    return false;
  }

  auto dirWithBackslash = bookmarkRootDir;
  if (bookmarkRootDir.back() != '\\') {
    dirWithBackslash += '\\';
  }
  
  dir = dirWithBackslash + "uuid\\" + uuid + "\\";

  utils::file::FileUtils::create_dir(dir);

  auto dirCreated = utils::file::FileUtils::is_directory(dir.c_str());
  if (!dirCreated) {
    logger_->log_error("Cannot create %s", dir.c_str());
    dir.clear();
  }

  return dirCreated;
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
    logger_->log_error("No '!' in bookmarXml '%ls'", bookmarkXml.c_str());
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
