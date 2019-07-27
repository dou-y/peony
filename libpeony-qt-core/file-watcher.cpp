#include "file-watcher.h"

#include <QDebug>

using namespace Peony;

FileWatcher::FileWatcher(QString uri, QObject *parent) : QObject(parent)
{
    m_uri = uri;
    m_file = g_file_new_for_uri(uri.toUtf8());

    //monitor target file if existed.
    prepare();

    m_monitor = g_file_monitor_file(m_file,
                                    G_FILE_MONITOR_WATCH_MOVES,
                                    m_cancellable,
                                    &m_monitor_err);
    if (m_monitor_err) {
        qDebug()<<m_monitor_err->code<<m_monitor_err->message;
    }

    m_dir_monitor = g_file_monitor_directory(m_file,
                                             G_FILE_MONITOR_NONE,
                                             m_cancellable,
                                             &m_dir_monitor_err);
    if (m_dir_monitor_err) {
        qDebug()<<m_dir_monitor_err->code<<m_dir_monitor_err->message;
    }
}

FileWatcher::~FileWatcher()
{
    disconnect();
    //qDebug()<<"~FileWatcher";
    stopMonitor();
    cancel();

    g_object_unref(m_cancellable);
    g_object_unref(m_dir_monitor);
    g_object_unref(m_monitor);
    g_object_unref(m_file);

    if (m_monitor_err)
        g_error_free(m_monitor_err);
    if (m_dir_monitor_err)
        g_error_free(m_dir_monitor_err);
}

/*!
 * \brief FileWatcher::prepare
 * <br>
 * If file handle has target uri, we need monitor file that target uri point to.
 * FileWatcher::prepare() just query if there is a target uri of current file handle,
 * and it asumed that everything is no problem. If you want to use
 * a file watcher instance, I recommend you call a file enumerator class instance
 * with FileEnumerator::prepare() and wait it finished first.
 * </br>
 * \see FileEnumerator::prepare().
 */
void FileWatcher::prepare()
{
    GFileInfo *info = g_file_query_info(m_file,
                                        G_FILE_ATTRIBUTE_STANDARD_TARGET_URI,
                                        G_FILE_QUERY_INFO_NONE,
                                        m_cancellable,
                                        nullptr);

    char *uri = g_file_info_get_attribute_as_string(info,
                                                    G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);

    if (uri) {
        g_object_unref(m_file);
        m_file = g_file_new_for_uri(uri);
        g_free(uri);
    }

    g_object_unref(info);
}

void FileWatcher::cancel()
{
    g_cancellable_cancel(m_cancellable);
    g_object_unref(m_cancellable);
    m_cancellable = g_cancellable_new();
}

void FileWatcher::startMonitor()
{
    m_file_handle = g_signal_connect(m_monitor, "changed", G_CALLBACK(file_changed_callback), this);
    m_dir_handle = g_signal_connect(m_dir_monitor, "changed", G_CALLBACK(dir_changed_callback), this);
}

void FileWatcher::stopMonitor()
{
    if (m_file_handle > 0) {
        g_signal_handler_disconnect(m_monitor, m_file_handle);
        m_file_handle = 0;
    }
    if (m_dir_handle > 0) {
        g_signal_handler_disconnect(m_dir_monitor, m_dir_handle);
        m_dir_handle = 0;
    }
}

void FileWatcher::changeMonitorUri(QString uri)
{
    QString oldUri = m_uri;

    stopMonitor();
    cancel();

    m_uri = uri;
    g_object_unref(m_file);
    g_object_unref(m_monitor);
    g_object_unref(m_dir_monitor);

    if (m_monitor_err) {
        g_error_free(m_monitor_err);
        m_monitor_err = nullptr;
    }
    if (m_dir_monitor_err) {
        g_error_free(m_dir_monitor_err);
        m_dir_monitor_err = nullptr;
    }

    m_file = g_file_new_for_uri(uri.toUtf8());

    prepare();

    m_monitor = g_file_monitor_file(m_file,
                                    G_FILE_MONITOR_WATCH_MOVES,
                                    m_cancellable,
                                    &m_monitor_err);
    if (m_monitor_err) {
        qDebug()<<m_monitor_err->code<<m_monitor_err->message;
    }

    m_dir_monitor = g_file_monitor_directory(m_file,
                                             G_FILE_MONITOR_NONE,
                                             m_cancellable,
                                             &m_dir_monitor_err);
    if (m_dir_monitor_err) {
        qDebug()<<m_dir_monitor_err->code<<m_dir_monitor_err->message;
    }

    startMonitor();

    Q_EMIT locationChanged(oldUri, m_uri);
}

void FileWatcher::file_changed_callback(GFileMonitor *monitor,
                                        GFile *file,
                                        GFile *other_file,
                                        GFileMonitorEvent event_type,
                                        FileWatcher *p_this)
{
    //qDebug()<<"file_changed_callback";
    Q_UNUSED(monitor);
    Q_UNUSED(file);
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_RENAMED: {
        char *new_uri = g_file_get_uri(other_file);
        QString uri = new_uri;
        g_free(new_uri);
        p_this->changeMonitorUri(uri);
        break;
    }
    case G_FILE_MONITOR_EVENT_DELETED: {
        p_this->stopMonitor();
        p_this->cancel();
        Q_EMIT p_this->directoryDeleted(p_this->m_uri);
        p_this->deleteLater();
        break;
    }
    default:
        break;
    }
}

void FileWatcher::dir_changed_callback(GFileMonitor *monitor,
                                       GFile *file,
                                       GFile *other_file,
                                       GFileMonitorEvent event_type,
                                       FileWatcher *p_this)
{
    //qDebug()<<"dir_changed_callback";
    Q_UNUSED(monitor);
    Q_UNUSED(other_file);
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED: {
        char *uri = g_file_get_uri(file);
        QString createdFileUri = uri;
        g_free(uri);
        Q_EMIT p_this->fileCreated(createdFileUri);
        break;
    }
    case G_FILE_MONITOR_EVENT_DELETED: {
        char *uri = g_file_get_uri(file);
        QString deletedFileUri = uri;
        g_free(uri);
        Q_EMIT p_this->fileDeleted(deletedFileUri);
        break;
    }
    default:
        break;
    }
}
