/*
 * Peony-Qt
 *
 * Copyright (C) 2019, Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Yue Lan <lanyue@kylinos.cn>
 *
 */

#include "peony-desktop-application.h"
#include "desktop-icon-view.h"

#include "icon-view-style.h"
#include "desktop-icon-view-delegate.h"

#include "desktop-item-model.h"
#include "desktop-item-proxy-model.h"

#include "file-operation-manager.h"
#include "file-move-operation.h"
#include "file-copy-operation.h"
#include "file-trash-operation.h"
#include "clipboard-utils.h"

#include "properties-window.h"
#include "file-utils.h"
#include "file-operation-utils.h"

#include "desktop-menu.h"
#include "desktop-window.h"

#include "file-item-model.h"
#include "file-info-job.h"
#include "file-launch-manager.h"
#include <QProcess>

#include <QDesktopServices>

#include "desktop-index-widget.h"

#include "file-meta-info.h"

#include "global-settings.h"

#include <QAction>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include <QHoverEvent>

#include <QWheelEvent>
#include <QApplication>

#include <QDebug>

using namespace Peony;

#define ITEM_POS_ATTRIBUTE "metadata::peony-qt-desktop-item-position"

DesktopIconView::DesktopIconView(QWidget *parent) : QListView(parent)
{
    m_refresh_timer.setInterval(500);
    m_refresh_timer.setSingleShot(true);

    installEventFilter(this);

    initShoutCut();
    //initMenu();
    initDoubleClick();

    connect(qApp, &QApplication::paletteChanged, this, [=]() {
        viewport()->update();
    });

    m_edit_trigger_timer.setSingleShot(true);
    m_edit_trigger_timer.setInterval(3000);
    m_last_index = QModelIndex();

    setContentsMargins(0, 0, 0, 0);
    setAttribute(Qt::WA_TranslucentBackground);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    //fix rubberband style.
    setStyle(DirectoryView::IconViewStyle::getStyle());

    setItemDelegate(new DesktopIconViewDelegate(this));

    setDefaultDropAction(Qt::MoveAction);

    setSelectionMode(QListView::ExtendedSelection);
    setEditTriggers(QListView::NoEditTriggers);
    setViewMode(QListView::IconMode);
    setMovement(QListView::Snap);
    setFlow(QListView::TopToBottom);
    setResizeMode(QListView::Adjust);
    setWordWrap(true);

    setDragDropMode(QListView::DragDrop);

    //setContextMenuPolicy(Qt::CustomContextMenu);
    setSelectionMode(QListView::ExtendedSelection);

    auto zoomLevel = this->zoomLevel();
    setDefaultZoomLevel(zoomLevel);

//#if QT_VERSION > QT_VERSION_CHECK(5, 12, 0)
//    QTimer::singleShot(500, this, [=](){
//#else
//    QTimer::singleShot(500, [=](){
//#endif
//        connect(this->selectionModel(), &QItemSelectionModel::selectionChanged, [=](const QItemSelection &selection, const QItemSelection &deselection){
//            //qDebug()<<"selection changed";
//            m_real_do_edit = false;
//            this->setIndexWidget(m_last_index, nullptr);
//            auto currentSelections = this->selectionModel()->selection().indexes();

//            if (currentSelections.count() == 1) {
//                //qDebug()<<"set index widget";
//                m_last_index = currentSelections.first();
//                auto delegate = qobject_cast<DesktopIconViewDelegate *>(itemDelegate());
//                this->setIndexWidget(m_last_index, new DesktopIndexWidget(delegate, viewOptions(), m_last_index, this));
//            } else {
//                m_last_index = QModelIndex();
//                for (auto index : deselection.indexes()) {
//                    this->setIndexWidget(index, nullptr);
//                }
//            }
//        });
//    });

    m_model = new DesktopItemModel(this);
    m_proxy_model = new DesktopItemProxyModel(m_model);

    m_proxy_model->setSourceModel(m_model);

    //connect(m_model, &DesktopItemModel::dataChanged, this, &DesktopIconView::clearAllIndexWidgets);

    connect(m_model, &DesktopItemModel::refreshed, this, [=]() {
        this->updateItemPosistions(nullptr);
        this->m_is_refreshing = false;
        if (isItemsOverlapped()) {
            QHash<QModelIndex, QRect> currentIndexesRects;
            for (int i = 0; i < m_proxy_model->rowCount(); i++) {
                auto tmp = m_proxy_model->index(i, 0);
                currentIndexesRects.insert(tmp, visualRect(tmp));
            }

            QModelIndexList overlappedIndexes;
            for (auto value : currentIndexesRects.values()) {
                auto keys = currentIndexesRects.keys(value);
                if (keys.count() > 1) {
                    keys.removeFirst();
                    overlappedIndexes<<keys;
                }
            }

            auto grid = this->gridSize();
            auto viewRect = this->rect();

            QRegion notEmptyRegion;
            for (auto rect : currentIndexesRects) {
                notEmptyRegion += rect;
            }

            for (auto overrlappedIndex : overlappedIndexes) {
                auto indexRect = QListView::visualRect(overrlappedIndex);
                if (notEmptyRegion.contains(indexRect.center())) {
                    // move index to closest empty grid.
                    auto next = indexRect;
                    bool isEmptyPos = false;
                    while (!isEmptyPos) {
                        next.translate(0, grid.height());
                        if (next.bottom() > viewRect.bottom()) {
                            int top = next.y();
                            while (true) {
                                if (top < gridSize().height()) {
                                    break;
                                }
                                top-=gridSize().height();
                            }
                            //put item to next column first column
                            next.moveTo(next.x() + grid.width(), top);
                        }
                        if (notEmptyRegion.contains(next.center()))
                            continue;

                        isEmptyPos = true;
                        setPositionForIndex(next.topLeft(), overrlappedIndex);
                        notEmptyRegion += next;
                    }
                }
            }

            saveAllItemPosistionInfos();
        }
    });

    connect(m_model, &DesktopItemModel::requestClearIndexWidget, this, &DesktopIconView::clearAllIndexWidgets);

    connect(m_model, &DesktopItemModel::requestLayoutNewItem, this, [=](const QString &uri) {
        auto index = m_proxy_model->mapFromSource(m_model->indexFromUri(uri));
        //qDebug()<<"=====================layout new item"<<index.data();

        //qDebug()<<"=====================find a new empty place put new item";
        auto rect = QRect(QPoint(0, 0), gridSize());
        rect.moveTo(this->contentsMargins().left(), this->contentsMargins().top());

        auto grid = this->gridSize();
        auto viewRect = this->rect();
        auto next = rect;
        while (true) {
            if (this->indexAt(next.center()).data(Qt::UserRole).toString() == uri) {
                this->setPositionForIndex(next.topLeft(), index);
                this->saveItemPositionInfo(uri);
                return;
            }
            if (!this->indexAt(next.center()).isValid()) {
                //put it into empty
                qDebug()<<"put"<<index.data()<<next.topLeft();
                this->setPositionForIndex(next.topLeft(), index);
                this->saveItemPositionInfo(uri);
                break;
            }
            //aligin.
            next = QListView::visualRect(this->indexAt(next.center()));
            next.translate(0, grid.height());
            if (next.bottom() > viewRect.bottom()) {
                int top = next.y();
                while (true) {
                    if (top < gridSize().height()) {
                        break;
                    }
                    top-=gridSize().height();
                }
                //put item to next column first column
                next.moveTo(next.x() + grid.width(), top);
            }
        }
    });

    connect(m_model, &DesktopItemModel::requestUpdateItemPositions, this, &DesktopIconView::updateItemPosistions);

    connect(m_model, &DesktopItemModel::fileCreated, this, [=](const QString &uri) {
        if (m_new_files_to_be_selected.isEmpty()) {
            m_new_files_to_be_selected<<uri;

            QTimer::singleShot(500, this, [=]() {
                if (this->state() & QAbstractItemView::EditingState)
                    return;
                this->setSelections(m_new_files_to_be_selected);
                m_new_files_to_be_selected.clear();
            });
        } else {
            if (!m_new_files_to_be_selected.contains(uri)) {
                m_new_files_to_be_selected<<uri;
            }
        }
        refresh();
    });

    connect(m_proxy_model, &QSortFilterProxyModel::layoutChanged, this, [=]() {
        //qDebug()<<"layout changed=========================\n\n\n\n\n";
        if (m_proxy_model->getSortType() == DesktopItemProxyModel::Other) {
            return;
        }
        if (m_proxy_model->sortColumn() != 0) {
            return;
        }
        //qDebug()<<"save====================================";

        QTimer::singleShot(100, this, [=]() {
            this->saveAllItemPosistionInfos();
        });
    });

    connect(this, &QListView::iconSizeChanged, this, [=]() {
        //qDebug()<<"save=============";
        this->setSortType(GlobalSettings::getInstance()->getValue(LAST_DESKTOP_SORT_ORDER).toInt());

        QTimer::singleShot(100, this, [=]() {
            this->saveAllItemPosistionInfos();
        });
    });

    setModel(m_proxy_model);
    //m_proxy_model->sort(0);

    this->refresh();
}

