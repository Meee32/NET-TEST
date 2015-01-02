#ifndef CLIENTMODEL_H
#define CLIENTMODEL_H

#include <QObject>

class OptionsModel;
class AddressTableModel;
class TransactionTableModel;
class CWallet;

QT_BEGIN_NAMESPACE
class QDateTime;
class QTimer;
QT_END_NAMESPACE

enum BlockSource {
    BLOCK_SOURCE_NONE,
    BLOCK_SOURCE_NETWORK,
    BLOCK_SOURCE_DISK,
    BLOCK_SOURCE_REINDEX
};

/** Model for Bitcoin network client. */
class ClientModel : public QObject
{
    Q_OBJECT
public:
    explicit ClientModel(OptionsModel *optionsModel, QObject *parent = 0);
    ~ClientModel();

    OptionsModel *getOptionsModel();

    int getNumConnections() const;
    int getNumBlocks() const;
    int getLastPoSBlock();
    int getNumBlocksAtStartup();
    int getProtocolVersion() const;
    qint64 getMoneySupply();
    double getDifficulty(bool fProofofStake=false);
    double getPoWMHashPS();
    double getProofOfStakeReward();
    double getPosKernalPS();
    int getStakeTargetSpacing();

    quint64 getTotalBytesRecv() const;
    quint64 getTotalBytesSent() const;

    QDateTime getLastBlockDate(bool fProofofStake=false) const;

    //! Return true if client connected to testnet
    bool isTestNet() const;
    //! Return true if core is doing initial block download
    bool inInitialBlockDownload() const;
    //! Return true if core is importing blocks
    enum BlockSource getBlockSource() const;
    //! Return conservative estimate of total number of blocks, or 0 if unknown
    int getNumBlocksOfPeers() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatBuildDate() const;
    QString clientName() const;
    QString formatClientStartupTime() const;

private:
    OptionsModel *optionsModel;

    int cachedNumBlocks;
    int cachedNumBlocksOfPeers;

    int numBlocksAtStartup;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
signals:
    void numConnectionsChanged(int count);
    void numBlocksChanged(int count, int countOfPeers);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);
    void walletAdded(const QString &name);
    void walletRemoved(const QString &name);

    //! Asynchronous error notification
    void error(const QString &title, const QString &message, bool modal);

public slots:
    void updateTimer();
    void updateNumConnections(int numConnections);
    void updateAlert(const QString &hash, int status);
    void updateWalletAdded(const QString &name);
    void updateWalletRemoved(const QString &name);
};

#endif // CLIENTMODEL_H