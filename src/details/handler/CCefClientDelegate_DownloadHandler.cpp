#include "details/CCefClientDelegate.h"

#include <QDebug>
#include <QSharedPointer>
#include <QThread>

#include "details/QCefDownloadItemPrivate.h"
#include "details/QCefViewPrivate.h"
#include "details/utils/CommonUtils.h"
#include "details/utils/ValueConvertor.h"

void
CCefClientDelegate::onBeforeDownload(CefRefPtr<CefBrowser>& browser,
                                     CefRefPtr<CefDownloadItem>& download_item,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback>& callback)
{
  FLog();
  AcquireAndValidateCefViewPrivate(pCefViewPrivate);

  // qDebug() << "onBeforeDownload, percent: " << download_item->GetPercentComplete() << "% \n"
  //          << download_item->GetTotalBytes() << "/" << download_item->GetReceivedBytes() << "\n"
  //          << "  inProgress: " << download_item->IsInProgress() << "\n"
  //          << "  canceled: " << download_item->IsCanceled() << "\n"
  //          << "  complete: " << download_item->IsComplete();

  // get id
  auto id = download_item->GetId();

  // process item
  QWeakPointer<QCefDownloadItem> weakRefItem;
  {
    QSharedPointer<QCefDownloadItem> item;
    auto it = pendingDownloadItemMap_.find(id);

    // find
    if (it != pendingDownloadItemMap_.end()) {
      // found, obtain item and remove it from pending map
      item = it.value();
      pendingDownloadItemMap_.erase(it);
    }

    // validate
    if (!item) {
      // pending item not found or invalid, create new pending item
      item = QCefDownloadItemPrivate::create(shared_from_this());
    }

    // set suggested file name
    auto suggestedFileName = QString::fromStdString(suggested_name);
    QCefDownloadItemPrivate::setSuggestedName(item.data(), suggestedFileName);

    // update
    QCefDownloadItemPrivate::update(item.data(), *(download_item.get()));
    QCefDownloadItemPrivate::setBeforeDownloadCallback(item.data(), callback);

    // notify user of the new download item
    weakRefItem = item;

    // marshal to main UI thread and need to block
    runInMainThreadAndWait([&]() { pCefViewPrivate->onNewDownloadItem(item, suggestedFileName); });

    // release the item
    item.reset();
  }

  // check whether user kept the item or not
  auto item = weakRefItem.lock();
  if (item) {
    // user kept the item, keep it in confirmed map
    confirmedDownloadItemMap_[id] = item;
  } else {
    // user discarded this, place a null item into the pending map
    pendingDownloadItemMap_[id] = nullptr;
  }
}

void
CCefClientDelegate::onDownloadUpdated(CefRefPtr<CefBrowser>& browser,
                                      CefRefPtr<CefDownloadItem>& download_item,
                                      CefRefPtr<CefDownloadItemCallback>& callback)
{
  FLog();

  // --- DIAGNOSTIC: check if CEF delivers new progress ---
  qDebug() << "[DL_DIAG] onDownloadUpdated: id=" << download_item->GetId()
           << " percent=" << download_item->GetPercentComplete() << "%"
           << " received=" << download_item->GetReceivedBytes()
           << " total=" << download_item->GetTotalBytes()
           << " inProgress=" << download_item->IsInProgress()
           << " canceled=" << download_item->IsCanceled()
           << " complete=" << download_item->IsComplete()
           << " isValid=" << download_item->IsValid();

  AcquireAndValidateCefViewPrivate(pCefViewPrivate);
  qDebug() << "[DL_DIAG] AcquireAndValidateCefViewPrivate PASSED";

  if (!download_item->IsValid()) {
    qDebug() << "[DL_DIAG] download_item->IsValid() == false, return";
    return;
  }

  // get id
  auto id = download_item->GetId();

  // find in confirmed map
  {
    auto it = confirmedDownloadItemMap_.find(id);
    if (it != confirmedDownloadItemMap_.end()) {
      qDebug() << "[DL_DIAG] found in confirmedDownloadItemMap_";
      // confirmed item found
      auto item = it.value().lock();
      if (!item) {
        // TODO(sheen): something went wrong, but i haven't got a good solution
        qDebug() << "[DL_DIAG] WARNING: weak pointer expired, erasing entry";
        confirmedDownloadItemMap_.erase(it);
        return;
      }

      qDebug() << "[DL_DIAG] isStarted=" << item->isStarted();
      if (!item->isStarted()) {
        // not started by user yet, pause it
        qDebug() << "[DL_DIAG] not started, calling callback->Pause()";
        callback->Pause();
        return;
      }

      // update
      QCefDownloadItemPrivate::update(item.data(), *(download_item.get()));
      QCefDownloadItemPrivate::setDownloadItemCallback(item.data(), callback);

      // If the user hasn't explicitly paused, ensure download is resumed.
      // Use the fresh callback here (not the potentially stale one stored in d_ptr)
      // because CefBeforeDownloadCallback::Continue() may invalidate old callbacks.
      if (!QCefDownloadItemPrivate::isUserPaused(item.data())) {
        callback->Resume();
      }

      qDebug() << "[DL_DIAG] updated QCefDownloadItem, queuing onUpdateDownloadItem";

      // notify (marshal to main UI thread but no need to block)
      runInMainThread([pCefViewPrivate, item]() { pCefViewPrivate->onUpdateDownloadItem(item); });

      // check status
      if (download_item->IsCanceled() || download_item->IsComplete()) {
        qDebug() << "[DL_DIAG] download canceled or complete, removing from confirmed map";
        confirmedDownloadItemMap_.remove(id);
        return;
      }

      return;
    }
  }

  qDebug() << "[DL_DIAG] NOT found in confirmedDownloadItemMap_";

  // check status
  if (download_item->IsCanceled() || download_item->IsComplete()) {
    qDebug() << "[DL_DIAG] download canceled or complete, removing from pending map";
    pendingDownloadItemMap_.remove(id);
    return;
  }

  // find in pending map
  {
    QSharedPointer<QCefDownloadItem> item;
    auto it = pendingDownloadItemMap_.find(id);

    // find
    if (it == pendingDownloadItemMap_.end()) {
      qDebug() << "[DL_DIAG] NOT found in pendingDownloadItemMap_, creating new pending item + Pause";
      // not found, this is new download item
      item = QCefDownloadItemPrivate::create(shared_from_this());
      pendingDownloadItemMap_[id] = item;

      // pause it
      callback->Pause();
    } else {
      qDebug() << "[DL_DIAG] found in pendingDownloadItemMap_";
      // found,
      item = it.value();

      // validate
      if (!item) {
        qDebug() << "[DL_DIAG] pending item is null (discarded by user), return";
        // null item, it means this download item has been discarded by the user
        return;
      }
    }

    // update pending item
    QCefDownloadItemPrivate::update(item.data(), *(download_item.get()));
    QCefDownloadItemPrivate::setDownloadItemCallback(item.data(), callback);
    qDebug() << "[DL_DIAG] updated pending item (no notification to user)";
  }
}