DesktopIconView::~DesktopIconView()
{
    //saveAllItemPosistionInfos();
}

bool DesktopIconView::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::StyleChange) {
        if (m_model)
            updateItemPosistions();
    }
    return false;
}

void DesktopIconView::initShoutCut()
{
    QAction *copyAction = new QAction(this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, [=]() {
        auto selectedUris = this->getSelections();
        if (!selectedUris.isEmpty())
            ClipboardUtils::setClipboardFiles(selectedUris, false);
    });
    addAction(copyAction);

    QAction *cutAction = new QAction(this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, [=]() {
        auto selectedUris = this->getSelections();
        if (!selectedUris.isEmpty())
            ClipboardUtils::setClipboardFiles(selectedUris, true);
    });
    addAction(cutAction);

    QAction *pasteAction = new QAction(this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, [=]() {
        auto clipUris = ClipboardUtils::getClipboardFilesUris();
        if (ClipboardUtils::isClipboardHasFiles()) {
            ClipboardUtils::pasteClipboardFiles(this->getDirectoryUri());
        }
    });
    addAction(pasteAction);

    QAction *trashAction = new QAction(this);
    trashAction->setShortcut(QKeySequence::Delete);
    connect(trashAction, &QAction::triggered, [=]() {
        auto selectedUris = this->getSelections();
        if (!selectedUris.isEmpty()) {
            clearAllIndexWidgets();
            auto op = new FileTrashOperation(selectedUris);
            FileOperationManager::getInstance()->startOperation(op, true);
        }
    });
    addAction(trashAction);

    QAction *undoAction = new QAction(this);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered,
    [=]() {
        FileOperationManager::getInstance()->undo();
    });
    addAction(undoAction);

    QAction *redoAction = new QAction(this);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered,
    [=]() {
        FileOperationManager::getInstance()->redo();
    });
    addAction(redoAction);

    QAction *zoomInAction = new QAction(this);
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, [=]() {
        this->zoomIn();
    });
    addAction(zoomInAction);

    QAction *zoomOutAction = new QAction(this);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, [=]() {
        this->zoomOut();
    });
    addAction(zoomOutAction);

    QAction *renameAction = new QAction(this);
    renameAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_E));
    connect(renameAction, &QAction::triggered, [=]() {
        auto selections = this->getSelections();
        if (selections.count() == 1) {
            this->editUri(selections.first());
        }
    });
    addAction(renameAction);

    QAction *removeAction = new QAction(this);
    removeAction->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Delete));
    connect(removeAction, &QAction::triggered, [=]() {
        qDebug() << "delete" << this->getSelections();
        clearAllIndexWidgets();
        FileOperationUtils::executeRemoveActionWithDialog(this->getSelections());
    });
    addAction(removeAction);

    QAction *helpAction = new QAction(this);
    helpAction->setShortcut(Qt::Key_F1);
    connect(helpAction, &QAction::triggered, this, [=]() {
        PeonyDesktopApplication::showGuide();
    });
    addAction(helpAction);

    auto propertiesWindowAction = new QAction(this);
    propertiesWindowAction->setShortcuts(QList<QKeySequence>()<<QKeySequence(Qt::ALT + Qt::Key_Return)
                                         <<QKeySequence(Qt::ALT + Qt::Key_Enter));
    connect(propertiesWindowAction, &QAction::triggered, this, [=]() {
        if (this->getSelections().count() > 0)
        {
            PropertiesWindow *w = new PropertiesWindow(this->getSelections());
            w->show();
        }
    });
    addAction(propertiesWindowAction);

    auto newFolderAction = new QAction(this);
    newFolderAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_N));
    connect(newFolderAction, &QAction::triggered, this, [=]() {
        CreateTemplateOperation op(this->getDirectoryUri(), CreateTemplateOperation::EmptyFolder, tr("New Folder"));
        op.run();
        auto targetUri = op.target();

        QTimer::singleShot(500, this, [=]() {
            this->scrollToSelection(targetUri);
        });
    });
    addAction(newFolderAction);

    auto refreshAction = new QAction(this);
    refreshAction->setShortcut(Qt::Key_F5);
    connect(refreshAction, &QAction::triggered, this, [=]() {
        this->refresh();
    });
    addAction(refreshAction);

    QAction *editAction = new QAction(this);
    editAction->setShortcuts(QList<QKeySequence>()<<QKeySequence(Qt::ALT + Qt::Key_E)<<Qt::Key_F2);
    connect(editAction, &QAction::triggered, this, [=]() {
        auto selections = this->getSelections();
        if (selections.count() == 1) {
            this->editUri(selections.first());
        }
    });
    addAction(editAction);
}

