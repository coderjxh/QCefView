#include "details/CCefClientDelegate.h"

#include <QDebug>
#include <QDir>
#include <QSharedPointer>
#include <QThread>

#include "details/QCefViewPrivate.h"
#include "details/utils/CommonUtils.h"
#include "details/utils/ValueConvertor.h"

namespace {

QCefFileDialogRequest::Mode
toQtFileDialogMode(CefBrowserHost::FileDialogMode mode)
{
  switch (mode) {
    case FILE_DIALOG_OPEN:
      return QCefFileDialogRequest::Open;
    case FILE_DIALOG_OPEN_MULTIPLE:
      return QCefFileDialogRequest::OpenMultiple;
    case FILE_DIALOG_OPEN_FOLDER:
      return QCefFileDialogRequest::OpenFolder;
    case FILE_DIALOG_SAVE:
      return QCefFileDialogRequest::Save;
    default:
      return QCefFileDialogRequest::Open;
  }
}

QStringList
toQStringList(const std::vector<CefString>& values)
{
  QStringList result;
  result.reserve(static_cast<int>(values.size()));
  for (const auto& value : values) {
    result << QString::fromStdString(value.ToString());
  }
  return result;
}

class CefFileDialogCallbackAdapter : public QCefFileDialogCallback
{
public:
  explicit CefFileDialogCallbackAdapter(CefRefPtr<CefFileDialogCallback> callback, int selectedAcceptFilter)
    : callback_(callback)
    , selectedAcceptFilter_(selectedAcceptFilter)
  {}

  void continueWithFiles(const QStringList& filePaths) override
  {
    if (finished_ || !callback_) {
      return;
    }

    finished_ = true;

    std::vector<CefString> cefFilePaths;
    cefFilePaths.reserve(static_cast<size_t>(filePaths.size()));
    for (const auto& filePath : filePaths) {
      cefFilePaths.push_back(QDir::toNativeSeparators(filePath).toStdString());
    }

#if CEF_VERSION_MAJOR < 102
    callback_->Continue(selectedAcceptFilter_, cefFilePaths);
#else
    callback_->Continue(cefFilePaths);
#endif
  }

  void cancel() override
  {
    if (finished_ || !callback_) {
      return;
    }

    finished_ = true;
    callback_->Cancel();
  }

private:
  CefRefPtr<CefFileDialogCallback> callback_;
  int selectedAcceptFilter_ = -1;
  bool finished_ = false;
};

} // namespace

bool
CCefClientDelegate::onFileDialog(CefRefPtr<CefBrowser>& browser,
                                 CefBrowserHost::FileDialogMode mode,
                                 const CefString& title,
                                 const CefString& default_file_path,
                                 const std::vector<CefString>& accept_filters,
#if CEF_VERSION_MAJOR >= 126
                                 const std::vector<CefString>& accept_extensions,
                                 const std::vector<CefString>& accept_descriptions,
#endif
#if CEF_VERSION_MAJOR < 102
                                 int selected_accept_filter,
#endif
                                 CefRefPtr<CefFileDialogCallback>& callback)
{
  AcquireAndValidateCefViewPrivateWithReturn(pCefViewPrivate, false);

  auto q = pCefViewPrivate->q_ptr;
  if (!q) {
    return false;
  }

  QCefFileDialogRequest request;
  request.mode = toQtFileDialogMode(mode);
  request.title = QString::fromStdString(title.ToString());
  request.defaultFilePath = QString::fromStdString(default_file_path.ToString());
  request.acceptFilters = toQStringList(accept_filters);
#if CEF_VERSION_MAJOR >= 126
  request.acceptExtensions = toQStringList(accept_extensions);
  request.acceptDescriptions = toQStringList(accept_descriptions);
#endif
#if CEF_VERSION_MAJOR < 102
  request.selectedAcceptFilter = selected_accept_filter;
#endif

  auto callbackAdapter = QSharedPointer<QCefFileDialogCallback>(new CefFileDialogCallbackAdapter(callback,
#if CEF_VERSION_MAJOR < 102
                                                                                                 selected_accept_filter
#else
                                                                                                 -1
#endif
                                                                                                 ));

  bool handled = false;
  runInMainThreadAndWait([&]() { handled = q->onFileDialog(request, callbackAdapter); });
  return handled;
}
