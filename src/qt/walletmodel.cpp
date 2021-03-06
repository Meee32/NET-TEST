#include "walletmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"
#include "init.h"
#include "ui_interface.h"
#include "wallet.h"
#include "walletdb.h" // for BackupWallet
#include "base58.h"

#include <QSet>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>

WalletModel::WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet), optionsModel(optionsModel), addressTableModel(0),
    transactionTableModel(0),
    cachedBalance(0), cachedStake(0), cachedUnconfirmedBalance(0), cachedImmatureBalance(0),
    cachedNumTransactions(0),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{

}
qint64 WalletModel::getBalance(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        qint64 nBalance = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, coinControl);
        BOOST_FOREACH(const COutput& out, vCoins)
            nBalance += out.tx->vout[out.i].nValue;

        return nBalance;
    }
    return wallet->GetBalance();
}

qint64 WalletModel::getTotBalance() const
{
    qint64 nTotBalance = 0;
    BOOST_FOREACH(const wallet_map::value_type& item, pWalletManager->GetWalletMap())
    {
       CWallet* pwallet = pWalletManager->GetWallet(item.first.c_str()).get();
       nTotBalance+=pwallet->GetBalance();
    }
    return nTotBalance;
}

qint64 WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

qint64 WalletModel::getStake() const
{
    return wallet->GetStake();
}

qint64 WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

int WalletModel::getNumTransactions() const
{
    int numTransactions = 0;
    {
        LOCK(wallet->cs_wallet);
        numTransactions = wallet->mapWallet.size();
    }
    return numTransactions;
}

int WalletModel::getWalletVersion() const
{
    return wallet->GetVersion();
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        emit encryptionStatusChanged(newEncryptionStatus);
}

void WalletModel::pollBalanceChanged()
{
    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(nBestHeight != cachedNumBlocks)
    {
         // Balance and number of transactions might have changed
         cachedNumBlocks = nBestHeight;
         checkBalanceChanged();
         if(transactionTableModel)
             transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    qint64 newBalance = getBalance();
    qint64 newTotBalance = getTotBalance();
    qint64 newStake = getStake();
    qint64 newUnconfirmedBalance = getUnconfirmedBalance();
    qint64 newImmatureBalance = getImmatureBalance();

    if(cachedBalance != newBalance || cachedStake != newStake || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedImmatureBalance != newImmatureBalance)
    {
        cachedBalance = newBalance;
        cachedStake = newStake;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        emit balanceChanged(newBalance, newStake, newUnconfirmedBalance, newImmatureBalance);
        emit totBalanceChanged(newTotBalance);
    }
}

void WalletModel::updateTransaction(const QString &hash, int status)
{
    if(transactionTableModel)
        transactionTableModel->updateTransaction(hash, status);

    // Balance and number of transactions might have changed
    checkBalanceChanged();

    int newNumTransactions = getNumTransactions();
    if(cachedNumTransactions != newNumTransactions)
    {
        cachedNumTransactions = newNumTransactions;
        emit numTransactionsChanged(newNumTransactions);
    }
}

void WalletModel::updateAddressBook(const QString &address, const QString &label, bool isMine, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, status);
}

