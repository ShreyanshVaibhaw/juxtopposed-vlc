/*****************************************************************************
 * open.cpp : Advanced open dialog
 *****************************************************************************
 * Copyright © 2006-2011 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
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

#include "dialogs/open/open.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "util/qt_dirs.hpp"
#include "util/interface_translator.hpp"
#include "playlist/playlist_controller.hpp"

#include <QRegularExpression>
#include <QMenu>
#include <QTabBar>
#include <QSignalBlocker>

#include <vlc_url.h>

#include <cstddef>

#ifndef NDEBUG
# define DEBUG_QT 1
#endif

static QString sourceSettingsGroup(OpenDialog::OpenTab source)
{
    switch (source)
    {
    case OpenDialog::OPEN_DISC_TAB: return QStringLiteral("OpenDiscDialog");
    case OpenDialog::OPEN_CAPTURE_TAB: return QStringLiteral("OpenCaptureDialog");
    case OpenDialog::OPEN_NETWORK_TAB: return QStringLiteral("OpenNetworkDialog");
    case OpenDialog::OPEN_FILE_TAB: return QStringLiteral("OpenFileDialog");
    default: return QStringLiteral("OpenDialog");
    }
}

OpenDialog::OpenDialog(qt_intf_t *_p_intf, QWindow* parent, OpenTab sourceKind)
    : QVLCDialog(parent, _p_intf), source(sourceKind), settingsGroup(sourceSettingsGroup(sourceKind))
{
    /* Basic Creation of the Window */
    ui.setupUi( this );
    setWindowTitle( qtr( "Open Media" ) );
    setWindowRole( "vlc-open-media" );
    setWindowModality( Qt::WindowModal );

    const auto addSource = [this](OpenTab tab, const QIcon& icon, const QString& label) {
        if (source != OPEN_TAB_MAX && source != tab) return;
        OpenPanel *panel = nullptr;
        switch (tab)
        {
        case OPEN_FILE_TAB: panel = new FileOpenPanel(this, p_intf); break;
        case OPEN_DISC_TAB: panel = new DiscOpenPanel(this, p_intf); break;
        case OPEN_NETWORK_TAB: panel = new NetOpenPanel(this, p_intf); break;
        case OPEN_CAPTURE_TAB: panel = new CaptureOpenPanel(this, p_intf); break;
        default: Q_UNREACHABLE();
        }
        sourcePanels[tab] = panel;
        ui.Tab->addTab(panel, icon, label);
    };
    addSource(OPEN_FILE_TAB, QIcon(":/menu/file.svg"), qtr("&File"));
    addSource(OPEN_DISC_TAB, QIcon(":/menu/disc.svg"), qtr("&Disc"));
    addSource(OPEN_NETWORK_TAB, QIcon(":/menu/network.svg"), qtr("&Network"));
    addSource(OPEN_CAPTURE_TAB, QIcon(":/menu/capture-card.svg"), qtr("Capture &Device"));
    ui.Tab->tabBar()->setVisible(source == OPEN_TAB_MAX);

    /* Hide the Slave input widgets */
    ui.slaveLabel->hide();
    ui.slaveText->hide();
    ui.slaveBrowseButton->hide();

    /* Buttons Creation */
    /* Play Button */
    playButton = ui.playButton;

    /* Cancel Button */
    cancelButton = new QPushButton( qtr( "&Cancel" ) );

    /* Select Button */
    selectButton = new QPushButton( qtr( "&Select" ) );
    selectButton->setProperty("redesignPrimary", true);
    actionButton = new QPushButton;
    actionButton->setObjectName("openActionButton");
    actionButton->setProperty("redesignPrimary", true);

    /* Menu for the Play button */
    QMenu * openButtonMenu = new QMenu( "Open", playButton );
    openButtonMenu->addAction( qtr( "&Enqueue" ), this, [this] { enqueue(); }
                                    )->setShortcut( QKeySequence( "Alt+E" ) );
    openButtonMenu->addAction( qtr( "&Play" ), this, &OpenDialog::play
                                    )->setShortcut( QKeySequence( "Alt+P" ) );
    openButtonMenu->addAction( qtr( "&Stream" ), this, &OpenDialog::stream
                                    )->setShortcut( QKeySequence( "Alt+S" ) );
    openButtonMenu->addAction( qtr( "C&onvert" ), this, &OpenDialog::transcode
                                    )->setShortcut( QKeySequence( "Alt+O" ) );

    playButton->setMenu( openButtonMenu );

    /* Add the action and cancellation buttons. */
    ui.buttonsBox->addButton( actionButton, QDialogButtonBox::AcceptRole );
    ui.buttonsBox->addButton( selectButton, QDialogButtonBox::AcceptRole );
    ui.buttonsBox->addButton( cancelButton, QDialogButtonBox::RejectRole );

    /* At creation time, modify the default buttons */
    setMenuAction();

    /* Force MRL update on tab change */
    connect( ui.Tab, &QTabWidget::currentChanged, this, &OpenDialog::signalCurrent );

    for (OpenPanel *panel : sourcePanels)
        if (panel)
        {
            connect(panel, &OpenPanel::mrlUpdated, this,
                    QOverload<const QStringList&, const QString&>::of(&OpenDialog::updateMRL));
            connect(panel, &OpenPanel::methodChanged, this, &OpenDialog::newCachingMethod);
            connect(panel, &OpenPanel::acceptRequested, this, &OpenDialog::close);
            connect(panel, &OpenPanel::cancelRequested, this, &OpenDialog::cancel);
        }

    /* Advanced frame Connects */
    connect( ui.slaveCheckbox, &QCheckBox::toggled,
             this, QOverload<>::of(&OpenDialog::updateMRL) );
    connect( ui.slaveText, &QLineEdit::textChanged,
             this, QOverload<>::of(&OpenDialog::updateMRL) );
    connect( ui.cacheSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
             this, QOverload<>::of(&OpenDialog::updateMRL) );
    connect( ui.startTimeTimeEdit, &QTimeEdit::timeChanged,
             this, QOverload<>::of(&OpenDialog::updateMRL) );
    connect( ui.stopTimeTimeEdit, &QTimeEdit::timeChanged,
             this, QOverload<>::of(&OpenDialog::updateMRL) );
    BUTTONACT( ui.advancedCheckBox, &OpenDialog::toggleAdvancedPanel );
    BUTTONACT( ui.slaveBrowseButton, &OpenDialog::browseInputSlave );

    /* Buttons action */
    BUTTONACT( playButton, &OpenDialog::selectSlots );
    BUTTONACT( actionButton, &OpenDialog::selectSlots );
    BUTTONACT( selectButton, &OpenDialog::close );
    BUTTONACT( cancelButton, &OpenDialog::cancel );

    /* Hide the advancedPanel */
    if( !getSettings()->value(settingsGroup + "/advanced", false).toBool())
    {
        ui.advancedFrame->hide();
        ui.advancedFrame->setEnabled( false );
    }
    else
        ui.advancedCheckBox->setChecked( true );

    /* Initialize caching */
    storedMethod = "";
    newCachingMethod( "file-caching" );

    /* enforce section due to .ui bug */
    ui.startTimeTimeEdit->setCurrentSection( QDateTimeEdit::SecondSection );
    ui.stopTimeTimeEdit->setCurrentSection( QDateTimeEdit::SecondSection );

    setMinimumSize( sizeHint() );
    setMaximumWidth( 900 );
    resize(getSettings()->value(settingsGroup + "/size", QSize(500, 400)).toSize());
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
            ui.retranslateUi(this);
            const char* const tabs[] = {N_("&File"), N_("&Disc"), N_("&Network"), N_("Capture &Device")};
            for (int tab = 0; tab < OPEN_TAB_MAX; ++tab)
                if (sourcePanels[tab]) ui.Tab->setTabText(ui.Tab->indexOf(sourcePanels[tab]), qtr(tabs[tab]));
            cancelButton->setText(qtr("&Cancel"));
            selectButton->setText(qtr("&Select"));
            const char* const actions[] = {N_("&Enqueue"), N_("&Play"), N_("&Stream"), N_("C&onvert")};
            const auto menu = playButton->menu()->actions();
            for (int i = 0; i < menu.size() && i < int(ARRAY_SIZE(actions)); ++i)
                menu[i]->setText(qtr(actions[i]));
            setMenuAction();
        });
}