void DesktopIconView::initMenu()
{
    /*!
     * \bug
     *
     * when view switch to another desktop window,
     * menu might no be callable.
     */
    return;
    setContextMenuPolicy(Qt::CustomContextMenu);

    // menu
    connect(this, &QListView::customContextMenuRequested, this,
    [=](const QPoint &pos) {
        // FIXME: use other menu
        qDebug() << "menu request";
        if (!this->indexAt(pos).isValid()) {
            this->clearSelection();
        } else {
            this->clearSelection();
            this->selectionModel()->select(this->indexAt(pos), QItemSelectionModel::Select);
        }

        QTimer::singleShot(1, [=]() {
            DesktopMenu menu(this);
            if (this->getSelections().isEmpty()) {
                auto action = menu.addAction(tr("set background"));
                connect(action, &QAction::triggered, [=]() {
                    //go to control center set background
                    DesktopWindow::gotoSetBackground();
//                    QFileDialog dlg;
//                    dlg.setNameFilters(QStringList() << "*.jpg"
//                                       << "*.png");
//                    if (dlg.exec()) {
//                        auto url = dlg.selectedUrls().first();
//                        this->setBg(url.path());
//                        // qDebug()<<url;
//                        Q_EMIT this->changeBg(url.path());
//                    }
                });
            }
            menu.exec(QCursor::pos());
            auto urisToEdit = menu.urisToEdit();
            if (urisToEdit.count() == 1) {
                QTimer::singleShot(
                100, this, [=]() {
                    this->editUri(urisToEdit.first());
                });
            }
        });
    }, Qt::UniqueConnection);
}

