/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"

#include "messagepage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"

#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "walletstack.h"
#include "messagemodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "ui_interface.h"
#include "wallet.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTextDocument>
#include <QSettings>
#include <iostream>

extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
double GetPoSKernelPS();

#define VERTICAL_TOOBAR_STYLESHEET "QToolBar {\
border:none;\
height:100%;\
padding-top:20px;\
text-align: left;\
}\
QToolButton {\
min-width:180px;\
background-color: transparent;\
border: 1px solid #3A3939;\
border-radius: 3px;\
margin: 3px;\
padding-left: 5px;\
/*padding-right:50px;*/\
padding-top:5px;\
width:100%;\
text-align: left;\
padding-bottom:5px;\
}\
QToolButton:pressed {\
background-color: #4A4949;\
border: 1px solid silver;\
}\
QToolButton:checked {\
background-color: #777777;\
border: 1px solid silver;\
}\
QToolButton:hover {\
background-color: #4A4949;\
border: 1px solid gray;\
}"
#define HORIZONTAL_TOOLBAR_STYLESHEET "QToolBar {\
    border: 1px solid #393838;\
    background: 1px solid #302F2F;\
    font-weight: bold;\
}"

ActiveLabel::ActiveLabel(const QString & text, QWidget * parent):
    QLabel(parent){}

void ActiveLabel::mouseReleaseEvent(QMouseEvent * event)
{
    emit clicked();
}


BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),

    walletManager(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0),
    prevBlocks(0),
    nWeight(0)
{
    resize(950, 550);
    setWindowTitle(tr("ShadowCoin") + " - " + tr("Wallet"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // TODO: Theme switching :)
    QFile f(":qdarkstyle/style.qss");
    if (f.exists())
    {
        f.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&f);
        qApp->setStyleSheet(ts.readAll());
    }
    else
        QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    // Create wallet list, load/unload buttons, and view
    QFrame *walletFrame = new QFrame();
    QHBoxLayout *walletFrameLayout = new QHBoxLayout(walletFrame);

    // Create wallet list control, load and unload buttons
    QFrame *listFrame = new QFrame();
    listFrame->setMinimumWidth(150);
    listFrame->setMaximumWidth(150);
    listFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout *listFrameLayout = new QVBoxLayout(listFrame);

    QLabel *listFrameLabel = new QLabel();
    listFrameLabel->setText(tr("Wallets"));
    listFrameLabel->setAlignment(Qt::AlignHCenter);
    listFrameLayout->addWidget(listFrameLabel);

    walletList = new QListWidget();
    listFrameLayout->addWidget(walletList);


    newWalletButton = new QPushButton(QIcon(":/icons/add"),tr("New"));
    listFrameLayout->addWidget(newWalletButton);
    newWalletButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    newWalletButton->setStatusTip(tr("Create a new wallet. Must be called wallet-[name].dat, (wallet-stake.dat) for example"));

    loadWalletButton = new QPushButton(QIcon(":/icons/load_wallet"),tr("Load"));
    listFrameLayout->addWidget(loadWalletButton);
    loadWalletButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    loadWalletButton->setStatusTip(tr("Load an existing wallet"));

    unloadWalletButton = new QPushButton(QIcon(":/icons/unload_wallet"),tr("Unload"));
    listFrameLayout->addWidget(unloadWalletButton);
    unloadWalletButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    unloadWalletButton->setStatusTip(tr("Remove an open wallet from memory"));

    connect(newWalletButton, SIGNAL(clicked()), this, SLOT(newWallet()));
    connect(loadWalletButton, SIGNAL(clicked()), this, SLOT(loadWallet()));
    connect(unloadWalletButton, SIGNAL(clicked()), this, SLOT(unloadWallet()));

    // Create wallet stack
    walletStack = new WalletStack(this);
    walletStack->setBitcoinGUI(this);

    walletFrameLayout->addWidget(listFrame);
    walletFrameLayout->addWidget(walletStack);
    setCentralWidget(walletFrame);

    connect(walletList, SIGNAL(currentTextChanged(const QString&)), walletStack, SLOT(setCurrentWalletView(const QString&)));


    // Create status bar
    statusBar();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new GUIUtil::ClickableLabel();
    labelStakingIcon = new GUIUtil::ClickableLabel();
    labelConnectionsIcon = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();

    connect(labelStakingIcon, SIGNAL(clicked()), this, SLOT(stakingIconClicked()));
    connect(labelBlocksIcon, SIGNAL(clicked()),this,SLOT(blocksIconClicked()));
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }

    connect(labelEncryptionIcon, SIGNAL(clicked()), unlockWalletAction, SLOT(trigger()));

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = qApp->style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    rpcConsole = new RPCConsole(this);

    connect(openTrafficAction, SIGNAL(triggered()), rpcConsole, SLOT(showTab_Stats()));
    connect(connectionIconAction, SIGNAL(triggered()), rpcConsole, SLOT(showTab_Peers()));
    connect(labelConnectionsIcon, SIGNAL(clicked()), rpcConsole, SLOT(showTab_Peers()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);
}

BitcoinGUI::~BitcoinGUI()
{
  QMap<QString, WalletModel*>::const_iterator item = mapWalletModels.constBegin();
  while (item != mapWalletModels.constEnd())
  {
      walletStack->removeWalletView(item.key());
      delete item.value();
      item++;
  }
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send coins"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a ShadowCoin address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive coins"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    messageAction = new QAction(QIcon(":/icons/edit"), tr("&Messages"), this);
    messageAction->setToolTip(tr("View and Send Encrypted messages"));
    messageAction->setCheckable(true);
    messageAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    tabGroup->addAction(messageAction);

    shoppingAction = new QAction(QIcon(":/icons/res/icons/services.png"), tr("&Services"), this);
    shoppingAction->setToolTip(tr("<html><head/><body><p><img src=:/toolTip/res/tooltips/servicesTooltip.png/></p></body></html>"));
    shoppingAction->setCheckable(true);
    shoppingAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_7));
    tabGroup->addAction(shoppingAction);

    charityAction = new QAction(QIcon(":/icons/send"), tr("Stake For &Charity"), this);
    charityAction->setStatusTip(tr("Enable Stake For Charity"));
    charityAction->setToolTip(charityAction->statusTip());
    charityAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
    charityAction->setCheckable(true);
    tabGroup->addAction(charityAction);

    blockAction = new QAction(QIcon(":/icons/blexp"), tr("Block Bro&wser"), this);
    blockAction->setStatusTip(tr("Explore the BlockChain"));
    blockAction->setToolTip(blockAction->statusTip());

    blocksIconAction = new QAction(QIcon(":/icons/info"), tr("Current &Block Info"), this);
    blocksIconAction->setStatusTip(tr("Get Current Block Information"));
    blocksIconAction->setToolTip(blocksIconAction->statusTip());

    stakingIconAction = new QAction(QIcon(":/icons/info"), tr("Current &PoS Block Info"), this);
    stakingIconAction->setStatusTip(tr("Get Current PoS Block Information"));
    stakingIconAction->setToolTip(stakingIconAction->statusTip());

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));
    connect(shoppingAction, SIGNAL(triggered()), this, SLOT(gotoShoppingPage()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(messageAction, SIGNAL(triggered()), this, SLOT(gotoMessagePage()));


    newWalletAction = new QAction(QIcon(":/icons/add"),tr("&New Wallet..."), this);
    newWalletAction->setStatusTip(tr("Create a new wallet. Must be called wallet-[name].dat, (wallet-stake.dat) for example"));
    newWalletAction->setToolTip(newWalletAction->statusTip());

    loadWalletAction = new QAction(QIcon(":/icons/load_wallet"), tr("&Load Wallet..."), this);
    loadWalletAction->setStatusTip(tr("Load an existing wallet"));
    loadWalletAction->setToolTip(newWalletAction->statusTip());

    unloadWalletAction = new QAction(QIcon(":/icons/unload_wallet"), tr("&Unload Wallet..."), this);
    unloadWalletAction->setStatusTip(tr("Remove an open wallet from memory"));
    unloadWalletAction->setToolTip(unloadWalletAction->statusTip());

    connect(newWalletAction, SIGNAL(triggered()), this, SLOT(newWallet()));
    connect(loadWalletAction, SIGNAL(triggered()), this, SLOT(loadWallet()));
    connect(unloadWalletAction, SIGNAL(triggered()), this, SLOT(unloadWallet()));

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About ShadowCoin"), this);
    aboutAction->setToolTip(tr("Show information about ShadowCoin"));
    aboutAction->setMenuRole(QAction::AboutRole);



    aboutQtAction = new QAction(QIcon(":/trolltech/qmessagebox/images/qtlogo-64.png"), tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for ShadowCoin"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));
    encryptWalletAction->setCheckable(true);

    unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Wallet..."), this);
    unlockWalletAction->setToolTip(tr("Unlock wallet"));

    lockWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Lock Wallet"), this);
    lockWalletAction->setToolTip(tr("Lock wallet"));

    checkWalletAction = new QAction(QIcon(":/icons/inspect"), tr("&Check Wallet..."), this);
    checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));

    repairWalletAction = new QAction(QIcon(":/icons/res/icons/repair.png"), tr("&Repair Wallet..."), this);
    repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));

    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));
    backupAllWalletsAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup All Wallets..."), this);
    backupAllWalletsAction->setStatusTip(tr("Backup all loaded wallets to another location"));

    dumpWalletAction = new QAction(QIcon(":/icons/export2"), tr("&Export Wallet..."), this);
    dumpWalletAction->setStatusTip(tr("Export wallet's keys to a text file"));

    importWalletAction = new QAction(QIcon(":/icons/import"), tr("&Import Wallet..."), this);
    importWalletAction->setStatusTip(tr("Import a file's keys into a wallet"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));

    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your HoboNickels addresses to prove you own them"));

    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);

    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    openTrafficAction = new QAction(QIcon(":/icons/traffic"), tr("&Traffic window"), this);
    openTrafficAction->setStatusTip(tr("Open Network Traffic Graph"));
    openTrafficAction->setToolTip(openTrafficAction->statusTip());

    connectionIconAction = new QAction(QIcon(":/icons/p2p"), tr("Current &Peer Info"), this);
    connectionIconAction->setStatusTip(tr("Get Current Peer Information"));
    connectionIconAction->setToolTip(connectionIconAction->statusTip());

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
    connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
    connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(backupAllWalletsAction, SIGNAL(triggered()), this, SLOT(backupAllWallets()));
    connect(dumpWalletAction, SIGNAL(triggered()), this, SLOT(dumpWallet()));
    connect(importWalletAction, SIGNAL(triggered()), this, SLOT(importWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWalletForMint()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(blockAction, SIGNAL(triggered()), this, SLOT(gotoBlockBrowser()));
    connect(blocksIconAction, SIGNAL(triggered()), this, SLOT(blocksIconClicked()));
    connect(stakingIconAction, SIGNAL(triggered()), this, SLOT(stakingIconClicked()));
    connect(charityAction, SIGNAL(triggered()), this, SLOT(charityClicked()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addAction(backupAllWalletsAction);
    file->addSeparator();
    file->addAction(dumpWalletAction);
    file->addAction(importWalletAction);
    file->addSeparator();
    file->addAction(newWalletAction);
    file->addAction(loadWalletAction);
    file->addAction(unloadWalletAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(optionsAction);
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addSeparator();
    settings->addAction(unlockWalletAction);
    settings->addAction(lockWalletAction);
    settings->addSeparator();
    settings->addAction(checkWalletAction);
    settings->addAction(repairWalletAction);
    settings->addSeparator();
    settings->addAction(signMessageAction);
    settings->addAction(verifyMessageAction);

    QMenu *network = appMenuBar->addMenu(tr("&Network"));
    network->addAction(blockAction);
    network->addAction(openTrafficAction);
    network->addAction(connectionIconAction);
    network->addSeparator();
    network->addAction(blocksIconAction);
    network->addAction(stakingIconAction);



    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    mainIcon = new QLabel (this);
    mainIcon->setPixmap(QPixmap(":images/sdc-vertical"));
    mainIcon->show();

    mainToolbar = addToolBar(tr("Tabs toolbar"));
    mainToolbar->setObjectName("main");
    mainToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mainToolbar->addWidget(mainIcon);
    mainToolbar->addAction(overviewAction);
    mainToolbar->addAction(sendCoinsAction);
    mainToolbar->addAction(receiveCoinsAction);
    mainToolbar->addAction(historyAction);
    mainToolbar->addAction(addressBookAction);
    mainToolbar->addAction(shoppingAction);
    mainToolbar->addAction(messageAction);
    mainToolbar->addAction(charityAction);
    mainToolbar->setContextMenuPolicy(Qt::NoContextMenu);

    secondaryToolbar = addToolBar(tr("Actions toolbar"));
    secondaryToolbar->setObjectName("actions");
    secondaryToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    secondaryToolbar->addAction(exportAction);
    secondaryToolbar->setContextMenuPolicy(Qt::NoContextMenu);

    connect(mainToolbar,      SIGNAL(orientationChanged(Qt::Orientation)), this, SLOT(mainToolbarOrientation(Qt::Orientation)));
    connect(secondaryToolbar, SIGNAL(orientationChanged(Qt::Orientation)), this, SLOT(secondaryToolbarOrientation(Qt::Orientation)));
    mainToolbarOrientation(mainToolbar->orientation());
    secondaryToolbarOrientation(secondaryToolbar->orientation());
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("ShadowCoin client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }

            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        if(trayIcon)
            createTrayIconMenu();

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        walletStack->setClientModel(clientModel);
        rpcConsole->setClientModel(clientModel);


        // Watch for wallets being loaded or unloaded
        connect(clientModel, SIGNAL(walletAdded(QString)), this, SLOT(addWallet(QString)));
        connect(clientModel, SIGNAL(walletRemoved(QString)), this, SLOT(removeWallet(QString)));
    }
}

void BitcoinGUI::createTrayIconMenu()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow*)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

void BitcoinGUI::addWallet(const QString& name)
{
    WalletModel *walletModel = new WalletModel(walletManager->GetWallet(name.toStdString()).get(), clientModel->getOptionsModel());
    addWallet(name, walletModel);
    setCurrentWallet(name);
}

bool BitcoinGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if (!walletStack->addWalletView(name, walletModel)) return false;
    walletList->addItem(name);
    mapWalletModels[name] = walletModel;
    return true;
}

bool BitcoinGUI::setCurrentWallet(const QString& name)
{
    QList<QListWidgetItem*> walletItems = walletList->findItems(name, Qt::MatchExactly);
    if (walletItems.count() == 0) return false;
    walletList->setCurrentItem(walletItems[0]);
    return true;
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::setMessageModel(MessageModel *messageModel)
{
    this->messageModel = messageModel;
    if(messageModel)
    {
        // Report errors from message thread
        connect(messageModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        // Put transaction list in tabs
        messagePage->setModel(messageModel);

        // Balloon pop-up for new message
        connect(messageModel, SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingMessage(QModelIndex,int,int)));
    }
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("ShadowCoin client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon, this);
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbelled text)
    statusBar()->clearMessage();

    enum BlockSource blockSource = clientModel ? clientModel->getBlockSource() : BLOCK_SOURCE_NONE;
    QString tooltip;
    QString importText;

    switch (blockSource) {
    case BLOCK_SOURCE_NONE:
    case BLOCK_SOURCE_NETWORK:
        importText = tr("Synchronizing with network...");
        break;
    case BLOCK_SOURCE_DISK:
        importText = tr("Importing blocks from disk...");
        break;
    case BLOCK_SOURCE_REINDEX:
        importText = tr("Reindexing blocks on disk...");
    }

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int totalSecs = GetTime() - 1374628210;
    int secs = lastBlockDate.secsTo(currentDate);

    if(count < nTotalBlocks) {
        tooltip = tr("Processed %1 of %2 (estimated) blocks of transaction history.").arg(count).arg(nTotalBlocks);
    } else {
        tooltip = tr("Processed %1 blocks of transaction history.").arg(count);
    }


    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        walletStack->showOutOfSyncWarning(false);
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);

    }
    else
    {
        // Represent time from last generated block in human readable text
        QString timeBehindText;
        timeBehindText=(GUIUtil::formatDurationStr(secs));

        progressBarLabel->setText(tr("Synchronizing with network..."));
        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(totalSecs);
        progressBar->setValue(totalSecs - secs);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        if(count != prevBlocks)
            syncIconMovie->jumpToNextFrame();
        prevBlocks = count;

        walletStack->showOutOfSyncWarning(true);

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::gotoBlockBrowser(QString transactionId)
{
    if (walletStack) walletStack->gotoBlockBrowser(transactionId);
}

void BitcoinGUI::gotoOverviewPage()
{
    if (walletStack) walletStack->gotoOverviewPage();
}

void BitcoinGUI::gotoHistoryPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    historyAction->setChecked(true);
    if (walletStack) walletStack->gotoHistoryPage();
}

void BitcoinGUI::gotoAddressBookPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    if (walletStack) walletStack->gotoAddressBookPage();
}

void BitcoinGUI::gotoShoppingPage()
{
    shoppingAction->setChecked(true);
    if (walletStack) walletStack->gotoShoppingPage();
}

void BitcoinGUI::gotoReceiveCoinsPage(bool fExportOnly, bool fExportConnect, bool fExportFirstTime)
{
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    if (walletStack) walletStack->gotoReceiveCoinsPage();
}

QString BitcoinGUI::getCurrentWallet()
{

  if (walletStack) return QString (walletStack->getCurrentWallet());
  return QString();

}

void BitcoinGUI::gotoSendCoinsPage()
{
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    if (walletStack) walletStack->gotoSendCoinsPage();
}

void BitcoinGUI::gotoMessagePage()
{
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
   if (walletStack) walletStack->gotoMessagePage();
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    if (walletStack) walletStack->gotoSignMessageTab(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletStack) walletStack->gotoVerifyMessageTab(addr);
}
void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to ShadowCoin network", "", count));
}