void OpenDialog::setActionFlag(OpenDialog::ActionFlag flag)
{
    i_action_flag = flag;
    setMenuAction();
}

/* Finish the dialog and decide if you open another one after */
void OpenDialog::setMenuAction()
{
    const bool advanced = source == OPEN_TAB_MAX && i_action_flag == OPEN_AND_PLAY;
    for (QAction *action : playButton->menu()->actions())
        action->setEnabled(advanced);
    selectButton->setDefault(false);
    actionButton->setDefault(false);
    actionButton->hide();
    playButton->hide();
    selectButton->hide();

    if (source != OPEN_TAB_MAX)
    {
        switch (source)
        {
        case OPEN_DISC_TAB: setWindowTitle(qtr("Open Disc")); break;
        case OPEN_CAPTURE_TAB: setWindowTitle(qtr("Open Capture Device")); break;
        case OPEN_NETWORK_TAB: setWindowTitle(qtr("Open Network Stream")); break;
        default: setWindowTitle(qtr("Open File")); break;
        }
    }
    else
    {
        switch (i_action_flag)
        {
        case OPEN_AND_SAVE: setWindowTitle(qtr("Convert")); break;
        case OPEN_AND_STREAM: setWindowTitle(qtr("Stream")); break;
        case OPEN_AND_ENQUEUE: setWindowTitle(qtr("Add to Queue")); break;
        case SELECT: setWindowTitle(qtr("Select Media")); break;
        default: setWindowTitle(qtr("Open Media")); break;
        }
    }
    if( i_action_flag == SELECT )
    {
        playButton->hide();
        selectButton->show();
        selectButton->setDefault( true );
    }
    else if (advanced)
    {
        playButton->setText(qtr("&Play"));
        playButton->show();
    }
    else
    {
        switch ( i_action_flag )
        {
        case OPEN_AND_STREAM:
            actionButton->setText( qtr( "&Stream" ) );
            break;
        case OPEN_AND_SAVE:
            actionButton->setText( qtr( "C&onvert" ) );
            break;
        case OPEN_AND_ENQUEUE:
            actionButton->setText( qtr( "Add to &Queue" ) );
            break;
        case OPEN_AND_PLAY:
        default:
            actionButton->setText( qtr( "&Open" ) );
        }
        actionButton->show();
        actionButton->setDefault(true);
    }
}