void DesktopIconView::initDoubleClick()
{
    connect(this, &QListView::doubleClicked, this, [=](const QModelIndex &index) {
        qDebug() << "double click" << index.data(FileItemModel::UriRole);
        auto uri = index.data(FileItemModel::UriRole).toString();
        auto info = FileInfo::fromUri(uri, false);
        auto job = new FileInfoJob(info);
        job->setAutoDelete();
        job->connect(job, &FileInfoJob::queryAsyncFinished, [=]() {
            if (info->isDir() || info->isVolume() || info->isVirtual()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
                QProcess p;
                QUrl url = uri;
                p.setProgram("peony");
                p.setArguments(QStringList() << url.toEncoded() <<"%U&");
                p.startDetached();
#else
                QProcess p;
                p.startDetached("peony", QStringList()<<uri<<"%U&");
#endif
            } else {
                FileLaunchManager::openAsync(uri, false, false);
            }
            this->clearSelection();
        });
        job->queryAsync();
    }, Qt::UniqueConnection);
}

void DesktopIconView::saveAllItemPosistionInfos()
{
    //qDebug()<<"======================save";
    for (int i = 0; i < m_proxy_model->rowCount(); i++) {
        auto index = m_proxy_model->index(i, 0);
        auto indexRect = QListView::visualRect(index);
        QStringList topLeft;
        topLeft<<QString::number(indexRect.top());
        topLeft<<QString::number(indexRect.left());

        auto metaInfo = FileMetaInfo::fromUri(index.data(Qt::UserRole).toString());
        if (metaInfo) {
            //qDebug()<<"save real"<<index.data()<<topLeft;
            metaInfo->setMetaInfoStringList(ITEM_POS_ATTRIBUTE, topLeft);
        }
    }
    //qDebug()<<"======================save finished";
}

void DesktopIconView::saveItemPositionInfo(const QString &uri)
{
    auto index = m_proxy_model->mapFromSource(m_model->indexFromUri(uri));
    auto indexRect = QListView::visualRect(index);
    QStringList topLeft;
    topLeft<<QString::number(indexRect.top());
    topLeft<<QString::number(indexRect.left());
    auto metaInfo = FileMetaInfo::fromUri(index.data(Qt::UserRole).toString());
    if (metaInfo) {
        //qDebug()<<"save"<<uri<<topLeft;
        metaInfo->setMetaInfoStringList(ITEM_POS_ATTRIBUTE, topLeft);
    }
}

void DesktopIconView::resetAllItemPositionInfos()
{
    for (int i = 0; i < m_proxy_model->rowCount(); i++) {
        auto index = m_proxy_model->index(i, 0);
        auto indexRect = QListView::visualRect(index);
        QStringList topLeft;
        topLeft<<QString::number(indexRect.top());
        topLeft<<QString::number(indexRect.left());
        auto metaInfo = FileMetaInfo::fromUri(index.data(Qt::UserRole).toString());
        if (metaInfo) {
            QStringList tmp;
            tmp<<"-1"<<"-1";
            metaInfo->setMetaInfoStringList(ITEM_POS_ATTRIBUTE, tmp);
        }
    }
}