void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if(modal)
    {
        QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
    } else {
        notificator->notify(Notificator::Critical, title, message);
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction is over the size limit.  You can still send it for a fee of %1, "
          "which goes to the nodes that process your transaction and helps to support the network.  "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QString& date, int unit, qint64 amount, const QString& type, const QString& address)
{

  // On new transaction, make an info balloon
  // Unless the initial block download is in progress, to prevent balloon-spam
  if(!clientModel->inInitialBlockDownload())
  {
         message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
            tr("Date: %1\n"
               "Amount: %2\n"
               "Type: %3\n"
               "Address: %4\n")
                 .arg(date)
                 .arg(BitcoinUnits::formatWithUnit(unit, amount, true))
                 .arg(type)
                 .arg(address), CClientUIInterface::MSG_INFORMATION);
   }
}



void BitcoinGUI::newWallet()
{

  if (!clientModel || !walletManager) return;

  QString dataDir = GetDataDir().string().c_str();

  QString walletFile = QFileDialog::getSaveFileName(this,
      tr("Create Wallet"), dataDir, tr("Wallet Files (*.dat)"));

  if (walletFile == "") return;
  std::ostringstream err;
  std::string walletName;

  if (!walletManager->LoadWalletFromFile(walletFile.toStdString(), walletName, err))
  {
      QMessageBox errBox;
      errBox.setText(err.str().c_str());
      errBox.exec();
      return;
  }
  WalletModel *walletModel = new WalletModel(walletManager->GetWallet(walletName).get(), clientModel->getOptionsModel());
  addWallet(walletName.c_str(), walletModel);
  setCurrentWallet(walletName.c_str());

}