OpenDialog::~OpenDialog()
{
    getSettings()->setValue(settingsGroup + "/size", size() -
                 ( ui.advancedFrame->isEnabled() ?
                   QSize(0, ui.advancedFrame->height()) : QSize(0, 0) ) );
    getSettings()->setValue(settingsGroup + "/advanced", ui.advancedFrame->isEnabled());
}

/* Used by VLM dialog and inputSlave selection */
QString OpenDialog::getMRL( bool b_all )
{
    if( itemsMRL.count() == 0 ) return "";
    return b_all ? itemsMRL[0] + getOptions()
                 : itemsMRL[0];
}

QStringList OpenDialog::getMRLs()
{
    return itemsMRL;
}

QString OpenDialog::getOptions()
{
    return ui.advancedLineInput->text();
}

void OpenDialog::showTab( OpenDialog::OpenTab i_tab, OpenDialog::ActionFlag i_action )
{
    assert(source == OPEN_TAB_MAX || source == i_tab);
    const int page = source == OPEN_TAB_MAX ? i_tab : 0;
    {
        const QSignalBlocker blocker(ui.Tab);
        ui.Tab->setCurrentIndex(page);
    }
    setActionFlag(i_action);
    signalCurrent(page);
    show();
    if( ui.Tab->currentWidget() != NULL )
    {
        OpenPanel *panel = qobject_cast<OpenPanel *>( ui.Tab->currentWidget() );
        assert( panel );
        panel->onFocus();
    }
}