void DesktopIconView::resetItemPosistionInfo(const QString &uri)
{
    auto metaInfo = FileMetaInfo::fromUri(uri);
    if (metaInfo)
        metaInfo->removeMetaInfo(ITEM_POS_ATTRIBUTE);
}

void DesktopIconView::updateItemPosistions(const QString &uri)
{
    if (uri.isNull()) {
        for (int i = 0; i < m_proxy_model->rowCount(); i++) {
            auto index = m_proxy_model->index(i, 0);
            auto metaInfo = FileMetaInfo::fromUri(index.data(Qt::UserRole).toString());
            if (metaInfo) {
                updateItemPosistions(index.data(Qt::UserRole).toString());
            }
        }
        return;
    }

    auto index = m_proxy_model->mapFromSource(m_model->indexFromUri(uri));
    //qDebug()<<"update"<<uri<<index.data();

    if (!index.isValid()) {
        //qDebug()<<"err: index invalid";
        return;
    }
    auto metaInfo = FileMetaInfo::fromUri(uri);
    if (!metaInfo) {
        //qDebug()<<"err: no meta data";
        return;
    }

    auto list = metaInfo->getMetaInfoStringList(ITEM_POS_ATTRIBUTE);
    if (!list.isEmpty()) {
        if (list.count() == 2) {
            int top = list.first().toInt();
            int left = list.at(1).toInt();
            qDebug() << "index:" << index << "uri:" << uri << " top:" << top << " left:" << left;
            if (top > 0 && left >= 0) {
//                auto rect = visualRect(index);
//                auto grid = gridSize();
//                if (abs(rect.top() - top) < grid.width() && abs(rect.left() - left))
//                    return;
                QPoint p(left, top);
                //qDebug()<<"set"<<index.data()<<p;
                setPositionForIndex(QPoint(left, top), index);
            } else {
                saveItemPositionInfo(uri);
            }
        }
    }
}

const QStringList DesktopIconView::getSelections()
{
    QStringList uris;
    auto indexes = selectionModel()->selection().indexes();
    for (auto index : indexes) {
        uris<<index.data(Qt::UserRole).toString();
    }
    return uris;
}

const QStringList DesktopIconView::getAllFileUris()
{

}

void DesktopIconView::setSelections(const QStringList &uris)
{
    clearSelection();
    for (int i = 0; i < m_proxy_model->rowCount(); i++) {
        auto index = m_proxy_model->index(i, 0);
        if (uris.contains(index.data(Qt::UserRole).toString())) {
            selectionModel()->select(index, QItemSelectionModel::Select);
        }
    }
}

void DesktopIconView::invertSelections()
{
    QItemSelectionModel *selectionModel = this->selectionModel();
    const QItemSelection currentSelection = selectionModel->selection();
    this->selectAll();
    selectionModel->select(currentSelection, QItemSelectionModel::Deselect);
    clearAllIndexWidgets();
}

void DesktopIconView::scrollToSelection(const QString &uri)
{

}

int DesktopIconView::getSortType()
{
    return m_proxy_model->getSortType();
}

void DesktopIconView::setSortType(int sortType)
{
    //resetAllItemPositionInfos();
    m_proxy_model->setSortType(sortType);
    m_proxy_model->sort(1);
    m_proxy_model->sort(0, m_proxy_model->sortOrder());
}

int DesktopIconView::getSortOrder()
{
    return m_proxy_model->sortOrder();
}

void DesktopIconView::setSortOrder(int sortOrder)
{
    m_proxy_model->sort(0, Qt::SortOrder(sortOrder));
}

void DesktopIconView::editUri(const QString &uri)
{
    clearAllIndexWidgets();
    QTimer::singleShot(100, this, [=]() {
        edit(m_proxy_model->mapFromSource(m_model->indexFromUri(uri)));
    });
}

void DesktopIconView::editUris(const QStringList uris)
{

}

void DesktopIconView::setCutFiles(const QStringList &uris)
{
    ClipboardUtils::setClipboardFiles(uris, true);
}

void DesktopIconView::closeView()
{
    deleteLater();
}

void DesktopIconView::wheelEvent(QWheelEvent *e)
{
    if (QApplication::keyboardModifiers() == Qt::ControlModifier)
    {
        if (e->delta() > 0) {
            zoomIn();
        } else {
            zoomOut();
        }
        resetAllItemPositionInfos();
    }
}