void BitcoinGUI::loadWallet()
{
    if (!clientModel || !walletManager) return;

    QString dataDir = GetDataDir().string().c_str();
    QString walletFile = QFileDialog::getOpenFileName(this,
        tr("Load Wallet"), dataDir, tr("Wallet Files (*.dat)"));

    if (walletFile == "") return;

    std::ostringstream err;
    std::string walletName;
    if (!walletManager->LoadWalletFromFile(walletFile.toStdString(), walletName, err))
    {
        QMessageBox errBox;
        errBox.setText(err.str().c_str());
        errBox.exec();
        return;
    }
    WalletModel *walletModel = new WalletModel(walletManager->GetWallet(walletName).get(), clientModel->getOptionsModel());
    addWallet(walletName.c_str(), walletModel);
    setCurrentWallet(walletName.c_str());
}

void BitcoinGUI::removeWallet(const QString& name)
{
    QList<QListWidgetItem*> walletItems = walletList->findItems(name, Qt::MatchExactly);
    if (walletItems.count() == 0) return;
    walletList->setCurrentItem(walletItems[0]);
    int row = walletList->currentRow();
    if (row <= 0) return;
    QListWidgetItem* selectedItem = walletList->takeItem(row);
    QString walletName = selectedItem->text();
    walletStack->removeWalletView(walletName);
    delete selectedItem;
    WalletModel *walletModel = mapWalletModels.take(walletName);
    delete walletModel;
}

