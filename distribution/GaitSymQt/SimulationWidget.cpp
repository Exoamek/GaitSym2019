/*
 *  SimulationWidget.cpp
 *  GaitSymODE2019
 *
 *  Created by Bill Sellers on 08/10/2018.
 *  Copyright 2018 Bill Sellers. All rights reserved.
 *
 */

#include "SimulationWidget.h"
#include "Simulation.h"
#include "Body.h"
#include "Joint.h"
#include "Geom.h"
#include "Muscle.h"
#include "Marker.h"
#include "FluidSac.h"
#include "FacetedObject.h"
#include "TrackBall.h"
#include "AVIWriter.h"
#include "DrawBody.h"
#include "DrawJoint.h"
#include "DrawGeom.h"
#include "DrawMuscle.h"
#include "DrawMarker.h"
#include "DrawFluidSac.h"
#include "FacetedSphere.h"
#include "Preferences.h"
#include "MainWindow.h"
#include "GLUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QDir>
#include <QMessageBox>
#include <QDirIterator>
#include <QDebug>
#include <QBuffer>
#include <QtGlobal>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QImage>
#include <QOpenGLExtraFunctions>

#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <memory>
#include <regex>

SimulationWidget::SimulationWidget(QWidget *parent)
    : QOpenGLWidget(parent), m_mouseClickEvent(QEvent::Type::None, QPointF(), Qt::MouseButton::NoButton, Qt::NoButton, Qt::NoModifier)
{
    m_cursorColour = Preferences::valueQColor("CursorColour");
    m_cursorLevel = size_t(Preferences::valueInt("CursorLevel"));
    m_backgroundColour = Preferences::valueQColor("BackgroundColour");
    m_axesScale = Preferences::valueFloat("GlobalAxesSize");
    m_cursorRadius = Preferences::valueFloat("CursorRadius");
    m_cursor3DNudge = Preferences::valueFloat("CursorNudge");

    m_cursor3D = std::make_unique<FacetedSphere>(1, m_cursorLevel, m_cursorColour, 1);
    m_globalAxes = std::make_unique<FacetedObject>();
    m_globalAxes->ReadFromResource(":/objects/global_axes.tri");
    m_trackball = std::make_unique<Trackball>();

    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::WheelFocus);
    setMouseTracking(true);
}

SimulationWidget::~SimulationWidget()
{
    cleanup();
}

void SimulationWidget::cleanup()
{
    makeCurrent();
    if (m_facetedObjectShader)
    {
        delete m_facetedObjectShader;
        m_facetedObjectShader = nullptr;
    }
    if (m_fixedColourObjectShader)
    {
        delete m_fixedColourObjectShader;
        m_fixedColourObjectShader = nullptr;
    }
    if (m_aviWriter)
    {
        delete m_aviWriter;
        m_aviWriter = nullptr;
    }
    doneCurrent();
}

void SimulationWidget::initializeGL()
{
    // In this example the widget's corresponding top-level window can change
    // several times during the widget's lifetime. Whenever this happens, the
    // QOpenGLWidget's associated context is destroyed and a new one is created.
    // Therefore we have to be prepared to clean up the resources on the
    // aboutToBeDestroyed() signal, instead of the destructor. The emission of
    // the signal will be followed by an invocation of initializeGL() where we
    // can recreate all resources.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &SimulationWidget::cleanup);

    initializeOpenGLFunctions();

    QString versionString(QLatin1String(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    qDebug() << "Driver Version String:" << versionString;
    qDebug() << "Current Context:" << format();

    int openGLVersion = format().majorVersion() * 100 + format().minorVersion() * 10;
    if (openGLVersion < 330)
    {
        QString errorMessage = QString("This application requires OpenGL 3.3 or greater.\nCurrent version is %1.\nApplication will abort.").arg(versionString);
        QMessageBox::critical(this, tr("GaitSym2019"), errorMessage);
        exit(EXIT_FAILURE);
    }

    // Create a vertex array object. In OpenGL ES 2.0 and OpenGL 2.x
    // implementations this is optional and support may not be present
    // at all. Nonetheless the below code works in all cases and makes
    // sure there is a VAO when one is needed.
    m_vao.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

    glClearColor(GLclampf(m_backgroundColour.redF()), GLclampf(m_backgroundColour.greenF()), GLclampf(m_backgroundColour.blueF()), GLclampf(m_backgroundColour.alphaF()));

    m_facetedObjectShader = new QOpenGLShaderProgram();
    m_facetedObjectShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/opengl/vertex_shader.glsl");
    m_facetedObjectShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/opengl/fragment_shader.glsl");
    m_facetedObjectShader->bindAttributeLocation("vertex", 0); // instead of "layout (location = 0)" in shader
    m_facetedObjectShader->bindAttributeLocation("vertexNormal", 1); // instead of "layout (location = 1)" in shader
    m_facetedObjectShader->bindAttributeLocation("vertexColor", 2); // instead of "layout (location = 2)" in shader
    m_facetedObjectShader->bindAttributeLocation("vertexUV", 3); // instead of "layout (location = 3)" in shader
    m_facetedObjectShader->link();

    m_facetedObjectShader->bind();

    m_facetedObjectShader->setUniformValue("diffuse", QVector4D(0.5, 0.5, 0.5, 1.0) );  // diffuse
    m_facetedObjectShader->setUniformValue("ambient", QVector4D(0.5, 0.5, 0.5, 1.0) );  // ambient
    m_facetedObjectShader->setUniformValue("specular", QVector4D(0.5, 0.5, 0.5, 1.0) );  // specular reflectivity
    m_facetedObjectShader->setUniformValue("shininess", 5.0f ); // specular shininess
    m_facetedObjectShader->setUniformValue("blendColour", QVector4D(1.0, 1.0, 1.0, 1.0) );  // blend colour
    m_facetedObjectShader->setUniformValue("blendFraction", 0.0f ); // blend fraction
    m_facetedObjectShader->setUniformValue("hasTexture", 0 ); // use texture fraction

    m_facetedObjectShader->release();

    m_fixedColourObjectShader = new QOpenGLShaderProgram();
    m_fixedColourObjectShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/opengl/vertex_shader_2.glsl");
    m_fixedColourObjectShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/opengl/fragment_shader_2.glsl");
    m_fixedColourObjectShader->bindAttributeLocation("vertex", 0);
    m_fixedColourObjectShader->bindAttributeLocation("vertexColor", 1);
    m_fixedColourObjectShader->link();

    m_fixedColourObjectShader->bind();
    m_fixedColourObjectShader->release();

    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);

}

void SimulationWidget::paintGL()
{
    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);

    glClearColor(GLclampf(m_backgroundColour.redF()), GLclampf(m_backgroundColour.greenF()), GLclampf(m_backgroundColour.blueF()), GLclampf(m_backgroundColour.alphaF()));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_CULL_FACE);

    // set the projection matrix
    float aspectRatio = float(width()) / float(height());
    float viewHeight = 2.0f * std::sin((m_FOV / 2.0f) * float(M_PI) / 180.0f) * m_cameraDistance;
    float viewWidth = viewHeight * aspectRatio;

    m_proj.setToIdentity();
    if (m_orthographicProjection)
    {
        m_proj.ortho(-viewWidth, viewWidth, -viewHeight, viewHeight, m_frontClip, m_backClip); // multiply by orthographic projection matrix
    }
    else
    {
        m_proj.perspective(m_FOV, aspectRatio, m_frontClip, m_backClip); // multiply by perspective projection
    }

    // set the view matrix
    m_view.setToIdentity();
    QVector3D eye(m_COIx - m_cameraVecX * m_cameraDistance, m_COIy - m_cameraVecY * m_cameraDistance, m_COIz - m_cameraVecZ * m_cameraDistance);
    QVector3D centre(m_COIx, m_COIy, m_COIz);
    QVector3D up(m_upX, m_upY, m_upZ);
    m_view.lookAt(eye, centre, up);

    // now draw things
    if (m_simulation)
    {
        drawModel();
    }