bool WalletModel::validateAddress(const QString &address)
{
    std::string sAddr = address.toStdString();
    
    if (sAddr.length() > 75)
    {
        if (IsStealthAddress(sAddr))
            return true;
    };
    
    CBitcoinAddress addressParsed(sAddr);
    return addressParsed.IsValid();
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(const QList<SendCoinsRecipient> &recipients, const CCoinControl *coinControl)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    qint64 nBalance = getBalance(coinControl);
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(vCoins, true, coinControl);

    BOOST_FOREACH(const COutput& out, vCoins)
        nBalance += out.tx->vout[out.i].nValue;

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    if((total + nTransactionFee) > nBalance)
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance, nTransactionFee);
    }
    
    std::map<int, std::string> mapStealthNarr;

    {
        LOCK2(cs_main, wallet->cs_wallet);
        
        // Sendmany
        std::vector<std::pair<CScript, int64_t> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            std::string sAddr = rcp.address.toStdString();
            
            if (rcp.typeInd == AddressTableModel::AT_Stealth)
            {
                CStealthAddress sxAddr;
                if (sxAddr.SetEncoded(sAddr))
                {
                    ec_secret ephem_secret;
                    ec_secret secretShared;
                    ec_point pkSendTo;
                    ec_point ephem_pubkey;
                    
                    
                    if (GenerateRandomSecret(ephem_secret) != 0)
                    {
                        printf("GenerateRandomSecret failed.\n");
                        return Aborted;
                    };
                    
                    if (StealthSecret(ephem_secret, sxAddr.scan_pubkey, sxAddr.spend_pubkey, secretShared, pkSendTo) != 0)
                    {
                        printf("Could not generate receiving public key.\n");
                        return Aborted;
                    };
                    
                    CPubKey cpkTo(pkSendTo);
                    if (!cpkTo.IsValid())
                    {
                        printf("Invalid public key generated.\n");
                        return Aborted;
                    };
                    
                    CKeyID ckidTo = cpkTo.GetID();
                    
                    CBitcoinAddress addrTo(ckidTo);
                    
                    if (SecretToPublicKey(ephem_secret, ephem_pubkey) != 0)
                    {
                        printf("Could not generate ephem public key.\n");
                        return Aborted;
                    };
                    
                    if (fDebug)
                    {
                        printf("Stealth send to generated pubkey %"PRIszu": %s\n", pkSendTo.size(), HexStr(pkSendTo).c_str());
                        printf("hash %s\n", addrTo.ToString().c_str());
                        printf("ephem_pubkey %"PRIszu": %s\n", ephem_pubkey.size(), HexStr(ephem_pubkey).c_str());
                    };
                    
                    CScript scriptPubKey;
                    scriptPubKey.SetDestination(addrTo.Get());
                    
                    vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
                    
                    CScript scriptP = CScript() << OP_RETURN << ephem_pubkey;
                    
                    if (rcp.narration.length() > 0)
                    {
                        std::string sNarr = rcp.narration.toStdString();
                        
                        if (sNarr.length() > 24)
                        {
                            printf("Narration is too long.\n");
                            return NarrationTooLong;
                        };
                        
                        std::vector<unsigned char> vchNarr;
                        
                        SecMsgCrypter crypter;
                        crypter.SetKey(&secretShared.e[0], &ephem_pubkey[0]);
                        
                        if (!crypter.Encrypt((uint8_t*)&sNarr[0], sNarr.length(), vchNarr))
                        {
                            printf("Narration encryption failed.\n");
                            return Aborted;
                        };
                        
                        if (vchNarr.size() > 48)
                        {
                            printf("Encrypted narration is too long.\n");
                            return Aborted;
                        };
                        
                        if (vchNarr.size() > 0)
                            scriptP = scriptP << OP_RETURN << vchNarr;
                        
                        int pos = vecSend.size()-1;
                        mapStealthNarr[pos] = sNarr;
                    };
                    
                    vecSend.push_back(make_pair(scriptP, 0));
                    
                    continue;
                }; // else drop through to normal
            }
            
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(sAddr).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
            
            
            
            
            if (rcp.narration.length() > 0)
            {
                std::string sNarr = rcp.narration.toStdString();
                
                if (sNarr.length() > 24)
                {
                    printf("Narration is too long.\n");
                    return NarrationTooLong;
                };
                
                std::vector<uint8_t> vNarr(sNarr.c_str(), sNarr.c_str() + sNarr.length());
                std::vector<uint8_t> vNDesc;
                
                vNDesc.resize(2);
                vNDesc[0] = 'n';
                vNDesc[1] = 'p';
                
                CScript scriptN = CScript() << OP_RETURN << vNDesc << OP_RETURN << vNarr;
                
                vecSend.push_back(make_pair(scriptN, 0));
            }
        }

        CWalletTx wtx;
        CReserveKey keyChange(wallet);
        int64_t nFeeRequired = 0;
        int nChangePos = -1;
        bool fCreated = wallet->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, false, nChangePos, coinControl);
        
        std::map<int, std::string>::iterator it;
        for (it = mapStealthNarr.begin(); it != mapStealthNarr.end(); ++it)
        {
            int pos = it->first;
            if (nChangePos > -1 && it->first >= nChangePos)
                pos++;
            
            char key[64];
            if (snprintf(key, sizeof(key), "n_%u", pos) < 1)
            {
                printf("CreateStealthTransaction(): Error creating narration key.");
                continue;
            };
            wtx.mapValue[key] = it->second;
        };
        
        
        if(!fCreated)
        {
            if((total + nFeeRequired) > nBalance) // FIXME: could cause collisions in the future
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired);
            }
            return TransactionCreationFailed;
        }
        if(!uiInterface.ThreadSafeAskFee(nFeeRequired, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        if(!wallet->CommitTransaction(wtx, keyChange))
        {
            return TransactionCommitFailed;
        }
        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString();
        {
            LOCK(wallet->cs_wallet);
            
            if (rcp.typeInd == AddressTableModel::AT_Stealth)
            {
                wallet->UpdateStealthAddress(strAddress, strLabel, true);
            } else
            {
                std::map<CTxDestination, std::string>::iterator mi = wallet->mapAddressBook.find(dest);
                
                // Check if we have a new address or an updated label
                if (mi == wallet->mapAddressBook.end() || mi->second != strLabel)
                {
                    wallet->SetAddressBookName(dest, strLabel);
                };
            };
        }
    }

    return SendCoinsReturn(OK, 0, hex);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase, bool formint)
{
    bool rc;
    if(locked)
    {
        if(formint)
        {
           // Lock as Requested by user
           rc = wallet->Lock();
           wallet->fWalletUnlockMintOnly=false;
           fStopStaking=true;
           MilliSleep(1000);
           pWalletManager->RestartStakeMiner();
           return rc;
        }
        else
          return  wallet->Lock(); // Lock
    }
    else
    {
        // Unlock
        rc = wallet->Unlock(passPhrase);
        if (rc && formint)
        {
            if (!NewThread(ThreadStakeMiner, wallet))
                qDebug() << "setWalletLocked Error: NewThread(ThreadStakeMiner) failed\n";
            else
                wallet->fWalletUnlockMintOnly=true;
        }
        return rc;
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data(), false);
}