void BitcoinGUI::unloadWallet()
{
    int row = walletList->currentRow();
    if (row <= 0) return;
    QListWidgetItem* selectedItem = walletList->takeItem(row);
    QString walletName = selectedItem->text();
    walletStack->removeWalletView(walletName);
    delete selectedItem;
    WalletModel *walletModel = mapWalletModels.take(walletName);
    delete walletModel;
    walletManager->UnloadWallet(walletName.toStdString());
    walletStack->setTotBalance();
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}



void BitcoinGUI::handleURI(QString strURI)
{
  // URI has to be valid
  if (!walletStack->handleURI(strURI))
      message(tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid HoboNickels address or malformed URI parameters."),
                   CClientUIInterface::ICON_WARNING);
}

void BitcoinGUI::mainToolbarOrientation(Qt::Orientation orientation)
{
    if(orientation == Qt::Horizontal)
    {
        mainIcon->setPixmap(QPixmap(":images/sdc-horizontal"));
        mainIcon->show();
        mainToolbar->setStyleSheet(HORIZONTAL_TOOLBAR_STYLESHEET);
        messageAction->setIconText(tr("&Messages"));
    }
    else
    {
        mainIcon->setPixmap(QPixmap(":images/sdc-vertical"));
        mainIcon->show();

        mainToolbar->setStyleSheet(VERTICAL_TOOBAR_STYLESHEET);
        messageAction->setIconText(tr("Encrypted &Messages"));
    }
}

