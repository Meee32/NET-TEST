#ifndef BITCOINGUI_H
#define BITCOINGUI_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QLabel>
#include <QMap>

#include "util.h"


class TransactionTableModel;
class WalletView;
class ClientModel;
class WalletModel;
class WalletStack;
class TransactionView;
class OverviewPage;
class AddressBookPage;
class SendCoinsDialog;
class SignVerifyMessageDialog;
class Notificator;
class RPCConsole;
class StakeForCharityDialog;
class CWallet;
class CWalletManager;
class MessageModel;

QT_BEGIN_NAMESPACE
class QLabel;
class QModelIndex;
class QProgressBar;
class QStackedWidget;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

class ActiveLabel : public QLabel
{
    Q_OBJECT
public:
    ActiveLabel(const QString & text = "", QWidget * parent = 0);
    ~ActiveLabel(){}

signals:
    void clicked();

protected:
    void mouseReleaseEvent (QMouseEvent * event) ;

};

/**
  Bitcoin GUI main class. This class represents the main window of the Bitcoin UI. It communicates with both the client and
  wallet models to give the user an up-to-date view of the current core state.
*/
class BitcoinGUI : public QMainWindow
{
    Q_OBJECT

public:
    explicit BitcoinGUI(QWidget *parent = 0);
    ~BitcoinGUI();

    int nHeight;
    /** Set the client model.
        The client model represents the part of the core that communicates with the P2P network, and is wallet-agnostic.
    */
    void setClientModel(ClientModel *clientModel);
    /** Set the wallet model.
        The wallet model represents a bitcoin wallet, and offers access to the list of transactions, address book and sending
        functionality.
    */

    void setWalletManager(CWalletManager *walletManager) { this->walletManager = walletManager; }
    bool addWallet(const QString& name, WalletModel *walletModel, MessageModel *messageModel);
    QString getCurrentWallet();
    bool setCurrentWallet(const QString& name);

    QAction *exportAction;

   /// Get window identifier of QMainWindow (BitcoinGUI)
   WId getMainWinId() const;

   Notificator* getNotificator();

protected:
    void changeEvent(QEvent *e);
    void closeEvent(QCloseEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);
    bool eventFilter(QObject *object, QEvent *event);    

private:
    ClientModel *clientModel;
    CWalletManager *walletManager;
    StakeForCharityDialog *stakeForCharityDialog;

    QMap<QString, WalletModel*> mapWalletModels;
    QListWidget *walletList;
    WalletStack *walletStack;
    WalletView *walletView;
    QPushButton *loadWalletButton;
    QPushButton *unloadWalletButton;
    QPushButton *newWalletButton;


    QLabel *labelEncryptionIcon;
    QLabel *labelStakingIcon;
    QLabel *labelConnectionsIcon;
    QLabel *labelBlocksIcon;
    QLabel *progressBarLabel;
    QLabel *mainIcon;
    QToolBar *mainToolbar;
    QToolBar *secondaryToolbar;
    QProgressBar *progressBar;

    QMenuBar *appMenuBar;
    QAction *overviewAction;
    QAction *historyAction;
    QAction *quitAction;
    QAction *sendCoinsAction;
    QAction *addressBookAction;
    QAction *shoppingAction;
    QAction *messageAction;
    QAction *signMessageAction;
    QAction *verifyMessageAction;
    QAction *aboutAction;
    QAction *charityAction;
    QAction *receiveCoinsAction;
    QAction *optionsAction;
    QAction *toggleHideAction;
    QAction *encryptWalletAction;
    QAction *unlockWalletAction;
    QAction *lockWalletAction;
    QAction *checkWalletAction;
    QAction *repairWalletAction;
    QAction *backupWalletAction;
    QAction *backupAllWalletsAction;
    QAction *dumpWalletAction;
    QAction *importWalletAction;
    QAction *changePassphraseAction;
    QAction *aboutQtAction;
    QAction *openRPCConsoleAction;
    QAction *openTrafficAction;
    QAction *loadWalletAction;
    QAction *unloadWalletAction;
    QAction *newWalletAction;
    QAction *blockAction;
    QAction *blocksIconAction;
    QAction *connectionIconAction;
    QAction *stakingIconAction;

    QSystemTrayIcon *trayIcon;    
    TransactionView *transactionView;
    RPCConsole *rpcConsole;
    Notificator *notificator;