bool WalletModel::backupAllWallets(const QString &filename)
{

    bool mretval=true;
    BOOST_FOREACH(const wallet_map::value_type& item, pWalletManager->GetWalletMap())
    {
       bool retval;
       CWallet* pwallet = pWalletManager->GetWallet(item.first.c_str()).get();
       retval = BackupWallet(*pwallet, filename.toLocal8Bit().data(), true);
       if(retval != true)
         mretval=false;

    }
    return mretval;
}

void WalletModel::setStakeForCharity(bool fStakeForCharity, int& nStakeForCharityPercent,
                                     CBitcoinAddress& strStakeForCharityAddress,
                                     CBitcoinAddress& strStakeForCharityChangeAddress,
                                     qint64& nStakeForCharityMinAmout,
                                     qint64& nStakeForCharityMaxAmount)
{
    // This function assumes the values were checked before being called
    if (wallet->fFileBacked) // Tranz add option to not save.
    {
        CWalletDB walletdb(wallet->strWalletFile);
        if (fStakeForCharity) {
            walletdb.EraseStakeForCharity(wallet->strStakeForCharityAddress.ToString());
            walletdb.WriteStakeForCharity(strStakeForCharityAddress.ToString(),
                                          nStakeForCharityPercent,
                                          strStakeForCharityChangeAddress.ToString(),
                                          nStakeForCharityMinAmout,
                                          nStakeForCharityMaxAmount);
        }
        else {
            walletdb.EraseStakeForCharity(wallet->strStakeForCharityAddress.ToString());
            walletdb.EraseStakeForCharity(strStakeForCharityAddress.ToString());
        }

        if(fDebug)
            qDebug() << "setStakeForCharity:" + QString(strStakeForCharityAddress.ToString().c_str()) + QString(nStakeForCharityPercent);
    }

    {
        LOCK(wallet->cs_wallet);
        wallet->fStakeForCharity = fStakeForCharity;
        wallet->nStakeForCharityPercent = nStakeForCharityPercent;
        wallet->strStakeForCharityAddress = strStakeForCharityAddress;
        wallet->strStakeForCharityChangeAddress = strStakeForCharityChangeAddress;
        wallet->nStakeForCharityMin = nStakeForCharityMinAmout;
        wallet->nStakeForCharityMax = nStakeForCharityMaxAmount;
    }
}

void  WalletModel::getStakeForCharity(int& nStakeForCharityPercent,
                                      CBitcoinAddress& strStakeForCharityAddress,
                                      CBitcoinAddress& strStakeForCharityChangeAddress,
                                      qint64& nStakeForCharityMinAmout,
                                      qint64& nStakeForCharityMaxAmount)
{
     nStakeForCharityPercent = wallet->nStakeForCharityPercent;
     strStakeForCharityAddress = wallet->strStakeForCharityAddress;
     strStakeForCharityChangeAddress = wallet->strStakeForCharityChangeAddress;
     nStakeForCharityMinAmout = wallet->nStakeForCharityMin;
     nStakeForCharityMaxAmount =  wallet->nStakeForCharityMax;

}

bool WalletModel::dumpWallet(const QString &filename)
{
    return DumpWallet(wallet, filename.toLocal8Bit().data());
}