void BitcoinGUI::secondaryToolbarOrientation(Qt::Orientation orientation)
{
    secondaryToolbar->setStyleSheet(orientation == Qt::Horizontal ? HORIZONTAL_TOOLBAR_STYLESHEET : VERTICAL_TOOBAR_STYLESHEET);
}

void BitcoinGUI::message(const QString &title, const QString &message, unsigned int style, const QString &detail)
{
    QString strTitle = tr("HoboNickels") + " - ";
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;


    // Check for usage of predefined title
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strTitle += tr("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strTitle += tr("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strTitle += tr("Information");
        break;
    default:
        strTitle += title; // Use supplied title
    }

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons);

        if(!detail.isEmpty()) { mBox.setDetailedText(detail); }

        mBox.exec();
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    case WalletModel::Unlocked:
        disconnect(labelEncryptionIcon, SIGNAL(clicked()), unlockWalletAction, SLOT(trigger()));
        disconnect(labelEncryptionIcon, SIGNAL(clicked()),   lockWalletAction, SLOT(trigger()));
        connect   (labelEncryptionIcon, SIGNAL(clicked()),   lockWalletAction, SLOT(trigger()));
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>")); // TODO: Click to lock + translations
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        connect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    case WalletModel::Locked:
        disconnect(labelEncryptionIcon, SIGNAL(clicked()), unlockWalletAction, SLOT(trigger()));
        disconnect(labelEncryptionIcon, SIGNAL(clicked()),   lockWalletAction, SLOT(trigger()));
        connect   (labelEncryptionIcon, SIGNAL(clicked()), unlockWalletAction, SLOT(trigger()));
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>")); // TODO: Click to unlock + translations
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        connect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    }
    // Put here as this function will be called on any wallet or lock status change.
    updateStakingIcon();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (walletStack->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            walletStack->gotoSendCoinsPage();
        else
             message(tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid HoboNickels address or malformed URI parameters."),
                     CClientUIInterface::ICON_WARNING);

    }

    event->acceptProposedAction();
}

