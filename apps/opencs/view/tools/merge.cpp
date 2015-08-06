
#include "merge.hpp"

#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

#include "../../model/doc/document.hpp"

#include "../doc/filewidget.hpp"
#include "../doc/adjusterwidget.hpp"

CSVTools::Merge::Merge (QWidget *parent)
: QDialog (parent), mDocument (0)
{
    setWindowTitle ("Merge Content Files into a new Game File");

    QVBoxLayout *mainLayout = new QVBoxLayout;
    setLayout (mainLayout);

    QSplitter *splitter = new QSplitter (Qt::Horizontal, this);

    mainLayout->addWidget (splitter, 1);

    // left panel (files to be merged)
    QWidget *left = new QWidget (this);
    left->setContentsMargins (0, 0, 0, 0);
    splitter->addWidget (left);

    QVBoxLayout *leftLayout = new QVBoxLayout;
    left->setLayout (leftLayout);

    leftLayout->addWidget (new QLabel ("Files to be merged", this));

    mFiles = new QListWidget (this);
    leftLayout->addWidget (mFiles, 1);

    // right panel (new game file)
    QWidget *right = new QWidget (this);
    right->setContentsMargins (0, 0, 0, 0);
    splitter->addWidget (right);

    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->setAlignment (Qt::AlignTop);
    right->setLayout (rightLayout);

    rightLayout->addWidget (new QLabel ("New game file", this));

    mNewFile = new CSVDoc::FileWidget (this);
    mNewFile->setType (false);
    mNewFile->extensionLabelIsVisible (true);
    rightLayout->addWidget (mNewFile);

    mAdjuster = new CSVDoc::AdjusterWidget (this);

    rightLayout->addWidget (mAdjuster);

    connect (mNewFile, SIGNAL (nameChanged (const QString&, bool)),
        mAdjuster, SLOT (setName (const QString&, bool)));
    connect (mAdjuster, SIGNAL (stateChanged (bool)), this, SLOT (stateChanged (bool)));

    // buttons
    QDialogButtonBox *buttons = new QDialogButtonBox (QDialogButtonBox::Cancel, Qt::Horizontal, this);

    connect (buttons->button (QDialogButtonBox::Cancel), SIGNAL (clicked()), this, SLOT (reject()));
    mOkay = new QPushButton ("Merge", this);
    mOkay->setDefault (true);
    buttons->addButton (mOkay, QDialogButtonBox::AcceptRole);

    mainLayout->addWidget (buttons);
}

void CSVTools::Merge::configure (CSMDoc::Document *document)
{
    mDocument = document;

    mNewFile->setName ("");

    // content files
    while (mFiles->count())
        delete mFiles->takeItem (0);

    std::vector<boost::filesystem::path> files = document->getContentFiles();

    for (std::vector<boost::filesystem::path>::const_iterator iter (files.begin());
        iter!=files.end(); ++iter)
        mFiles->addItem (QString::fromUtf8 (iter->filename().string().c_str()));
}

void CSVTools::Merge::setLocalData (const boost::filesystem::path& localData)
{
    mAdjuster->setLocalData (localData);
}

CSMDoc::Document *CSVTools::Merge::getDocument() const
{
    return mDocument;
}

void CSVTools::Merge::cancel()
{
    mDocument = 0;
    hide();
}

void CSVTools::Merge::accept()
{
    QDialog::accept();
}

void CSVTools::Merge::reject()
{
    QDialog::reject();
    cancel();
}

void CSVTools::Merge::stateChanged (bool valid)
{
    mOkay->setEnabled (valid);
}