void DesktopIconView::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Up: {
        if (getSelections().isEmpty()) {
            selectionModel()->select(model()->index(0, 0), QItemSelectionModel::SelectCurrent);
        } else {
            auto index = selectionModel()->selectedIndexes().first();
            auto center = visualRect(index).center();
            auto up = center - QPoint(0, gridSize().height());
            auto upIndex = indexAt(up);
            if (upIndex.isValid()) {
                clearAllIndexWidgets();
                selectionModel()->select(upIndex, QItemSelectionModel::SelectCurrent);
                auto delegate = qobject_cast<DesktopIconViewDelegate *>(itemDelegate());
                setIndexWidget(upIndex, new DesktopIndexWidget(delegate, viewOptions(), upIndex, this));
            }
        }
        return;
    }
    case Qt::Key_Down: {
        if (getSelections().isEmpty()) {
            selectionModel()->select(model()->index(0, 0), QItemSelectionModel::SelectCurrent);
        } else {
            auto index = selectionModel()->selectedIndexes().first();
            auto center = visualRect(index).center();
            auto down = center + QPoint(0, gridSize().height());
            auto downIndex = indexAt(down);
            if (downIndex.isValid()) {
                clearAllIndexWidgets();
                selectionModel()->select(downIndex, QItemSelectionModel::SelectCurrent);
                auto delegate = qobject_cast<DesktopIconViewDelegate *>(itemDelegate());
                setIndexWidget(downIndex, new DesktopIndexWidget(delegate, viewOptions(), downIndex, this));
            }
        }
        return;
    }
    case Qt::Key_Left: {
        if (getSelections().isEmpty()) {
            selectionModel()->select(model()->index(0, 0), QItemSelectionModel::SelectCurrent);
        } else {
            auto index = selectionModel()->selectedIndexes().first();
            auto center = visualRect(index).center();
            auto left = center - QPoint(gridSize().width(), 0);
            auto leftIndex = indexAt(left);
            if (leftIndex.isValid()) {
                clearAllIndexWidgets();
                selectionModel()->select(leftIndex, QItemSelectionModel::SelectCurrent);
                auto delegate = qobject_cast<DesktopIconViewDelegate *>(itemDelegate());
                setIndexWidget(leftIndex, new DesktopIndexWidget(delegate, viewOptions(), leftIndex, this));
            }
        }
        return;
    }
    case Qt::Key_Right: {
        if (getSelections().isEmpty()) {
            selectionModel()->select(model()->index(0, 0), QItemSelectionModel::SelectCurrent);
        } else {
            auto index = selectionModel()->selectedIndexes().first();
            auto center = visualRect(index).center();
            auto right = center + QPoint(gridSize().width(), 0);
            auto rightIndex = indexAt(right);
            if (rightIndex.isValid()) {
                clearAllIndexWidgets();
                selectionModel()->select(rightIndex, QItemSelectionModel::SelectCurrent);
                auto delegate = qobject_cast<DesktopIconViewDelegate *>(itemDelegate());
                setIndexWidget(rightIndex, new DesktopIndexWidget(delegate, viewOptions(), rightIndex, this));
            }
        }
        return;
    }
    case Qt::Key_Shift:
    case Qt::Key_Control:
        m_ctrl_or_shift_pressed = true;
        break;
    default:
        return QListView::keyPressEvent(e);
    }
}

void DesktopIconView::keyReleaseEvent(QKeyEvent *e)
{
    QListView::keyReleaseEvent(e);
    m_ctrl_or_shift_pressed = false;
}

void DesktopIconView::focusOutEvent(QFocusEvent *e)
{
    QListView::focusOutEvent(e);
    m_ctrl_or_shift_pressed = false;
}

void DesktopIconView::resizeEvent(QResizeEvent *e)
{
    QListView::resizeEvent(e);
    refresh();
}

bool DesktopIconView::isItemsOverlapped()
{
    QList<QRect> itemRects;
    if (model()) {
        for (int i = 0; i < model()->rowCount(); i++) {
            auto index = model()->index(i, 0);
            auto rect = visualRect(index);
            if (itemRects.contains(rect))
                return true;
            itemRects<<visualRect(index);
        }
    }

    return false;
}

void DesktopIconView::zoomOut()
{
    clearAllIndexWidgets();
    switch (zoomLevel()) {
    case Huge:
        setDefaultZoomLevel(Large);
        break;
    case Large:
        setDefaultZoomLevel(Normal);
        break;
    case Normal:
        setDefaultZoomLevel(Small);
        break;
    default:
        setDefaultZoomLevel(zoomLevel());
        break;
    }
}

void DesktopIconView::zoomIn()
{
    clearAllIndexWidgets();
    switch (zoomLevel()) {
    case Small:
        setDefaultZoomLevel(Normal);
        break;
    case Normal:
        setDefaultZoomLevel(Large);
        break;
    case Large:
        setDefaultZoomLevel(Huge);
        break;
    default:
        setDefaultZoomLevel(zoomLevel());
        break;
    }
}