void BitcoinGUI::encryptWallet(bool status)
{
   if (walletStack) walletStack->encryptWallet(status);
}

void BitcoinGUI::checkWallet()
{
   if (walletStack) walletStack->checkWallet();
}

void BitcoinGUI::repairWallet()
{
   if (walletStack) walletStack->repairWallet();
}

void BitcoinGUI::backupWallet()
{
   if (walletStack) walletStack->backupWallet();
}

void BitcoinGUI::backupAllWallets()
{
   if (walletStack) walletStack->backupAllWallets();
}

void BitcoinGUI::dumpWallet()
{
   if (walletStack) walletStack->dumpWallet();
}

void BitcoinGUI::importWallet()
{
   if (walletStack) walletStack->importWallet();
}

void BitcoinGUI::changePassphrase()
{
   if (walletStack) walletStack->changePassphrase();
}

void BitcoinGUI::unlockWallet()
{
   if (walletStack) walletStack->unlockWallet();
}

void BitcoinGUI::lockWallet()
{
   if (walletStack) walletStack->lockWallet();
}

void BitcoinGUI::unlockWalletForMint()
{
   if (walletStack) walletStack->unlockWalletForMint();
}

void BitcoinGUI::charityClicked(QString addr)
{
    if (walletStack) walletStack->charityClicked(addr);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateWeight()
{
    if (!pwalletMain)
        return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet)
        return;

    uint64_t nMinWeight = 0, nMaxWeight = 0;
    pwalletMain->GetStakeWeight(*pwalletMain, nMinWeight, nMaxWeight, nWeight);
}

void BitcoinGUI::blocksIconClicked()
{
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;

    int unit = clientModel->getOptionsModel()->getDisplayUnit();

    message(tr("Extended Block Chain Information"),
        tr("Client Version: %1\n"
           "Protocol Version: %2\n\n"
           "Last Block Number: %3\n"
           "Last Block Time: %4\n\n"
           "Current PoW Difficulty: %5\n"
           "Current PoW Mh/s: %6\n"
           "Current PoW Reward: %7\n\n"
           "Current Wallet Viewed: %8\n"
           "Current Wallet Version: %9\n"
           "Total Wallets Loaded: %10\n\n"
           "Network Money Supply: %11\n")
           .arg(clientModel->formatFullVersion())
           .arg(clientModel->getProtocolVersion())
           .arg(clientModel->getNumBlocks())
           .arg(clientModel->getLastBlockDate().toString())
           .arg(clientModel->getDifficulty())
           .arg(clientModel->getPoWMHashPS())
           .arg(tr("5.0000000")) // Hard Coded as HBN is always 5, but should use GetProofOfWorkReward
           .arg(walletStack->getCurrentWallet())
           .arg(walletStack->getWalletVersion())
           .arg(walletManager->GetWalletCount())
           .arg(BitcoinUnits::formatWithUnit(unit, clientModel->getMoneySupply(), false))
        ,CClientUIInterface::MODAL);
}

