/*****************************************************************************
 * messages.cpp : Information about an item
 ****************************************************************************
 * Copyright (C) 2006-2011 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextBlock>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QScrollBar>
#include <QMutex>
#include <QMutexLocker>

#include <cassert>

#include <vlc_common.h>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <vlc_log_observer.h>
#include "maininterface/mainctx.hpp"
#include <vlc_input_item.h>


#include "dialogs/messages/messages.hpp"
#include "dialogs/dialogs/dialogmodel.hpp"
#include "util/interface_translator.hpp"
#include "util/message_log.hpp"

enum {
    MsgEvent_Type = QEvent::User + MsgEventTypeOffset + 1,
};

class MsgEvent : public QEvent
{
public:
    MsgEvent( int, const vlc_log_t *, const char * );

    int priority;
    uintptr_t object_id;
    QString object_type;
    QString header;
    QString module;
    QString text;
};

MsgEvent::MsgEvent( int type, const vlc_log_t *msg, const char *text )
    : QEvent( (QEvent::Type)MsgEvent_Type ),
      priority( type ),
      object_id( msg->i_object_id ),
      object_type( qfu(msg->psz_object_type) ),
      header( qfu(msg->psz_header) ),
      module( qfu(msg->psz_module) ),
      text( qfu(text) )
{
}

MessagesDialog::MessagesDialog( qt_intf_t *_p_intf)
               : QVLCFrame( _p_intf )
{
    setWindowTitle( qtr( "Messages" ) );
    setWindowRole( "vlc-messages" );
    /* Build Ui */
    ui.setupUi( this );
    auto* closeButton = new QPushButton(qtr("&Close"), this);
    ui.bottomButtonsBox->addButton(closeButton, QDialogButtonBox::RejectRole);

    /* Error messages */
    DialogErrorModel *errors = DialogErrorModel::getInstance<false>();
    connect( errors, &DialogErrorModel::countChanged, this, &MessagesDialog::errorsCountChanged );
    ui.mainTab->setTabText(1, qtr("Errors and warnings"));
    connect(errors, &QAbstractItemModel::rowsAboutToBeRemoved, this,
        [this](const QModelIndex &, int first, int last) {
            int offset = 0, length = 0;
            for (int row = 0; row < first; ++row) offset += m_errorRowLengths.at(row);
            for (int row = first; row <= last; ++row) length += m_errorRowLengths.at(row);
            QTextCursor cursor(ui.errors->document());
            cursor.setPosition(offset);
            cursor.setPosition(offset + length, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            m_errorRowLengths.remove(first, last - first + 1);
        });
    for(int i=0; i<errors->count(); i++)
        addError(i);

    /* Modules tree */
    ui.modulesTree->setHeaderHidden( true );

    /* Buttons and general layout */
    ui.saveLogButton->setToolTip( qtr( "Saves all the displayed logs to a file" ) );

    int i_verbosity = var_InheritInteger( p_intf, "verbose" );
    changeVerbosity( i_verbosity );
    ui.verbosityBox->setValue( qMin( i_verbosity, 2 ) );
    ui.verbosityBox->retranslate();

    ui.filterEdit->setText( getSettings()->value( "Messages/messages-filter" ).toString() );

    updateButton = new QToolButton;
    updateButton->setIcon( QIcon(":/menu/update.svg") );
    updateButton->setAutoRaise( true );
    ui.mainTab->setCornerWidget( updateButton );

#ifndef NDEBUG
    QWidget *pldebugTab = new QWidget();
    QVBoxLayout *pldebugTabLayout = new QVBoxLayout();
    pldebugTab->setLayout( pldebugTabLayout );
    ui.mainTab->addTab( pldebugTab, "Playlist Tree" );
    pldebugTree = new QTreeWidget();
    pldebugTree->headerItem()->setText( 0, "Name" );
    pldebugTree->headerItem()->setText( 1, "PL id" );
    pldebugTree->headerItem()->setText( 2, "Item id" );
    pldebugTree->headerItem()->setText( 3, "PL flags" );
    pldebugTree->headerItem()->setText( 4, "Item flags" );
    pldebugTree->setColumnCount( 5 );
    pldebugTabLayout->addWidget( pldebugTree );
#endif

    tabChanged(0);

    BUTTONACT( updateButton, &MessagesDialog::updateOrClear );
    BUTTONACT( ui.saveLogButton, &MessagesDialog::save );
    connect( ui.filterEdit, &QLineEdit::editingFinished, this, &MessagesDialog::updateConfig );
    connect( ui.filterEdit, &QLineEdit::textChanged, this, &MessagesDialog::filterMessages );
    connect( ui.bottomButtonsBox, &QDialogButtonBox::rejected, this, &MessagesDialog::hide );
    connect( ui.verbosityBox, QOverload<int>::of(&QVLCDebugLevelSpinBox::valueChanged),
             this, &MessagesDialog::changeVerbosity );

    connect( ui.mainTab, &QTabWidget::currentChanged, this, &MessagesDialog::tabChanged );

    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this, closeButton] {
            ui.retranslateUi(this);
            ui.mainTab->setTabText(1, qtr("Errors and warnings"));
            ui.saveLogButton->setToolTip(qtr("Saves all the displayed logs to a file"));
            closeButton->setText(qtr("&Close"));
            ui.verbosityBox->retranslate();
            tabChanged(ui.mainTab->currentIndex());
        });

    /* General action */
    restoreWidgetPosition( "Messages", QSize( 600, 450 ) );

    /* Hook up to LibVLC messaging */
    static const struct vlc_logger_operations log_ops =
    {
        MessagesDialog::MsgCallback,
        NULL
    };
    libvlc_int_t *vlc = vlc_object_instance(p_intf);

    m_logSubscription = vlc_LogSubscribe(vlc, &log_ops, this);
    if (!m_logSubscription)
        msg_Err(p_intf, "Cannot read log messages.");

    buildTree( NULL, VLC_OBJECT(vlc) );
}