void OpenDialog::toggleAdvancedPanel()
{
    if( ui.advancedFrame->isVisible() )
    {
        ui.advancedFrame->hide();
        ui.advancedFrame->setEnabled( false );
        if( size().isValid() )
            resize( size().width(), size().height()
                    - ui.advancedFrame->height() );
    }
    else
    {
        ui.advancedFrame->show();
        ui.advancedFrame->setEnabled( true );
        if( size().isValid() )
            resize( size().width(), size().height()
                    + ui.advancedFrame->height() );
    }
}

void OpenDialog::browseInputSlave()
{
    QWidget* windowWidget = window();
    QWindow* parentWindow = windowWidget ? windowWidget->windowHandle() : nullptr;
    OpenDialog od( p_intf, parentWindow );
    od.setActionFlag(SELECT);
    od.exec();
    ui.slaveText->setText( od.getMRL( false ) );
}

/* Function called on signal currentChanged triggered */
void OpenDialog::signalCurrent( int )
{
    if (auto *capture = qobject_cast<CaptureOpenPanel*>(ui.Tab->currentWidget()))
        capture->initialize();
    if( ui.Tab->currentWidget() != NULL )
    {
        OpenPanel *panel = qobject_cast<OpenPanel *>( ui.Tab->currentWidget() );
        assert( panel );
        panel->onFocus();
        panel->updateMRL();
        panel->updateContext( i_action_flag == OPEN_AND_PLAY ?
                              OpenPanel::CONTEXT_INTERACTIVE :
                              OpenPanel::CONTEXT_BATCH );
    }
    // Capture controls are built lazily. Their styled minimum sizes are ready after polish.
    QMetaObject::invokeMethod(this, [this] { setMinimumSize(minimumSizeHint()); }, Qt::QueuedConnection);
}

/***********
 * Actions *
 ***********/
/* If Cancel is pressed or escaped */
void OpenDialog::cancel()
{
    /* Clear the panels */
    for (OpenPanel *panel : sourcePanels)
        if (panel) panel->clear();

    /* Clear the variables */
    itemsMRL.clear();
    optionsMRL.clear();

    /* If in Select Mode, reject instead of hiding */
    if( i_action_flag == SELECT ) reject();
    else hide();
}

/* If EnterKey is pressed */
void OpenDialog::close()
{
    /* If in Select Mode, accept instead of selecting a Slot */
    if( i_action_flag == SELECT )
        accept();
    else
        selectSlots();
}

/* Play button */
void OpenDialog::selectSlots()
{
    switch ( i_action_flag )
    {
    case OPEN_AND_STREAM:
        stream();
        break;
    case OPEN_AND_SAVE:
        transcode();
        break;
    case OPEN_AND_ENQUEUE:
        enqueue();
        break;
    case OPEN_AND_PLAY:
    default:
        play();
    }
}

/* Play Action, called from selectSlots or play Menu */
void OpenDialog::play()
{
    enqueue( false );
}

/* Enqueue Action, called from selectSlots or enqueue Menu */
void OpenDialog::enqueue( bool b_enqueue )
{
    toggleVisible();

    if( i_action_flag == SELECT )
    {
        accept();
        return;
    }

    for (OpenPanel *panel : sourcePanels)
        if (panel) panel->onAccept();

    /* Sort alphabetically */
    itemsMRL.sort();

    /* Go through the item list */
    QVector<vlc::playlist::Media> medias;
    /* Take options from the UI, not from what we stored */
    QStringList optionsList = getOptions().split( " :" );
    for( const QString& mrl : itemsMRL)
        medias.push_back( vlc::playlist::Media{mrl, nullptr, optionsList} );
    if (!medias.empty())
        THEMPL->append(medias, !b_enqueue);
}

void OpenDialog::transcode()
{
    stream( true );
}