/*
Small, //icon: 32x32; grid: 56x64; hover rect: 40x56; font: system*0.8
Normal, //icon: 48x48; grid: 64x72; hover rect = 56x64; font: system
Large, //icon: 64x64; grid: 115x135; hover rect = 105x118; font: system*1.2
Huge //icon: 96x96; grid: 130x150; hover rect = 120x140; font: system*1.4
*/
void DesktopIconView::setDefaultZoomLevel(ZoomLevel level)
{
    //qDebug()<<"set default zoom level:"<<level;
    m_zoom_level = level;
    switch (level) {
    case Small:
        setIconSize(QSize(24, 24));
        setGridSize(QSize(64, 64));
        break;
    case Large:
        setIconSize(QSize(64, 64));
        setGridSize(QSize(115, 135));
        break;
    case Huge:
        setIconSize(QSize(96, 96));
        setGridSize(QSize(140, 170));
        break;
    default:
        m_zoom_level = Normal;
        setIconSize(QSize(48, 48));
        setGridSize(QSize(96, 96));
        break;
    }
    clearAllIndexWidgets();
    auto metaInfo = FileMetaInfo::fromUri("computer:///");
    if (metaInfo) {
        qDebug()<<"set zoom level"<<m_zoom_level;
        metaInfo->setMetaInfoInt("peony-qt-desktop-zoom-level", int(m_zoom_level));
    } else {

    }
}

DesktopIconView::ZoomLevel DesktopIconView::zoomLevel() const
{
    //FIXME:
    if (m_zoom_level != Invalid)
        return m_zoom_level;

    auto metaInfo = FileMetaInfo::fromUri("computer:///");
    if (metaInfo) {
        auto i = metaInfo->getMetaInfoInt("peony-qt-desktop-zoom-level");
        return ZoomLevel(i);
    }

    GFile *computer = g_file_new_for_uri("computer:///");
    GFileInfo *info = g_file_query_info(computer,
                                        "metadata::peony-qt-desktop-zoom-level",
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        nullptr,
                                        nullptr);
    char* zoom_level = g_file_info_get_attribute_as_string(info, "metadata::peony-qt-desktop-zoom-level");
    if (!zoom_level) {
        //qDebug()<<"======================no zoom level meta info\n\n\n";
        g_object_unref(info);
        g_object_unref(computer);
        return Normal;
    }
    g_object_unref(info);
    g_object_unref(computer);
    QString zoomLevel = zoom_level;
    g_free(zoom_level);
    //qDebug()<<ZoomLevel(QString(zoomLevel).toInt())<<"\n\n\n\n\n\n\n\n";
    return ZoomLevel(zoomLevel.toInt()) == Invalid? Normal: ZoomLevel(QString(zoomLevel).toInt());
}

void DesktopIconView::mousePressEvent(QMouseEvent *e)
{
    // bug extend selection bug
    m_real_do_edit = false;

    if (!m_ctrl_or_shift_pressed) {
        if (!indexAt(e->pos()).isValid()) {
            clearAllIndexWidgets();
            clearSelection();
        } else {
            auto index = indexAt(e->pos());
            clearAllIndexWidgets();
            m_last_index = index;
            if (!indexWidget(m_last_index)) {
                setIndexWidget(m_last_index,
                               new DesktopIndexWidget(qobject_cast<DesktopIconViewDelegate *>(itemDelegate()), viewOptions(), m_last_index));
            }
        }
    }

    //qDebug()<<m_last_index.data();
    if (e->button() != Qt::LeftButton) {
        return;
    }

    QListView::mousePressEvent(e);
}

void DesktopIconView::mouseReleaseEvent(QMouseEvent *e)
{
    QListView::mouseReleaseEvent(e);
}

void DesktopIconView::mouseDoubleClickEvent(QMouseEvent *event)
{
    QListView::mouseDoubleClickEvent(event);
    m_real_do_edit = false;
}

void DesktopIconView::dragEnterEvent(QDragEnterEvent *e)
{
    m_real_do_edit = false;
    //qDebug()<<"drag enter event";
    if (e->mimeData()->hasUrls()) {
        e->setDropAction(Qt::MoveAction);
        e->acceptProposedAction();
    }

    if (e->source() == this) {
        m_drag_indexes = selectedIndexes();
    }
}