void BitcoinGUI::lockIconClicked()
{
    if(walletStack->isWalletLocked())
        unlockWalletForMint();
}

void BitcoinGUI::stakingIconClicked()
{
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;

    uint64_t nMinWeight = 0, nMaxWeight = 0;
    walletStack->getStakeWeight(nMinWeight,nMaxWeight,nWeight);

    CBitcoinAddress strAddress;
    CBitcoinAddress strChangeAddress;
    int nPer;
    qint64 nMin;
    qint64 nMax;

    walletStack->getStakeForCharity(nPer, strAddress, strChangeAddress, nMin, nMax);

    int unit = clientModel->getOptionsModel()->getDisplayUnit();

    message(tr("Extended Staking Information"),
      tr("Client Version: %1\n"
         "Protocol Version: %2\n\n"
         "Last PoS Block Number: %3\n"
         "Last PoS Block Time: %4\n\n"
         "Current PoS Difficulty: %5\n"
         "Current PoS Netweight: %6\n"
         "Current PoS Yearly Interest: %7\%\n\n"
         "Current Wallet Viewed: %8\n"
         "Current Wallet Version: %9\n"
         "Current Wallet PoS Weight: %10\n"
         "Current Wallet Reserve Balance: %11\n\n"
         "Stake Split Threshold %12\n"
         "Stake Combine Threshold %13\n\n"
         "Stake for Charity Address: %14\n"
         "Stake for Charity Percentage: %15\n\n"
         "Total Wallets Loaded: %16\n"
         "Total Wallets PoS Weight: %17\n\n"
         "Network Money Supply: %18\n")
         .arg(clientModel->formatFullVersion())
         .arg(clientModel->getProtocolVersion())
         .arg(clientModel->getLastPoSBlock())
         .arg(clientModel->getLastBlockDate(true).toString())
         .arg(clientModel->getDifficulty(true))
         .arg(clientModel->getPosKernalPS())
         .arg(clientModel->getProofOfStakeReward())
         .arg(walletStack->getCurrentWallet())
         .arg(walletStack->getWalletVersion())
         .arg(nWeight)
         .arg(BitcoinUnits::formatWithUnit(unit, walletStack->getReserveBalance()))
         .arg(BitcoinUnits::formatWithUnit(unit, nSplitThreshold, false))
         .arg(BitcoinUnits::formatWithUnit(unit, nCombineThreshold, false))
         .arg(strAddress.IsValid() ? strAddress.ToString().c_str() : "Not Giving")
         .arg(nPer)
         .arg(walletManager->GetWalletCount())
         .arg(walletStack->getTotStakeWeight())
         .arg(BitcoinUnits::formatWithUnit(unit, clientModel->getMoneySupply(), false))
      ,CClientUIInterface::MODAL);
}

void BitcoinGUI::updateStakingIcon()
{
    if (!walletStack)
        return;

    labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));

    if (!clientModel->getNumConnections())
        labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
    else if (clientModel->getNumConnections() < 3 )
        labelStakingIcon->setToolTip(tr("Not staking because wallet is still acquiring nodes"));
    else if (clientModel->inInitialBlockDownload() ||
             clientModel->getNumBlocks() < clientModel->getNumBlocksOfPeers())
        labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
    else if (walletStack->isWalletLocked())
        labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
    else
    {
        uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;

        walletStack->getStakeWeight(nMinWeight,nMaxWeight,nWeight);
        if (!nWeight)
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
        else
        {
            quint64 nNetworkWeight = clientModel->getPosKernalPS();
            int nEstimateTime = clientModel->getStakeTargetSpacing() * 10 * nNetworkWeight / nWeight;
            QString text = (GUIUtil::formatDurationStr(nEstimateTime));

            labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
            labelStakingIcon->setToolTip(tr("Staking.\n Your weight is %1\n Network weight is %2\n You have 50\% chance of producing a stake within %3").arg(nWeight).arg(nNetworkWeight).arg(text));
          }
     }
}

WId BitcoinGUI::getMainWinId() const
{
    return winId();
}