MessagesDialog::~MessagesDialog()
{
    vlc_LogUnsubscribe(m_logSubscription);
    saveWidgetPosition( "Messages" );
};

void MessagesDialog::changeVerbosity( int i_verbosity )
{
    verbosity = i_verbosity;
}

void MessagesDialog::updateConfig()
{
    getSettings()->setValue( "Messages/messages-filter", ui.filterEdit->text() );
}

void MessagesDialog::addError(int row)
{
    auto *errors = DialogErrorModel::getInstance<false>();
    auto *view = ui.errors;
    const bool autoscroll = view->verticalScrollBar()->value() + view->verticalScrollBar()->pageStep()
        >= view->verticalScrollBar()->maximum();
    const int before = view->document()->characterCount();
    QTextCursor cursor(view->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setFontItalic(true);
    const bool dark = view->palette().color(QPalette::Base).lightness() < 128;
    const bool warning = errors->data(errors->index(row), DialogErrorModel::DIALOG_WARNING).toBool();
    format.setForeground(warning ? QColor(dark ? "#d69c38" : "#775100")
                                : QColor(dark ? "#ff7373" : "#a31b1b"));
    cursor.insertText(errors->data(errors->index(row), DialogErrorModel::DIALOG_TITLE).toString() + ": ", format);
    format.setFontItalic(false);
    format.setForeground(view->palette().color(QPalette::Text));
    cursor.insertText(errors->data(errors->index(row), DialogErrorModel::DIALOG_TEXT).toString(), format);
    cursor.insertBlock();
    m_errorRowLengths.append(view->document()->characterCount() - before);
    if (autoscroll) view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
}

void MessagesDialog::errorsCountChanged(){
    DialogErrorModel *errors = DialogErrorModel::getInstance(p_intf);
    addError(errors->count() - 1);
}

void MessagesDialog::filterMessages()
{
    QMutexLocker locker( &messageLocker );
    QPlainTextEdit *messages = ui.messages;
    QTextBlock block = messages->document()->firstBlock();

    while( block.isValid() )
    {
        block.setVisible( matchFilter( block.text() ) );
        block = block.next();
    }

    /* Consider the whole QTextDocument as dirty now */
    messages->document()->markContentsDirty( 0, messages->document()->characterCount() );

    /* FIXME This solves a bug (Qt?) with the viewport not resizing the
       vertical scroll bar when one or more QTextBlock are hidden */
    QSize vsize = messages->viewport()->size();
    messages->viewport()->resize( vsize + QSize( 1, 1 ) );
    messages->viewport()->resize( vsize );
}

bool MessagesDialog::matchFilter( const QString& text )
{
    return messageLogMatches(text, ui.filterEdit->text());
}

void MessagesDialog::sinkMessage( const MsgEvent *msg )
{
    QMutexLocker locker( &messageLocker );

    const QColor accent = p_intf->p_mi ? p_intf->p_mi->redesignAccent() : palette().color(QPalette::Link);
    appendMessageLog(ui.messages, {{msg->priority, msg->module, msg->text}}, ui.filterEdit->text(), accent);
}

void MessagesDialog::customEvent( QEvent *event )
{
    MsgEvent *msge = static_cast<MsgEvent *>(event);

    assert( msge );
    sinkMessage( msge );
}

bool MessagesDialog::save()
{
    QString saveLogFileName = QFileDialog::getSaveFileName(
            this, qtr( "Save log file as..." ),
            QVLCUserDir( VLC_DOCUMENTS_DIR ),
            qtr( "Texts/Logs (*.log *.txt);; All (*.*)") );

    if( !saveLogFileName.isNull() )
    {
        QFile file( saveLogFileName );
        if ( !file.open( QFile::WriteOnly | QFile::Text ) ) {
            QMessageBox::warning( this, qtr( "Application" ),
                    qtr( "Cannot write to file %1:\n%2." )
                    .arg( saveLogFileName )
                    .arg( file.errorString() ) );
            return false;
        }

        QTextStream out( &file );

        QTextBlock block = ui.messages->document()->firstBlock();
        while( block.isValid() )
        {
            if( block.isVisible() )
                out << block.text() << "\n";

            block = block.next();
        }
        return true;
    }
    return false;
}

void MessagesDialog::buildTree( QTreeWidgetItem *parentItem,
                                vlc_object_t *p_obj )
{
    QTreeWidgetItem *item;

    if( parentItem )
        item = new QTreeWidgetItem( parentItem );
    else
        item = new QTreeWidgetItem( ui.modulesTree );

    char *name = vlc_object_get_name( p_obj );
    item->setText( 0, QString("%1%2 (0x%3)")
                   .arg( qfu( vlc_object_typename(p_obj) ) )
                   .arg( ( name != NULL )
                         ? QString( " \"%1\"" ).arg( qfu( name ) )
                             : "" )
                   .arg( (uintptr_t)p_obj, 0, 16 )
                 );
    free( name );
    item->setExpanded( true );

    size_t count = 0, size;
    vlc_object_t **tab = NULL;

    do
    {
        delete[] tab;
        size = count;
        tab = new vlc_object_t *[size];
        count = vlc_list_children(p_obj, tab, size);
    }
    while (size < count);

    for (size_t i = 0; i < count ; i++)
    {
        buildTree( item, tab[i] );
        vlc_object_release(tab[i]);
    }

    delete[] tab;
}

void MessagesDialog::updateOrClear()
{
    if( ui.mainTab->currentIndex() == 2)
    {
        ui.modulesTree->clear();
        buildTree( NULL, VLC_OBJECT( vlc_object_instance(p_intf) ) );
    }
    else if( ui.mainTab->currentIndex() == 0 )
        ui.messages->clear();
    else if( ui.mainTab->currentIndex() == 1 )
    {
        ui.errors->clear();
        m_errorRowLengths.fill(0);
    }
#ifndef NDEBUG
    else
        updatePLTree();
#endif
}

void MessagesDialog::tabChanged( int i )
{
    updateButton->setIcon( i > 1 ? QIcon(":/menu/update.svg") : QIcon(":/menu/clear.svg") );
    updateButton->setToolTip( i > 1 ? qtr("Update the tree")
                                     : i == 0 ? qtr("Clear the messages")
                                              : qtr("Clear errors and warnings"));
}

void MessagesDialog::showTab( int i )
{
    ui.mainTab->setCurrentIndex(i);
    show();
    raise();
}

void MessagesDialog::MsgCallback( void *self, int type, const vlc_log_t *item,
                                  const char *format, va_list ap )
{
    MessagesDialog *dialog = (MessagesDialog *)self;
    char *str;
    int verbosity = dialog->verbosity.load();

    if( verbosity < 0 || verbosity < (type - VLC_MSG_ERR)
     || unlikely(vasprintf( &str, format, ap ) == -1) )
        return;

    int canc = vlc_savecancel();
    QApplication::postEvent( dialog, new MsgEvent( type, item, str ) );
    vlc_restorecancel( canc );
    free( str );
}

#ifndef NDEBUG

void MessagesDialog::updatePLTree()
{
    pldebugTree->clear();
    {
        vlc_playlist_t* playlist = p_intf->p_playlist;
        vlc_playlist_Lock(playlist);
        size_t count = vlc_playlist_Count( playlist );
        for (size_t i = 0; i < count; i++)
        {
            QTreeWidgetItem *current = new QTreeWidgetItem();
            vlc_playlist_item_t* item = vlc_playlist_Get( playlist, i );
            input_item_t* media = vlc_playlist_item_GetMedia( item );
            current->setText( 0, qfu( media->psz_name ) );
            current->setToolTip( 0, qfu( media->psz_uri ) );
            current->setText( 1, QString("%1").arg( i ) );
            current->setText( 2, QString("%1").arg( (uintptr_t)media ) );
            //current->setText( 3, QString("0x%1").arg( p_node->i_flags, 0, 16 ) );
            current->setText( 3, QString("0x%1").arg( media->i_type, 0, 16 ) );
            pldebugTree->addTopLevelItem( current );
        }
        vlc_playlist_Unlock(playlist);
    }

    pldebugTree->expandAll();
    for ( int i=0; i< 4; i++ )
        pldebugTree->resizeColumnToContents( i );
}
#endif