void DesktopIconView::dragMoveEvent(QDragMoveEvent *e)
{
    m_real_do_edit = false;
    auto index = indexAt(e->pos());
    if (index.isValid() && index != m_last_index) {
        QHoverEvent he(QHoverEvent::HoverMove, e->posF(), e->posF());
        viewportEvent(&he);
    } else {
        QHoverEvent he(QHoverEvent::HoverLeave, e->posF(), e->posF());
        viewportEvent(&he);
    }
    if (e->isAccepted())
        return;
    //qDebug()<<"drag move event";
    if (this == e->source()) {
        e->accept();
        return QListView::dragMoveEvent(e);
    }
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void DesktopIconView::dropEvent(QDropEvent *e)
{
    m_real_do_edit = false;
    //qDebug()<<"drop event";
    /*!
      \todo
      fix the bug that move drop action can not move the desktop
      item to correct position.

      i use copy action to avoid this bug, but the drop indicator
      is incorrect.
      */
    m_edit_trigger_timer.stop();
    if (this == e->source()) {

        auto index = indexAt(e->pos());
        if (index.isValid()) {
            auto info = FileInfo::fromUri(index.data(Qt::UserRole).toString());
            if (!info->isDir())
                return;
        }

        QListView::dropEvent(e);

        QHash<QModelIndex, QRect> currentIndexesRects;
        for (int i = 0; i < m_proxy_model->rowCount(); i++) {
            auto tmp = m_proxy_model->index(i, 0);
            currentIndexesRects.insert(tmp, visualRect(tmp));
        }

        //fixme: handle overlapping.
        if (!m_drag_indexes.isEmpty()) {
            QModelIndexList overlappedIndexes;
            for (auto value : currentIndexesRects.values()) {
                auto keys = currentIndexesRects.keys(value);
                if (keys.count() > 1) {
                    for (auto key : keys) {
                        if (m_drag_indexes.contains(key) && !overlappedIndexes.contains(key)) {
                            overlappedIndexes<<key;
                        }
                    }
                }
            }

            auto grid = this->gridSize();
            auto viewRect = this->rect();

            QRegion notEmptyRegion;
            for (auto rect : currentIndexesRects) {
                notEmptyRegion += rect;
            }

            for (auto dragedIndex : overlappedIndexes) {
                auto indexRect = QListView::visualRect(dragedIndex);
                if (notEmptyRegion.contains(indexRect.center())) {
                    // move index to closest empty grid.
                    auto next = indexRect;
                    bool isEmptyPos = false;
                    while (!isEmptyPos) {
                        next.translate(0, grid.height());
                        if (next.bottom() > viewRect.bottom()) {
                            int top = next.y();
                            while (true) {
                                if (top < gridSize().height()) {
                                    break;
                                }
                                top-=gridSize().height();
                            }
                            //put item to next column first column
                            next.moveTo(next.x() + grid.width(), top);
                        }
                        if (notEmptyRegion.contains(next.center()))
                            continue;

                        isEmptyPos = true;
                        setPositionForIndex(next.topLeft(), dragedIndex);
                        notEmptyRegion += next;
                    }
                }
            }
        }

        m_drag_indexes.clear();

        auto urls = e->mimeData()->urls();
        for (auto url : urls) {
//            if (url.path() == QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
//                continue;
            saveItemPositionInfo(url.toDisplayString());
        }
        return;
    }
    m_model->dropMimeData(e->mimeData(), Qt::MoveAction, -1, -1, this->indexAt(e->pos()));
    //FIXME: save item position
}

const QFont DesktopIconView::getViewItemFont(QStyleOptionViewItem *item)
{
    return item->font;
//    auto font = item->font;
//    if (font.pixelSize() <= 0) {
//        font = QApplication::font();
//    }
//    switch (zoomLevel()) {
//    case DesktopIconView::Small:
//        font.setPixelSize(int(font.pixelSize() * 0.8));
//        break;
//    case DesktopIconView::Large:
//        font.setPixelSize(int(font.pixelSize() * 1.2));
//        break;
//    case DesktopIconView::Huge:
//        font.setPixelSize(int(font.pixelSize() * 1.4));
//        break;
//    default:
//        break;
//    }
//    return font;
}

void DesktopIconView::clearAllIndexWidgets()
{
    if (!model())
        return;

    int row = 0;
    auto index = model()->index(row, 0);
    while (index.isValid()) {
        setIndexWidget(index, nullptr);
        row++;
        index = model()->index(row, 0);
    }
}

void DesktopIconView::refresh()
{
    if (m_refresh_timer.isActive())
        return;

    if (!m_model)
        return;

    if (m_is_refreshing)
        return;
    m_is_refreshing = true;
    m_model->refresh();

    m_refresh_timer.start();
}

QRect DesktopIconView::visualRect(const QModelIndex &index) const
{
    auto rect = QListView::visualRect(index);
    QPoint p(10, 5);

    switch (zoomLevel()) {
    case Small:
        p *= 0.8;
        break;
    case Large:
        p *= 1.2;
        break;
    case Huge:
        p *= 1.4;
        break;
    default:
        break;
    }
    rect.moveTo(rect.topLeft() + p);
    return rect;
}
