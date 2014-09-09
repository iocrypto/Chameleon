#ifndef SENDCOINSENTRY_H
#define SENDCOINSENTRY_H

#include <QFrame>
#include <QWebFrame>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class SendCoinsEntry;

#include "ionslookupaddressprocessor.h"

namespace Ui {
    class SendCoinsEntry;
}
class WalletModel;
class SendCoinsRecipient;

/** A single entry in the dialog for sending bitcoins. */
class SendCoinsEntry : public QFrame
{
    Q_OBJECT

public:
    explicit SendCoinsEntry(QWidget *parent = 0);
    ~SendCoinsEntry();

    void setModel(WalletModel *model);
    bool validate();
    SendCoinsRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCoinsRecipient &value);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

    void setPaymentAddress(QString ionsName, QString address);

public slots:
    void setRemoveEnabled(bool enabled);
    void clear();

signals:
    void removeEntry(SendCoinsEntry *entry);
    void payAmountChanged();

private slots:
    void on_deleteButton_clicked();
    void on_payTo_textChanged(const QString &address);
    void on_ionsUsername_textChanged(const QString &username);
    void on_addressBookButton_clicked();
    void on_ionsLookupButton_clicked();
    void on_pasteButton_clicked();
    void updateDisplayUnit();
    void ionsProcessorSetup();
    void updateIONSAddress(QNetworkReply* reply);

private:
    Ui::SendCoinsEntry *ui;
    WalletModel *model;
    QWebFrame * ionsFrame;
    IONSLookupAddressProcessor * ionsProcessor;
    QNetworkAccessManager networkManager;
    QNetworkReply * currentReply;
};

#endif // SENDCOINSENTRY_H