void OpenDialog::stream( bool b_transcode_only )
{
//    QString soutMRL = getMRL( false );
//    if( soutMRL.isEmpty() ) return;

    for (OpenPanel *panel : sourcePanels)
        if (panel) panel->onAccept();

    QStringList soutMRLS = getMRLs();
    if(soutMRLS.empty())
    {
        return;
    }

    toggleVisible();

    /* Dbg and send :D */
    msg_Dbg( p_intf, "MRL(s) passed to the Sout: %zu", static_cast<size_t>( soutMRLS.length() ) );
    for(int i = 0; i < soutMRLS.length(); i++)
    {
        msg_Dbg( p_intf, "MRL(s) passed to the Sout: %s", qtu( soutMRLS[i] ) );
    }
    QWidget* windowWidget = window();
    QWindow* parentWindow = windowWidget ? windowWidget->windowHandle() : nullptr;
    THEDP->streamingDialog( parentWindow, soutMRLS, b_transcode_only,
                            getOptions().split( " :" ) );
}

/* Update the MRL items from the panels */
void OpenDialog::updateMRL( const QStringList& item, const QString& tempMRL )
{
    optionsMRL = tempMRL;
    itemsMRL = item;
    updateMRL();
}

/* Format the time to milliseconds */
QString TimeToMilliseconds( const QTimeEdit *t ) {
    return QString::number( ( t->minimumTime().msecsTo( t->time() ) ) / 1000.0, 'f', 3);
}

/* Update the complete MRL */
void OpenDialog::updateMRL() {
    QString mrl = optionsMRL;
    if( ui.slaveCheckbox->isChecked() ) {
        mrl += " :input-slave=" + ui.slaveText->text();
    }
    mrl += QString( " :%1=%2" ).arg( storedMethod ).
                                arg( ui.cacheSpinBox->value() );
    if( ui.startTimeTimeEdit->time() != ui.startTimeTimeEdit->minimumTime() ) {
        mrl += " :start-time=" + TimeToMilliseconds( ui.startTimeTimeEdit );
    }
    if( ui.stopTimeTimeEdit->time() > ui.startTimeTimeEdit->time() ) {
        mrl += " :stop-time=" + TimeToMilliseconds( ui.stopTimeTimeEdit );
    }
    ui.advancedLineInput->setText( mrl );
    ui.mrlLine->setText( itemsMRL.join( " " ) );
    /* Only allow action without valid items */
    playButton->setEnabled( !itemsMRL.isEmpty() );
    actionButton->setEnabled( !itemsMRL.isEmpty() );
    selectButton->setEnabled( !itemsMRL.isEmpty() );
}

/* Change the caching combobox */
void OpenDialog::newCachingMethod( const QString& method )
{
    if( method != storedMethod ) {
        storedMethod = method;
        int i_value = var_InheritInteger( p_intf, qtu( storedMethod ) );
        ui.cacheSpinBox->setValue( i_value );
    }
}

/* Split the entries
 * FIXME! */
QStringList OpenDialog::SeparateEntries( const QString& entries )
{
    bool b_quotes_mode = false;

    QStringList entries_array;
    QString entry;

    int index = 0;
    while( index < entries.length() )
    {
        int delim_pos = entries.indexOf( QRegularExpression( QStringLiteral( "\\s+|\"" ) ), index );
        if( delim_pos < 0 ) delim_pos = entries.length() - 1;
        entry += entries.mid( index, delim_pos - index + 1 );
        index = delim_pos + 1;

        if( entry.isEmpty() ) continue;

        if( !b_quotes_mode && entry.endsWith( "\"" ) )
        {
            /* Enters quotes mode */
            entry.truncate( entry.length() - 1 );
            b_quotes_mode = true;
        }
        else if( b_quotes_mode && entry.endsWith( "\"" ) )
        {
            /* Finished the quotes mode */
            entry.truncate( entry.length() - 1 );
            b_quotes_mode = false;
        }
        else if( !b_quotes_mode && !entry.endsWith( "\"" ) )
        {
            /* we found a non-quoted standalone string */
            if( index < entries.length() ||
                entry.endsWith( " " ) || entry.endsWith( "\t" ) ||
                entry.endsWith( "\r" ) || entry.endsWith( "\n" ) )
                entry.truncate( entry.length() - 1 );
            if( !entry.isEmpty() ) entries_array.append( entry );
            entry.clear();
        }
        else
        {;}
    }

    if( !entry.isEmpty() ) entries_array.append( entry );

    return entries_array;
}