//    for (auto &&iter : m_extraObjectsToDrawMap)
//    {
//        iter.second->Draw();
//    }

    // the 3d cursor
    // qDebug() << "Cursor " << m_cursor3DPosition.x() << " " << m_cursor3DPosition.y() << " " << m_cursor3DPosition.z();
    m_cursor3D->SetDisplayPosition(double(m_cursor3DPosition.x()), double(m_cursor3DPosition.y()), double(m_cursor3DPosition.z()));
    m_cursor3D->SetDisplayScale(double(m_cursorRadius), double(m_cursorRadius), double(m_cursorRadius));
    m_cursor3D->setSimulationWidget(this);
    m_cursor3D->Draw();

    // the global axes
    m_globalAxes->SetDisplayPosition(0, 0, 0);
    m_globalAxes->SetDisplayScale(double(m_axesScale), double(m_axesScale), double(m_axesScale));
    m_globalAxes->setSimulationWidget(this);
    m_globalAxes->Draw();

    // any manipulation feedback?

    // raster mode positioning
    // with origin at top left
    glDisable(GL_DEPTH_TEST);
    StrokeFont strokeFont;
    const GLfloat threshold = 105.0f / 255.0f;
    GLfloat backgroundDelta = GLfloat((m_backgroundColour.redF() * 0.299) + (m_backgroundColour.greenF() * 0.587) + (m_backgroundColour.blueF() * 0.114));
    if (backgroundDelta > threshold) strokeFont.SetRGBA(0, 0, 0, 1);
    else strokeFont.SetRGBA(1, 1, 1, 1);
    strokeFont.setGlWidget(this);
    QMatrix4x4 lineVP;
    lineVP.ortho(0, float(width()), 0, float(height()), -1, 1);
    strokeFont.setVpMatrix(lineVP);
    glLineWidth(2); // this doesn't seem to work on the Mac

    if (m_trackballFlag  && m_trackball->GetOutsideRadius())
    {
        float centreX = float(width()) / 2;
        float centreY = float(height()) / 2;
        float radius = float(m_trackball->GetTrackballRadius());
        strokeFont.AddCircle(centreX, centreY, 0, radius, 180);
    }

   strokeFont.Draw();
}

void SimulationWidget::resizeGL(int width, int height)
{
//    qDebug() << "resizeGL";
//    qDebug() << width << " " << height << " ";
//    int viewport[4];
//    glGetIntegerv(GL_VIEWPORT, viewport);
//    qDebug() << viewport[0] << " " << viewport[1] << " " << viewport[2] << " " << viewport[3] << " ";
//    QImage image = grabFramebuffer();
//    qDebug() << image.width() << " " << image.height() << " ";
    int openGLWidth = devicePixelRatio() * width;
    int openGLHeight = devicePixelRatio() * height;
    EmitResize(openGLWidth, openGLHeight);
}

void SimulationWidget::mousePressEvent(QMouseEvent *event)
{
    m_mouseClickEvent = *event;

    // on high resolution (e.g.retina) displays the units of the viewport are device pixels whereas the units of event->pos() are scaled pixels
    // the following mapping should always give the right values for the UnProject matrix
    GLfloat winX = (GLfloat(event->pos().x()) / GLfloat(width())) * 2 - 1;
    GLfloat winY = -1 * ((GLfloat(event->pos().y()) / GLfloat(height())) * 2 - 1);
    intersectModel(winX, winY);

    if (m_moveMarkerMode)
    {
        m_moveMarkerMode = false;
        if (m_mouseClickEvent.modifiers() == Qt::NoModifier) emit EmitMoveMarkerRequest(QString::fromStdString(m_moveMarkerName), m_cursor3DPosition);
        return;
    }

    if (m_mouseClickEvent.buttons() & Qt::LeftButton)
    {
        if (m_mouseClickEvent.modifiers() == Qt::NoModifier)
        {
            int trackballRadius;
            if (width() < height()) trackballRadius = int(float(width()) / 2.2f);
            else trackballRadius = int(float(height()) / 2.2f);
            m_trackballStartCameraVec = QVector3D(m_cameraVecX, m_cameraVecY, m_cameraVecZ);
            m_trackballStartUp = QVector3D(m_upX, m_upY, m_upZ);
            m_trackball->StartTrackball(m_mouseClickEvent.pos().x(), m_mouseClickEvent.pos().y(), width() / 2, height() / 2, trackballRadius,
                                        pgd::Vector3(double(m_trackballStartUp.x()), double(m_trackballStartUp.y()), double(m_trackballStartUp.z())),
                                        pgd::Vector3(double(-m_trackballStartCameraVec.x()), double(-m_trackballStartCameraVec.y()), double(-m_trackballStartCameraVec.z())));
            m_trackballFlag = true;
            emit EmitStatusString(tr("Rotate"), 2);
            update();
        }
        else if (m_mouseClickEvent.modifiers() & Qt::ShiftModifier)
        {
            // detect the collision point of the mouse click
            if (m_hits.size() > 0)
            {
                m_cursor3DPosition = QVector3D(float(getClosestHit()->worldLocation().x), float(getClosestHit()->worldLocation().y), float(getClosestHit()->worldLocation().z));
                QClipboard *clipboard = QApplication::clipboard();
                clipboard->setText(QString("%1\t%2\t%3").arg(double(m_cursor3DPosition.x())).arg(double(m_cursor3DPosition.y())).arg(double(m_cursor3DPosition.z())), QClipboard::Clipboard);
                emit EmitStatusString(QString("3D Cursor %1\t%2\t%3").arg(double(m_cursor3DPosition.x())).arg(double(m_cursor3DPosition.y())).arg(double(m_cursor3DPosition.z())), 2);
                update();
            }
        }
    }
    else if (m_mouseClickEvent.buttons() & Qt::MidButton)
    {
        if (m_mouseClickEvent.modifiers() == Qt::NoModifier)
        {
            m_panStartCOI = QVector3D(m_COIx, m_COIy, m_COIz);
            m_projectPanMatrix = m_proj * m_view; // model would be identity so mvpMatrix isn't needed
            bool invertible;
            m_unprojectPanMatrix = m_projectPanMatrix.inverted(&invertible); // we need the unproject matrix
            if (!invertible)
            {
                qDebug() << "Problem inverting (m_proj * m_view)";
                return;
            }
            m_panFlag = true;
            // detect the collision point of the mouse click
            if (m_hits.size() > 0)
            {
                m_panStartPoint = QVector3D(float(getClosestHit()->worldLocation().x), float(getClosestHit()->worldLocation().y), float(getClosestHit()->worldLocation().z));
                QVector3D screenStartPoint = m_projectPanMatrix * m_panStartPoint;
                m_panStartScreenPoint.setZ(screenStartPoint.z());
            }
            else     // this is harder since we don't know the screen Z were are interested in.
            {
                // generate a screen Z from projecting the COI into screen coordinates (-1 to 1 box)
                QVector3D screenStartPoint = m_projectPanMatrix * m_panStartCOI;
                m_panStartScreenPoint.setZ(screenStartPoint.z());
                // now unproject this point to get the pan start point
                m_panStartPoint = m_unprojectPanMatrix * m_panStartScreenPoint;
            }
            emit EmitStatusString(tr("Pan"), 2);
            update();
        }
        else if (m_mouseClickEvent.modifiers() & Qt::AltModifier)
        {
            if (m_hits.size() > 0)
            {
                QVector3D worldIntersection = QVector3D(float(getClosestHit()->worldLocation().x), float(getClosestHit()->worldLocation().y), float(getClosestHit()->worldLocation().z));
                m_COIx = worldIntersection.x();
                m_COIy = worldIntersection.y();
                m_COIz = worldIntersection.z();
                QClipboard *clipboard = QApplication::clipboard();
                clipboard->setText(QString("%1\t%2\t%3").arg(double(worldIntersection.x())).arg(double(worldIntersection.y())).arg(double(worldIntersection.z())), QClipboard::Clipboard);
                emit EmitStatusString(QString("Centre of Interest %1\t%2\t%3").arg(double(worldIntersection.x())).arg(double(worldIntersection.y())).arg(double(worldIntersection.z())), 2);
                emit EmitCOI(worldIntersection.x(), worldIntersection.y(), worldIntersection.z());
                update();
            }
        }
    }
    else if (m_mouseClickEvent.buttons() & Qt::RightButton)
    {
        if (m_mouseClickEvent.modifiers() == Qt::NoModifier)
        {
            menuRequest(m_mouseClickEvent.pos());
        }
    }
}

