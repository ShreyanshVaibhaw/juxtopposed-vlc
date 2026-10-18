// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFileDialog>

class QLineEdit;

// A widget-based file chooser whose host owns the action buttons.
class EmbeddedFileBrowser : public QFileDialog
{
    Q_OBJECT
public:
    explicit EmbeddedFileBrowser(QWidget* parent);
    void clearSelection();
    void selectUrls(const QList<QUrl>& urls);
    QList<QUrl> selectedMediaUrls() const;

signals:
    void selectionChanged();
    void filesActivated();
    void cancelRequested();

protected:
    void accept() override;
    void done(int result) override;

private:
    QLineEdit* m_nameEdit = nullptr;
    QList<QUrl> m_explicitUrls;
};
