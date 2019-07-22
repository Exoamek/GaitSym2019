#include "ElementTreeWidget.h"
#include "Preferences.h"
#include "Simulation.h"
#include "Body.h"
#include "Marker.h"
#include "Joint.h"
#include "Geom.h"
#include "Muscle.h"
#include "FluidSac.h"
#include "Driver.h"
#include "MainWindow.h"

#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>

ElementTreeWidget::ElementTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
    this->setSortingEnabled(false);
    QTreeWidgetItem *rootItem = this->invisibleRootItem();
    QStringList itemStrings;
    itemStrings << "BODY" << "" << "";
    m_bodyTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_bodyTree->setData(1, Qt::CheckStateRole, QVariant());
    m_bodyTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    itemStrings.clear();
    itemStrings << "MARKER" << "" << "";
    m_markerTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_markerTree->setData(1, Qt::CheckStateRole, QVariant());
    m_markerTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    itemStrings.clear();
    itemStrings << "JOINT" << "" << "";
    m_jointTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_jointTree->setData(1, Qt::CheckStateRole, QVariant());
    m_jointTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    itemStrings.clear();
    itemStrings << "GEOM" << "" << "";
    m_geomTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_geomTree->setData(1, Qt::CheckStateRole, QVariant());
    m_geomTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    itemStrings.clear();
    itemStrings << "MUSCLE" << "" << "";
    m_muscleTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_muscleTree->setData(1, Qt::CheckStateRole, QVariant());
    m_muscleTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    itemStrings.clear();
    itemStrings << "FLUIDSAC" << "" << "";
    m_fluidSacTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_fluidSacTree->setData(1, Qt::CheckStateRole, QVariant());
    m_fluidSacTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
#ifndef EXPERIMENTAL
    m_fluidSacTree->setHidden(true);
#endif
    itemStrings.clear();
    itemStrings << "DRIVER" << "" << "";
    m_driverTree = new QTreeWidgetItem(rootItem, itemStrings, ROOT_ITEM_TYPE);
    m_driverTree->setData(1, Qt::CheckStateRole, QVariant());
    m_driverTree->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(menuRequest(QPoint)));

#ifdef ELEMENT_TREE_WIDGET_AUTOFIT_COLUMNS
    // sets the header so it allows resizing
    this->header()->setStretchLastSection(false);
    this->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    this->resizeColumnToContents(0);
    this->resizeColumnToContents(1);
#else
    this->header()->setStretchLastSection(true);
    this->header()->setSectionResizeMode(QHeaderView::Interactive);
    this->header()->restoreState(Preferences::valueQByteArray("ElementTreeHeaderState"));
#endif

    connect(this, SIGNAL(itemChanged(QTreeWidgetItem *, int)), this,
            SLOT(elementsItemChanged(QTreeWidgetItem *, int)));

}

void ElementTreeWidget::clearLists()
{
    foreach(auto i, m_bodyTree->takeChildren()) delete i;
    foreach(auto i, m_markerTree->takeChildren()) delete i;
    foreach(auto i, m_jointTree->takeChildren()) delete i;
    foreach(auto i, m_geomTree->takeChildren()) delete i;
    foreach(auto i, m_muscleTree->takeChildren()) delete i;
    foreach(auto i, m_fluidSacTree->takeChildren()) delete i;
    foreach(auto i, m_driverTree->takeChildren()) delete i;
}