void SimulationWidget::mouseMoveEvent(QMouseEvent *event)
{
    // qDebug() << event->localPos();
    if (m_moveMarkerMode)
    {
        if (event->modifiers() == Qt::NoModifier)
        {
            GLfloat winX = (GLfloat(event->pos().x()) / GLfloat(width())) * 2 - 1;
            GLfloat winY = -1 * ((GLfloat(event->pos().y()) / GLfloat(height())) * 2 - 1);
            intersectModel(winX, winY);
            if (m_hits.size() == 0) return;
            m_cursor3DPosition = QVector3D(float(getClosestHit()->worldLocation().x), float(getClosestHit()->worldLocation().y), float(getClosestHit()->worldLocation().z));
            update();
            return;
        }
        else
        {
            m_moveMarkerMode = false;
        }
        return;
    }
    if (event->buttons() & Qt::LeftButton)
    {
        if (m_trackballFlag)
        {
            pgd::Quaternion pgdRotation;
            m_trackball->RollTrackballToClick(event->pos().x(), event->pos().y(), &pgdRotation);
            QQuaternion rotation(float(pgdRotation.n), float(pgdRotation.x), float(pgdRotation.y), float(pgdRotation.z));
            rotation = rotation.conjugated();
            QVector3D newCameraVec = rotation * m_trackballStartCameraVec;
            m_cameraVecX = newCameraVec.x();
            m_cameraVecY = newCameraVec.y();
            m_cameraVecZ = newCameraVec.z();
            QVector3D newUp = rotation *  m_trackballStartUp;
            m_upX = newUp.x();
            m_upY = newUp.y();
            m_upZ = newUp.z();
            update();
            emit EmitStatusString(QString("Camera %1 %2 %3 Up %4 %5 %6").arg(double(m_cameraVecX)).arg(double(m_cameraVecY)).arg(double(m_cameraVecZ)).arg(double(m_upX)).arg(double(m_upY)).arg(double(m_upZ)), 2);
        }
    }
    else if (event->buttons() & Qt::MidButton)
    {
        if (m_panFlag)
        {
            GLfloat winX = (GLfloat(event->pos().x()) / GLfloat(width())) * 2 - 1;
            GLfloat winY = -1 * ((GLfloat(event->pos().y()) / GLfloat(height())) * 2 - 1);
            QVector3D screenPoint(winX, winY, m_panStartScreenPoint.z());
            QVector3D panCurrentPoint = m_unprojectPanMatrix * screenPoint;
            m_COIx = m_panStartCOI.x() - (panCurrentPoint.x() - m_panStartPoint.x());
            m_COIy = m_panStartCOI.y() - (panCurrentPoint.y() - m_panStartPoint.y());
            m_COIz = m_panStartCOI.z() - (panCurrentPoint.z() - m_panStartPoint.z());
            // qDebug() << "panCurrentPoint=" << panCurrentPoint;
            emit EmitStatusString(QString("COI %1 %2 %3").arg(double(m_COIx)).arg(double(m_COIy)).arg(double(m_COIz)), 2);
            emit EmitCOI(m_COIx, m_COIy, m_COIz);
            update();
        }
    }
}

void SimulationWidget::mouseReleaseEvent(QMouseEvent * /* event */)
{
    m_trackballFlag = false;
    m_panFlag = false;
    update();
}

void SimulationWidget::wheelEvent(QWheelEvent *event)
{
    // assume each ratchet of the wheel gives a score of 120 (8 * 15 degrees)
    float sensitivity = 2400;
    float scale = 1.0f + float(event->angleDelta().y()) / sensitivity;
    m_FOV *= scale;
    if (m_FOV > 170) m_FOV = 170;
    else if (m_FOV < 0.001f) m_FOV = 0.001f;
    update();

    emit EmitStatusString(QString("FOV %1").arg(double(m_FOV)), 2);
    emit EmitFoV(m_FOV);
}