    QMovie *syncIconMovie;
    /** Keep track of previous number of blocks, to detect progress */
    int prevBlocks;

    uint64_t nWeight;

    /** Create the main UI actions. */
    void createActions();
    /** Create the menu bar and sub-menus. */
    void createMenuBar();
    /** Create the toolbars */
    void createToolBars();
    /** Create system tray (notification) icon */
    void createTrayIcon();
    /** Create system tray menu (or setup the dock menu) */
    void createTrayIconMenu();

public slots:
    /** Switch to overview (home) page */
    void gotoOverviewPage();
    /** Switch to history (transactions) page */
    void gotoHistoryPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to address book page */
    void gotoAddressBookPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage(bool fExportOnly=false, bool fExportConnect=true, bool fExportFirstTime=false);
    /** Switch to send coins page */
    void gotoSendCoinsPage();
    /** Switch to shopping page */
    void gotoShoppingPage();
    /** Switch to message page */
    void gotoMessagePage();
    /** Switch to block browser page */
    void gotoBlockBrowser(QString transactionId = "");

    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");

    /** Set number of connections shown in the UI */
    void setNumConnections(int count);
    /** Set number of blocks shown in the UI */
    void setNumBlocks(int count, int nTotalBlocks);
    /** Set the encryption status as shown in the UI.
       @param[in] status            current encryption status
       @see WalletModel::EncryptionStatus
    */
    void setEncryptionStatus(int status);

    /** Show information about stakes */
    void stakingIconClicked();

    /** Show incoming transaction notification for new transactions.

        The new items are those between start and end inclusive, under the given parent item.
    */
    void incomingTransaction(const QString& date, int unit, qint64 amount, const QString& type, const QString& address);
   
    /** Notify the user of an event from the core network or transaction handling code.
       @param[in] title     the message box / notification title
       @param[in] message   the displayed text
       @param[in] style     modality and style definitions (icon and used buttons - buttons only for message boxes)
                            @see CClientUIInterface::MessageBoxFlags
       @param[in] detail    optional detail text
    */

    void message(const QString &title, const QString &message, unsigned int style, const QString &detail=QString());
    /** Asks the user whether to pay the transaction fee or to cancel the transaction.
       It is currently not possible to pass a return value to another thread through
       BlockingQueuedConnection, so an indirected pointer is used.
       https://bugreports.qt-project.org/browse/QTBUG-10440

      @param[in] nFeeRequired       the required fee
      @param[out] payFee            true to pay the fee, false to not pay the fee
    */
    void askFee(qint64 nFeeRequired, bool *payFee);
    void handleURI(QString strURI);

    void mainToolbarOrientation(Qt::Orientation orientation);
    void secondaryToolbarOrientation(Qt::Orientation orientation);
    
    void loadWallet();
    void unloadWallet();
    void newWallet();

private slots:
    /** Show configuration dialog */
    void optionsClicked();
    /** Show about dialog */
    void aboutClicked();
    /** Show information about network */
    void blocksIconClicked();
    /** Show Stake For Charity Dialog */
    void charityClicked(QString addr = "");
    /** Allow user to lock/unlock wallet from click */
    void lockIconClicked();
    /** Ask for passphrase to unlock wallet during entire session */
        void unlockWalletForMint();

#ifndef Q_OS_MAC
    /** Handle tray icon clicked */
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
#endif
    /** Encrypt the wallet */
    void encryptWallet(bool status);
    /** Check the wallet */
    void checkWallet();
    /** Repair the wallet */
    void repairWallet();
    /** Backup the wallet */
    void backupWallet();
    void backupAllWallets();
    /** Import/Export the wallet's keys */
    void dumpWallet();
    void importWallet();
    /** Change encrypted wallet passphrase */
    void changePassphrase();
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();

    void lockWallet();

    /** Give user information about staking */
    void updateStakingIcon();

    /** Show window if hidden, unminimize when minimized, rise when obscured or show if hidden and fToggleHidden is true */
    void showNormalIfMinimized(bool fToggleHidden = false);
    /** simply calls showNormalIfMinimized(true) for use in SLOT() macro */
    void toggleHidden();

    /** Adds or removes wallets to the stack */
    void addWallet(const QString& name);
    void removeWallet(const QString& name);
};

#endif // BITCOINGUI_H
