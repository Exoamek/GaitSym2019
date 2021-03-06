#include "DialogBodyBuilder.h"
#include "ui_DialogBodyBuilder.h"

#include "SimulationWidget.h"
#include "Preferences.h"
#include "FacetedObject.h"
#include "Body.h"
#include "LineEditUniqueName.h"
#include "DialogProperties.h"

#include "ode/ode.h"
#include "pystring.h"

#include <QBoxLayout>
#include <QFileInfo>
#include <QProgressDialog>
#include <QThread>
#include <QValidator>


DialogBodyBuilder::DialogBodyBuilder(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogBodyBuilder)
{
    ui->setupUi(this);

    setWindowTitle(tr("Body Builder"));
#ifdef Q_OS_MACOS
    setWindowFlags(windowFlags() & (~Qt::Dialog) | Qt::Window); // allows the window to be resized on macs
#endif
    restoreGeometry(Preferences::valueQByteArray("DialogBodyBuilderGeometry"));
    ui->checkBoxMoveMarkers->setChecked(Preferences::valueBool("DialogBodyBuilderMoveMarkers", false));

    connect(ui->pushButtonOK, SIGNAL(clicked()), this, SLOT(accept()));
    connect(ui->pushButtonCancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(ui->pushButtonProperties, SIGNAL(clicked()), this, SLOT(properties()));
    connect(ui->pushButtonCalculate, SIGNAL(clicked()), this, SLOT(calculate()));
    connect(ui->lineEditMesh1, SIGNAL(focussed(bool)), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditMesh2, SIGNAL(focussed(bool)), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditMesh3, SIGNAL(focussed(bool)), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditMesh1, SIGNAL(editingFinished()), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditMesh2, SIGNAL(editingFinished()), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditMesh3, SIGNAL(editingFinished()), this, SLOT(lineEditMeshClicked()));
    connect(ui->lineEditID, SIGNAL(textChanged(const QString &)), this, SLOT(lineEditIDTextChanged(const QString &)));

    ui->pushButtonCalculate->setEnabled(false);
    ui->pushButtonOK->setEnabled(false);

    ui->lineEditDensity->setValue(1.0);
    ui->lineEditMass->setValue(1.0);
    ui->lineEditI11->setValue(1.0);
    ui->lineEditI22->setValue(1.0);
    ui->lineEditI33->setValue(1.0);
    ui->lineEditDensity->setBottom(DBL_MIN);
    ui->lineEditMass->setBottom(DBL_MIN);
    ui->lineEditI11->setBottom(DBL_MIN);
    ui->lineEditI22->setBottom(DBL_MIN);
    ui->lineEditI33->setBottom(DBL_MIN);

    qobject_cast<LineEditPath *>(ui->lineEditMesh1)->setPathType(LineEditPath::FileForOpen);
    qobject_cast<LineEditPath *>(ui->lineEditMesh2)->setPathType(LineEditPath::FileForOpen);
    qobject_cast<LineEditPath *>(ui->lineEditMesh3)->setPathType(LineEditPath::FileForOpen);

    setInputBody(nullptr);
}

DialogBodyBuilder::~DialogBodyBuilder()
{
    delete ui;
}

void DialogBodyBuilder::lateInitialise()
{
    Q_ASSERT_X(m_simulation, "DialogBodyBuilder::lateInitialise", "m_simulation not defined");

    Body referenceBody(m_simulation->GetWorldID());
    referenceBody.SetConstructionDensity(Preferences::valueDouble("BodyDensity", 1000.0));
    if (m_inputBody)
    {
        Q_ASSERT_X(m_inputBody, "DialogBodyBuilder::lateInitialise", "m_simulation not defined");
        ui->lineEditID->setText(QString::fromStdString(m_inputBody->name()));
        ui->lineEditID->setEnabled(false);
    }
    else
    {
        QString name("World"); // World always exists
        ui->lineEditID->addString(name);
        auto nameSet = m_simulation->GetNameSet();
        ui->lineEditID->addStrings(nameSet);
        int initialNameCount = 0;
        QString initialName = QString("Body%1").arg(initialNameCount, 3, 10, QLatin1Char('0'));
        while (nameSet.count(initialName.toStdString()))
        {
            initialNameCount++;
            initialName = QString("Body%1").arg(initialNameCount, 3, 10, QLatin1Char('0'));
            if (initialNameCount >= 999) break; // only do this for the first 999 bodies
        }
        ui->lineEditID->setText(initialName);
    }

    if (m_inputBody)
    {
        dMass mass;
        m_inputBody->GetMass(&mass);
        ui->lineEditMass->setValue(mass.mass);
        ui->lineEditI11->setValue(mass.I[0 * 4 + 0]);
        ui->lineEditI22->setValue(mass.I[1 * 4 + 1]);
        ui->lineEditI33->setValue(mass.I[2 * 4 + 2]);
        ui->lineEditI12->setValue(mass.I[0 * 4 + 1]);
        ui->lineEditI13->setValue(mass.I[0 * 4 + 2]);
        ui->lineEditI23->setValue(mass.I[1 * 4 + 2]);
        ui->lineEditDensity->setValue(m_inputBody->GetConstructionDensity());
        const double *constructionPosition = m_inputBody->GetConstructionPosition();
        ui->lineEditX->setValue(constructionPosition[0]);
        ui->lineEditY->setValue(constructionPosition[1]);
        ui->lineEditZ->setValue(constructionPosition[2]);
        const double *initialPosition = m_inputBody->GetInitialPosition();
        ui->lineEditRunX->setValue(initialPosition[0]);
        ui->lineEditRunY->setValue(initialPosition[1]);
        ui->lineEditRunZ->setValue(initialPosition[2]);
        const double *initialQuaternion = m_inputBody->GetInitialQuaternion();
        pgd::Vector3 eulerAngles = pgd::MakeEulerAnglesFromQ(pgd::Quaternion(initialQuaternion));
        ui->lineEditEulerX->setValue(eulerAngles.x);
        ui->lineEditEulerY->setValue(eulerAngles.y);
        ui->lineEditEulerZ->setValue(eulerAngles.z);
        const double *velocity = m_inputBody->GetLinearVelocity();
        ui->lineEditVX->setValue(velocity[0]);
        ui->lineEditVY->setValue(velocity[1]);
        ui->lineEditVZ->setValue(velocity[2]);
        const double *angularV = m_inputBody->GetAngularVelocity();
        ui->lineEditAVX->setValue(angularV[0]);
        ui->lineEditAVY->setValue(angularV[1]);
        ui->lineEditAVZ->setValue(angularV[2]);
        const double *positionHighBound = m_inputBody->GetPositionHighBound();
        ui->lineEditHighX->setValue(positionHighBound[0]);
        ui->lineEditHighY->setValue(positionHighBound[1]);
        ui->lineEditHighZ->setValue(positionHighBound[2]);
        const double *positionLowBound = m_inputBody->GetPositionLowBound();
        ui->lineEditLowX->setValue(positionLowBound[0]);
        ui->lineEditLowY->setValue(positionLowBound[1]);
        ui->lineEditLowZ->setValue(positionLowBound[2]);
        const double *velocityHighBound = m_inputBody->GetLinearVelocityHighBound();
        ui->lineEditHighVX->setValue(velocityHighBound[0]);
        ui->lineEditHighVY->setValue(velocityHighBound[1]);
        ui->lineEditHighVZ->setValue(velocityHighBound[2]);
        const double *velocityLowBound = m_inputBody->GetLinearVelocityLowBound();
        ui->lineEditLowVX->setValue(velocityLowBound[0]);
        ui->lineEditLowVY->setValue(velocityLowBound[1]);
        ui->lineEditLowVZ->setValue(velocityLowBound[2]);

        std::string completePath = DialogBodyBuilder::findCompletePath(m_inputBody->GetGraphicFile1());
        if (completePath.size()) ui->lineEditMesh1->setText(QString::fromStdString(completePath));
        else ui->lineEditMesh1->setText(QString::fromStdString(m_inputBody->GetGraphicFile1()));

        completePath = DialogBodyBuilder::findCompletePath(m_inputBody->GetGraphicFile2());
        if (completePath.size()) ui->lineEditMesh2->setText(QString::fromStdString(completePath));
        else ui->lineEditMesh2->setText(QString::fromStdString(m_inputBody->GetGraphicFile2()));

        completePath = DialogBodyBuilder::findCompletePath(m_inputBody->GetGraphicFile3());
        if (completePath.size()) ui->lineEditMesh3->setText(QString::fromStdString(completePath));
        else ui->lineEditMesh3->setText(QString::fromStdString(m_inputBody->GetGraphicFile3()));
    }

    lineEditMeshActivated(ui->lineEditMesh1);

}

void DialogBodyBuilder::accept() // this catches OK and return/enter
{
    qDebug() << "DialogBodyBuilder::accept()";

    Body *bodyPtr;
    if (m_inputBody) bodyPtr = m_inputBody;
    else
    {
        m_outputBody = std::make_unique<Body>(m_simulation->GetWorldID());
        bodyPtr = m_outputBody.get();
        bodyPtr->EnterConstructionMode();
    }
    bodyPtr->setName(ui->lineEditID->text().toStdString());
    std::string head, tail;
    pystring::os::path::split(head, tail, ui->lineEditMesh1->text().toStdString());
    bodyPtr->SetGraphicFile1(tail);
    m_simulation->GetGlobal()->MeshSearchPathAddToFront(head);
    pystring::os::path::split(head, tail, ui->lineEditMesh2->text().toStdString());
    bodyPtr->SetGraphicFile2(tail);
    m_simulation->GetGlobal()->MeshSearchPathAddToFront(head);
    pystring::os::path::split(head, tail, ui->lineEditMesh3->text().toStdString());
    bodyPtr->SetGraphicFile3(tail);
    m_simulation->GetGlobal()->MeshSearchPathAddToFront(head);
    bodyPtr->setSimulation(m_simulation);

    dMass mass;
    mass.c[0] = mass.c[1] = mass.c[2] = 0;
    mass.mass = ui->lineEditMass->value();
    mass.I[0 * 4 + 0] = ui->lineEditI11->value();
    mass.I[1 * 4 + 1] = ui->lineEditI22->value();
    mass.I[2 * 4 + 2] = ui->lineEditI33->value();
    mass.I[0 * 4 + 1] = ui->lineEditI12->value();
    mass.I[0 * 4 + 2] = ui->lineEditI13->value();
    mass.I[1 * 4 + 2] = ui->lineEditI23->value();
    bodyPtr->SetMass(&mass);
    bodyPtr->SetConstructionDensity(ui->lineEditDensity->value());
    dVector3 constructionPosition;
    constructionPosition[0] = ui->lineEditX->value();
    constructionPosition[1] = ui->lineEditY->value();
    constructionPosition[2] = ui->lineEditZ->value();
    bodyPtr->SetConstructionPosition(constructionPosition[0], constructionPosition[1], constructionPosition[2]);
    dVector3 positionHighBound;
    positionHighBound[0] = ui->lineEditHighX->value();
    positionHighBound[1] = ui->lineEditHighY->value();
    positionHighBound[2] = ui->lineEditHighZ->value();
    bodyPtr->SetPositionHighBound(positionHighBound[0], positionHighBound[1], positionHighBound[2]);
    dVector3 positionLowBound;
    positionLowBound[0] = ui->lineEditLowX->value();
    positionLowBound[1] = ui->lineEditLowY->value();
    positionLowBound[2] = ui->lineEditLowZ->value();
    bodyPtr->SetPositionLowBound(positionLowBound[0], positionLowBound[1], positionLowBound[2]);
    dVector3 velocityHighBound;
    velocityHighBound[0] = ui->lineEditHighVX->value();
    velocityHighBound[1] = ui->lineEditHighVY->value();
    velocityHighBound[2] = ui->lineEditHighVZ->value();
    bodyPtr->SetLinearVelocityHighBound(velocityHighBound[0], velocityHighBound[1], velocityHighBound[2]);
    dVector3 velocityLowBound;
    velocityLowBound[0] = ui->lineEditLowVX->value();
    velocityLowBound[1] = ui->lineEditLowVY->value();
    velocityLowBound[2] = ui->lineEditLowVZ->value();
    bodyPtr->SetLinearVelocityLowBound(velocityLowBound[0], velocityLowBound[1], velocityLowBound[2]);

    // and because we are in construction mode we set the position to the construction position/quaternion
    // and the initial position to the desired position/quaternion
    bodyPtr->SetPosition(constructionPosition[0], constructionPosition[1], constructionPosition[2]);
    dVector3 initialPosition;
    initialPosition[0] = ui->lineEditRunX->value();
    initialPosition[1] = ui->lineEditRunY->value();
    initialPosition[2] = ui->lineEditRunZ->value();
    bodyPtr->SetInitialPosition(initialPosition[0], initialPosition[1], initialPosition[2]);
    double ex = ui->lineEditEulerX->value();
    double ey = ui->lineEditEulerY->value();
    double ez = ui->lineEditEulerZ->value();
    pgd::Quaternion q = pgd::MakeQFromEulerAngles(ex, ey, ez);
    bodyPtr->SetInitialQuaternion(q.n, q.x, q.y, q.z);
    // but the velocities can just be set
    dVector3 velocity;
    velocity[0] = ui->lineEditVX->value();
    velocity[1] = ui->lineEditVY->value();
    velocity[2] = ui->lineEditVZ->value();
    bodyPtr->SetLinearVelocity(velocity[0], velocity[1], velocity[2]);
    dVector3 angularV;
    angularV[0] = ui->lineEditAVX->value();
    angularV[1] = ui->lineEditAVY->value();
    angularV[2] = ui->lineEditAVZ->value();
    bodyPtr->SetAngularVelocity(angularV[0], angularV[1], angularV[2]);

    if (m_inputBody)
    {
        bodyPtr->setSize1(m_inputBody->size1());
        bodyPtr->setSize2(m_inputBody->size2());
        bodyPtr->setColour1(m_inputBody->colour1());
        bodyPtr->setColour2(m_inputBody->colour2());
        bodyPtr->setColour3(m_inputBody->colour3());
    }
    else
    {
        bodyPtr->setSize1(Preferences::valueDouble("BodyAxesSize"));
        bodyPtr->setSize2(Preferences::valueDouble("BodyBlendFraction"));
        bodyPtr->setColour1(Preferences::valueQColor("BodyColour1").name(QColor::HexArgb).toStdString());
        bodyPtr->setColour2(Preferences::valueQColor("BodyColour2").name(QColor::HexArgb).toStdString());
        bodyPtr->setColour3(Preferences::valueQColor("BodyColour3").name(QColor::HexArgb).toStdString());
    }

    if (m_properties.size() > 0)
    {
        if (m_properties.count("BodyAxesSize"))
            bodyPtr->setSize1(m_properties["BodyAxesSize"].value.toDouble());
        if (m_properties.count("BodyBlendFraction"))
            bodyPtr->setSize2(m_properties["BodyBlendFraction"].value.toDouble());
        if (m_properties.count("BodyColour1"))
            bodyPtr->setColour1(qvariant_cast<QColor>(m_properties["BodyColour1"].value).name(QColor::HexArgb).toStdString());
        if (m_properties.count("BodyColour2"))
            bodyPtr->setColour2(qvariant_cast<QColor>(m_properties["BodyColour2"].value).name(QColor::HexArgb).toStdString());
        if (m_properties.count("BodyColour3"))
            bodyPtr->setColour3(qvariant_cast<QColor>(m_properties["BodyColour3"].value).name(QColor::HexArgb).toStdString());
    }

    // this is needed because there are some parts of Body that do not have a public interface
    bodyPtr->saveToAttributes();
    bodyPtr->createFromAttributes();

    Preferences::insert("DialogBodyBuilderMoveMarkers", ui->checkBoxMoveMarkers->isChecked());
    Preferences::insert("DialogBodyBuilderGeometry", saveGeometry());

    QDialog::accept();
}

void DialogBodyBuilder::reject() // this catches cancel, close and escape key
{
    qDebug() << "DialogBodyBuilder::reject()";
    Preferences::insert("DialogBodyBuilderMoveMarkers", ui->checkBoxMoveMarkers->isChecked());
    Preferences::insert("DialogBodyBuilderGeometry", saveGeometry());

    QDialog::reject();
}

void DialogBodyBuilder::closeEvent(QCloseEvent *event)
{
    qDebug() << "DialogBodyBuilder::closeEvent()";
    Preferences::insert("DialogBodyBuilderMoveMarkers", ui->checkBoxMoveMarkers->isChecked());
    Preferences::insert("DialogBodyBuilderGeometry", saveGeometry());

    QDialog::closeEvent(event);
}

void DialogBodyBuilder::calculate()
{
    if (!m_referenceObject) return;
    dMass mass;
    double density = ui->lineEditDensity->text().toDouble();
    bool clockwise = false;
    m_referenceObject->CalculateMassProperties(&mass, density, clockwise);
    ui->lineEditMass->setValue(mass.mass);
    ui->lineEditX->setValue(mass.c[0]);
    ui->lineEditY->setValue(mass.c[1]);
    ui->lineEditZ->setValue(mass.c[2]);
// #define _I(i,j) I[(i)*4+(j)]
// regex _I\(([0-9]+),([0-9]+)\) to I[(\1)*4+(\2)]
//    mass->_I(0,0) = I11;
//    mass->_I(1,1) = I22;
//    mass->_I(2,2) = I33;
//    mass->_I(0,1) = I12;
//    mass->_I(0,2) = I13;
//    mass->_I(1,2) = I23;
//    mass->_I(1,0) = I12;
//    mass->_I(2,0) = I13;
//    mass->_I(2,1) = I23;
    ui->lineEditI11->setValue(mass.I[0 * 4 + 0]);
    ui->lineEditI22->setValue(mass.I[1 * 4 + 1]);
    ui->lineEditI33->setValue(mass.I[2 * 4 + 2]);
    ui->lineEditI12->setValue(mass.I[0 * 4 + 1]);
    ui->lineEditI13->setValue(mass.I[0 * 4 + 2]);
    ui->lineEditI23->setValue(mass.I[1 * 4 + 2]);
}

void DialogBodyBuilder::lineEditMeshClicked()
{
    LineEditPath *lineEdit = qobject_cast<LineEditPath *>(sender());
    if (lineEdit == nullptr) return;
    lineEditMeshActivated(lineEdit);
}

void DialogBodyBuilder::lineEditMeshActivated(LineEditPath *lineEdit)
{
    while (true)
    {
        if (lineEdit == ui->lineEditMesh1)
        {
            if (lineEdit->text().toStdString() == m_mesh1.filename())
            {
                m_referenceObject = &m_mesh1;
            }
            else
            {
                int err = m_mesh1.ParseMeshFile(lineEdit->text().toStdString());
                if (err) m_referenceObject = nullptr;
                else m_referenceObject = &m_mesh1;
            }
            break;
        }
        if (lineEdit == ui->lineEditMesh2)
        {
            if (lineEdit->text().toStdString() == m_mesh2.filename())
            {
                m_referenceObject = &m_mesh2;
            }
            else
            {
                int err = m_mesh2.ParseMeshFile(lineEdit->text().toStdString());
                if (err) m_referenceObject = nullptr;
                else m_referenceObject = &m_mesh2;
            }
            break;
        }
        if (lineEdit == ui->lineEditMesh3)
        {
            if (lineEdit->text().toStdString() == m_mesh3.filename())
            {
                m_referenceObject = &m_mesh3;
            }
            else
            {
                int err = m_mesh3.ParseMeshFile(lineEdit->text().toStdString());
                if (err) m_referenceObject = nullptr;
                else m_referenceObject = &m_mesh3;
            }
            break;
        }
    }

    ui->pushButtonCalculate->setEnabled(m_referenceObject ? true:false);
    ui->lineEditMesh1->setHighlighted(false);
    ui->lineEditMesh2->setHighlighted(false);
    ui->lineEditMesh3->setHighlighted(false);
    lineEdit->setHighlighted(true);
}


void DialogBodyBuilder::lineEditIDTextChanged(const QString & /* text */)
{
    LineEditUniqueName *lineEdit = qobject_cast<LineEditUniqueName *>(sender());
    if (lineEdit == nullptr) return;
    QString textCopy = lineEdit->text();
    int pos = lineEdit->cursorPosition();
    ui->pushButtonOK->setEnabled(lineEdit->validator()->validate(textCopy, pos) == QValidator::Acceptable);
}

void DialogBodyBuilder::properties()
{
    DialogProperties dialogProperties(this);

    SettingsItem bodyAxesSize = Preferences::settingsItem("BodyAxesSize");
    SettingsItem bodyBlendFraction = Preferences::settingsItem("BodyBlendFraction");
    SettingsItem bodyColour1 = Preferences::settingsItem("BodyColour1");
    SettingsItem bodyColour2 = Preferences::settingsItem("BodyColour2");
    SettingsItem bodyColour3 = Preferences::settingsItem("BodyColour3");

    if (m_inputBody)
    {
        bodyAxesSize.value = m_inputBody->size1();
        bodyBlendFraction.value = m_inputBody->size2();
        bodyColour1.value = QColor(QString::fromStdString(m_inputBody->colour1().GetHexArgb()));
        bodyColour2.value = QColor(QString::fromStdString(m_inputBody->colour2().GetHexArgb()));
        bodyColour3.value = QColor(QString::fromStdString(m_inputBody->colour3().GetHexArgb()));
    }
    m_properties.clear();
    m_properties = { { bodyAxesSize.key, bodyAxesSize },
                     { bodyBlendFraction.key, bodyBlendFraction },
                     { bodyColour1.key, bodyColour1 },
                     { bodyColour2.key, bodyColour2 },
                     { bodyColour3.key, bodyColour3 } };
    dialogProperties.setInputSettingsItems(m_properties);
    dialogProperties.initialise();

    int status = dialogProperties.exec();
    if (status == QDialog::Accepted)
    {
        dialogProperties.update();
        m_properties = dialogProperties.getOutputSettingsItems();
    }
}

std::string DialogBodyBuilder::findCompletePath(const std::string &filename)
{
    std::string completePath;
    if (m_simulation)
    {
        auto searchPath = m_simulation->GetGlobal()->MeshSearchPath();
        for (auto &&it : *searchPath)
        {
            completePath = pystring::os::path::join(it, filename);
            if (QFileInfo(QString::fromStdString(completePath)).isFile()) return completePath;
        }
    }
    return std::string();
}

std::unique_ptr<Body> DialogBodyBuilder::outputBody()
{
    return std::move(m_outputBody);
}

void DialogBodyBuilder::setInputBody(Body *inputBody)
{
    m_inputBody = inputBody;
}

Simulation *DialogBodyBuilder::simulation() const
{
    return m_simulation;
}

void DialogBodyBuilder::setSimulation(Simulation *simulation)
{
    m_simulation = simulation;
}