// handle key presses
void SimulationWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_moveMarkerMode) m_moveMarkerMode = false;
    QVector3D newPosition = m_cursor3DPosition;
    switch ( event->key() )
    {

    // X, Y and Z move the cursor
    case Qt::Key_X:
        if (event->modifiers() == Qt::NoModifier)
            newPosition.setX(newPosition.x() + m_cursor3DNudge);
        else
            newPosition.setX(newPosition.x() - m_cursor3DNudge);
        break;

    case Qt::Key_Y:
        if (event->modifiers() == Qt::NoModifier)
            newPosition.setY(newPosition.y() + m_cursor3DNudge);
        else
            newPosition.setY(newPosition.y() - m_cursor3DNudge);
        break;

    case Qt::Key_Z:
        if (event->modifiers() == Qt::NoModifier)
            newPosition.setZ(newPosition.z() + m_cursor3DNudge);
        else
            newPosition.setZ(newPosition.z() - m_cursor3DNudge);
        break;

    // S snaps the cursor to the nearest whole number multiple of the nudge value
    case Qt::Key_S:
        newPosition.setX(round(newPosition.x() / m_cursor3DNudge) * m_cursor3DNudge);
        newPosition.setY(round(newPosition.y() / m_cursor3DNudge) * m_cursor3DNudge);
        newPosition.setZ(round(newPosition.z() / m_cursor3DNudge) * m_cursor3DNudge);
        break;
    }

    if (newPosition != m_cursor3DPosition)
    {
        m_cursor3DPosition = newPosition;
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(QString("%1\t%2\t%3").arg(double(newPosition.x())).arg(double(newPosition.y())).arg(double(newPosition.z())), QClipboard::Clipboard);
        emit EmitStatusString(QString("3D Cursor %1\t%2\t%3").arg(double(newPosition.x())).arg(double(newPosition.y())).arg(double(newPosition.z())), 2);
        update();
    }
}