bool WalletModel::importWallet(const QString &filename)
{
    return ImportWallet(wallet, filename.toLocal8Bit().data());
}

void WalletModel::getStakeWeight(uint64_t& nMinWeight, uint64_t& nMaxWeight, uint64_t& nWeight)
{
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if (!lockWallet)
      return;

    wallet->GetStakeWeight(*wallet, nMinWeight, nMaxWeight, nWeight);
}

quint64 WalletModel::getReserveBalance()
{
    return wallet->nReserveBalance;
}

quint64 WalletModel::getTotStakeWeight()
{

    quint64 nTotWeight = 0;
    BOOST_FOREACH(const wallet_map::value_type& item, pWalletManager->GetWalletMap())
    {
        CWallet* pwallet = pWalletManager->GetWallet(item.first.c_str()).get();
        uint64_t nMinWeight = 0 ,nMaxWeight =  0, nWeight = 0;
        pwallet->GetStakeWeight(*pwallet, nMinWeight,nMaxWeight,nWeight);

        nTotWeight+=nWeight;
    }
    return nTotWeight;
}

void WalletModel::getStakeWeightFromValue(const int64_t& nTime, const int64_t& nValue, uint64_t& nWeight)
{
    wallet->GetStakeWeightFromValue(nTime, nValue, nWeight);
}

void WalletModel::checkWallet(int& nMismatchSpent, int64_t& nBalanceInQuestion, int& nOrphansFound)
{
    wallet->FixSpentCoins(nMismatchSpent, nBalanceInQuestion, nOrphansFound, true);
}


bool WalletModel::isMine(const CBitcoinAddress &address)
{
    return IsMine(*wallet, address.Get());
}

void WalletModel::repairWallet(int& nMismatchSpent, int64_t& nBalanceInQuestion, int& nOrphansFound)
{
    wallet->FixSpentCoins(nMismatchSpent, nBalanceInQuestion, nOrphansFound);
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet, const CTxDestination &address, const std::string &label, bool isMine, ChangeType status)
{
    if (address.type() == typeid(CStealthAddress))
    {
        CStealthAddress sxAddr = boost::get<CStealthAddress>(address);
        std::string enc = sxAddr.Encoded();
        LogPrintf("NotifyAddressBookChanged %s %s isMine=%i status=%i\n", enc.c_str(), label.c_str(), isMine, status);
        QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(enc)),
                                  Q_ARG(QString, QString::fromStdString(label)),
                                  Q_ARG(bool, isMine),
                                  Q_ARG(int, status));
    } else
    {
        LogPrintf("NotifyAddressBookChanged %s %s isMine=%i status=%i\n", CBitcoinAddress(address).ToString().c_str(), label.c_str(), isMine, status);
        QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(CBitcoinAddress(address).ToString())),
                                  Q_ARG(QString, QString::fromStdString(label)),
                                  Q_ARG(bool, isMine),
                                  Q_ARG(int, status));
    }
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    LogPrintf("NotifyTransactionChanged %s status=%i\n", hash.GetHex().c_str(), status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;
    
    if ((!was_locked) && wallet->fWalletUnlockMintOnly)
    {
       setWalletLocked(true);
       was_locked = getEncryptionStatus() == Locked;

    }
    if(was_locked)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    // We need to shut off PoS for encrypted/locked wallets. If the pass is not accecpted
    // the wallet is locked.
    if( (!valid) && wallet->fWalletUnlockMintOnly)
    {
       fStopStaking=true;
       MilliSleep(1000);
       pWalletManager->RestartStakeMiner();
    }

    return UnlockContext(this, valid, was_locked && !wallet->fWalletUnlockMintOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);   
}

// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    BOOST_FOREACH(const COutPoint& outpoint, vOutpoints)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth);
        vOutputs.push_back(out);
    }
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address) 
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(vCoins);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet
    std::vector<COutPoint> vLockedCoins;

    // add locked coins
    BOOST_FOREACH(const COutPoint& outpoint, vLockedCoins)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth);
        vCoins.push_back(out);
    }

    BOOST_FOREACH(const COutput& out, vCoins)
    {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0]))
        {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0);
        }

        CTxDestination address;
        if(!ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address)) continue;
        mapCoins[CBitcoinAddress(address).ToString().c_str()].push_back(out);
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    return false;
}

void WalletModel::lockCoin(COutPoint& output)
{
    return;
}

void WalletModel::unlockCoin(COutPoint& output)
{
    return;
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    return;
}
