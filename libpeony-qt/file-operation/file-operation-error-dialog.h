/*
 * Peony-Qt's Library
 *
 * Copyright (C) 2019, Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Yue Lan <lanyue@kylinos.cn>
 *
 */

#ifndef FILEOPERATIONERRORDIALOG_H
#define FILEOPERATIONERRORDIALOG_H

#include <QDialog>
#include "file-operation-error-handler.h"

class QFormLayout;
class QLabel;
class QDialogButtonBox;
class QButtonGroup;

namespace Peony {

class PEONYCORESHARED_EXPORT FileOperationErrorDialog : public QDialog, public FileOperationErrorHandler
{
    Q_OBJECT
    Q_INTERFACES(Peony::FileOperationErrorHandler)
public:
    explicit FileOperationErrorDialog(QWidget *parent = nullptr);
    ~FileOperationErrorDialog() override;

public Q_SLOTS:
    QVariant handleError(const QString &srcUri,
                         const QString &destDirUri,
                         const GErrorWrapperPtr &err,
                         bool isCritical = false) override;

private:
    QFormLayout *m_layout = nullptr;
    QLabel *m_src_line = nullptr;
    QLabel *m_dest_line = nullptr;
    QLabel *m_err_line = nullptr;
    QDialogButtonBox *m_button_box = nullptr;
    QDialogButtonBox *m_button_box2 = nullptr;

    QButtonGroup *btGroup = nullptr;
};

}

#endif // FILEOPERATIONERRORDIALOG_H