void SimulationWidget::menuRequest(const QPoint &pos)
{
    if (m_hits.size() == 0) return;

    QMenu menu;
    QAction *action = menu.addAction(tr("Centre View"));
    menu.addSeparator();

    Drawable *drawable = getClosestHit()->drawable();
    std::string name;
    if (drawable)
    {
        std::string className = drawable->className();
        // I want the bit after the Draw
        std::regex elementNameRegEx(".*Draw([A-Za-z]*)");
        std::string elementName = std::regex_replace(className, elementNameRegEx, "$1");
        action = menu.addAction(QString::fromStdString(elementName) + QString(" Info..."));
        name = drawable->name();
    }

    while (m_simulation && m_mainWindow->mode() == MainWindow::constructionMode) // use while to prevent nesting of if else statements
    {
        action = menu.addAction(tr("Create Marker..."));
        menu.addSeparator();
        auto body = dynamic_cast<DrawBody *>(drawable);
        if (body)
        {
            action = menu.addAction(tr("Edit Body..."));
            action = menu.addAction(tr("Delete Body..."));
            break;
        }
//        auto fluisac = dynamic_cast<DrawFluidSac *>(drawable);
//        if (fluisac)
//        {
//            action = menu.addAction(tr("Edit Fluid Sac..."));
//            action = menu.addAction(tr("Delete Fluid Sac..."));
//            break;
//        }
        auto geom = dynamic_cast<DrawGeom *>(drawable);
        if (geom)
        {
            action = menu.addAction(tr("Edit Geom..."));
            action = menu.addAction(tr("Delete Geom..."));
            break;
        }
        auto joint = dynamic_cast<DrawJoint *>(drawable);
        if (joint)
        {
            action = menu.addAction(tr("Edit Joint..."));
            action = menu.addAction(tr("Delete Joint..."));
            break;
        }
        auto marker = dynamic_cast<DrawMarker *>(drawable);
        if (marker)
        {
            action = menu.addAction(tr("Edit Marker..."));
            action = menu.addAction(tr("Delete Marker..."));
            action = menu.addAction(tr("Move Marker"));
            break;
        }
        auto muscle = dynamic_cast<DrawMuscle *>(drawable);
        if (muscle)
        {
            action = menu.addAction(tr("Edit Muscle..."));
            action = menu.addAction(tr("Delete Muscle..."));
            break;
        }
        break;
    }

    QPoint gp = this->mapToGlobal(pos);
    action = menu.exec(gp);
    while (action) // use while to prevent nesting of if else statements
    {
        m_lastMenuItem = action->text();
        if (action->text() == tr("Centre View"))
        {
            m_COIx = float(getClosestHit()->worldLocation().x);
            m_COIy = float(getClosestHit()->worldLocation().y);
            m_COIz = float(getClosestHit()->worldLocation().z);
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setText(QString("%1\t%2\t%3").arg(double(m_COIx)).arg(double(m_COIy)).arg(double(m_COIz)), QClipboard::Clipboard);
            emit EmitStatusString(QString("Centre of Interest %1\t%2\t%3").arg(double(m_COIx)).arg(double(m_COIy)).arg(double(m_COIz)), 2);
            emit EmitCOI(m_COIx, m_COIy, m_COIz);
            update();
            break;
        }
        if (action->text() == tr("Create Marker..."))
        {
            emit EmitCreateMarkerRequest();
            break;
        }
        if (action->text() == tr("Edit Marker..."))
        {
            emit EmitEditMarkerRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Edit Body..."))
        {
            emit EmitEditBodyRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Edit Geom..."))
        {
            emit EmitEditGeomRequest(QString::fromStdString(name));
        }
        if (action->text() == tr("Edit Joint..."))
        {
            emit EmitEditJointRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Edit Muscle..."))
        {
            emit EmitEditMuscleRequest(QString::fromStdString(name));
            break;
        }
//        if (action->text() == tr("Edit Fluid Sac..."))
//        {
//            emit EmitEditFluidSacRequest(QString::fromStdString(name)));
//            break;
//        }
        if (action->text() == tr("Delete Marker..."))
        {
            emit EmitDeleteMarkerRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Delete Body..."))
        {
            emit EmitDeleteBodyRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Delete Geom..."))
        {
            emit EmitDeleteGeomRequest(QString::fromStdString(name));
        }
        if (action->text() == tr("Delete Joint..."))
        {
            emit EmitDeleteJointRequest(QString::fromStdString(name));
            break;
        }
        if (action->text() == tr("Delete Muscle..."))
        {
            emit EmitDeleteMuscleRequest(QString::fromStdString(name));
            break;
        }
//        if (action->text() == tr("Delete Fluid Sac..."))
//        {
//            emit EmitDeleteFluidSacRequest(QString::fromStdString(name));
//            break;
//        }
        if (action->text() == tr("Move Marker"))
        {
            m_moveMarkerMode = true;
            m_moveMarkerName = name;
            break;
        }
        if (action->text().contains("Info"))
        {
            QStringList tokens = action->text().split(" ");
            if (tokens.size())
                emit EmitInfoRequest(tokens[0], QString::fromStdString(name));
        }
        break;
    }
}

MainWindow *SimulationWidget::getMainWindow() const
{
    return m_mainWindow;
}

void SimulationWidget::setMainWindow(MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;
}

float SimulationWidget::axesScale() const
{
    return m_axesScale;
}

void SimulationWidget::setAxesScale(float axesScale)
{
    m_axesScale = axesScale;
}

QVector3D SimulationWidget::cursor3DPosition() const
{
    return m_cursor3DPosition;
}

void SimulationWidget::setCursor3DPosition(const QVector3D &cursor3DPosition)
{
    m_cursor3DPosition = cursor3DPosition;
    update();
}

QColor SimulationWidget::backgroundColour() const
{
    return m_backgroundColour;
}

void SimulationWidget::setBackgroundColour(const QColor &backgroundColour)
{
    m_backgroundColour = backgroundColour;
}

int SimulationWidget::aviQuality() const
{
    return m_aviQuality;
}

void SimulationWidget::setAviQuality(int aviQuality)
{
    m_aviQuality = aviQuality;
}

AVIWriter *SimulationWidget::aviWriter() const
{
    return m_aviWriter;
}

void SimulationWidget::setAviWriter(AVIWriter *aviWriter)
{
    m_aviWriter = aviWriter;
}

QColor SimulationWidget::cursorColour() const
{
    return m_cursorColour;
}

void SimulationWidget::setCursorColour(const QColor &cursorColour)
{
    m_cursorColour = cursorColour;
}

// write the current frame out to a file
int SimulationWidget::WriteStillFrame(const QString &filename)
{
    QImage image = grabFramebuffer();
    if (image.save(filename) == false) return __LINE__;
    return 0;
}

// write the current frame out to a file
int SimulationWidget::WriteMovieFrame()
{
    Q_ASSERT(m_aviWriter);
    QImage image = grabFramebuffer();
    if (image.sizeInBytes() == 0) return __LINE__; //should always be OK, but you never know. ;)
    m_aviWriter->WriteAVI(image, m_aviQuality);
    return 0;
}

int SimulationWidget::StartAVISave(const QString &filename)
{
    if (m_aviWriter) delete m_aviWriter;
    m_aviWriter = new AVIWriter();
    if (m_aviQuality == 0) return __LINE__; // should always be true
    QImage image = grabFramebuffer();
    if (image.sizeInBytes() == 0) return __LINE__; //should always be OK, but you never know. ;)
    m_aviWriter->InitialiseFile(filename, static_cast<unsigned int>(image.size().width()), static_cast<unsigned int>(image.size().height()), m_fps);
    m_aviWriter->WriteAVI(image, m_aviQuality);
    return 0;
}

int SimulationWidget::StopAVISave()
{
    if (m_aviWriter == nullptr) return __LINE__;
    delete m_aviWriter;
    m_aviWriter = nullptr;
    return 0;
}

// write the scene as a series of OBJ files in a folder
int SimulationWidget::WriteCADFrame(const QString &pathname)
{
    QString workingFolder = QDir::currentPath();
    if (QDir(pathname).exists() == false)
    {
        if (QDir().mkdir(pathname) == false)
        {
            QMessageBox::warning(nullptr, "Snapshot Error", QString("Could not create folder '%1' for OBJ files\n").arg(pathname), "Click button to return to simulation");
            return __LINE__;
        }
    }
    QDir::setCurrent(pathname);

    int meshCount = 0;
    for (auto drawableIter : m_drawables)
    {
        for (auto facetedObjectIter : drawableIter->facetedObjectList())
        {
            if (facetedObjectIter->GetNumVertices())
            {
                QString numberedFilename = QString("mesh%1.obj").arg(meshCount, 6, 10, QChar('0'));
                facetedObjectIter->WriteOBJFile(numberedFilename.toStdString());
                meshCount++;
            }
        }
    }

    QDir::setCurrent(workingFolder);
    return 0;
}

void SimulationWidget::SetCameraVec(double x, double y, double z)
{
    SetCameraVec(float(x), float(y), float(z));
}

void SimulationWidget::SetCameraVec(float x, float y, float z)
{
    m_cameraVecX = x;
    m_cameraVecY = y;
    m_cameraVecZ = z;
    if (z > 0.999f || z < -0.999f)
    {
        m_upX = 0;
        m_upY = 1;
        m_upZ = 0;
    }
    else
    {
        m_upX = 0;
        m_upY = 0;
        m_upZ = 1;
    }
    update();
}

bool SimulationWidget::DeleteDrawBody(const std::string &bodyName)
{
    auto drawBodyMapIter = m_drawBodyMap.find(bodyName);
    if (drawBodyMapIter == m_drawBodyMap.end()) return false;
    delete drawBodyMapIter->second;
    drawBodyMapIter = m_drawBodyMap.erase(drawBodyMapIter);
    return true;
}


void SimulationWidget::drawModel()
{
    if (!m_simulation) return;
    auto bodyList = m_simulation->GetBodyList();
    auto drawBodyMapIter = m_drawBodyMap.begin();
    while (drawBodyMapIter != m_drawBodyMap.end())
    {
        if (bodyList->find(drawBodyMapIter->first) == bodyList->end() || drawBodyMapIter->second->body()->redraw() == true)
        {
            delete drawBodyMapIter->second;
            drawBodyMapIter = m_drawBodyMap.erase(drawBodyMapIter);
        }
        else drawBodyMapIter++;
    }
    for (auto &&iter : *bodyList)
    {
        std::map<std::string, DrawBody *>::iterator it = m_drawBodyMap.find(iter.first);
        if (it == m_drawBodyMap.end() || it->second->body() != iter.second.get())
        {
            if (it != m_drawBodyMap.end()) delete it->second;
            DrawBody *drawBody = new DrawBody();
            drawBody->setBody(iter.second.get());
            drawBody->initialise(this);
            m_drawBodyMap[iter.first] = drawBody;
            it = m_drawBodyMap.find(iter.first);
        }
        it->second->updateEntityPose();
        it->second->axes()->setVisible(iter.second->visible());
        it->second->meshEntity1()->setVisible(m_drawBodyMesh1 && iter.second->visible());
        it->second->meshEntity2()->setVisible(m_drawBodyMesh2 && iter.second->visible());
        it->second->meshEntity3()->setVisible(m_drawBodyMesh3 && iter.second->visible());
        it->second->Draw();
    }

    auto jointList = m_simulation->GetJointList();
    auto drawJointMapIter = m_drawJointMap.begin();
    while (drawJointMapIter != m_drawJointMap.end())
    {
        if (jointList->find(drawJointMapIter->first) == jointList->end() || drawJointMapIter->second->joint()->redraw() == true)
        {
            delete drawJointMapIter->second;
            drawJointMapIter = m_drawJointMap.erase(drawJointMapIter);
        }
        else drawJointMapIter++;
    }
    for (auto &&iter : *jointList)
    {
        std::map<std::string, DrawJoint *>::iterator it = m_drawJointMap.find(iter.first);
        if (it == m_drawJointMap.end() || it->second->joint() != iter.second.get())
        {
            if (it != m_drawJointMap.end()) delete it->second;
            DrawJoint *drawJoint = new DrawJoint();
            drawJoint->setJoint(iter.second.get());
            drawJoint->initialise(this);
            m_drawJointMap[iter.first] = drawJoint;
            it = m_drawJointMap.find(iter.first);
        }
        it->second->updateEntityPose();
        it->second->setVisible(iter.second->visible());
        it->second->Draw();
    }

    auto geomList = m_simulation->GetGeomList();
    auto drawGeomMapIter = m_drawGeomMap.begin();
    while (drawGeomMapIter != m_drawGeomMap.end())
    {
        if (geomList->find(drawGeomMapIter->first) == geomList->end() || drawGeomMapIter->second->geom()->redraw() == true)
        {
            delete drawGeomMapIter->second;
            drawGeomMapIter = m_drawGeomMap.erase(drawGeomMapIter);
        }
        else drawGeomMapIter++;
    }
    for (auto &&iter : *geomList)
    {
        auto it = m_drawGeomMap.find(iter.first);
        if (it == m_drawGeomMap.end() || it->second->geom() != iter.second.get())
        {
            if (it != m_drawGeomMap.end()) delete it->second;
            DrawGeom *drawGeom = new DrawGeom();
            drawGeom->setGeom(iter.second.get());
            drawGeom->initialise(this);
            m_drawGeomMap[iter.first] = drawGeom;
            it = m_drawGeomMap.find(iter.first);
        }
        it->second->updateEntityPose();
        it->second->setVisible(iter.second->visible());
        it->second->Draw();
    }

    auto markerList = m_simulation->GetMarkerList();
    auto drawMarkerMapIter = m_drawMarkerMap.begin();
    while (drawMarkerMapIter != m_drawMarkerMap.end())
    {
        if (markerList->find(drawMarkerMapIter->first) == markerList->end() || drawMarkerMapIter->second->marker()->redraw() == true)
        {
            delete drawMarkerMapIter->second;
            drawMarkerMapIter = m_drawMarkerMap.erase(drawMarkerMapIter);
        }
        else drawMarkerMapIter++;
    }
    for (auto &&iter : *markerList)
    {
        auto it = m_drawMarkerMap.find(iter.first);
        if (it == m_drawMarkerMap.end() || it->second->marker() != iter.second.get())
        {
            if (it != m_drawMarkerMap.end()) delete it->second;
            DrawMarker *drawMarker = new DrawMarker();
            drawMarker->setMarker(iter.second.get());
            drawMarker->initialise(this);
            m_drawMarkerMap[iter.first] = drawMarker;
            it = m_drawMarkerMap.find(iter.first);
        }
        it->second->updateEntityPose();
        it->second->setVisible(iter.second->visible());
        it->second->Draw();
    }

    auto muscleList = m_simulation->GetMuscleList();
    auto drawMuscleMapIter = m_drawMuscleMap.begin();
    while (drawMuscleMapIter != m_drawMuscleMap.end())
    {
        if (muscleList->find(drawMuscleMapIter->first) == muscleList->end() || drawMuscleMapIter->second->muscle()->redraw() == true || drawMuscleMapIter->second->muscle()->GetStrap()->redraw() == true)
        {
            delete drawMuscleMapIter->second;
            drawMuscleMapIter = m_drawMuscleMap.erase(drawMuscleMapIter);
        }
        else drawMuscleMapIter++;
    }
    for (auto &&iter : *muscleList)
    {
        auto it = m_drawMuscleMap.find(iter.first);
        if (it == m_drawMuscleMap.end() || it->second->muscle() != iter.second.get())
        {
            if (it != m_drawMuscleMap.end()) delete it->second;
            DrawMuscle *drawMuscle = new DrawMuscle();
            drawMuscle->setMuscle(iter.second.get());
            drawMuscle->initialise(this);
            m_drawMuscleMap[iter.first] = drawMuscle;
            it = m_drawMuscleMap.find(iter.first);
        }
        it->second->setVisible(iter.second->visible());
        it->second->Draw();
    }

    auto fluidSacList = m_simulation->GetFluidSacList();
    auto drawFluidSacMapIter = m_drawFluidSacMap.begin();
    while (drawFluidSacMapIter != m_drawFluidSacMap.end())
    {
        if (fluidSacList->find(drawFluidSacMapIter->first) == fluidSacList->end() || drawFluidSacMapIter->second->fluidSac()->redraw() == true)
        {
            delete drawFluidSacMapIter->second;
            drawFluidSacMapIter = m_drawFluidSacMap.erase(drawFluidSacMapIter);
        }
        else drawFluidSacMapIter++;
    }
    for (auto &&iter : *fluidSacList)
    {
        auto it = m_drawFluidSacMap.find(iter.first);
        if (it == m_drawFluidSacMap.end() || it->second->fluidSac() != iter.second.get())
        {
            if (it != m_drawFluidSacMap.end()) delete it->second;
            DrawFluidSac *drawFluidSac = new DrawFluidSac();
            drawFluidSac->setFluidSac(iter.second.get());
            drawFluidSac->initialise(this);
            m_drawFluidSacMap[iter.first] = drawFluidSac;
            it = m_drawFluidSacMap.find(iter.first);
        }
        it->second->setVisible(iter.second->visible());
        it->second->Draw();
    }

    m_drawables.clear();
    for (auto &&it : m_drawBodyMap) m_drawables.push_back(it.second);
    for (auto &&it : m_drawJointMap) m_drawables.push_back(it.second);
    for (auto &&it : m_drawGeomMap) m_drawables.push_back(it.second);
    for (auto &&it : m_drawMarkerMap) m_drawables.push_back(it.second);
    for (auto &&it : m_drawMuscleMap) m_drawables.push_back(it.second);
    for (auto &&it : m_drawFluidSacMap) m_drawables.push_back(it.second);
}

const IntersectionHits *SimulationWidget::getClosestHit() const
{
    if (m_hits.size() == 0) return nullptr;
    return m_hits[m_hitsIndexByZ[0]].get();
}

bool SimulationWidget::getDrawBodyMesh3() const
{
    return m_drawBodyMesh3;
}

void SimulationWidget::setDrawBodyMesh3(bool drawBodyMesh3)
{
    m_drawBodyMesh3 = drawBodyMesh3;
}

bool SimulationWidget::getDrawBodyMesh2() const
{
    return m_drawBodyMesh2;
}

void SimulationWidget::setDrawBodyMesh2(bool drawBodyMesh2)
{
    m_drawBodyMesh2 = drawBodyMesh2;
}

bool SimulationWidget::getDrawBodyMesh1() const
{
    return m_drawBodyMesh1;
}

void SimulationWidget::setDrawBodyMesh1(bool drawBodyMesh1)
{
    m_drawBodyMesh1 = drawBodyMesh1;
}

bool SimulationWidget::intersectModel(float winX, float winY)
{
    m_hits.clear();
    m_hitsIndexByZ.clear();
    std::vector<pgd::Vector3> intersectionCoordList;
    std::vector<size_t> intersectionIndexList;
    bool hit;
    for (auto &&drawableIter : m_drawables)
    {
        for (auto &&facetedObjectIter : drawableIter->facetedObjectList())
        {
            QMatrix4x4 mvpMatrix = m_proj * m_view * facetedObjectIter->model();
            bool invertible;
            QMatrix4x4 unprojectMatrix = mvpMatrix.inverted(&invertible);
            if (!invertible) // usually because the scale is zero so not an error condition
            {
                qDebug() << "mvpMatrix matrix not invertible: " << drawableIter->name().c_str();
                qDebug() << "m_proj " << m_proj;
                qDebug() << "m_view " << m_view;
                qDebug() << "facetedObjectIter->model() " << facetedObjectIter->model();
                break;
            }
            QVector4D screenPoint(winX, winY, -1, 1);
            QVector4D nearPoint4D = unprojectMatrix * screenPoint;
            screenPoint.setZ(+1);
            QVector4D farPoint4D = unprojectMatrix * screenPoint;
            QVector3D rayOrigin = nearPoint4D.toVector3DAffine();
            QVector3D rayVector = farPoint4D.toVector3DAffine() - rayOrigin;
            pgd::Vector3 origin(double(rayOrigin.x()), double(rayOrigin.y()), double(rayOrigin.z()));
            pgd::Vector3 vector(double(rayVector.x()), double(rayVector.y()), double(rayVector.z()));
            pgd::Vector3 vectorNorm = vector / vector.Magnitude();

            intersectionCoordList.clear();
            intersectionIndexList.clear();
            hit = facetedObjectIter->FindIntersection(origin, vectorNorm, &intersectionCoordList, &intersectionIndexList);
            if (hit)
            {
                for (size_t i = 0; i < intersectionCoordList.size(); i++)
                {
                    auto newHits = std::make_unique<IntersectionHits>();
                    newHits->setDrawable(drawableIter);
                    newHits->setFacetedObject(facetedObjectIter);
                    newHits->setTriangleIndex(intersectionIndexList[i]);
                    newHits->setModelLocation(intersectionCoordList[i]);
                    QVector3D modelIntersection(float(intersectionCoordList[i].x), float(intersectionCoordList[i].y), float(intersectionCoordList[i].z));
                    QVector3D screenIntersection = mvpMatrix * modelIntersection;
                    if (screenIntersection.z() < -1.0f || screenIntersection.z() > 1.0f) continue; // this means that only visible intersections are allowed
                    QVector3D worldIntersection = facetedObjectIter->model() * modelIntersection;
                    newHits->setWorldLocation(pgd::Vector3(double(worldIntersection.x()), double(worldIntersection.y()), double(worldIntersection.z())));
                    newHits->setScreenLocation(pgd::Vector3(double(screenIntersection.x()), double(screenIntersection.y()), double(screenIntersection.z())));
                    m_hits.push_back(std::move(newHits));
                }
            }
        }
    }
    // now handle the non-drawables
    std::vector<FacetedObject *> facetedObjectList = { m_cursor3D.get(), m_globalAxes.get() };
    for (auto &&facetedObjectIter : facetedObjectList)
    {
        QMatrix4x4 mvpMatrix = m_proj * m_view * facetedObjectIter->model();
        bool invertible;
        QMatrix4x4 unprojectMatrix = mvpMatrix.inverted(&invertible);
        if (!invertible) break; // usually because the scale is zero so not an error condition
        QVector4D screenPoint(winX, winY, -1, 1);
        QVector4D nearPoint4D = unprojectMatrix * screenPoint;
        screenPoint.setZ(+1);
        QVector4D farPoint4D = unprojectMatrix * screenPoint;
        QVector3D rayOrigin = nearPoint4D.toVector3DAffine();
        QVector3D rayVector = farPoint4D.toVector3DAffine() - rayOrigin;
        pgd::Vector3 origin(double(rayOrigin.x()), double(rayOrigin.y()), double(rayOrigin.z()));
        pgd::Vector3 vector(double(rayVector.x()), double(rayVector.y()), double(rayVector.z()));
        pgd::Vector3 vectorNorm = vector / vector.Magnitude();

        intersectionCoordList.clear();
        intersectionIndexList.clear();
        hit = facetedObjectIter->FindIntersection(origin, vectorNorm, &intersectionCoordList, &intersectionIndexList);
        if (hit)
        {
            for (size_t i = 0; i < intersectionCoordList.size(); i++)
            {
                auto newHits = std::make_unique<IntersectionHits>();
                newHits->setFacetedObject(facetedObjectIter);
                newHits->setTriangleIndex(intersectionIndexList[i]);
                newHits->setModelLocation(intersectionCoordList[i]);
                QVector3D modelIntersection(float(intersectionCoordList[i].x), float(intersectionCoordList[i].y), float(intersectionCoordList[i].z));
                QVector3D screenIntersection = mvpMatrix * modelIntersection;
                if (screenIntersection.z() < -1.0f || screenIntersection.z() > 1.0f) continue; // this means that only visible intersections are allowed
                QVector3D worldIntersection = facetedObjectIter->model() * modelIntersection;
                newHits->setWorldLocation(pgd::Vector3(double(worldIntersection.x()), double(worldIntersection.y()), double(worldIntersection.z())));
                newHits->setScreenLocation(pgd::Vector3(double(screenIntersection.x()), double(screenIntersection.y()), double(screenIntersection.z())));
                m_hits.push_back(std::move(newHits));
            }
        }
    }

#ifdef QT_DEBUG
    qDebug() << "SimulationWidget::intersectModel m_hits.size() = " << m_hits.size();
    qDebug() << "SimulationWidget::intersectModel unsorted";
    for (size_t i = 0; i < m_hits.size(); i++)
    {
        std::stringstream ss;
        if (dynamic_cast<DrawBody *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " body " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawFluidSac *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " fluidSac " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawGeom *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " geom " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawJoint *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " joint " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawMarker *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " marker " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawMuscle *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " " << m_hits[i]->drawable()->name();
        qDebug() << ss.str().c_str();
    }
#endif

    // I'd have thought sorting this would work but it seems to cause problems
    // std::sort(m_hits.begin(), m_hits.end(), [](const std::unique_ptr<IntersectionHits> &a, const std::unique_ptr<IntersectionHits> &b) -> bool { return *a < *b; });
    // so instead lets create a array of indices that point to the locations in order
    m_hitsIndexByZ.resize(m_hits.size());
    std::iota(m_hitsIndexByZ.begin(), m_hitsIndexByZ.end(), 0); // fills index list from 0 to size() - 1
    // passing this to the lambda function allows it to access all the members
    // and using the comparison on mhits means that m_hitsIndexByZ gets sorted in the order of m_hits
    std::stable_sort(m_hitsIndexByZ.begin(), m_hitsIndexByZ.end(), [this](const size_t i1, const size_t i2) { return m_hits[i1]->screenLocation().z < m_hits[i2]->screenLocation().z; });

    #ifdef QT_DEBUG
    qDebug() << "SimulationWidget::intersectModel sorted";
    for (size_t j = 0; j < m_hits.size(); j++)
    {
        std::stringstream ss;
        size_t i = m_hitsIndexByZ[j];
        if (dynamic_cast<DrawBody *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " body " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawFluidSac *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " fluidSac " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawGeom *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " geom " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawJoint *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " joint " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawMarker *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " marker " << m_hits[i]->drawable()->name();
        if (dynamic_cast<DrawMuscle *>(m_hits[i]->drawable()))
            ss << i << " " << m_hits[i]->screenLocation() << " " << m_hits[i]->worldLocation() << " " << m_hits[i]->drawable()->name();
        qDebug() << ss.str().c_str();
    }
#endif
    return (m_hits.size() != 0);
}

std::map<std::string, DrawMarker *> *SimulationWidget::getDrawMarkerMap()
{
    return &m_drawMarkerMap;
}

std::map<std::string, DrawFluidSac *> *SimulationWidget::getDrawFluidSacMap()
{
    return &m_drawFluidSacMap;
}

std::map<std::string, DrawMuscle *> *SimulationWidget::getDrawMuscleMap()
{
    return &m_drawMuscleMap;
}

std::map<std::string, DrawGeom *> *SimulationWidget::getDrawGeomMap()
{
    return &m_drawGeomMap;
}

std::map<std::string, DrawJoint *> *SimulationWidget::getDrawJointMap()
{
    return &m_drawJointMap;
}

std::map<std::string, DrawBody *> *SimulationWidget::getDrawBodyMap()
{
    return &m_drawBodyMap;
}

QString SimulationWidget::getLastMenuItem() const
{
    return m_lastMenuItem;
}

//void SimulationWidget::AddExtraObjectToDraw(const std::string &name, std::shared_ptr<FacetedObject> object)
//{
//    m_extraObjectsToDrawMap[name] = object;
//}

//size_t SimulationWidget::DeleteExtraObjectToDraw(const std::string &name)
//{
//    // returns the number of items removed from the map (will be 0 or 1)
//    return (m_extraObjectsToDrawMap.erase(name));
//}

//std::shared_ptr<FacetedObject> SimulationWidget::GetExtraObjectToDraw(const std::string &name)
//{
//    auto iter = m_extraObjectsToDrawMap.find(name);
//    if (iter != m_extraObjectsToDrawMap.end()) return iter->second;
//    return nullptr;
//}

bool SimulationWidget::halfTransparency() const
{
    return m_halfTransparency;
}

void SimulationWidget::setHalfTransparency(bool halfTransparency)
{
    m_halfTransparency = halfTransparency;
}

bool SimulationWidget::normals() const
{
    return m_normals;
}

void SimulationWidget::setNormals(bool normals)
{
    m_normals = normals;
}

float SimulationWidget::cursor3DNudge() const
{
    return m_cursor3DNudge;
}

void SimulationWidget::setCursor3DNudge(float cursor3DNudge)
{
    m_cursor3DNudge = cursor3DNudge;
}

float SimulationWidget::cursorRadius() const
{
    return m_cursorRadius;
}

void SimulationWidget::setCursorRadius(float cursorRadius)
{
    m_cursorRadius = cursorRadius;
}

float SimulationWidget::upZ() const
{
    return m_upZ;
}

void SimulationWidget::setUpZ(float upZ)
{
    m_upZ = upZ;
}

float SimulationWidget::upY() const
{
    return m_upY;
}

void SimulationWidget::setUpY(float upY)
{
    m_upY = upY;
}

float SimulationWidget::upX() const
{
    return m_upX;
}

void SimulationWidget::setUpX(float upX)
{
    m_upX = upX;
}

Simulation *SimulationWidget::simulation() const
{
    return m_simulation;
}

void SimulationWidget::setSimulation(Simulation *simulation)
{
    if (simulation == m_simulation) return;
    m_drawBodyMap.clear();
    m_drawJointMap.clear();
    m_drawGeomMap.clear();
    m_drawMuscleMap.clear();
    m_drawFluidSacMap.clear();
    m_drawMarkerMap.clear();
    m_drawables.clear();
    m_simulation = simulation;
}

bool SimulationWidget::wireFrame() const
{
    return m_wireFrame;
}

void SimulationWidget::setWireFrame(bool wireFrame)
{
    m_wireFrame = wireFrame;
}

bool SimulationWidget::boundingBox() const
{
    return m_boundingBox;
}

void SimulationWidget::setBoundingBox(bool boundingBox)
{
    m_boundingBox = boundingBox;
}

bool SimulationWidget::orthographicProjection() const
{
    return m_orthographicProjection;
}

void SimulationWidget::setOrthographicProjection(bool orthographicProjection)
{
    m_orthographicProjection = orthographicProjection;
}

float SimulationWidget::cameraDistance() const
{
    return m_cameraDistance;
}

void SimulationWidget::setCameraDistance(float cameraDistance)
{
    m_cameraDistance = cameraDistance;
}

float SimulationWidget::FOV() const
{
    return m_FOV;
}

void SimulationWidget::setFOV(float FOV)
{
    m_FOV = FOV;
}

float SimulationWidget::cameraVecX() const
{
    return m_cameraVecX;
}

void SimulationWidget::setCameraVecX(float cameraVecX)
{
    m_cameraVecX = cameraVecX;
}

float SimulationWidget::cameraVecY() const
{
    return m_cameraVecY;
}

void SimulationWidget::setCameraVecY(float cameraVecY)
{
    m_cameraVecY = cameraVecY;
}

float SimulationWidget::cameraVecZ() const
{
    return m_cameraVecZ;
}

void SimulationWidget::setCameraVecZ(float cameraVecZ)
{
    m_cameraVecZ = cameraVecZ;
}

float SimulationWidget::COIx() const
{
    return m_COIx;
}

void SimulationWidget::setCOIx(float COIx)
{
    m_COIx = COIx;
}

float SimulationWidget::COIy() const
{
    return m_COIy;
}

void SimulationWidget::setCOIy(float COIy)
{
    m_COIy = COIy;
}

float SimulationWidget::COIz() const
{
    return m_COIz;
}

void SimulationWidget::setCOIz(float COIz)
{
    m_COIz = COIz;
}

float SimulationWidget::frontClip() const
{
    return m_frontClip;
}

void SimulationWidget::setFrontClip(float frontClip)
{
    m_frontClip = frontClip;
}

float SimulationWidget::backClip() const
{
    return m_backClip;
}

void SimulationWidget::setBackClip(float backClip)
{
    m_backClip = backClip;
}

QMatrix4x4 SimulationWidget::view() const
{
    return m_view;
}

QMatrix4x4 SimulationWidget::proj() const
{
    return m_proj;
}

QOpenGLShaderProgram *SimulationWidget::facetedObjectShader() const
{
    return m_facetedObjectShader;
}

QOpenGLShaderProgram *SimulationWidget::fixedColourObjectShader() const
{
    return m_fixedColourObjectShader;
}