void ElementTreeWidget::menuRequest(const QPoint &pos)
{
    if (!m_simulation) return;
    if (m_mainWindow->mode() != MainWindow::constructionMode) return;

    QTreeWidgetItem *item = this->itemAt(pos);
    QMenu menu(this);

    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "BODY")
    {
        QAction *action = menu.addAction(tr("Create New Body..."));
        action->setEnabled(m_simulation != nullptr);
        menu.addSeparator();
    }
    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "MARKER")
    {
        QAction *action = menu.addAction(tr("Create New Marker..."));
        action->setEnabled(m_simulation != nullptr && m_simulation->GetBodyList()->size() > 0);
        menu.addSeparator();
    }
    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "JOINT")
    {
        QAction *action = menu.addAction(tr("Create New Joint..."));
        action->setEnabled(m_simulation != nullptr && m_simulation->GetBodyList()->size() > 1
                           && m_simulation->GetMarkerList()->size() > 0);
        menu.addSeparator();
    }
    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "GEOM")
    {
        QAction *action = menu.addAction(tr("Create New Geom..."));
        action->setEnabled(m_simulation != nullptr && m_simulation->GetBodyList()->size() > 0
                           && m_simulation->GetMarkerList()->size() > 0);
        menu.addSeparator();
    }
    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "MUSCLE")
    {
        QAction *action = menu.addAction(tr("Create New Muscle..."));
        action->setEnabled(m_simulation != nullptr && m_simulation->GetBodyList()->size() > 1
                           && m_simulation->GetMarkerList()->size() > 0);
        menu.addSeparator();
    }
    if (item->type() == ROOT_ITEM_TYPE && item->data(0, Qt::DisplayRole).toString() == "DRIVER")
    {
        QAction *action = menu.addAction(tr("Create New Driver..."));
        action->setEnabled(m_simulation != nullptr && m_simulation->GetMuscleList()->size() > 0);
        menu.addSeparator();
    }

    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "BODY")
    {
        menu.addAction(tr("Edit Body..."));
        menu.addAction(tr("Delete Body..."));
        menu.addSeparator();
    }
    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "MARKER")
    {
        menu.addAction(tr("Edit Marker..."));
        menu.addAction(tr("Delete Marker..."));
        menu.addSeparator();
    }
    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "JOINT")
    {
        menu.addAction(tr("Edit Joint..."));
        menu.addAction(tr("Delete Joint..."));
        menu.addSeparator();
    }
    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "GEOM")
    {
        menu.addAction(tr("Edit Geom..."));
        menu.addAction(tr("Delete Geom..."));
        menu.addSeparator();
    }
    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "MUSCLE")
    {
        menu.addAction(tr("Edit Muscle..."));
        menu.addAction(tr("Delete Muscle..."));
        menu.addSeparator();
    }

    if (item->type() == ELEMENT_ITEM_TYPE
            && item->parent()->data(0, Qt::DisplayRole).toString() == "DRIVER")
    {
        menu.addAction(tr("Edit Driver..."));
        menu.addAction(tr("Delete Driver..."));
        menu.addSeparator();
    }

    QPoint gp = this->viewport()->mapToGlobal(pos);
    QAction *action = menu.exec(gp);
    if (action)
    {
        if (action->text() == tr("Create New Body..."))
        {
            emit createNewBody();
        }
        if (action->text() == tr("Create New Marker..."))
        {
            emit createNewMarker();
        }
        if (action->text() == tr("Create New Joint..."))
        {
            emit createNewJoint();
        }
        if (action->text() == tr("Create New Geom..."))
        {
            emit createNewGeom();
        }
        if (action->text() == tr("Create New Muscle..."))
        {
            emit createNewMuscle();
        }
        if (action->text() == tr("Create New Driver..."))
        {
            emit createNewDriver();
        }
        if (action->text() == tr("Edit Body..."))
        {
            emit editBody(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Edit Marker..."))
        {
            emit editMarker(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Edit Joint..."))
        {
            emit editJoint(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Edit Geom..."))
        {
            emit editGeom(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Edit Muscle..."))
        {
            emit editMuscle(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Edit Driver..."))
        {
            emit editDriver(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Body..."))
        {
            emit deleteBody(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Marker..."))
        {
            emit deleteMarker(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Joint..."))
        {
            emit deleteJoint(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Geom..."))
        {
            emit deleteGeom(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Muscle..."))
        {
            emit deleteMuscle(item->data(0, Qt::DisplayRole).toString());
        }
        if (action->text() == tr("Delete Driver..."))
        {
            emit deleteDriver(item->data(0, Qt::DisplayRole).toString());
        }
    }

}

MainWindow *ElementTreeWidget::mainWindow() const
{
    return m_mainWindow;
}

void ElementTreeWidget::setMainWindow(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

int ElementTreeWidget::insertBody(const QString &name)
{
    int index;
    bool found = BinarySearch(m_bodyTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_bodyTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertMarker(const QString &name)
{
    int index;
    bool found = BinarySearch(m_markerTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_markerTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertJoint(const QString &name)
{
    int index;
    bool found = BinarySearch(m_jointTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_jointTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertMuscle(const QString &name)
{
    int index;
    bool found = BinarySearch(m_muscleTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_muscleTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertGeom(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_geomTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertFluidSac(const QString &name)
{
    int index;
    bool found = BinarySearch(m_fluidSacTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Checked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_fluidSacTree->insertChild(index, item);
    return index;
}

int ElementTreeWidget::insertDriver(const QString &name)
{
    int index;
    bool found = BinarySearch(m_driverTree, name, &index);
    if (found) return index;
    QStringList itemStrings;
    itemStrings << name << "" << "";
    QTreeWidgetItem *item = new QTreeWidgetItem(itemStrings, ELEMENT_ITEM_TYPE);
    item->setData(1, Qt::CheckStateRole, Qt::Unchecked);
    item->setData(2, Qt::CheckStateRole, Qt::Unchecked);
    m_driverTree->insertChild(index, item);
    return index;
}


int ElementTreeWidget::removeBody(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_bodyTree->child(index);
    m_bodyTree->removeChild(currentChild);
    delete currentChild;
    return index;
}


int ElementTreeWidget::removeMarker(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_markerTree->child(index);
    m_markerTree->removeChild(currentChild);
    delete currentChild;
    return index;
}


int ElementTreeWidget::removeJoint(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_jointTree->child(index);
    m_jointTree->removeChild(currentChild);
    delete currentChild;
    return index;
}


int ElementTreeWidget::removeMuscle(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_muscleTree->child(index);
    m_muscleTree->removeChild(currentChild);
    delete currentChild;
    return index;
}


int ElementTreeWidget::removeGeom(const QString &name)
{
    int index;
    bool found = BinarySearch(m_geomTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_geomTree->child(index);
    m_geomTree->removeChild(currentChild);
    delete currentChild;
    return index;
}


int ElementTreeWidget::removeFluidSac(const QString &name)
{
    int index;
    bool found = BinarySearch(m_fluidSacTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_fluidSacTree->child(index);
    m_fluidSacTree->removeChild(currentChild);
    delete currentChild;
    return index;
}

int ElementTreeWidget::removeDriver(const QString &name)
{
    int index;
    bool found = BinarySearch(m_driverTree, name, &index);
    if (!found) return -1;
    QTreeWidgetItem *currentChild = m_driverTree->child(index);
    m_driverTree->removeChild(currentChild);
    delete currentChild;
    return index;
}

bool ElementTreeWidget::BinarySearch(QTreeWidgetItem *A, const QString &value, int *index)
{
    int low = 0;
    int high = A->childCount() - 1;
    int mid, compare;
    while (low <= high)
    {
        // invariants: value > A[i] for all i < low
        //             value < A[i] for all i > high
        mid = (low + high) / 2;
        compare = QString::compare(A->child(mid)->text(0), value, Qt::CaseSensitive);
        if (compare > 0)
            high = mid - 1;
        else if (compare < 0)
            low = mid + 1;
        else
        {
            *index = mid;
            return true;
        }
//        if (A->child(mid)->text(0) > value)
//            high = mid - 1;
//        else if (A->child(mid)->text(0) < value)
//            low = mid + 1;
//        else
//        {
//            *index = mid;
//            return true;
//        }
    }
    // not found value would be inserted at index "low"
    *index = low;
    return false;
}

void ElementTreeWidget::elementsItemChanged(QTreeWidgetItem *item, int column)
{
    if (!m_simulation) return;
    if (item->parent()->text(0) == QString("BODY"))
    {
        auto it = m_simulation->GetBodyList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetBodyList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("MARKER"))
    {
        auto it = m_simulation->GetMarkerList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetMarkerList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("JOINT"))
    {
        auto it = m_simulation->GetJointList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetJointList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("GEOM"))
    {
        auto it = m_simulation->GetGeomList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetGeomList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("MUSCLE"))
    {
        auto it = m_simulation->GetMuscleList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetMuscleList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("FLUIDSAC"))
    {
        auto it = m_simulation->GetFluidSacList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetFluidSacList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    else if (item->parent()->text(0) == QString("DRIVER"))
    {
        auto it = m_simulation->GetDriverList()->find(item->text(0).toStdString());
        if (it != m_simulation->GetDriverList()->end())
        {
            if (column == 1) it->second->setVisible((item->checkState(column) == Qt::Checked));
            else if (column == 2) it->second->SetDump((item->checkState(column) == Qt::Checked));
        }
    }
    emit elementTreeWidgetItemChanged(item, column);
}

void ElementTreeWidget::fillVisibitilityLists(Simulation *simulation)
{
    m_simulation = simulation;
    if (m_simulation == nullptr) return;

    std::map<std::string, Body *> *bodyList = m_simulation->GetBodyList();
    std::map<std::string, Marker *> *markerList = m_simulation->GetMarkerList();
    std::map<std::string, Joint *> *jointList = m_simulation->GetJointList();
    std::map<std::string, Geom *> *geomList = m_simulation->GetGeomList();
    std::map<std::string, Muscle *> *muscleList = m_simulation->GetMuscleList();
    std::map<std::string, FluidSac *> *fluidSacList = m_simulation->GetFluidSacList();
    std::map<std::string, Driver *> *driverList = m_simulation->GetDriverList();

    this->clearLists();
    for (std::map<std::string, Body *>::const_iterator iter = bodyList->begin(); iter != bodyList->end(); iter++)
        this->insertBody(QString::fromStdString(iter->first));
    for (std::map<std::string, Marker *>::const_iterator iter = markerList->begin(); iter != markerList->end(); iter++)
        this->insertMarker(QString::fromStdString(iter->first));
    for (std::map<std::string, Joint *>::const_iterator iter = jointList->begin(); iter != jointList->end(); iter++)
        this->insertJoint(QString::fromStdString(iter->first));
    for (std::map<std::string, Geom *>::const_iterator iter = geomList->begin(); iter != geomList->end(); iter++)
        this->insertGeom(QString::fromStdString(iter->first));
    for (std::map<std::string, Muscle *>::const_iterator iter = muscleList->begin(); iter != muscleList->end(); iter++)
        this->insertMuscle(QString::fromStdString(iter->first));
    for (std::map<std::string, FluidSac *>::const_iterator iter = fluidSacList->begin(); iter != fluidSacList->end(); iter++)
        this->insertFluidSac(QString::fromStdString(iter->first));
    for (std::map<std::string, Driver *>::const_iterator iter = driverList->begin(); iter != driverList->end(); iter++)
        this->insertDriver(QString::fromStdString(iter->first));
}

void ElementTreeWidget::setSimulation(Simulation *simulation)
{
    m_simulation = simulation;
}

Simulation *ElementTreeWidget::simulation() const
{
    return m_simulation;
}