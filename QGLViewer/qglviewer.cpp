#include "domUtils.h"
#include "qglviewer.h"
#include "camera.h"
#include "keyFrameInterpolator.h"

#include <qapplication.h>
#include <qfileinfo.h>
#include <qdatetime.h>
#include <qmessagebox.h>
#include <qpushbutton.h>
#include <qtabwidget.h>
#include <qtimer.h>
#include <qimage.h>
#include <qdir.h>

#if QT_VERSION >= 0x030000
# include <qtextedit.h>
# include <qvaluevector.h>
# include "qkeysequence.h"
#else
# include <qtextview.h>
  typedef QTextView QTextEdit;
# include <qtextstream.h>
# include <algorithm>
# include "keySequence.h"
#endif

using namespace std;
using namespace qglviewer;

// Static private variable
QPtrList<QGLViewer> QGLViewer::QGLViewerPool_;

/*! \mainpage

libQGLViewer is a free C++ library based on Qt that enables the quick creation of OpenGL 3D viewers.
It features a powerful camera trackball and simple applications simply require an implementation of
the <code>draw()</code> method. This makes it a tool of choice for OpenGL beginners and
assignments. It provides screenshot saving, mouse manipulated frames, stereo display, interpolated
keyFrames, object selection, and much
<a href="http://artis.imag.fr/Members/Gilles.Debunne/QGLViewer/features.html">more</a>. It is fully
customizable and easy to extend to create complex applications, with a possible Qt GUI.

libQGLViewer is <i>not</i> a 3D viewer that can be used directly to view 3D scenes in various
formats. It is more likely to be the starting point for the coding of such a viewer.

libQGLViewer is based on the Qt toolkit and hence compiles on any architecture (Unix-Linux, Mac,
Windows, ...). Full <a
href="http://artis.imag.fr/Members/Gilles.Debunne/QGLViewer/refManual/hierarchy.html">reference
documentation</a> and many <a
href="http://artis.imag.fr/Members/Gilles.Debunne/QGLViewer/examples/index.html">examples</a> are
provided.

See the <a href="http://artis.imag.fr/Members/Gilles.Debunne/QGLViewer">project main page</a> for
details on the project and installation steps. */

void QGLViewer::defaultConstructor()
{
  //      - - -  W A R N I N G  - - -
  // This method should not call initializeGL(). Otherwise, as we are in the
  // base class constructor, the user-defined init() would never be called.
  // The different QGLViewer::setXXX are hence protected, so that updateGL is not called.
  // The different constructor code should then be EMPTY.
  updateGLOK_ = false;

  // Test OpenGL context
  // if (glGetString(GL_VERSION) == 0)
    // qWarning("Unable to get OpenGL version, context may not be available - Check your configuration");

  QGLViewer::QGLViewerPool_.append(this);

  camera_ = new Camera();
  setCamera(camera());

  setFocusPolicy(QWidget::StrongFocus);

  setDefaultShortcuts();
  setDefaultMouseBindings();

  setSnapshotFilename("snapshot");
  initializeSnapshotFormats();
  setSnapshotCounter(0);
  setSnapshotQuality(95);

  fpsTime_.start();
  fpsCounter_		= 0;
  f_p_s_		= 0.0;
  fpsString_		= "?Hz";
  visualHint_		= 0;
  previousPathId_	= 0;
  // prevPos_ is not initialized since pos() is not meaningful here. It will be set by setFullScreen().

  // #CONNECTION# default values in initFromDOMElement()
  manipulatedFrame_ = NULL;
  manipulatedFrameIsACamera_ = false;
  mouseGrabberIsAManipulatedFrame_ = false;
  mouseGrabberIsAManipulatedCameraFrame_ = false;
  displayMessage_ = false;
  connect(&messageTimer_, SIGNAL(timeout()), SLOT(hideMessage()));
  helpWidget_ = NULL;
  setMouseGrabber(NULL);

  setSceneRadius(1.0);
  showEntireScene();
  setStateFileName(".qglviewer.xml");

  // #CONNECTION# default values in initFromDOMElement()
  setAxisIsDrawn(false);
  setGridIsDrawn(false);
  setZBufferIsDisplayed(false);
  setFPSIsDisplayed(false);
  setCameraIsEdited(false);
  setTextIsEnabled(true);
  setStereoDisplay(false);
  setFullScreen(false);

  animationTimerId_ = 0;
  stopAnimation();
  setAnimationPeriod(40); // 25Hz

  selectBuffer_ = NULL;
  setSelectBufferSize(4*1000);
  setSelectRegionWidth(3);
  setSelectRegionHeight(3);
  setSelectedName(-1);

  bufferTextureId_ = 0;
  bufferTextureMaxU_ = 0.0;
  bufferTextureMaxV_ = 0.0;
  bufferTextureWidth_ = 0;
  bufferTextureHeight_ = 0;
  previousBufferTextureFormat_ = 0;
  previousBufferTextureInternalFormat_ = 0;
}

/*! Implementation of the \c QGLWidget associated constructor.

 The display flags, scene parameters, associated objects... are all set to their default values. See
 documentation.

 If the \p shareWidget parameter points to a valid \c QGLWidget, the QGLViewer will share the OpenGL
 context with \p shareWidget (see isSharing()). */
QGLViewer::QGLViewer(QWidget *parent, const char *name, const QGLWidget* shareWidget, WFlags flags)
  : QGLWidget(parent, name, shareWidget, flags)
{
  // Read the defaultConstructor warning !!
  defaultConstructor();
}

/*! Implementation of the \c QGLWidget associated constructor.

 Same as QGLViewer(), but a \c Qt::QGLFormat can be provided. This is for instance needed for stereo
 display as is illustrated in the <a href="../examples/stereoViewer.html">stereoViewer
 example</a>. */
QGLViewer::QGLViewer(const QGLFormat& format, QWidget *parent, const char *name, const QGLWidget* shareWidget, WFlags flags)
  : QGLWidget(format, parent, name, shareWidget, flags)
{
  // Read the defaultConstructor warning !!
  defaultConstructor();
}

/*! Implementation of the \c QGLWidget associated constructor.

 Same as QGLViewer(), but a \c Qt::QGLContext can be provided so that viewers share GL contexts,
 even with \c QGLContext sub-classes.

 \note This constructor is only available with Qt versions greater or equal than 3.2. The provided
 \p context is simply ignored otherwise. */
QGLViewer::QGLViewer(QGLContext* context, QWidget* parent, const char* name, const QGLWidget* shareWidget, WFlags flags)
#if QT_VERSION >= 0x030200
  : QGLWidget(context, parent, name, shareWidget, flags) {
#else
  : QGLWidget(parent, name, shareWidget, flags) {
    context = NULL;
#endif
  // Read the defaultConstructor warning !!
  defaultConstructor();
}

/*! Virtual destructor.

Removes viewer from QGLViewerPool() and releases allocated memory. The camera() is deleted and
should be copied before if it is shared. */
QGLViewer::~QGLViewer()
{
  // See closeEvent comment. Destructor is called (and not closeEvent) only when the widget is embedded.
  // Hence we saveToFile here. It is however a bad idea if virtual domElement() has been overloaded !
  // if (parent())
    // saveStateToFileForAllViewers();
  QGLViewer::QGLViewerPool_.removeRef(this);
  delete camera();
  delete[] selectBuffer_;
  delete helpWidget_;
}


static QString QGLViewerVersionString()
{
  return QString::number((QGLVIEWER_VERSION & 0xff0000) >> 16) + "." +
    QString::number((QGLVIEWER_VERSION & 0x00ff00) >> 8) + "." +
    QString::number(QGLVIEWER_VERSION & 0x0000ff);
}

/*! Opens an about dialog.

Default implementation displays libQGLViewer version, copyright notice and web site. */
void QGLViewer::aboutQGLViewer()
{
  QMessageBox mb("About libQGLViewer",
		 QString("libQGLViewer, version ")+QGLViewerVersionString()+QString(".<br>"
		 "A versatile 3D viewer based on OpenGL and Qt.<br>"
		 "Copyright 2002-2005 Gilles Debunne.<br>"
		 "<code>http://artis.imag.fr/Software/QGLViewer</code>"),
		 QMessageBox::Information,
		 QMessageBox::Ok,
		 QMessageBox::NoButton,
		 QMessageBox::NoButton,
		 this);

#include "icon.h"
  QImage img(qglviewer_data, 79, 84, 8, qglviewer_ctable, 256, QImage::BigEndian);
  img.setAlphaBuffer(true);

#if QT_VERSION < 0x030000
  QPixmap pixmap;
  pixmap.convertFromImage(img);
  mb.setIconPixmap(pixmap);
#else
  mb.setIconPixmap(QPixmap(img));
#endif
  mb.setTextFormat(Qt::RichText);
  mb.exec();
}

/*! Initializes the QGLViewer OpenGL context and then calls user-defined init().

 This method is automatically called once, before the first call to paintGL().

 Overload init() instead of this method to modify viewer specific OpenGL state or to create display
 lists.

 To make beginners' life easier and to simplify the examples, this method slightly modifies the
 standard OpenGL state:
 \code
 glEnable(GL_LIGHT0);
 glEnable(GL_LIGHTING);
 glEnable(GL_DEPTH_TEST);
 glEnable(GL_COLOR_MATERIAL);
 \endcode

 If you port an existing application to QGLViewer and your display changes, you probably want to
 disable these flags in init() to get back to a standard OpenGL state. */
void QGLViewer::initializeGL()
{
  if (updateGLOK_)
    qWarning("Internal debug: initializeGL() is called in QGLViewer constructor.");

  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_COLOR_MATERIAL);

  // Default colors
  setForegroundColor(QColor(180, 180, 180));
  setBackgroundColor(QColor(51, 51, 51));

  // Clear the buffer where we're going to draw
  if (format().stereo())
    {
      glDrawBuffer(GL_BACK_RIGHT);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glDrawBuffer(GL_BACK_LEFT);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
  else
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Calls user defined method. Default emits a signal.
  init();

  // Give time to glInit to finish and then call setFullScreen().
  if (isFullScreen())
    QTimer::singleShot( 100, this, SLOT(delayedFullScreen()) );

  updateGLOK_ = true;
}

/*! Main paint method, inherited from \c QGLWidget.

 Calls the following methods, in that order:
 \arg preDraw() (or preDrawStereo() if viewer displaysInStereo()) : places the camera in the world coordinate system.
 \arg draw() (or fastDraw() when the camera is manipulated) : main drawing method. Should be overloaded.
 \arg postDraw() : display of visual hints (world axis, FPS...) */
void QGLViewer::paintGL()
{
  updateGLOK_ = false;
  if (displaysInStereo())
    {
      for (int view=1; view>=0; --view)
	{
	  // Clears screen, set model view matrix with shifted matrix for ith buffer
	  preDrawStereo(view);
	  // Used defined method. Default is empty
	  if (camera()->frame()->isManipulated())
	    fastDraw();
	  else
	    draw();
	  postDraw();
	}
    }
  else
    {
      // Clears screen, set model view matrix...
      preDraw();
      // Used defined method. Default calls draw()
      if (camera()->frame()->isManipulated())
	fastDraw();
      else
	draw();
      // Add visual hints: axis, camera, grid...
      postDraw();
    }
  updateGLOK_ = true;
  emit drawFinished(true);
}

/*! Sets OpenGL state before draw().

 Default behavior clears screen and sets the projection and modelView matrices:
 \code
 glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

 camera()->loadProjectionMatrix();
 camera()->loadModelViewMatrix();
 \endcode

 Emits the drawNeeded() signal once this is done (see the <a href="../examples/callback.html">callback example</a>). */
void QGLViewer::preDraw()
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // GL_PROJECTION matrix
  camera()->loadProjectionMatrix();
  // GL_MODELVIEW matrix
  camera()->loadModelViewMatrix();

  emit drawNeeded();
}

/*! Called after draw() to draw viewer visual hints.

 Default implementation displays axis, grid, FPS... when the respective flags are sets.

 See the <a href="../examples/multiSelect.html">multiSelect</a> and <a
 href="../examples/contribs.html#thumbnail">thumbnail</a> examples for an overloading illustration.

 The GLContext (color, LIGHTING, BLEND...) should \e not be modified by this method, so that in
 draw(), the user can rely on the OpenGL context. Respect this convention (by pushing/popping the
 different attributes) if you overload this method. */
void QGLViewer::postDraw()
{
  // Reset model view matrix to world coordinates origin
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  camera()->loadModelViewMatrix();
  // TODO restore model loadProjectionMatrixStereo

  // Save OpenGL state
  glPushAttrib(GL_ALL_ATTRIB_BITS);

  // Set neutral GL state
  glDisable(GL_TEXTURE_1D);
  glDisable(GL_TEXTURE_2D);
#ifdef GL_TEXTURE_3D  // OpenGL 1.2 Only...
  glDisable(GL_TEXTURE_3D);
#endif

  glDisable(GL_TEXTURE_GEN_Q);
  glDisable(GL_TEXTURE_GEN_R);
  glDisable(GL_TEXTURE_GEN_S);
  glDisable(GL_TEXTURE_GEN_T);

#ifdef GL_RESCALE_NORMAL  // OpenGL 1.2 Only...
  glEnable(GL_RESCALE_NORMAL);
#endif

  glDisable(GL_COLOR_MATERIAL);
  qglColor(foregroundColor());

  if (cameraIsEdited())
    camera()->drawAllPaths();

  // Revolve Around Point, line when camera rolls, zoom region
  drawVisualHints();

  if (gridIsDrawn()) drawGrid(camera()->sceneRadius());
  if (axisIsDrawn()) drawAxis(camera()->sceneRadius());

  // FPS computation
  const unsigned int maxCounter = 20;
  if (++fpsCounter_ == maxCounter)
    {
      f_p_s_ = 1000.0 * maxCounter / fpsTime_.restart();
      fpsString_ = QString("%1Hz").arg(f_p_s_, 0, 'f', ((f_p_s_ < 10.0)?1:0));
      fpsCounter_ = 0;
    }

  // Restore foregroundColor
  float color[4];
  color[0] = foregroundColor().red()   / 255.0;
  color[1] = foregroundColor().green() / 255.0;
  color[2] = foregroundColor().blue()  / 255.0;
  color[3] = 1.0;
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
  glDisable(GL_LIGHTING);

  if (FPSIsDisplayed()) displayFPS();
  if (displayMessage_) drawText(10, height()-10,  message_);
  if (zBufferIsDisplayed())
    {
      copyBufferToTexture(GL_DEPTH_COMPONENT);
      displayZBuffer();
    }

  // Restore GL state
  glPopAttrib();
  glPopMatrix();
}

/*! Called before draw() (instead of preDraw()) when viewer displaysInStereo().

 Same as preDraw() except that the glDrawBuffer() is set to \c GL_BACK_LEFT or \c GL_BACK_RIGHT
 depending on \p leftBuffer, and it uses qglviewer::Camera::loadProjectionMatrixStereo() and
 qglviewer::Camera::loadModelViewMatrixStereo() instead. */
void QGLViewer::preDrawStereo(bool leftBuffer)
{
  // Set buffer to draw in
  // Seems that SGI and Crystal Eyes are not synchronized correctly !
  // That's why we don't draw in the appropriate buffer...
  if (!leftBuffer)
    glDrawBuffer(GL_BACK_LEFT);
  else
    glDrawBuffer(GL_BACK_RIGHT);

  // Clear the buffer where we're going to draw
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // GL_PROJECTION matrix
  camera()->loadProjectionMatrixStereo(leftBuffer);
  // GL_MODELVIEW matrix
  camera()->loadModelViewMatrixStereo(leftBuffer);

  emit drawNeeded();
}

/*! Draws a simplified version of the scene to guarantee interactive camera displacements.

 This method is called instead of draw() when the qglviewer::Camera::frame() is
 qglviewer::ManipulatedCameraFrame::isManipulated(). Default implementation simply calls draw().

 Overload this method if your scene is too complex to allow for interactive camera manipulation. See
 the <a href="../examples/fastDraw.html">fastDraw example</a> for an illustration. */
void QGLViewer::fastDraw()
{
  draw();
}

/*! Starts (\p edit = \c true, default) or stops (\p edit=\c false) the edition of the camera().

 Current implementation is limited to paths display. Get current state using cameraIsEdited().

 \attention This method sets the qglviewer::Camera::zClippingCoefficient() to 5.0 when \p edit is \c
 true, so that the Camera paths (see qglviewer::Camera::keyFrameInterpolator()) are not clipped. It
 restores the previous value when \p edit is \c false. */
void QGLViewer::setCameraIsEdited(bool edit)
{
  cameraIsEdited_ = edit;
  if (edit)
    {
      previousCameraZClippingCoefficient_ = camera()->zClippingCoefficient();
      camera()->setZClippingCoefficient(5.0);
    }
  else
    camera()->setZClippingCoefficient(previousCameraZClippingCoefficient_);

  emit cameraIsEditedChanged(edit);

  if (updateGLOK_)
    updateGL();
}

// Key bindings. 0 means not defined
void QGLViewer::setDefaultShortcuts()
{
  // D e f a u l t   a c c e l e r a t o r s
  setShortcut(DRAW_AXIS,		Key_A);
  setShortcut(DRAW_GRID,		Key_G);
  setShortcut(DISPLAY_FPS,		Key_F);
  setShortcut(DISPLAY_Z_BUFFER,		Key_Z);
  setShortcut(ENABLE_TEXT,		SHIFT+Key_Question);
  setShortcut(EXIT_VIEWER,		Key_Escape);
  setShortcut(SAVE_SCREENSHOT,	CTRL+Key_S);
  setShortcut(CAMERA_MODE,		Key_Space);
  setShortcut(FULL_SCREEN,		ALT+Key_Return);
  setShortcut(STEREO,		Key_S);
  setShortcut(ANIMATION,		Key_Return);
  setShortcut(HELP,			Key_H);
  setShortcut(EDIT_CAMERA,		Key_C);
  setShortcut(MOVE_CAMERA_LEFT,	Key_Left);
  setShortcut(MOVE_CAMERA_RIGHT,	Key_Right);
  setShortcut(MOVE_CAMERA_UP,	Key_Up);
  setShortcut(MOVE_CAMERA_DOWN,	Key_Down);
  setShortcut(INCREASE_FLYSPEED,	Key_Plus);
  setShortcut(DECREASE_FLYSPEED,	Key_Minus);

  keyboardActionDescription_[DISPLAY_FPS] = 		"Toggles the display of the FPS";
  keyboardActionDescription_[DISPLAY_Z_BUFFER] = 	"Toggles the display of the z-buffer";
  keyboardActionDescription_[SAVE_SCREENSHOT] = 	"Saves a screenshot";
  keyboardActionDescription_[FULL_SCREEN] = 		"Toggles full screen display";
  keyboardActionDescription_[DRAW_AXIS] = 		"Toggles the display of the world axis";
  keyboardActionDescription_[DRAW_GRID] = 		"Toggles the display of the XY grid";
  keyboardActionDescription_[CAMERA_MODE] = 		"Changes camera mode (revolve or fly)";
  keyboardActionDescription_[STEREO] = 			"Toggles stereo display";
  keyboardActionDescription_[HELP] = 			"Opens this help window";
  keyboardActionDescription_[ANIMATION] = 		"Starts/stops the animation";
  keyboardActionDescription_[EDIT_CAMERA] = 		"Toggles camera paths display"; // TODO change
  keyboardActionDescription_[ENABLE_TEXT] = 		"Toggles the display of the text";
  keyboardActionDescription_[EXIT_VIEWER] =		"Exits program";
  keyboardActionDescription_[MOVE_CAMERA_LEFT] = 	"Moves camera left";
  keyboardActionDescription_[MOVE_CAMERA_RIGHT] = 	"Moves camera right";
  keyboardActionDescription_[MOVE_CAMERA_UP] = 		"Moves camera up";
  keyboardActionDescription_[MOVE_CAMERA_DOWN] = 	"Moves camera down";
  keyboardActionDescription_[INCREASE_FLYSPEED] = 	"Increases fly speed";
  keyboardActionDescription_[DECREASE_FLYSPEED] = 	"Decreases fly speed";

  // K e y f r a m e s   s h o r t c u t   k e y s
  setPathKey(Qt::Key_F1,   1);
  setPathKey(Qt::Key_F2,   2);
  setPathKey(Qt::Key_F3,   3);
  setPathKey(Qt::Key_F4,   4);
  setPathKey(Qt::Key_F5,   5);
  setPathKey(Qt::Key_F6,   6);
  setPathKey(Qt::Key_F7,   7);
  setPathKey(Qt::Key_F8,   8);
  setPathKey(Qt::Key_F9,   9);
  setPathKey(Qt::Key_F10, 10);
  setPathKey(Qt::Key_F11, 11);
  setPathKey(Qt::Key_F12, 12);

  setAddKeyFrameStateKey(Qt::AltButton);
  setPlayPathStateKey(Qt::NoButton);
}

// M o u s e   b e h a v i o r
void QGLViewer::setDefaultMouseBindings()
{
  const Qt::ButtonState frameStateKey = Qt::ControlButton;
  //#CONNECTION# toggleCameraMode()
  for (int handler=0; handler<2; ++handler)
    {
      MouseHandler mh = (MouseHandler)(handler);
      Qt::ButtonState state = Qt::NoButton;
      if (mh == FRAME)
	state = frameStateKey;

      setMouseBinding(state | Qt::LeftButton,  mh, ROTATE);
      setMouseBinding(state | Qt::MidButton,   mh, ZOOM);
      setMouseBinding(state | Qt::RightButton, mh, TRANSLATE);

      setMouseBinding(state | Qt::LeftButton  | Qt::MidButton,  mh, SCREEN_ROTATE);
      setMouseBinding(state | Qt::RightButton | Qt::MidButton,  mh, SCREEN_TRANSLATE);

      setWheelBinding(state, mh, ZOOM);
    }

  // Z o o m   o n   r e g i o n
  setMouseBinding(Qt::ShiftButton | Qt::MidButton, CAMERA, ZOOM_ON_REGION);

  // S e l e c t
  setMouseBinding(Qt::ShiftButton | Qt::LeftButton, SELECT);

  // D o u b l e   c l i c k
  setMouseBinding(Qt::LeftButton,  ALIGN_CAMERA,      true);
  setMouseBinding(Qt::MidButton,   SHOW_ENTIRE_SCENE, true);
  setMouseBinding(Qt::RightButton, CENTER_SCENE,      true);

  setMouseBinding(frameStateKey | Qt::LeftButton,  ALIGN_FRAME,  true);
  setMouseBinding(frameStateKey | Qt::RightButton, CENTER_FRAME, true);

  // S p e c i f i c   d o u b l e   c l i c k s
  setMouseBinding(Qt::LeftButton,  RAP_FROM_PIXEL, true, Qt::RightButton);
  setMouseBinding(Qt::RightButton, RAP_IS_CENTER,  true, Qt::LeftButton);
  setMouseBinding(Qt::LeftButton,  ZOOM_ON_PIXEL,  true, Qt::MidButton);
  setMouseBinding(Qt::RightButton, ZOOM_TO_FIT,    true, Qt::MidButton);
}

/*! Associates a new qglviewer::Camera to the viewer.

You should only use this method when you derive a new class from qglviewer::Camera and want to use
one of its instances instead of the original class.

It you simply want to save and restore Camera positions, use qglviewer::Camera::addKeyFrameToPath()
and qglviewer::Camera::playPath() instead.

This method silently ignores NULL \p camera pointers. The calling method is responsible for deleting
the previous camera pointer in order to prevent memory leaks if needed.

The sceneRadius() and sceneCenter() of \p camera are set to the \e current QGLViewer values.

All the \p camera qglviewer::Camera::keyFrameInterpolator()
qglviewer::KeyFrameInterpolator::interpolated() signals are connected to the viewer updateGL() slot.
The connections with the previous viewer's camera are removed. */
void QGLViewer::setCamera(Camera* const camera)
{
  if (!camera)
    return;

  camera->setSceneRadius(sceneRadius());
  camera->setSceneCenter(sceneCenter());
  camera->setScreenWidthAndHeight(width(),height());

  // Disconnect current camera to this viewer.
  disconnect(this->camera()->frame(), SIGNAL(manipulated()), this, SLOT(updateGL()));
  disconnect(this->camera()->frame(), SIGNAL(spun()), this, SLOT(updateGL()));

  // Connect camera frame to this viewer.
  connect(camera->frame(), SIGNAL(manipulated()), SLOT(updateGL()));
  connect(camera->frame(), SIGNAL(spun()), SLOT(updateGL()));

  connectAllCameraKFIInterpolatedSignals(false);
  camera_ = camera;
  connectAllCameraKFIInterpolatedSignals();

  previousCameraZClippingCoefficient_ = this->camera()->zClippingCoefficient();
}

void QGLViewer::connectAllCameraKFIInterpolatedSignals(bool connection)
{
  for (QMap<int, KeyFrameInterpolator*>::ConstIterator it = camera()->kfi_.begin(), end=camera()->kfi_.end(); it != end; ++it)
    {
      if (connection)
	connect(camera()->keyFrameInterpolator(it.key()), SIGNAL(interpolated()), SLOT(updateGL()));
      else
	disconnect(camera()->keyFrameInterpolator(it.key()), SIGNAL(interpolated()), this, SLOT(updateGL()));
    }

  if (connection)
    connect(camera()->interpolationKfi_, SIGNAL(interpolated()), SLOT(updateGL()));
  else
    disconnect(camera()->interpolationKfi_, SIGNAL(interpolated()), this, SLOT(updateGL()));
}

/*! Draws a representation of \p light.

 Called in draw(), this method is useful to debug or display your light setup. Light drawing depends
 on the type of light (point, spot, directional).

 The method retrieves the light setup using \c glGetLightfv. Position and define your lights before
 calling this method.

 Light is drawn using its diffuse color. Disabled lights are not displayed.

 Drawing size is proportional to sceneRadius(). Use \p scale to rescale it.

 See the <a href="../examples/drawLight.html">drawLight example</a> for an illustration.

 \attention You need to enable \c GL_COLOR_MATERIAL before calling this method. \c glColor is set to
 the light diffuse color. */
void QGLViewer::drawLight(GLenum light, float scale) const
{
  static GLUquadric* quadric = gluNewQuadric();

  const float length = sceneRadius() / 5.0 * scale;

  GLboolean lightIsOn;
  glGetBooleanv(light, &lightIsOn);

  if (lightIsOn)
    {
      // All light values are given in eye coordinates
      glPushMatrix();
      glLoadIdentity();

      float color[4];
      glGetLightfv(light, GL_DIFFUSE, color);
      glColor4fv(color);

      float pos[4];
      glGetLightfv(light, GL_POSITION, pos);

      if (pos[3] != 0.0)
	{
	  glTranslatef(pos[0]/pos[3], pos[1]/pos[3], pos[2]/pos[3]);

	  GLfloat cutOff;
	  glGetLightfv(light, GL_SPOT_CUTOFF, &cutOff);
	  if (cutOff != 180.0)
	    {
	      GLfloat dir[4];
	      glGetLightfv(light, GL_SPOT_DIRECTION, dir);
	      glMultMatrixd(Quaternion(Vec(0,0,1), Vec(dir)).matrix());
	      QGLViewer::drawArrow(length);
	      gluCylinder(quadric, 0.0, 0.7 * length * sin(cutOff * M_PI / 180.0), 0.7 * length * cos(cutOff * M_PI / 180.0), 12, 1);
	    }
	  else
	    gluSphere(quadric, 0.2*length, 10, 10);
	}
      else
	{
	  // Directional light.
	  Vec dir(pos[0], pos[1], pos[2]);
	  dir.normalize();
	  Frame fr=Frame(camera()->cameraCoordinatesOf(4.0 * length * camera()->frame()->inverseTransformOf(dir)),
			 Quaternion(Vec(0,0,-1), dir));
	  glMultMatrixd(fr.matrix());
	  drawArrow(length);
	}

      glPopMatrix();
    }
}


/*! Draws \p text at position \p x, \p y (expressed in screen coordinates pixels, origin in the
  upper left corner of the widget).

  The default QApplication::font() is used to render the text when no \p fnt is specified. Use
  QApplication::setFont() to define this default font.

  You should disable \c GL_LIGHTING before this method so that colors are properly rendered.

  This method can be used in conjunction with the qglviewer::Camera::projectedCoordinatesOf()
  method to display a text attached to an object. In your draw() method use:
  \code
  qglviewer::Vec screenPos = camera()->projectedCoordinatesOf(myFrame.position());
  drawText((int)screenPos[0], (int)screenPos[1], "My Object");
  \endcode
  See the <a href="../examples/screenCoordSystem.html">screenCoordSystem example</a> for an illustration.

  Text is displayed only when textIsEnabled() (default). This mechanism allows the user to
  conveniently remove all the displayed text with a single keyboard shortcut.

  Use displayMessage() to drawText() for only a short amount of time.

  Use the QGLWidget::renderText(x,y,z, text) method (Qt version >= 3.1) to draw a text (fixed size,
  facing the camera) located at a specific 3D position instead of 2D screen coordinates.

  The \c GL_MODELVIEW and \c GL_PROJECTION matrices are not modified by this method.

  \attention This method uses display lists to render the characters, with an index that starts at
  2000 by default (see the QGLWidget::renderText() documentation). If you use more than 2000 Display
  Lists, they may overlap. Directly use QGLWidget::renderText() in that case, with a higher \c
  listBase parameter.

  \attention There is a problem with anti-aliased font with nVidia cards and Qt versions lower than
  3.3. Until this version, the \p fnt parameter is not taken into account to prevent a crash. It is
  replaced by a fixed font that should be compatible with the \c qtconfig anti-aliased font
  configuration (disable this option otherwise).

  \note This method calls QGLWidget::renderText() if your Qt version is at least 3.1, otherwise it
  uses GLUT. The Qt minimum version that disables GLUT is set by QT_VERSION_WITHOUT_GLUT in \c
  config \c .h. Default value is 3.1. Only the \p fnt size (set with QFont::setPixelSize() or
  QFont::setPointSize()) is taken into account with the GLUT version.

  \note With Qt versions < QT_VERSION_WITHOUT_GLUT, each call to drawText() changes the camera
  projection matrix and restores it back (using startScreenCoordinatesSystem() and
  stopScreenCoordinatesSystem()). If you call this method several times and it slows down your
  frame rate, consider factorizing the context changes. */
void QGLViewer::drawText(int x, int y, const QString& text, const QFont& fnt)
{
  if (!textIsEnabled())
    return;

#if QT_VERSION < QT_VERSION_WITHOUT_GLUT
  const GLfloat font_scale = 119.05f - 33.33f; // see glutStrokeCharacter man page

  startScreenCoordinatesSystem();

  // Anti-aliased characters
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glDisable(GL_LIGHTING);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_LINE_SMOOTH);
  glLineWidth(1.0);

  glTranslatef((GLfloat)x, (GLfloat)y, 0.0);
  const GLfloat scale = ((fnt.pixelSize()>0)?fnt.pixelSize():fnt.pointSize()) / font_scale;
  glScalef(scale, -scale, scale);

  for (uint i=0; i<text.length(); ++i)
    glutStrokeCharacter(GLUT_STROKE_ROMAN, text.at(i));

  glPopAttrib();

  stopScreenCoordinatesSystem();

#else

# if QT_VERSION < 0x030300 && defined Q_OS_UNIX
  // Fix bug with anti-aliased fonts on nVidia driver
  QFont newFont(fnt);
  newFont.setFamily("fixed");
  newFont.setRawMode(true);
  newFont.setPixelSize(10);
  newFont.setFixedPitch(true);
#if QT_VERSION >= 0x030200
  newFont.setStyleStrategy(QFont::OpenGLCompatible);
#endif
  newFont.setStyleHint(QFont::AnyStyle, QFont::PreferBitmap);
  renderText(x, y, text, newFont);
# else
  renderText(x, y, text, fnt);
# endif

#endif
}

/* Similar to drawText(), but the text is handled as a classical 3D object of the scene.

Although useful, this method is deprecated with recent Qt versions. Indeed, Qt renders text as
pixmaps that cannot be orientated. However, when GLUT is used instead of Qt (when your Qt version is
lower than 3.1, see QT_VERSION_WITHOUT_GLUT in config .h) orientated characters are possible and this
method will work.

\p pos and \p normal respectively represent the 3D coordinate of the text and the normal to the text
plane. They are expressed with respect to the \e current \c GL_MODELVIEW matrix.

If you want your text to always face the camera (normal parallel to camera()->viewDirection), use
QGLWidget::renderText(x,y,z).

See the <a href="../examples/draw3DText.html">draw3DText example</a> for an illustration. */
/*
 void QGLViewer::draw3DText(const Vec& pos, const Vec& normal, const QString& text, GLfloat height)
 {
 #if QT_VERSION < QT_VERSION_WITHOUT_GLUT
 if (!textIsEnabled())
 return;

 glMatrixMode(GL_MODELVIEW) ;
 glPushMatrix() ;

 const GLfloat font_scale = (119.05f - 33.33f) / 8; // see glutStrokeCharacter man page
 // const GLfloat font_scale = (119.05f - 33.33f) * 15.0f; // see glutStrokeCharacter man page

 static GLfloat lineWidth;
 glGetFloatv(GL_LINE_WIDTH, &lineWidth);

 glTranslatef(pos.x, pos.y, pos.z);
 glMultMatrixd(Quaternion(Vec(0.0, 0.0, 1.0), normal).matrix());

 glLineWidth(2.0);

 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
 glEnable(GL_BLEND);
 glEnable(GL_LINE_SMOOTH);

 const GLfloat scale = height / font_scale;
 glScalef(scale, scale, scale);

 for (uint i=0; i<text.length(); ++i)
 glutStrokeCharacter(GLUT_STROKE_ROMAN, text.at(i));

 glLineWidth(lineWidth);

 glMatrixMode(GL_MODELVIEW);
 glPopMatrix() ;
 #else
 static bool displayed = false;

 if (!displayed)
 {
   qWarning("draw3DText is not supported with Qt >= 3.1.");
   qWarning("Use QGLWidget::renderText() instead,");
   qWarning("or use the glut glutStrokeCharacter() method.");
   displayed = true;
 }

  Q_UNUSED(pos)
    Q_UNUSED(normal)
    Q_UNUSED(text)
    Q_UNUSED(height)
 #endif
 }
*/

/*! Briefly displays a message in the lower left corner of the widget. Convenient to provide
 feedback to the user.

 \p message is displayed during \p delay milliseconds (default is 2 seconds) using drawText().

 This method should not be called in draw(). If you want to display a text in each draw(), use
 drawText() instead.

 If this method is called when a message is already displayed, the new message replaces the old one.
 Use setTextIsEnabled() (default shortcut is '?') to enable or disable text (and hence messages)
 display. */
void QGLViewer::displayMessage(const QString& message, int delay)
{
  message_ = message;
  displayMessage_ = true;
  if (messageTimer_.isActive())
    messageTimer_.changeInterval(delay);
  else
    messageTimer_.start(delay, true);
  if (textIsEnabled() && updateGLOK_)
    updateGL();
}

void QGLViewer::hideMessage()
{
  displayMessage_ = false;
  if (textIsEnabled())
    updateGL();
}


/*! Displays the averaged currentFPS() frame rate in the upper left corner of the widget.

 updateGL() should be called in a loop in order to have a meaningful value (this is the case when
 you continuously move the camera using the mouse or when animationIsStarted()).
 setAnimationPeriod(0) to make this loop as fast as possible in order to reach and measure the
 maximum available frame rate.

 When FPSIsDisplayed() is \c true (default is \c false), this method is called by postDraw() to
 display the currentFPS(). Use QApplication::setFont() to define the font (see drawText()). */
void QGLViewer::displayFPS()
{
  drawText(10, int(1.5*((QApplication::font().pixelSize()>0)?QApplication::font().pixelSize():QApplication::font().pointSize())), fpsString_);
}

/*! Modify the projection matrix so that drawing can be done directly with 2D screen coordinates.

 Once called, the \p x and \p y coordinates passed to \c glVertex are expressed in pixels screen
 coordinates. The origin (0,0) is in the upper left corner of the widget by default. This follows
 the Qt standards, so that you can directly use the \c pos() provided by for instance \c
 QMouseEvent. Set \p upward to \c true to place the origin in the \e lower left corner, thus
 following the OpenGL and mathematical standards. It is always possible to switch between the two
 representations using \c newY = height() - \c y.

 You need to call stopScreenCoordinatesSystem() at the end of the drawing block to restore the
 previous camera matrix.

 In practice, this method should be used in draw(). It sets an appropriate orthographic projection
 matrix and then sets \c glMatrixMode to \c GL_MODELVIEW.

 See the <a href="../examples/screenCoordSystem.html">screenCoordSystem</a>, <a
 href="../examples/multiSelect.html">multiSelect</a> and <a
 href="../examples/contribs.html#backgroundImage">backgroundImage</a> examples for an illustration.

 You may want to disable \c GL_LIGHTING, to enable \c GL_LINE_SMOOTH or \c GL_BLEND to draw when
 this method is used.

 If you want to link 2D drawings to 3D objects, use qglviewer::Camera::projectedCoordinatesOf() to
 compute the 2D projection on screen of a 3D point (see the <a
 href="../examples/screenCoordSystem.html">screenCoordSystem</a> example). See also drawText().

 In this mode, you should use z values that are in the [0.0, 1.0[ range (0.0 corresponding to the
 near clipping plane and 1.0 being just beyond the far clipping plane). This interval matches the
 values that can be read from the z-buffer. Note that if you use the convenient \c glVertex2i() to
 provide coordinates, the implicit 0.0 z coordinate will make your drawings appear \e on \e top of
 the rest of the scene. */
void QGLViewer::startScreenCoordinatesSystem(bool upward) const
{
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  if (upward)
    glOrtho(0, width(), 0, height(), 0.0, -1.0);
  else
    glOrtho(0, width(), height(), 0, 0.0, -1.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
}

/*! Stops the pixel coordinate drawing block started by startScreenCoordinatesSystem().

 The \c GL_MODELVIEW and \c GL_PROJECTION matrices modified in
 startScreenCoordinatesSystem() are restored. \c glMatrixMode is set to \c GL_MODELVIEW. */
void QGLViewer::stopScreenCoordinatesSystem() const
{
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

/*! Overloading of the \c QObject method.

 If animationIsStarted(), calls animate() and draw(). */
void QGLViewer::timerEvent(QTimerEvent *)
{
  if (animationIsStarted())
    {
      animate();
      updateGL();
    }
}

/*! Starts the animation loop. See animationIsStarted(). */
void QGLViewer::startAnimation()
{
  animationTimerId_ = startTimer(animationPeriod());
  animationStarted_ = true;
}

/*! Stops animation. See animationIsStarted(). */
void QGLViewer::stopAnimation()
{
  animationStarted_ = false;
  killTimer(animationTimerId_);
}

/*! Overloading of the \c QWidget method.

Saves the viewer state using saveStateToFile() and then calls QGLWidget::closeEvent(). */
void QGLViewer::closeEvent(QCloseEvent *e)
{
  // When the user clicks on the window close (x) button:
  // - If the viewer is a top level window, closeEvent is called and then saves to file.
  // - Otherwise, nothing happen s:(
  // When the user press the EXIT_VIEWER keyboard shortcut:
  // - If the viewer is a top level window, saveStateToFile() is also called
  // - Otherwise, closeEvent is NOT called and keyPressEvent does the job.

  /* After tests:
     E : Embedded widget
     N : Widget created with new
     C : closeEvent called
     D : destructor called

     E	N	C	D
     y	y
     y	n		y
     n	y	y
     n	n	y	y

     closeEvent is called iif the widget is NOT embedded.

     Destructor is called iif the widget is created on the stack
     or if widget (resp. parent if embedded) is created with WDestructiveClose flag.

     closeEvent always before destructor.

     Close using qApp->closeAllWindows or (x) is identical.
  */

  // #CONNECTION# Also done for EXIT_VIEWER in keyPressEvent().
  saveStateToFile();
  QGLWidget::closeEvent(e);
}

/*! Simple wrapper method: calls \c select(event->pos()).

  Emits \c pointSelected(e) which is useful only if you rely on the Qt signal-slot mechanism and you
  did not overload QGLViewer. If you choose to derive your own viewer class, simply overload
  select() (or probably simply drawWithNames(), see the <a href="../examples/select.html">select
  example</a>) to implement your selection mechanism.

  This method is called when you use the QGLViewer::SELECT mouse binding(s) (default is Shift + left
  button). Overload to make the selection mechanism depend on the \p event state (keyboard
  modifiers). */
void QGLViewer::select(const QMouseEvent* event)
{
  // For those who don't derive but rather rely on the signal-slot mechanism.
  emit pointSelected(event);
  select(event->pos());
}

/*! This method performs a selection in the scene from pixel coordinates.

 It is called when the user clicks on the QGLViewer::SELECT QGLViewer::ClickAction binded button(s)
 (default is Shift + LeftButton).

 This method successively calls four other methods:
 \code
 beginSelection(point);
 drawWithNames();
 endSelection(point);
 postSelection(point);
 \endcode

 The default implementation of these methods is as follows (see the methods' documentation for
 more details):

 \arg beginSelection() sets the \c GL_SELECT mode with the appropriate picking matrices. A
 rectangular frustum (of size defined by selectRegionWidth() and selectRegionHeight()) centered on
 \p point is created.

 \arg drawWithNames() is empty and should be overloaded. It draws each selectable object of the
 scene, enclosed by calls to \c glPushName() / \c glPopName() to tag the object with an integer id.

 \arg endSelection() then restores \c GL_RENDER mode and analyzes the selectBuffer() to set in
 selectedName() the id of the object that was drawn in the region. If several object are in the
 region, the closest one in the depth buffer is chosen. If no object has been drawn under cursor,
 selectedName() is set to -1.

 \arg postSelection() is empty and can be overloaded for possible signal/display/interface update.

 See the \c glSelectBuffer() man page for details on this \c GL_SELECT mechanism.

 This default implementation is quite limited: only the closer object is selected, and only one
 level of names can be pushed. However, this reveals sufficient in many cases and you usually only
 have to overload drawWithNames() to implement a simple object selection process. See the <a
 href="../examples/select.html">select example</a> for an illustration.

 If you need a more complex selection process (such as a point, edge or triangle selection, which
 is easier with a 2 or 3 levels selectBuffer() heap, and which requires a finer depth sorting to
 privilege point over edge and edges over triangles), overload the endSelection() method. Use
 setSelectRegionWidth(), setSelectRegionHeight() and setSelectBufferSize() to tune the select
 buffer configuration. See the <a href="../examples/multiSelect.html">multiSelect example</a> for
 an illustration.

 \p point is the center pixel (origin in the upper left corner) of the selection region. Use
 qglviewer::Camera::convertClickToLine() to transform these coordinates in a 3D ray if you want to
 perform an analytical intersection.

 \attention \c GL_SELECT mode seems to report wrong results when used in conjunction with backface
 culling. If you encounter problems try to \c glDisable(GL_CULL_FACE). */
void QGLViewer::select(const QPoint& point)
{
  beginSelection(point);
  drawWithNames();
  endSelection(point);
  postSelection(point);
}

/*! This method should prepare the selection. It is called by select() before drawWithNames().

 The default implementation uses the \c GL_SELECT mode to perform a selection. It uses
 selectBuffer() and selectBufferSize() to define a \c glSelectBuffer(). The \c GL_PROJECTION is then
 set using \c gluPickMatrix(), with a window selection size defined by selectRegionWidth() and
 selectRegionHeight(). Finally, the \c GL_MODELVIEW matrix is set to the world coordinate system
 using qglviewer::Camera::loadModelViewMatrix(). See the gluPickMatrix() documentation for details.

 You should not need to redefine this method (if you use the \c GL_SELECT mode to perform your
 selection), since this code is fairly classical and can be tuned. You are more likely to overload
 endSelection() if you want to use a more complex select buffer structure. */
void QGLViewer::beginSelection(const QPoint& point)
{
  // Make OpenGL context current (may be needed with several viewers ?)
  makeCurrent();

  // Prepare the selection mode
  glSelectBuffer(selectBufferSize(), selectBuffer());
  glRenderMode(GL_SELECT);
  glInitNames();

  // Loads the matrices
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  static GLint viewport[4];
  camera()->getViewport(viewport);
  gluPickMatrix(point.x(), point.y(), selectRegionWidth(), selectRegionHeight(), viewport);

  // loadProjectionMatrix() first resets the GL_PROJECTION matrix with a glLoadIdentity().
  // The false parameter prevents this and hence multiplies the matrices.
  camera()->loadProjectionMatrix(false);
  // Reset the original (world coordinates) modelview matrix
  camera()->loadModelViewMatrix();
}

/*! This method is called by select() after scene elements were drawn by drawWithNames(). It should
 analyze the selection result to determine which object is actually selected.

 The default implementation relies on \c GL_SELECT mode (see beginSelection()). It assumes that
 names were pushed and popped in drawWithNames(), and analyzes the selectBuffer() to find the name
 that corresponds to the closer (z min) object. It then setSelectedName() to this value, or to -1 if
 the selectBuffer() is empty (no object drawn in selection region). Use selectedName() (probably in
 the postSelection() method) to retrieve this value and update your data structure accordingly.

 This default implementation, although sufficient for many cases is however limited and you may have
 to overload this method. This will be the case if drawWithNames() uses several push levels in the
 name heap. A more precise depth selection, for instance privileging points over edges and
 triangles to avoid z precision problems, will also require an overloading. A typical implementation
 will look like:
 \code
 glFlush();

 // Get the number of objects that were seen through the pick matrix frustum.
 // Resets GL_RENDER mode.
 GLint nbHits = glRenderMode(GL_RENDER);

 if (nbHits <= 0)
   setSelectedName(-1);
 else
 {
   // Interpret results: each object created values in the selectBuffer().
   // See the glSelectBuffer() man page for details on the buffer structure.
   // The following code depends on your selectBuffer() structure.
   for (int i=0; i<nbHits; ++i)
    if ((selectBuffer())[i*4+1] < zMin)
      setSelectedName((selectBuffer())[i*4+3])
 }
 \endcode

 See the <a href="../examples/multiSelect.html">multiSelect example</a> for
 a multi-object selection implementation of this method. */
void QGLViewer::endSelection(const QPoint& point)
{
  Q_UNUSED(point);

  // Flush GL buffers
  glFlush();

  // Get the number of objects that were seen through the pick matrix frustum. Reset GL_RENDER mode.
  GLint nbHits = glRenderMode(GL_RENDER);

  if (nbHits <= 0)
    setSelectedName(-1);
  else
    {
      // Interpret results: each object created 4 values in the selectBuffer().
      // selectBuffer[4*i+1] is the object minimum depth value, while selectBuffer[4*i+3] is the id pushed on the stack.
      // Of all the objects that were projected in the pick region, we select the closest one (zMin comparison).
      // This code needs to be modified if you use several stack levels. See glSelectBuffer() man page.
      GLuint zMin = (selectBuffer())[1];
      setSelectedName((selectBuffer())[3]);
      for (int i=1; i<nbHits; ++i)
	if ((selectBuffer())[4*i+1] < zMin)
	  {
	    zMin = (selectBuffer())[4*i+1];
	    setSelectedName((selectBuffer())[4*i+3]);
	  }
    }
}

/*! Sets the selectBufferSize().

The previous selectBuffer() is deleted and a new one is created. */
void QGLViewer::setSelectBufferSize(int size)
{
  if (selectBuffer_)
    delete[] selectBuffer_;
  selectBufferSize_ = size;
  selectBuffer_ = new GLuint[selectBufferSize()];
}

void QGLViewer::performClickAction(ClickAction ca, const QMouseEvent* const e)
{
  // Note: action that need it should updateGL().
  switch (ca)
    {
    case NO_CLICK_ACTION :
      break;
    case ZOOM_ON_PIXEL :
      camera()->interpolateToZoomOnPixel(e->pos());
      break;
    case ZOOM_TO_FIT :
      camera()->interpolateToFitScene();
      break;
    case SELECT :
      select(e);
      updateGL();
      break;
    case RAP_FROM_PIXEL :
      if (camera()->setRevolveAroundPointFromPixel(e->pos()))
	{
	  setVisualHintsMask(1);
	  updateGL();
	}
      break;
    case RAP_IS_CENTER :
      camera()->setRevolveAroundPoint(sceneCenter());
      setVisualHintsMask(1);
      updateGL();
      break;
    case CENTER_FRAME :
      if (manipulatedFrame())
	manipulatedFrame()->projectOnLine(camera()->position(), camera()->viewDirection());
      break;
    case CENTER_SCENE :
      camera()->centerScene();
      break;
    case SHOW_ENTIRE_SCENE :
      camera()->showEntireScene();
      break;
    case ALIGN_FRAME :
      if (manipulatedFrame())
	manipulatedFrame()->alignWithFrame(camera()->frame());
      break;
    case ALIGN_CAMERA :
      camera()->frame()->alignWithFrame(NULL, true);
      break;
    }
}

/*! Overloading of the \c QWidget method.

 When the user clicks on the mouse:
 \arg if a mouseGrabber() is defined, qglviewer::MouseGrabber::mousePressEvent() is called,
 \arg otherwise, the camera() or the manipulatedFrame() interprets the mouse displacements,
 depending on mouse bindings.

 Mouse bindings customization can be achieved using setMouseBinding() and setWheelBinding(). See the
 <a href="../mouse.html">mouse page</a> for a complete description of mouse bindings.

 See the mouseMoveEvent() documentation for an example of more complex mouse behavior customization
 using overloading.

 \note When the mouseGrabber() is a manipulatedFrame(), the modifier keys are not taken into
 account. This allows for a direct manipulation of the manipulatedFrame() when the mouse hovers,
 which is probably what is expected. */
void QGLViewer::mousePressEvent(QMouseEvent* e)
{
  if (mouseGrabber())
    {
      if (mouseGrabberIsAManipulatedFrame_)
	{
	  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator it=mouseBinding_.begin(), end=mouseBinding_.end(); it!=end; ++it)
	    if ((it.data().handler == FRAME) && ((it.key() & Qt::MouseButtonMask) == (e->stateAfter() & Qt::MouseButtonMask)))
	      {
		ManipulatedFrame* mf = dynamic_cast<ManipulatedFrame*>(mouseGrabber());
		if (mouseGrabberIsAManipulatedCameraFrame_)
		  {
		    mf->ManipulatedFrame::startAction(it.data().action, it.data().withConstraint);
		    mf->ManipulatedFrame::mousePressEvent(e, camera());
		  }
		else
		  {
		    mf->startAction(it.data().action, it.data().withConstraint);
		    mf->mousePressEvent(e, camera());
		  }
		break;
	      }
	}
      else
	mouseGrabber()->mousePressEvent(e, camera());
      updateGL();
    }
  else
    {
      //#CONNECTION# mouseDoubleClickEvent has the same structure
      //#CONNECTION# mouseString() concatenates bindings description in inverse order.
      ClickActionPrivate cap;
      cap.doubleClick = false;
      cap.buttonState = (Qt::ButtonState)((e->state() & Qt::KeyButtonMask) |
					  ((e->stateAfter() & Qt::MouseButtonMask) & (~(e->state() & Qt::MouseButtonMask))));
      cap.buttonBefore = (Qt::ButtonState)(e->state() & Qt::MouseButtonMask);
      const QMap<ClickActionPrivate, ClickAction>::ConstIterator ca = clickBinding_.find(cap);
      if (ca != clickBinding_.end())
	performClickAction(ca.data(), e);
      else
	{
	  //#CONNECTION# wheelEvent has the same structure
	  const QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator map = mouseBinding_.find(e->stateAfter());
	  if (map != mouseBinding_.end())
	    switch (map.data().handler)
	      {
	      case CAMERA :
		camera()->frame()->startAction(map.data().action, map.data().withConstraint);
		camera()->frame()->mousePressEvent(e, camera());
		if (map.data().action == SCREEN_ROTATE)
		  // Display visual hint line
		  updateGL();
		break;
	      case FRAME :
		if (manipulatedFrame())
		  {
		    if (manipulatedFrameIsACamera_)
		      {
			manipulatedFrame()->ManipulatedFrame::startAction(map.data().action, map.data().withConstraint);
			manipulatedFrame()->ManipulatedFrame::mousePressEvent(e, camera());
		      }
		    else
		      {
			manipulatedFrame()->startAction(map.data().action, map.data().withConstraint);
			manipulatedFrame()->mousePressEvent(e, camera());
		      }
		    if (map.data().action == SCREEN_ROTATE)
		      updateGL();
		  }
		break;
	      }
#if QT_VERSION >= 0x030000
	  else
	    e->ignore();
#endif
	}
    }
}

/*! Overloading of the \c QWidget method.

 Mouse move event is sent to the mouseGrabber() (if any) or to the camera() or the
 manipulatedFrame(), depending on mouse bindings (see setMouseBinding()).

 If you want to define your own mouse behavior, do something like this:
 \code
 void Viewer::mousePressEvent(QMouseEvent* e)
 {
   // Qt::KeyButtonMask separates the Qt::ControlButton/Qt::AltButton/Qt::ShiftButton state key
   // from the Qt::LeftButton/Qt::MidButton/Qt::RightButton mouse buttons.
   if ((e->state() & Qt::KeyButtonMask) == myStateKeyCombo)
     myMouseBehavior = true;
   else
     QGLViewer::mousePressEvent(e);
 }

 void Viewer::mouseMoveEvent(QMouseEvent *e)
 {
   if (myMouseBehavior)
     // Use e->x() and e->y() as you want...
   else
     QGLViewer::mouseMoveEvent(e);
 }

 void Viewer::mouseReleaseEvent(QMouseEvent* e)
 {
   if (myMouseBehavior)
     myMouseBehavior = false;
   else
     QGLViewer::mouseReleaseEvent(e);
 }
 \endcode */
void QGLViewer::mouseMoveEvent(QMouseEvent* e)
{
  if (mouseGrabber())
    {
      mouseGrabber()->checkIfGrabsMouse(e->x(), e->y(), camera());
      if (mouseGrabber()->grabsMouse())
	if (mouseGrabberIsAManipulatedCameraFrame_)
	  (dynamic_cast<ManipulatedFrame*>(mouseGrabber()))->ManipulatedFrame::mouseMoveEvent(e, camera());
	else
	  mouseGrabber()->mouseMoveEvent(e, camera());
      else
	setMouseGrabber(NULL);
      updateGL();
    }

  if (!mouseGrabber())
    {
      //#CONNECTION# mouseReleaseEvent has the same structure
      if (camera()->frame()->isManipulated())
	{
	  camera()->frame()->mouseMoveEvent(e, camera());
	  // #CONNECTION# manipulatedCameraFrame::mouseMoveEvent specific if at the beginning
	  if (camera()->frame()->action_ == ZOOM_ON_REGION)
	    updateGL();
	}
      else // !
	if ((manipulatedFrame()) && (manipulatedFrame()->isManipulated()))
	  if (manipulatedFrameIsACamera_)
	    manipulatedFrame()->ManipulatedFrame::mouseMoveEvent(e, camera());
	  else
	    manipulatedFrame()->mouseMoveEvent(e, camera());
	else
	  if (hasMouseTracking())
	    {
	      QPtrListIterator<MouseGrabber> it(MouseGrabber::MouseGrabberPool());
	      for (MouseGrabber* mg; (mg = it.current()); ++it)
		{
		  mg->checkIfGrabsMouse(e->x(), e->y(), camera());
		  if (mg->grabsMouse())
		    {
		      setMouseGrabber(mg);
		      // Check that MouseGrabber is not disabled
		      if (mouseGrabber() == mg)
			{
			  updateGL();
			  break;
			}
		    }
		}
	    }
    }
}

/*! Overloading of the \c QWidget method.

 Calls the mouseGrabber(), camera() or manipulatedFrame \c mouseReleaseEvent method.

 See the mouseMoveEvent() documentation for an example of mouse behavior customization. */
void QGLViewer::mouseReleaseEvent(QMouseEvent* e)
{
  if (mouseGrabber())
    {
      if (mouseGrabberIsAManipulatedCameraFrame_)
	(dynamic_cast<ManipulatedFrame*>(mouseGrabber()))->ManipulatedFrame::mouseReleaseEvent(e, camera());
      else
	mouseGrabber()->mouseReleaseEvent(e, camera());
      mouseGrabber()->checkIfGrabsMouse(e->x(), e->y(), camera());
      if (!(mouseGrabber()->grabsMouse()))
	setMouseGrabber(NULL);
      // updateGL();
    }
  else
    //#CONNECTION# mouseMoveEvent has the same structure
    if (camera()->frame()->isManipulated())
      {
	// bool updateGLNeeded = ((camera()->frame()->action_ == ZOOM_ON_REGION) ||
			       // (camera()->frame()->action_ == SCREEN_ROTATE));
	camera()->frame()->mouseReleaseEvent(e, camera());
	// if (updateGLNeeded)
	// Needed in all cases because of fastDraw().
	// updateGL();
      }
    else
      if ((manipulatedFrame()) && (manipulatedFrame()->isManipulated()))
	{
	  // bool updateGLNeeded = (manipulatedFrame()->action_ == SCREEN_ROTATE);
	  if (manipulatedFrameIsACamera_)
	    manipulatedFrame()->ManipulatedFrame::mouseReleaseEvent(e, camera());
	  else
	    manipulatedFrame()->mouseReleaseEvent(e, camera());
	  // if (updateGLNeeded)
	    // updateGL();
	}
#if QT_VERSION >= 0x030000
      else
	e->ignore();
#endif

  // Not absolutely needed (see above commented code for the optimal version), but may reveal
  // useful for specific applications.
  updateGL();
}

/*! Overloading of the \c QWidget method.

 If defined, the wheel event is sent to the mouseGrabber(). It is otherwise sent according to wheel
 bindings (see setWheelBinding()). */
void QGLViewer::wheelEvent(QWheelEvent* e)
{
  if (mouseGrabber())
    {
      if (mouseGrabberIsAManipulatedFrame_)
	{
	  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator it=wheelBinding_.begin(), end=wheelBinding_.end(); it!=end; ++it)
	    if (it.data().handler == FRAME)
	      {
		ManipulatedFrame* mf = dynamic_cast<ManipulatedFrame*>(mouseGrabber());
		if (mouseGrabberIsAManipulatedCameraFrame_)
		  {
		    mf->ManipulatedFrame::startAction(it.data().action, it.data().withConstraint);
		    mf->ManipulatedFrame::wheelEvent(e, camera());
		  }
		else
		  {
		    mf->startAction(it.data().action, it.data().withConstraint);
		    mf->wheelEvent(e, camera());
		  }
		break;
	      }
	}
      else
	mouseGrabber()->wheelEvent(e, camera());
      updateGL();
    }
  else
    {
      //#CONNECTION# mousePressEvent has the same structure
      const QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator map = wheelBinding_.find(e->state());
      if (map != wheelBinding_.end())
	switch (map.data().handler)
	  {
	  case CAMERA :
	    camera()->frame()->startAction(map.data().action, map.data().withConstraint);
	    camera()->frame()->wheelEvent(e, camera());
	    break;
	  case FRAME :
	    if (manipulatedFrame())
	      if (manipulatedFrameIsACamera_)
		{
		  manipulatedFrame()->ManipulatedFrame::startAction(map.data().action, map.data().withConstraint);
		  manipulatedFrame()->ManipulatedFrame::wheelEvent(e, camera());
		}
	      else
		{
		  manipulatedFrame()->startAction(map.data().action, map.data().withConstraint);
		  manipulatedFrame()->wheelEvent(e, camera());
		}
	    break;
	  }
#if QT_VERSION >= 0x030000
      else
	e->ignore();
#endif
    }
}

/*! Overloading of the \c QWidget method.

 The behavior of the mouse double click depends on the mouse binding. See setMouseBinding() and the
 <a href="../mouse.html">mouse page</a>. */
void QGLViewer::mouseDoubleClickEvent(QMouseEvent* e)
{
  if (mouseGrabber())
    mouseGrabber()->mouseDoubleClickEvent(e, camera());
  else
    {
      //#CONNECTION# mousePressEvent has the same structure
      ClickActionPrivate cap;
      cap.doubleClick = true;
      // Warning: with Qt < 3.1, the definition of Qt::KeyButtonMask is erroneous
      cap.buttonState = (Qt::ButtonState)((e->state() & Qt::KeyButtonMask) |
					  ((e->stateAfter() & Qt::MouseButtonMask) & (~(e->state() & Qt::MouseButtonMask))));
      cap.buttonBefore = (Qt::ButtonState)(e->state() & Qt::MouseButtonMask);
      const QMap<ClickActionPrivate, ClickAction>::ConstIterator ca = clickBinding_.find(cap);
      if (ca != clickBinding_.end())
	performClickAction(ca.data(), e);
#if QT_VERSION >= 0x030000
      else
	e->ignore();
#endif
    }
}

/*! Sets the state of displaysInStereo(). See also toggleStereoDisplay().

First checks that the display is able to handle stereovision using QGLWidget::format(). Opens a
warning message box in case of failure. Emits the stereoChanged() signal otherwise. */
void QGLViewer::setStereoDisplay(bool stereo)
{
  if (format().stereo())
    {
      stereo_ = stereo;
      if (!displaysInStereo())
	{
	  glDrawBuffer(GL_BACK_LEFT);
	  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	  glDrawBuffer(GL_BACK_RIGHT);
	  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

      emit stereoChanged(stereo_);

      if (updateGLOK_)
	updateGL();
    }
  else
    if (stereo)
      QMessageBox::warning(this, "Stereo not supported", "Stereo is not supported on this display");
    else
      stereo_ = false;
}

/*! Sets the isFullScreen() state.

 If the QGLViewer is embedded in an other QWidget (see QWidget::topLevelWidget()), this widget is
 displayed in full screen instead. */
void QGLViewer::setFullScreen(bool fullScreen)
{
  fullScreen_ = fullScreen;

  // Tricky. A timer does it later if !updateGLOK_.
  if (!updateGLOK_)
    return;

  QWidget* tlw = topLevelWidget();

  if (isFullScreen())
    {
      prevPos_ = topLevelWidget()->pos();
      tlw->showFullScreen();
      tlw->move(0,0);
    }
  else
    {
      tlw->showNormal();
      tlw->move(prevPos_);
    }
}

/*! Directly defines the mouseGrabber().

You should not call this method directly as it bypasses the
qglviewer::MouseGrabber::checkIfGrabsMouse() test performed by mouseMoveEvent().

If the MouseGrabber is disabled (see mouseGrabberIsEnabled()), this method silently does nothing. */
void QGLViewer::setMouseGrabber(MouseGrabber* mouseGrabber)
{
  if (!mouseGrabberIsEnabled(mouseGrabber))
    return;

  mouseGrabber_ = mouseGrabber;

  mouseGrabberIsAManipulatedFrame_       = (dynamic_cast<ManipulatedFrame*>(mouseGrabber) != NULL);
  mouseGrabberIsAManipulatedCameraFrame_ = ((dynamic_cast<ManipulatedCameraFrame*>(mouseGrabber) != NULL) &&
					    (mouseGrabber != camera()->frame()));
  emit mouseGrabberChanged(mouseGrabber);
}

/*! Sets the mouseGrabberIsEnabled() state. */
void QGLViewer::setMouseGrabberIsEnabled(const qglviewer::MouseGrabber* const mouseGrabber, bool enabled)
{
  if (enabled)
    disabledMouseGrabbers_.remove(reinterpret_cast<size_t>(mouseGrabber));
  else
    disabledMouseGrabbers_[reinterpret_cast<size_t>(mouseGrabber)];
}

static QString buttonStateKeyString(Qt::ButtonState s, bool noButton=false)
{
  QString result("");
  if (s & Qt::ControlButton) 	result += "Ctrl+";
  if (s & Qt::AltButton) 	result += "Alt+";
  if (s & Qt::ShiftButton) 	result += "Shift+";
#if QT_VERSION >= 0x030000
  if (s & Qt::MetaButton) 	result += "Meta+";
#endif
  if (noButton && (s==Qt::NoButton)) result += "(no button)";
  return result;
}

static QString buttonStateButtonString(Qt::ButtonState s)
{
  QString result("");
  int nb = 0;
  if (s & Qt::LeftButton)    { result += "Left"; nb++; }
  if (s & Qt::MidButton)     { if (nb) result += " & "; result += "Middle"; nb++; }
  if (s & Qt::RightButton)   { if (nb) result += " & "; result += "Right"; nb++; }
  // if (nb > 0) result += " button";
  // if (nb > 1) result += "s";
  return result;
}

QString QGLViewer::mouseActionString(QGLViewer::MouseAction ma)
{
  switch (ma)
    {
    case QGLViewer::NO_MOUSE_ACTION : 	return QString::null;
    case QGLViewer::ROTATE : 		return QString("Rotates");
    case QGLViewer::ZOOM : 		return QString("Zooms");
    case QGLViewer::TRANSLATE : 	return QString("Translates");
    case QGLViewer::MOVE_FORWARD : 	return QString("Moves forward");
    case QGLViewer::LOOK_AROUND : 	return QString("Looks around");
    case QGLViewer::MOVE_BACKWARD : 	return QString("Moves backward");
    case QGLViewer::SCREEN_ROTATE : 	return QString("Screen rotates");
    case QGLViewer::ROLL :		return QString("Rolls");
    case QGLViewer::SCREEN_TRANSLATE : 	return QString("Screen translates");
    case QGLViewer::ZOOM_ON_REGION : 	return QString("Zooms on region for");
    }
  return QString::null;
}

QString QGLViewer::clickActionString(QGLViewer::ClickAction ca)
{
  switch (ca)
    {
    case QGLViewer::NO_CLICK_ACTION : 	return QString::null;
    case QGLViewer::ZOOM_ON_PIXEL : 	return QString("Zooms on pixel");
    case QGLViewer::ZOOM_TO_FIT : 	return QString("Zooms to fit scene");
    case QGLViewer::SELECT : 		return QString("Selects");
    case QGLViewer::RAP_FROM_PIXEL : 	return QString("Sets revolve around point");
    case QGLViewer::RAP_IS_CENTER : 	return QString("Resets revolve around point");
    case QGLViewer::CENTER_FRAME : 	return QString("Centers frame");
    case QGLViewer::CENTER_SCENE : 	return QString("Centers scene");
    case QGLViewer::SHOW_ENTIRE_SCENE : return QString("Shows entire scene");
    case QGLViewer::ALIGN_FRAME : 	return QString("Aligns frame");
    case QGLViewer::ALIGN_CAMERA : 	return QString("Aligns camera");
    }
  return QString::null;
}

/*! Provides a custom mouse binding description, displayed in the help() window Mouse tab.

 \p buttonState is a combination of modifier keys (\c Qt::ControlButton, \c Qt::AltButton, \c
 Qt::ShiftButton) and mouse buttons (\c Qt::LeftButton, \c Qt::MidButton and \c Qt::RightButton),
 combined using the \c "|" bitwise operator.

 \p doubleClick indicates whether or not the user has to double click this button. Set an empty \p
 description to \e remove a mouse binding description.

 \code
 // Left and Right button together simulate a middle button
 setMouseBindingDescription(Qt::LeftButton | Qt::RightButton, "Emulates a middle button");

 // A left button double click toggles full screen
 setMouseBindingDescription(Qt::LeftButton, "Toggles full screen mode", true);

 // Remove the description of Ctrl+Right button
 setMouseBindingDescription(Qt::ControlButton | Qt::RightButton, "");
 \endcode

 Overload mouseMoveEvent() and friends to implement your custom mouse behavior (see the
 mouseMoveEvent() documentation for an example). See the <a
 href="../examples/keyboardAndMouse.html">keyboardAndMouse example</a> for an illustration.

 Use setMouseBinding() and setWheelBinding() to change the standard mouse action bindings. */
void QGLViewer::setMouseBindingDescription(int buttonState, QString description, bool doubleClick)
{
  ClickActionPrivate cap;
  cap.buttonState = Qt::ButtonState(buttonState);
  cap.doubleClick = doubleClick;
  cap.buttonBefore = Qt::NoButton;

  if (description.isEmpty())
    mouseDescription_.remove(cap);
  else
    mouseDescription_[cap] = description;
}

static QString tableLine(const QString& left, const QString& right)
{
  static bool even = false;
  const QString tdtd("</b></td><td>");
  const QString tdtr("</td></tr>\n");

  QString res("<tr bgcolor=\"");

  if (even)
    res += "#eeeeff\">";
  else
    res += "#ffffff\">";
  res += "<td><b>" + left + tdtd + right + tdtr;
  even = !even;

  return res;
}

/*! Returns a QString that describes the application mouse bindings, displayed in the help() window
  \c Mouse tab.

  Result is a table that describes custom application mouse binding descriptions defined using
  setMouseBindingDescription() as well as standard mouse bindings (defined using setMouseBinding()
  and setWheelBinding()). See the <a href="../mouse.html">mouse page</a> for details on mouse
  bindings.

  See also helpString() and keyboardString(). */
QString QGLViewer::mouseString() const
{
  QString text("<table border=\"1\" cellspacing=\"0\">\n");
  const QString trtd("<tr><td>");
  const QString tdtr("</td></tr>\n");
  const QString tdtd("</td><td>");

  text += "<tr bgcolor=\"#aaaacc\"><th align=\"center\">Button</th><th align=\"center\">Description</th></tr>\n";

  QMap<ClickActionPrivate, QString> mouseBinding;

  // User-defined mouse bondings come first.
  for (QMap<ClickActionPrivate, QString>::ConstIterator itm=mouseDescription_.begin(), endm=mouseDescription_.end(); itm!=endm; ++itm)
    mouseBinding[itm.key()] = itm.data();

  for (QMap<ClickActionPrivate, QString>::ConstIterator it=mouseBinding.begin(), end=mouseBinding.end(); it != end; ++it)
    {
      // Should not be needed (see setMouseBindingDescription())
      if (it.data().isNull())
	continue;

      QString button = buttonStateKeyString(it.key().buttonState) + buttonStateButtonString(it.key().buttonState);
      if (it.key().doubleClick)
	button += " double click";
      if (! (it.key().buttonState & Qt::MouseButtonMask))
	button += "Wheel";
      if (it.key().buttonBefore != Qt::NoButton)
	button += " with " + buttonStateButtonString(it.key().buttonBefore) + " pressed";

      text += tableLine(button, it.data());
    }

  // Optionnal separator line
  if (!mouseBinding.isEmpty())
    {
      mouseBinding.clear();
      text += "<tr bgcolor=\"#aaaacc\"><td colspan=2>Standard mouse bindings</td></tr>\n";
    }

  // Concatenate the descriptions of wheelBinding_, mouseBinding_, clickBinding_ and mouseDescription_.
  // The order is significant and corresponds to the priorities set in mousePressEvent()
  // #CONNECTION# mousePressEvent() order
  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator itw=wheelBinding_.begin(), endw=wheelBinding_.end(); itw != endw; ++itw)
    {
      ClickActionPrivate cap;
      cap.doubleClick = false;
      cap.buttonState = itw.key();
      cap.buttonBefore = Qt::NoButton;

      QString text = mouseActionString(itw.data().action);

      if (!text.isNull())
	{
	  switch (itw.data().handler)
	    {
	    case CAMERA: text += " camera"; break;
	    case FRAME:  text += " manipulated frame"; break;
	    }
	  if (!(itw.data().withConstraint))
	    text += "*";
	}

      mouseBinding[cap] = text;
    }

  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator itmb=mouseBinding_.begin(), endmb=mouseBinding_.end();
       itmb != endmb; ++itmb)
    {
      ClickActionPrivate cap;
      cap.doubleClick = false;
      cap.buttonState = itmb.key();
      cap.buttonBefore = Qt::NoButton;

      QString text = mouseActionString(itmb.data().action);

      if (!text.isNull())
	{
	  switch (itmb.data().handler)
	    {
	    case CAMERA: text += " camera"; break;
	    case FRAME:  text += " manipulated frame"; break;
	    }
	  if (!(itmb.data().withConstraint))
	    text += "*";
	}
      mouseBinding[cap] = text;
    }

  for (QMap<ClickActionPrivate, ClickAction>::ConstIterator itcb=clickBinding_.begin(), endcb=clickBinding_.end(); itcb!=endcb; ++itcb)
    mouseBinding[itcb.key()] = clickActionString(itcb.data());

  for (QMap<ClickActionPrivate, QString>::ConstIterator it2=mouseBinding.begin(), end2=mouseBinding.end(); it2 != end2; ++it2)
    {
      if (it2.data().isNull())
	continue;

      QString button = buttonStateKeyString(it2.key().buttonState) + buttonStateButtonString(it2.key().buttonState);
      if (it2.key().doubleClick)
	button += " double click";
      if (! (it2.key().buttonState & Qt::MouseButtonMask))
	button += "Wheel";
      if (it2.key().buttonBefore != Qt::NoButton)
	button += " with " + buttonStateButtonString(it2.key().buttonBefore) + " pressed";

      text += tableLine(button, it2.data());
    }

  text += "</table>";

  return text;
}

/*! Defines a custom keyboard shortcut description, that will be displayed in the help() window \c
 Keyboard tab.

 The \p key definition is given as an \c int using Qt enumerated values. Set an empty \p description
 to remove a shortcut description:
 \code
 setKeyDescription(Key_W, "Toggles wireframe display");
 setKeyDescription(CTRL+Key_L, "Loads a new scene");
 // Removes a description
 setKeyDescription(CTRL+Key_C, "");
 \endcode

 See the <a href="../examples/keyboardAndMouse.html">keyboardAndMouse example</a> for illustration
 and the <a href="../keyboard.html">keyboard page</a> for details. */
void QGLViewer::setKeyDescription(int key, QString description)
{
  if (description.isEmpty())
    keyDescription_.remove(key);
  else
    keyDescription_[key] = description;
}

static Qt::Modifier buttonStateToModifier(Qt::ButtonState state)
{
  int modifier=0;
  if (state & Qt::ShiftButton)	modifier += Qt::SHIFT;
  if (state & Qt::ControlButton)modifier += Qt::CTRL;
  if (state & Qt::AltButton) 	modifier += Qt::ALT;
#if QT_VERSION >= 0x030100
  if (state & Qt::MetaButton) 	modifier += Qt::META;
#endif
  return Qt::Modifier(modifier);
}

QString QGLViewer::cameraPathKeysString() const
{
  if (pathIndex_.isEmpty())
    return QString::null;

  QValueVector<int> keys;
  keys.reserve(pathIndex_.count());
  for (QMap<Qt::Key, int>::ConstIterator i = pathIndex_.begin(), endi=pathIndex_.end(); i != endi; ++i)
    keys.push_back(i.key());
#if QT_VERSION < 0x030000
  sort(keys.begin(), keys.end());
#else
  qHeapSort(keys);
#endif

  QValueVector<int>::const_iterator it = keys.begin(), end = keys.end();
  QString res = QString(QKeySequence(*it));

  const int maxDisplayedKeys = 6;
  int nbDisplayedKeys = 0;
  int previousKey = (*it);
  int state = 0;
  ++it;
  while ((it != end) && (nbDisplayedKeys < maxDisplayedKeys-1))
    {
      switch (state)
	{
	case 0 :
	  if ((*it) == previousKey + 1)
	    state++;
	  else
	    {
	      res += ", " + QString(QKeySequence(*it));
	      nbDisplayedKeys++;
	    }
	  break;
	case 1 :
	  if ((*it) == previousKey + 1)
	    state++;
	  else
	    {
	      res += ", " + QString(QKeySequence(previousKey));
	      res += ", " + QString(QKeySequence(*it));
	      nbDisplayedKeys += 2;
	      state = 0;
	    }
	  break;
	default :
	  if ((*it) != previousKey + 1)
	    {
	      res += ".." + QString(QKeySequence(previousKey));
	      res += ", " + QString(QKeySequence(*it));
	      nbDisplayedKeys += 2;
	      state = 0;
	    }
	  break;
	}
      previousKey = *it;
      ++it;
    }

  if (state == 1)
    res += ", " + QString(QKeySequence(previousKey));
  if (state == 2)
    res += ".." + QString(QKeySequence(previousKey));
  if (it != end)
    res += "...";

  return res;
}

/*! Returns a QString that describes the application keyboard shortcut bindings, and that will be
 displayed in the help() window \c Keyboard tab.

 Default value is a table that describes the custom shortcuts defined using setKeyDescription() as
 well as the \e standard QGLViewer::KeyboardAction shortcuts (defined using setShortcut()). See the
 <a href="../keyboard.html">keyboard page</a> for details on key customization.

 See also helpString() and mouseString(). */
QString QGLViewer::keyboardString() const
{
  QString text("<table border=\"1\" cellspacing=\"0\">\n");
  text += "<tr bgcolor=\"#aaaacc\"><th align=\"center\">Key</th><th align=\"center\">Description</th></tr>\n";

  QMap<int, QString> keyDescription;

  // User defined key descriptions
  for (QMap<int, QString>::ConstIterator kd=keyDescription_.begin(), kdend=keyDescription_.end(); kd!=kdend; ++kd)
    keyDescription[kd.key()] = kd.data();

  for (QMap<int, QString>::ConstIterator kb=keyDescription.begin(), endb=keyDescription.end(); kb!=endb; ++kb)
    text += tableLine(QString(QKeySequence(kb.key())), kb.data());

  // Optionnal separator line
  if (!keyDescription.isEmpty())
    {
      keyDescription.clear();
      text += "<tr bgcolor=\"#aaaacc\"><td colspan=2>Standard viewer keys</td></tr>\n";
    }

  // KeyboardAction bindings description
  for (QMap<KeyboardAction, int>::ConstIterator it=keyboardBinding_.begin(), end=keyboardBinding_.end(); it != end; ++it)
    if ((it.data() != 0) && ((!cameraIsInRevolveMode()) || ((it.key() != INCREASE_FLYSPEED) && (it.key() != DECREASE_FLYSPEED))))
      keyDescription[it.data()] = keyboardActionDescription_[it.key()];

  for (QMap<int, QString>::ConstIterator kb2=keyDescription.begin(), endb2=keyDescription.end(); kb2!=endb2; ++kb2)
    text += tableLine(QString(QKeySequence(kb2.key())), kb2.data());

  // Camera paths keys description
  const QString cpks = cameraPathKeysString();
  if (!cpks.isNull())
    {
      text += "<tr bgcolor=\"#ccccff\">><td colspan=2>\nCamera paths are controlled using " + cpks + " (noted <i>Fx</i> below):</td></tr>\n";
      text += tableLine(QString(QKeySequence(buttonStateToModifier(playPathStateKey()))) + "<i>Fx</i>",
			"Plays path (or resets saved position)");
      text += tableLine(QString(QKeySequence(buttonStateToModifier(addKeyFrameStateKey()))) + "<i>Fx</i>",
			"Adds a key frame (or defines a position)");
      text += tableLine(QString(QKeySequence(buttonStateToModifier(addKeyFrameStateKey()))) + "<i>Fx</i>+<i>Fx</i>",
			"Deletes path (or saved position)");
    }
  text += "</table>";

  return text;
}

/*! Opens a modal help window that includes three tabs, respectively filled with helpString(),
  keyboardString() and mouseString().

 Rich html-like text can be used (see the QStyleSheet documentation). This method is called when the
 user presses the QGLViewer::HELP (default is 'H').

 Use helpWidget() to access to the help widget (to add/remove tabs, change layout...). The "About"
 button (helpWidget()->cornerWidget()) is connected to the aboutQGLViewer() slot.

 The helpRequired() signal is emitted. */
void QGLViewer::help()
{
  emit helpRequired();

  bool resize = false;
  int width=600;
  int height=400;

  static QString label[] = {" &Help ", " &Keyboard ", " &Mouse "};

  if (!helpWidget_)
    {
      helpWidget_ = new QTabWidget(NULL, "Help window");
      helpWidget_->setCaption("Help");

#if QT_VERSION >= 0x030200
      QPushButton* aboutButton = new QPushButton("About", helpWidget_);
      connect(aboutButton, SIGNAL(released()), SLOT(aboutQGLViewer()));
      helpWidget_->setCornerWidget(aboutButton);
#endif

      resize = true;
      for (int i=0; i<3; ++i)
	{
	  QTextEdit* tab = new QTextEdit(helpWidget_);
	  tab->setTextFormat(Qt::RichText);
#if QT_VERSION >= 0x030000
	  tab->setReadOnly(true);
#endif
	  helpWidget_->insertTab(tab, label[i]);
	}
    }

#if QT_VERSION < 0x030000
  const int currentPageIndex = helpWidget_->currentPageIndex();
#endif

  for (int i=0; i<3; ++i)
    {
      QString text;
      switch (i)
	{
	case 0 : text = helpString();	  break;
	case 1 : text = keyboardString(); break;
	case 2 : text = mouseString();	  break;
	default : break;
	}

#if QT_VERSION < 0x030000
    helpWidget_->setCurrentPage(i);
    QTextEdit* textEdit = (QTextEdit*)(helpWidget_->currentPage());
#else
    QTextEdit* textEdit = (QTextEdit*)(helpWidget_->page(i));
#endif
    textEdit->setText(text);

    if (resize && (textEdit->heightForWidth(width) > height))
	height = textEdit->heightForWidth(width);
    }

#if QT_VERSION < 0x030000
  helpWidget_->setCurrentPage(currentPageIndex);
#endif

  if (resize)
    helpWidget_->resize(width, height+40); // 40 is tabs' height
  helpWidget_->show();
  helpWidget_->raise();
}

/*! Overloading of the \c QWidget method.

 Default keyboard shortcuts are defined using setShortcut(). Overload this method to implement a
 specific keyboard binding. Call the original method if you do not catch the event to preserve the
 viewer default key bindings:
 \code
 void Viewer::keyPressEvent(QKeyEvent *e)
 {
   // Retrieve state keys
   const Qt::ButtonState state = (Qt::ButtonState)(e->state() & Qt::KeyButtonMask);

   // Defines the Alt+R shortcut. Call updateGL to refresh display.
   if ((state == Qt::AltButton) && (e->key() == Qt::Key_R))
     {
       myResetFunction();
       updateGL();
     }
   else
     QGLViewer::keyPressEvent(e);
 }
 \endcode
 When you define a new keyboard shortcut, use setKeyDescription() to provide a short description
 which is displayed in the help() window Keyboard tab. See the <a
 href="../examples/keyboardAndMouse.html">keyboardAndMouse</a> example for an illustration.

 See also QGLWidget::keyReleaseEvent(). */
void QGLViewer::keyPressEvent(QKeyEvent *e)
{
  const int key = e->key();
  const Qt::ButtonState state = (Qt::ButtonState)(e->state() & Qt::KeyButtonMask);

  const int accel = buttonStateToModifier(state) + key;

  QMap<KeyboardAction, int>::ConstIterator it=keyboardBinding_.begin(), end=keyboardBinding_.end();
  while ((it != end) && (it.data() != accel))
    ++it;

  if (it != end)
    handleKeyboardAction(it.key());
  else
    if (pathIndex_.contains(Qt::Key(key)))
      {
	// Camera paths
	int index = pathIndex_[Qt::Key(key)];

	static QTime doublePress; // try to double press on two viewers at the same time !

	if (state == playPathStateKey())
	  {
	    int elapsed = doublePress.restart();
	    if ((elapsed < 250) && (index==previousPathId_))
	      camera()->resetPath(index);
	    else
	      {
		// Stop previous interpolation before starting a new one.
		if (index != previousPathId_)
		  {
		    KeyFrameInterpolator* previous = camera()->keyFrameInterpolator(previousPathId_);
		    if ((previous) && (previous->interpolationIsStarted()))
		      previous->resetInterpolation();
		  }
		camera()->playPath(index);
	      }
	    previousPathId_ = index;
	  }
	else if (state == addKFStateKey_)
	  {
	    int elapsed = doublePress.restart();
	    if ((elapsed < 250) && (index==previousPathId_))
	      {
		if (camera()->keyFrameInterpolator(index))
		  {
		    disconnect(camera()->keyFrameInterpolator(index), SIGNAL(interpolated()), this, SLOT(updateGL()));
		    if (camera()->keyFrameInterpolator(index)->numberOfKeyFrames() > 1)
		      displayMessage("Path "+QString::number(index)+" deleted");
		    else
		      displayMessage("Position "+QString::number(index)+" deleted");
		    camera()->deletePath(index);
		  }
	      }
	    else
	      {
		bool nullBefore = (camera()->keyFrameInterpolator(index) == NULL);
		camera()->addKeyFrameToPath(index);
		if (nullBefore)
		  connect(camera()->keyFrameInterpolator(index), SIGNAL(interpolated()), SLOT(updateGL()));
		int nbKF = camera()->keyFrameInterpolator(index)->numberOfKeyFrames();
		if (nbKF == 1)
		  displayMessage("Position "+QString::number(index)+" saved");
		else
		  displayMessage("Path "+QString::number(index)+", position "+QString::number(nbKF)+" saved");
	      }
	    previousPathId_ = index;
	  }
	updateGL();
      }
    else
      e->ignore();
}

void QGLViewer::handleKeyboardAction(KeyboardAction id)
{
  switch (id)
    {
    case DRAW_AXIS :		toggleAxisIsDrawn(); break;
    case DRAW_GRID :		toggleGridIsDrawn(); break;
    case DISPLAY_FPS :		toggleFPSIsDisplayed(); break;
    case DISPLAY_Z_BUFFER :	toggleZBufferIsDisplayed(); break;
    case ENABLE_TEXT :		toggleTextIsEnabled(); break;
    case EXIT_VIEWER :		saveStateToFileForAllViewers(); qApp->closeAllWindows(); break;
    case SAVE_SCREENSHOT :	saveSnapshot(false, false); break;
    case FULL_SCREEN :		toggleFullScreen(); break;
    case STEREO :		toggleStereoDisplay(); break;
    case ANIMATION :		toggleAnimation(); break;
    case HELP :			help(); break;
    case EDIT_CAMERA :		toggleCameraIsEdited(); break;
    case CAMERA_MODE :
      toggleCameraMode();
      displayMessage(cameraIsInRevolveMode()?"Camera in revolve around mode":"Camera in fly mode");
      break;

    case MOVE_CAMERA_LEFT :
      camera()->frame()->translate(camera()->frame()->inverseTransformOf(Vec(-10.0*camera()->flySpeed(), 0.0, 0.0)));
      updateGL();
      break;
    case MOVE_CAMERA_RIGHT :
      camera()->frame()->translate(camera()->frame()->inverseTransformOf(Vec( 10.0*camera()->flySpeed(), 0.0, 0.0)));
      updateGL();
      break;
    case MOVE_CAMERA_UP :
      camera()->frame()->translate(camera()->frame()->inverseTransformOf(Vec(0.0,  10.0*camera()->flySpeed(), 0.0)));
      updateGL();
      break;
    case MOVE_CAMERA_DOWN :
      camera()->frame()->translate(camera()->frame()->inverseTransformOf(Vec(0.0, -10.0*camera()->flySpeed(), 0.0)));
      updateGL();
      break;

    case INCREASE_FLYSPEED : 	camera()->setFlySpeed(camera()->flySpeed() * 1.5); break;
    case DECREASE_FLYSPEED : 	camera()->setFlySpeed(camera()->flySpeed() / 1.5); break;
    }
}

/*! Callback method used when the widget size is modified.

 If you overload this method, first call the inherited method. Also called when the widget is
 created, before its first display. */
void QGLViewer::resizeGL(int width, int height)
{
  QGLWidget::resizeGL(width, height);
  glViewport( 0, 0, GLint(width), GLint(height) );
  camera()->setScreenWidthAndHeight(this->width(), this->height());
}

////////////////////////////////////////////////////////////////////////////////
//              K e y b o a r d   a c c e l e r a t o r s                     //
////////////////////////////////////////////////////////////////////////////////

/*! Defines the shortcut() that triggers a given QGLViewer::KeyboardAction.

 Here are some examples:
 \code
 // Press 'Q' to exit application
 setShortcut(EXIT_VIEWER, Key_Q);

 // Alt+M toggles camera mode
 setShortcut(CAMERA_MODE, ALT+Key_M);

 // The DISPLAY_FPS action is disabled
 setShortcut(DISPLAY_FPS, 0);
 \endcode

 Only one shortcut can be assigned to a given QGLViewer::KeyboardAction (new bindings replace
 previous ones). If several KeyboardAction are binded to the same shortcut, only one of them is
 active. */
void QGLViewer::setShortcut(KeyboardAction action, int key)
{
  keyboardBinding_[action] = key;
}

/*! Returns the keyboard shortcut associated to a given QGLViewer::KeyboardAction.

 Result is an \c int defined using Qt enumerated values, as in \c Key_Q, \c CTRL+Key_X or \c
 CTRL+ALT+Key_Up. Use Qt::MODIFIER_MASK to separate the key from the state keys. Returns \c 0 if the
 KeyboardAction is disabled (not binded). Set using setShortcut().

 If you want to define keyboard shortcuts for custom actions (say, open a scene file), overload
 keyPressEvent() and then setKeyDescription().

 These shortcuts and their descriptions are automatically included in the help() window \c Keyboard
 tab.

 See the <a href="../keyboard.html">keyboard page</a> for details and default values and the <a
 href="../examples/keyboardAndMouse.html">keyboardAndMouse</a> example for a practical
 illustration. */
int QGLViewer::shortcut(KeyboardAction action) const
{
  if (keyboardBinding_.contains(action))
    return keyboardBinding_[action];
  else
    return 0;
}

#ifndef DOXYGEN
void QGLViewer::setKeyboardAccelerator(KeyboardAction action, int key)
{
  qWarning("setKeyboardAccelerator is deprecated. Use setShortcut instead.");
  setShortcut(action, key);
}

int QGLViewer::keyboardAccelerator(KeyboardAction action) const
{
  qWarning("keyboardAccelerator is deprecated. Use shortcut instead.");
  return shortcut(action);
}
#endif

///////     Key Frames associated keys       ///////

/*! Returns the keyboard key associated to camera Key Frame path \p index.

 Default values are F1..F12 for indexes 1..12.

 addKeyFrameStateKey() (resp. playPathStateKey()) define the state key(s) that must be
 pressed with this key to add a KeyFrame to (resp. to play) the associated Key Frame path. If you
 quickly press twice the pathKey(), the path is reset (resp. deleted).

 Use camera()->keyFrameInterpolator( \p index ) to retrieve the KeyFrameInterpolator that defines
 the path.

 If several keys are binded to a given \p index (see setPathKey()), one of them is returned.
 Returns \c 0 if no key is associated with this index.

 See also the <a href="../keyboard.html">keyboard page</a>. */
Qt::Key QGLViewer::pathKey(int index) const
{
  for (QMap<Qt::Key, int>::ConstIterator it = pathIndex_.begin(), end=pathIndex_.end(); it != end; ++it)
    if (it.data() == index)
      return it.key();
  return Qt::Key(0);
}

/*! Sets the pathKey() associated with the camera Key Frame path \p index.

 Several keys can be binded to the same \p index. Use a negated \p key value to delete the binding
 (the \p index value is then ignored):
 \code
 // Press 'space' to play/pause/add/delete camera path 0.
 setPathKey(Qt::Key_Space, 0);

 // Remove this binding
 setPathKey(-Qt::Key_Space);
 \endcode */
void QGLViewer::setPathKey(int key, int index)
{
  if (key < 0)
    pathIndex_.remove(Qt::Key(-key));
  else
    pathIndex_[Qt::Key(key)] = index;
}

/*! Sets the addKeyFrameStateKey(). */
void QGLViewer::setAddKeyFrameStateKey(int buttonState)
{ addKFStateKey_ = (Qt::ButtonState)(buttonState & Qt::KeyButtonMask); }

/*! Sets the playPathStateKey(). */
void QGLViewer::setPlayPathStateKey(int buttonState)
{ playPathStateKey_ = (Qt::ButtonState)(buttonState & Qt::KeyButtonMask); }

/*! Returns the state key that must be pressed with a pathKey() to add the current camera
  position to a KeyFrame path.

 It can be \p Qt::NoButton, \p Qt::ControlButton, \p Qt::ShiftButton, \p Qt::AltButton, or a
 combination of these (using the bit '|' operator, see setHandlerStateKey()). Default value is
 Qt::AltButton, defined using setAddKeyFrameStateKey().

 See also playPathStateKey(). */
Qt::ButtonState QGLViewer::addKeyFrameStateKey() const
{ return addKFStateKey_; }

/*! Returns the state key that must be pressed with a pathKey() to play a camera KeyFrame path.

 It can be \p Qt::NoButton, \p Qt::ControlButton, \p Qt::ShiftButton, \p Qt::AltButton, or a
 combination of these (using the bit '|' operator, see setHandlerStateKey()). Default value is
 Qt::NoButton, defined using setPlayPathStateKey().

 See also addKeyFrameStateKey(). */
Qt::ButtonState QGLViewer::playPathStateKey() const
{ return playPathStateKey_; }

#ifndef DOXYGEN
// Deprecated methods
Qt::Key QGLViewer::keyFrameKey(int index) const
{
  qWarning("keyFrameKey is deprecated, use pathKey instead.");
  return pathKey(index);
}

Qt::ButtonState QGLViewer::playKeyFramePathStateKey() const
{
  qWarning("playKeyFramePathStateKey is deprecated, use playPathStateKey instead.");
  return playPathStateKey();
}

void QGLViewer::setKeyFrameKey(int index, int key)
{
  qWarning("setKeyFrameKey is deprecated, use setPathKey instead, with swapped parameters.");
  setPathKey(key, index);
}

void QGLViewer::setPlayKeyFramePathStateKey(int buttonState)
{
  qWarning("setPlayKeyFramePathStateKey is deprecated, use instead.");
  setPlayPathStateKey(buttonState);
}
#endif

////////////////////////////////////////////////////////////////////////////////
//              M o u s e   b e h a v i o r   s t a t e   k e y s             //
////////////////////////////////////////////////////////////////////////////////
/*! Associates a given state key to a specific MouseHandler.

 The \p buttonState is Qt::AltButton, Qt::ShiftButton, Qt::ControlButton, Qt::MetaButton or a
 combinaison of these using the '|' bitwise operator.

 \e All the \p handler's associated bindings will then need the specified \p buttonState key to be
 activated.

 With this code,
 \code
 setHandlerStateKey(QGLViewer::CAMERA, Qt::AltButton);
 setHandlerStateKey(QGLViewer::FRAME,  Qt::NoButton);
 \endcode
 you will have to press the \c Alt key while pressing mouse buttons in order to move the camera(),
 while no key will be needed to move the associated manipulatedFrame().

 This method has a very basic implementation: every action binded to \p handler has its state keys
 replaced by \p buttonState. If the MouseHandler had some actions binded to different state keys,
 these settings will be lost. You should hence consider using setMouseBinding() for finer tuning.

 The default binding associates \c Qt::ControlButton to all the QGLViewer::FRAME actions and
 Qt::NoButton to QGLViewer::CAMERA actions. See <a href="../mouse.html">mouse page</a> for details.

 \attention This method calls setMouseBinding(), which ensures that only one action is binded to a
 given buttonState. If you want to \e swap the QGLViewer::CAMERA and QGLViewer::FRAME state keys,
 you have to use a temporary dummy buttonState (as if you were swapping two variables) or else the
 first call will overwrite the previous settings:
 \code
 // Associate FRAME with Alt (temporary value)
 setHandlerStateKey(QGLViewer::FRAME, Qt::Alt);
 // Control is associated with CAMERA
 setHandlerStateKey(QGLViewer::CAMERA, Qt::ControlButton);
 // And finally, FRAME can be associated with NoButton
 setHandlerStateKey(QGLViewer::FRAME, Qt::NoButton);
 \endcode */
void QGLViewer::setHandlerStateKey(MouseHandler handler, int buttonState)
{
  QMap<Qt::ButtonState, MouseActionPrivate> newMouseBinding;
  QMap<Qt::ButtonState, MouseActionPrivate> newWheelBinding;

  QMap<Qt::ButtonState, MouseActionPrivate>::Iterator it;

  // First copy unchanged bindings.
  for (it = mouseBinding_.begin(); it != mouseBinding_.end(); ++it)
    if ((it.data().handler != handler) || (it.data().action == ZOOM_ON_REGION))
      newMouseBinding[it.key()] = it.data();

  for (it = wheelBinding_.begin(); it != wheelBinding_.end(); ++it)
    if (it.data().handler != handler)
      newWheelBinding[it.key()] = it.data();

  // Then, add modified bindings, that can overwrite the previous ones.
  const Qt::ButtonState state = (Qt::ButtonState)(buttonState & Qt::KeyButtonMask);

  for (it = mouseBinding_.begin(); it != mouseBinding_.end(); ++it)
    if ((it.data().handler == handler) && (it.data().action != ZOOM_ON_REGION))
      {
	Qt::ButtonState newState = (Qt::ButtonState)(state | (it.key() & Qt::MouseButtonMask));
	newMouseBinding[newState] = it.data();
      }

  for (it = wheelBinding_.begin(); it != wheelBinding_.end(); ++it)
    if (it.data().handler == handler)
      {
	Qt::ButtonState newState = (Qt::ButtonState)(state | (it.key() & Qt::MouseButtonMask));
	newWheelBinding[newState] = it.data();
      }

  // Same for button bindings
  QMap<ClickActionPrivate, ClickAction> newClickBinding_;

  for (QMap<ClickActionPrivate, ClickAction>::ConstIterator cb=clickBinding_.begin(), end=clickBinding_.end(); cb != end; ++cb)
    if (((handler==CAMERA) && ((cb.data() == CENTER_SCENE) || (cb.data() == ALIGN_CAMERA))) ||
	((handler==FRAME)  && ((cb.data() == CENTER_FRAME) || (cb.data() == ALIGN_FRAME))))
      {
	ClickActionPrivate cap;
	cap.doubleClick = cb.key().doubleClick;
	cap.buttonState = (Qt::ButtonState)(state | (cb.key().buttonState & Qt::MouseButtonMask));
	cap.buttonBefore = (Qt::ButtonState)((~(state) & cb.key().buttonBefore) & Qt::MouseButtonMask);
	newClickBinding_[cap] = cb.data();
      }
    else
      newClickBinding_[cb.key()] = cb.data();

  mouseBinding_ = newMouseBinding;
  wheelBinding_ = newWheelBinding;
  clickBinding_ = newClickBinding_;
}

#ifndef DOXYGEN
void QGLViewer::setMouseStateKey(MouseHandler handler, int buttonState)
{
  qWarning("setMouseStateKey has been renamed setHandlerStateKey.");
  setHandlerStateKey(handler, buttonState);
}
#endif

/*! Associates a MouseAction to any Qt::ButtonState mouse button and state key combination. The
 receiver of the mouse events is a MouseHandler (QGLViewer::CAMERA or QGLViewer::FRAME).

 The parameters should read: when the \p buttonState mouse button and state key are pressed,
 activate \p action on \p handler. If \p withConstraint is \c true (default), the
 qglviewer::Frame::constraint() associated with the Frame will be enforced during motion.

 Use the '|' bitwise operator to combine keys and buttons:
 \code
 // Left and right buttons together make a camera zoom: emulates a mouse third button if needed.
 setMouseBinding(Qt::LeftButton | Qt::RightButton, CAMERA, ZOOM);

 // Alt + Shift + Left button rotates the manipulatedFrame().
 setMouseBinding(Qt::AltButton | Qt::ShiftButton | Qt::LeftButton, FRAME, ROTATE);
 \endcode

 The list of all possible MouseAction, some binding examples and default bindings are provided in
 the <a href="../mouse.html">mouse page</a>.

 See the <a href="../examples/keyboardAndMouse.html">keyboardAndMouse</a> example for an illustration.

 If no mouse button is specified in \p buttonState, the binding is ignored. If an action was
 previously associated with this \p buttonState, it is silently overwritten (use mouseAction()
 before to know if the \p buttonState is already binded).

 To remove a specific mouse binding, use code like:
 \code
 setMouseBinding(myButtonStateKeyCombo, myHandler, NO_MOUSE_ACTION);
 \endcode

 See also setMouseBinding(int, ClickAction, bool, int) and setWheelBinding(). */
void QGLViewer::setMouseBinding(int buttonState, MouseHandler handler, MouseAction action, bool withConstraint)
{
  if ((handler == FRAME) && ((action == MOVE_FORWARD) || (action == MOVE_BACKWARD) ||
			     (action == ROLL) || (action == LOOK_AROUND) ||
			     (action == ZOOM_ON_REGION)))
    qWarning("Cannot bind " + mouseActionString(action) + " to FRAME");
  else
    if ((buttonState & Qt::MouseButtonMask) == 0)
      qWarning("No mouse button specified in setMouseBinding");
    else
      {
	MouseActionPrivate map;
	map.handler = handler;
	map.action  = action;
	map.withConstraint  = withConstraint;
	mouseBinding_.replace((Qt::ButtonState)(buttonState), map);

	ClickActionPrivate cap;
	cap.buttonState = (Qt::ButtonState)(buttonState);
	cap.doubleClick = false;
	cap.buttonBefore = Qt::NoButton;
	clickBinding_.remove(cap);
      }
}

/*! Associates a ClickAction to any Qt::ButtonState mouse button and state key combination.

  The parameters should read: when the \p buttonState mouse button(s) is (are) pressed (possibly
  with Alt, Control, Shift or any combination of these), and possibly with a \p doubleClick, perform
  \p action.

  If \p buttonBefore is specified (valid only when \p doubleClick is \c true), then this mouse
  button(s) have to pressed \e before the double click occurs in order to perform \p action. For
  instance, with the default binding, pressing the right button, then double clicking on the left
  button will call QGLViewer::RAP_FROM_PIXEL (which defines the new
  qglviewer::Camera::revolveAroundPoint() as the point under the mouse cursor, if any).

  The list of all possible ClickAction, some binding examples and default bindings are provided in
  the <a href="../mouse.html">mouse page</a>. See also the setMouseBinding() documentation.

  See the <a href="../examples/keyboardAndMouse.html">keyboardAndMouse example</a> for an
  illustration.

  The binding is ignored if no mouse button is specified in \p buttonState. */
void QGLViewer::setMouseBinding(int buttonState, ClickAction action, bool doubleClick, int buttonBefore)
{
  if ((buttonBefore != Qt::NoButton) && (doubleClick == false))
    qWarning("An other button is meaningful only when doubleClick is true in setMouseBinding().");
  else
    if ((buttonState & Qt::MouseButtonMask) == 0)
      qWarning("No mouse button specified in setMouseBinding");
    else
      {
	ClickActionPrivate cap;
	cap.buttonState = (Qt::ButtonState)(buttonState);
	cap.doubleClick = doubleClick;
	cap.buttonBefore = (Qt::ButtonState)(buttonBefore & Qt::MouseButtonMask);
	clickBinding_.replace(cap, action);
	if ((!doubleClick) && (buttonBefore == Qt::NoButton))
	  mouseBinding_.remove((Qt::ButtonState)(buttonState));
      }
}

/*! Associates a MouseAction and a MouseHandler to a mouse wheel event.

 This method is very similar to setMouseBinding(), but specific to the wheel.

 In the current implementation only QGLViewer::ZOOM can be associated with QGLViewer::FRAME, while
 QGLViewer::CAMERA can receive QGLViewer::ZOOM and QGLViewer::MOVE_FORWARD.

 The difference between QGLViewer::ZOOM and QGLViewer::MOVE_FORWARD is that QGLViewer::ZOOM speed
 depends on the distance to the object, while QGLViewer::MOVE_FORWARD moves at a constant speed
 defined by qglviewer::Camera::flySpeed(). */
void QGLViewer::setWheelBinding(int buttonState, MouseHandler handler, MouseAction action, bool withConstraint)
{
  //#CONNECTION# ManipulatedFrame::wheelEvent and ManipulatedCameraFrame::wheelEvent switches
  if ((action != ZOOM) && (action != MOVE_FORWARD) && (action != MOVE_BACKWARD) && (action != NO_MOUSE_ACTION))
    qWarning("Cannot bind " + mouseActionString(action) + " to wheel");
  else
    if ((handler == FRAME) && (action != ZOOM) && (action != NO_MOUSE_ACTION))
      qWarning("Cannot bind " + mouseActionString(action) + " to FRAME wheel");
    else
      {
	MouseActionPrivate map;
	map.handler = handler;
	map.action  = action;
	map.withConstraint  = withConstraint;
	Qt::ButtonState key = (Qt::ButtonState)(buttonState);
	wheelBinding_.replace(key, map);
      }
}

/*! Returns the MouseAction associated with the Qt::ButtonState \p buttonState. Returns
 QGLViewer::NO_MOUSE_ACTION if no action is associated.

 For instance, to know which motion corresponds to Alt-LeftButton, do:
 \code
 QGLViewer::MouseAction mm = mouseAction(Qt::AltButton | Qt::LeftButton);
 if (mm != NO_MOUSE_ACTION) ...
 \endcode

 Use mouseHandler() to know which object (QGLViewer::CAMERA or QGLViewer::FRAME) will perform this
 motion. */
QGLViewer::MouseAction QGLViewer::mouseAction(int buttonState) const
{
  Qt::ButtonState state = (Qt::ButtonState)(buttonState);
  if (mouseBinding_.find(state) != mouseBinding_.end())
    return mouseBinding_[state].action;
  else
    return NO_MOUSE_ACTION;
}

/*! Returns the MouseHandler associated with the Qt::ButtonState \p buttonState. If no action is
 associated, returns \c -1.

 For instance, to know which handler receives the Alt-LeftButton, do:
 \code
 int mh = mouseHandler(Qt::AltButton | Qt::LeftButton);
 if (mh == QGLViewer::CAMERA) ...
 \endcode

 Use mouseAction() to know which action (see the MouseAction enum) will be perform on this handler. */
int QGLViewer::mouseHandler(int buttonState) const
{
  Qt::ButtonState state = (Qt::ButtonState)(buttonState);
  if (mouseBinding_.find(state) != mouseBinding_.end())
    return mouseBinding_[state].handler;
  else
    return -1;
}

/*! Returns the Qt::ButtonState (if any) that has to be used to activate \p action on \p handler
 (with constraint or not).

 If no Qt::ButtonState is associated, returns Qt::NoButton which is an impossible case since at
 least one mouse button has to be specified in setMouseBinding().

 To know which keys and mouse buttons have to be pressed to translate the camera, use tests like:
 \code
 Qt::ButtonState bs = mouseButtonState(QGLViewer::CAMERA, QGLViewer::TRANSLATE);
 if (bs & Qt::RightButton) ... // Right button needed to translate the camera
 if (bs & Qt::AltButton)   ... // Alt key needed
 if (bs & Qt::KeyButtonMask == Qt::NoButton) ... // No state key needed
 \endcode

 Note that mouse bindings are displayed in the 'Mouse' help window tab (use the 'H' key).

 See also mouseAction() and mouseHandler(). */
Qt::ButtonState QGLViewer::mouseButtonState(MouseHandler handler, MouseAction action, bool withConstraint) const
{
  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator it=mouseBinding_.begin(), end=mouseBinding_.end(); it != end; ++it)
    if ( (it.data().handler == handler) && (it.data().action == action) && (it.data().withConstraint == withConstraint) )
      return it.key();

  return Qt::NoButton;
}

/*! Same as mouseAction(), but for the wheel action. */
QGLViewer::MouseAction QGLViewer::wheelAction(int buttonState) const
{
  Qt::ButtonState state = (Qt::ButtonState)(buttonState);
  if (wheelBinding_.find(state) != wheelBinding_.end())
    return wheelBinding_[state].action;
  else
    return NO_MOUSE_ACTION;
}

/*! Same as mouseHandler but for the wheel action. */
int QGLViewer::wheelHandler(int buttonState) const
{
  Qt::ButtonState state = (Qt::ButtonState)(buttonState);
  if (wheelBinding_.find(state) != wheelBinding_.end())
    return wheelBinding_[state].handler;
  else
    return -1;
}

/*! Same as mouseButtonState(), but for the wheel.

\attention Returns -1 when no Qt::ButtonState was associated with this \p handler/ \p action/ \p
withConstraint value (mouseButtonState() returns Qt::NoButton instead). */
int QGLViewer::wheelButtonState(MouseHandler handler, MouseAction action, bool withConstraint) const
{
  for (QMap<Qt::ButtonState, MouseActionPrivate>::ConstIterator it=wheelBinding_.begin(), end=wheelBinding_.end(); it != end; ++it)
    if ( (it.data().handler == handler) && (it.data().action == action) && (it.data().withConstraint == withConstraint) )
      return it.key();

  return -1;
}

/*! Same as mouseAction(), but for the ClickAction set using setMouseBinding(). */
QGLViewer::ClickAction QGLViewer::clickAction(int buttonState, bool doubleClick, int buttonBefore) const
{
  ClickActionPrivate cap;
  cap.buttonState = (Qt::ButtonState)(buttonState);
  cap.doubleClick = doubleClick;
  cap.buttonBefore = (Qt::ButtonState)(buttonBefore & Qt::KeyButtonMask);
  if (clickBinding_.find(cap) != clickBinding_.end())
    return clickBinding_[cap];
  else
    return NO_CLICK_ACTION;
}

/*! Similar to mouseButtonState(), but for ClickAction.

 The results of the query are returned in the \p buttonState, \p doubleClick and \p buttonBefore
 parameters. If the ClickAction is not associated to any mouse button, \c Qt::NoButton is returned
 in \p buttonState. If several mouse buttons trigger in the ClickAction, one of them is returned. */
void QGLViewer::getClickButtonState(ClickAction ca, Qt::ButtonState& buttonState, bool& doubleClick, Qt::ButtonState& buttonBefore) const
{
  for (QMap<ClickActionPrivate, ClickAction>::ConstIterator it=clickBinding_.begin(), end=clickBinding_.end(); it != end; ++it)
    if (it.data() == ca)
      {
	buttonState = it.key().buttonState;
	doubleClick = it.key().doubleClick;
	buttonBefore = it.key().buttonBefore;
	return;
      }

  buttonState = Qt::NoButton;
}

/*! This function should be used in conjunction with toggleCameraMode(). It returns \c true when at
  least one mouse button is binded to the \c REVOLVE mouseAction. This is crude way of determining
  which "mode" the camera is in. */
bool QGLViewer::cameraIsInRevolveMode() const
{
  //#CONNECTION# used in toggleCameraMode() and keyboardString()
  return mouseButtonState(CAMERA, ROTATE) != Qt::NoButton;
}

/*! Swaps between two predefined camera mouse bindings.

  The first mode makes the camera observe the scene while revolving around the
  qglviewer::Camera::revolveAroundPoint(). The second mode is designed for walkthrough applications
  and simulates a flying camera.

  Practically, the three mouse buttons are respectively binded to:
  \arg In revolve mode: QGLViewer::ROTATE, QGLViewer::ZOOM, QGLViewer::TRANSLATE.
  \arg In fly mode: QGLViewer::MOVE_FORWARD, QGLViewer::LOOK_AROUND, QGLViewer::MOVE_BACKWARD.

  The current mode is determined by checking if a mouse button is binded to QGLViewer::ROTATE for
  the QGLViewer::CAMERA (using mouseButtonState()). The state key that was previously used to move
  the camera is preserved. */
void QGLViewer::toggleCameraMode()
{
  bool revolveMode = cameraIsInRevolveMode();
  Qt::ButtonState bs;
  if (revolveMode)
    bs = mouseButtonState(CAMERA, ROTATE);
  else
    bs = mouseButtonState(CAMERA, MOVE_FORWARD);
  Qt::ButtonState stateKey = (Qt::ButtonState)(bs & Qt::KeyButtonMask);

  //#CONNECTION# setDefaultMouseBindings()
  if (revolveMode)
    {
      camera()->frame()->updateFlyUpVector();
      camera()->frame()->stopSpinning();

      setMouseBinding(stateKey | Qt::LeftButton,  CAMERA, MOVE_FORWARD);
      setMouseBinding(stateKey | Qt::MidButton,   CAMERA, LOOK_AROUND);
      setMouseBinding(stateKey | Qt::RightButton, CAMERA, MOVE_BACKWARD);

      setMouseBinding(stateKey | Qt::LeftButton  | Qt::MidButton,  CAMERA, ROLL);
      setMouseBinding(stateKey | Qt::RightButton | Qt::MidButton,  CAMERA, SCREEN_TRANSLATE);

      setMouseBinding(Qt::LeftButton,  NO_CLICK_ACTION, true);
      setMouseBinding(Qt::MidButton,   NO_CLICK_ACTION, true);
      setMouseBinding(Qt::RightButton, NO_CLICK_ACTION, true);

      setWheelBinding(stateKey, CAMERA, MOVE_FORWARD);
    }
  else
    {
      // Should stop flyTimer. But unlikely and not easy.
      setMouseBinding(stateKey | Qt::LeftButton,  CAMERA, ROTATE);
      setMouseBinding(stateKey | Qt::MidButton,   CAMERA, ZOOM);
      setMouseBinding(stateKey | Qt::RightButton, CAMERA, TRANSLATE);

      setMouseBinding(stateKey | Qt::LeftButton  | Qt::MidButton,  CAMERA, SCREEN_ROTATE);
      setMouseBinding(stateKey | Qt::RightButton | Qt::MidButton,  CAMERA, SCREEN_TRANSLATE);

      setMouseBinding(Qt::LeftButton,  ALIGN_CAMERA,      true);
      setMouseBinding(Qt::MidButton,   SHOW_ENTIRE_SCENE, true);
      setMouseBinding(Qt::RightButton, CENTER_SCENE,      true);

      setWheelBinding(stateKey, CAMERA, ZOOM);
    }
}

////////////////////////////////////////////////////////////////////////////////
//              M a n i p u l a t e d   f r a m e s                           //
////////////////////////////////////////////////////////////////////////////////

/*! Sets the viewer's manipulatedFrame().

Note that a qglviewer::ManipulatedCameraFrame can be set as the manipulatedFrame(): it is possible
to manipulate the camera of a first viewer in a second viewer.

Defining the \e own viewer's camera()->frame() as the manipulatedFrame() is possible and will result
in a classical camera manipulation. See the <a href="../examples/luxo.html">luxo example</a> for an
illustration. */
void QGLViewer::setManipulatedFrame(ManipulatedFrame* frame)
{
  if (manipulatedFrame())
    {
      manipulatedFrame()->stopSpinning();

      if (manipulatedFrame() != camera()->frame())
	{
	  disconnect(manipulatedFrame(), SIGNAL(manipulated()), this, SLOT(updateGL()));
	  disconnect(manipulatedFrame(), SIGNAL(spun()), this, SLOT(updateGL()));
	}
    }

  manipulatedFrame_ = frame;

  manipulatedFrameIsACamera_ = ((manipulatedFrame() != camera()->frame()) &&
				(dynamic_cast<ManipulatedCameraFrame*>(manipulatedFrame()) != NULL));

  if (manipulatedFrame())
    {
      // Prevent multiple connections, that would result in useless display updates
      if (manipulatedFrame() != camera()->frame())
	{
	  connect(manipulatedFrame(), SIGNAL(manipulated()), SLOT(updateGL()));
	  connect(manipulatedFrame(), SIGNAL(spun()), SLOT(updateGL()));
	}
    }
}

#ifndef DOXYGEN
////////////////////////////////////////////////////////////////////////////////
//                          V i s u a l   H i n t s                           //
////////////////////////////////////////////////////////////////////////////////
/*! Draws viewer related visual hints.

 Displays the new qglviewer::Camera::revolveAroundPoint() when it is changed. See the <a
 href="../mouse.html">mouse page</a> for details. Also draws a line between
 qglviewer::Camera::revolveAroundPoint() and mouse cursor when the camera is rotated around the
 camera Z axis.

 See also setVisualHintsMask() and resetVisualHints(). The hint color is foregroundColor().

 \note These methods may become more interesting one day. The current design is too limited and
 should be improved when other visual hints must be drawn.

 Limitation : One needs to have access to visualHint_ to overload this method.

 Removed from the documentation for this reason. */
void QGLViewer::drawVisualHints()
{
  // Revolve Around point cross
  if (visualHint_ & 1)
    {
      const float size = 15.0;
      Vec proj = camera()->projectedCoordinatesOf(camera()->revolveAroundPoint());
      startScreenCoordinatesSystem();
      glDisable(GL_LIGHTING);
      glDisable(GL_DEPTH_TEST);
      glLineWidth(3.0);
      glBegin(GL_LINES);
      glVertex2f(proj.x - size, proj.y);
      glVertex2f(proj.x + size, proj.y);
      glVertex2f(proj.x, proj.y - size);
      glVertex2f(proj.x, proj.y + size);
      glEnd();
      glEnable(GL_DEPTH_TEST);
      stopScreenCoordinatesSystem();
    }

  // if (visualHint_ & 2)
    // drawText(80, 10, "Play");

  // Screen rotate line
  ManipulatedFrame* mf = NULL;
  Vec pnt;
  if (camera()->frame()->action_ == SCREEN_ROTATE)
    {
      mf = camera()->frame();
      pnt = camera()->revolveAroundPoint();
    }
  if (manipulatedFrame() && (manipulatedFrame()->action_ == SCREEN_ROTATE))
    {
      mf = manipulatedFrame();
      // Maybe useful if the mf is a manipCameraFrame...
      // pnt = manipulatedFrame()->revolveAroundPoint();
      pnt = manipulatedFrame()->position();
    }

  if (mf)
    {
      pnt = camera()->projectedCoordinatesOf(pnt);
      startScreenCoordinatesSystem();
      glDisable(GL_LIGHTING);
      glDisable(GL_DEPTH_TEST);
      glLineWidth(3.0);
      glBegin(GL_LINES);
      glVertex2f(pnt.x, pnt.y);
      glVertex2f(mf->prevPos_.x(), mf->prevPos_.y());
      glEnd();
      glEnable(GL_DEPTH_TEST);
      stopScreenCoordinatesSystem();
    }

  // Zoom on region: draw a rectangle
  if (camera()->frame()->action_ == ZOOM_ON_REGION)
    {
      startScreenCoordinatesSystem();
      glDisable(GL_LIGHTING);
      glDisable(GL_DEPTH_TEST);
      glLineWidth(2.0);
      glBegin(GL_LINE_LOOP);
      glVertex2i(camera()->frame()->pressPos_.x(), camera()->frame()->pressPos_.y());
      glVertex2i(camera()->frame()->prevPos_.x(),  camera()->frame()->pressPos_.y());
      glVertex2i(camera()->frame()->prevPos_.x(),  camera()->frame()->prevPos_.y());
      glVertex2i(camera()->frame()->pressPos_.x(), camera()->frame()->prevPos_.y());
      glEnd();
      glEnable(GL_DEPTH_TEST);
      stopScreenCoordinatesSystem();
    }
}

/*! Defines the mask that will be used to drawVisualHints(). The only available mask is currently 1,
corresponding to the display of the qglviewer::Camera::revolveAroundPoint(). resetVisualHints() is
automatically called after \p delay milliseconds (default is 2 seconds). */
void QGLViewer::setVisualHintsMask(int mask, int delay)
{
  visualHint_ = visualHint_ | mask;
  QTimer::singleShot(delay, this, SLOT(resetVisualHints()));
}

/*! Reset the mask used by drawVisualHints(). Called by setVisualHintsMask() after 2 seconds to reset the display. */
void QGLViewer::resetVisualHints()
{
  visualHint_ = 0;
}
#endif

////////////////////////////////////////////////////////////////////////////////
//       A x i s   a n d   G r i d   d i s p l a y   l i s t s                //
////////////////////////////////////////////////////////////////////////////////

/*! Draws a 3D arrow along the positive Z axis.

 \p length, \p radius and \p nbSub subdivisions define its geometry. If \p radius is negative
 (default), it is set to 0.06 * \p length.

 Uses current color and does not modify the OpenGL state. Change the modelView to place the arrow in
 3D (see qglviewer::Frame::matrix()). */
void QGLViewer::drawArrow(float length, float radius, int nbSubdivisions)
{
  static GLUquadric* quadric = gluNewQuadric();

  if (radius < 0.0)
    radius = 0.05 * length;

  const float head = 2.5*(radius / length) + 0.1;
  const float coneRadiusCoef = 4.0 - 5.0 * head;

  gluCylinder(quadric, radius, radius, length * (1.0 - head/coneRadiusCoef), nbSubdivisions, 1);
  glTranslatef(0.0, 0.0, length * (1.0 - head));
  gluCylinder(quadric, coneRadiusCoef * radius, 0.0, head * length, nbSubdivisions, 1);
  glTranslatef(0.0, 0.0, -length * (1.0 - head));
}

/*! Draws an XYZ axis, with a given size (default is 1.0).

  The axis position and orientation depends on the current modelView matrix state. Use the following
  code to display the current position and orientation of a qglviewer::Frame:
  \code
  glPushMatrix();
  glMultMatrixd(frame.matrix());
  QGLViewer::drawAxis(sceneRadius() / 5.0); // Or any scale
  glPopMatrix();
  \endcode

  The current color is used to draw the X, Y and Z characters at the extremities of the three
  arrows. The OpenGL state is modified: \c GL_LIGHTING and \c GL_COLOR_MATERIAL are enabled and line
  width is set to 2.0.

  axisIsDrawn() uses this method to draw a representation of the world coordinate system. See also
  QGLViewer::drawArrow(). */
void QGLViewer::drawAxis(float length)
{
  const float charWidth = length / 40.0;
  const float charHeight = length / 30.0;
  const float charShift = 1.04 * length;

  glDisable(GL_LIGHTING);
  glLineWidth(2.0);

  glBegin(GL_LINES);
  // The X
  glVertex3f(charShift,  charWidth, -charHeight);
  glVertex3f(charShift, -charWidth,  charHeight);
  glVertex3f(charShift, -charWidth, -charHeight);
  glVertex3f(charShift,  charWidth,  charHeight);
  // The Y
  glVertex3f( charWidth, charShift, charHeight);
  glVertex3f(0.0,        charShift, 0.0);
  glVertex3f(-charWidth, charShift, charHeight);
  glVertex3f(0.0,        charShift, 0.0);
  glVertex3f(0.0,        charShift, 0.0);
  glVertex3f(0.0,        charShift, -charHeight);
  // The Z
  glVertex3f(-charWidth,  charHeight, charShift);
  glVertex3f( charWidth,  charHeight, charShift);
  glVertex3f( charWidth,  charHeight, charShift);
  glVertex3f(-charWidth, -charHeight, charShift);
  glVertex3f(-charWidth, -charHeight, charShift);
  glVertex3f( charWidth, -charHeight, charShift);
  glEnd();

  glEnable(GL_LIGHTING);
  glDisable(GL_COLOR_MATERIAL);

  float color[4];
  color[0] = 0.7f;  color[1] = 0.7f;  color[2] = 1.0f;  color[3] = 1.0f;
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
  QGLViewer::drawArrow(length, 0.01*length);

  color[0] = 1.0f;  color[1] = 0.7f;  color[2] = 0.7f;  color[3] = 1.0f;
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
  glPushMatrix();
  glRotatef(90.0, 0.0, 1.0, 0.0);
  QGLViewer::drawArrow(length, 0.01*length);
  glPopMatrix();

  color[0] = 0.7f;  color[1] = 1.0f;  color[2] = 0.7f;  color[3] = 1.0f;
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
  glPushMatrix();
  glRotatef(-90.0, 1.0, 0.0, 0.0);
  QGLViewer::drawArrow(length, 0.01*length);
  glPopMatrix();

  glEnable(GL_COLOR_MATERIAL);
}

/*! Draws a grid in the XY plane, centered on (0,0,0).

\p size (OpenGL units) and \p nbSubdivisions define its geometry. Set the \c GL_MODELVIEW matrix to place and
orientate the grid in 3D space. See the drawAxis() documentation.

\attention The OpenGL state is modified by this method: \c GL_LIGHTING is disabled and line width
is set to 1. */
void QGLViewer::drawGrid(float size, int nbSubdivisions)
// static void createGridDL(GLuint& dlNumber, float length=1.0f, float width=1.0f, float nbSub=10)
{
  glDisable(GL_LIGHTING);
  glLineWidth(1.0);

  glBegin(GL_LINES);
  for (int i=0; i<=nbSubdivisions; ++i)
    {
      const float pos = size*(2.0*i/nbSubdivisions-1.0);
      glVertex2f(pos, -size);
      glVertex2f(pos, +size);
      glVertex2f(-size, pos);
      glVertex2f( size, pos);
    }
  glEnd();
}

////////////////////////////////////////////////////////////////////////////////
//       S t a t i c    m e t h o d s   :  Q G L V i e w e r   P o o l        //
////////////////////////////////////////////////////////////////////////////////

/*! saveStateToFile() is called on all the QGLViewers using the QGLViewerPool(). */
void QGLViewer::saveStateToFileForAllViewers()
{
  QPtrListIterator<QGLViewer> it(QGLViewer::QGLViewerPool());
  for (QGLViewer* viewer; (viewer = it.current()) != 0; ++it)
    viewer->saveStateToFile();
}

//////////////////////////////////////////////////////////////////////////
//       S a v e   s t a t e   b e t w e e n    s e s s i o n s         //
//////////////////////////////////////////////////////////////////////////

/*! Returns the state file name. Default value is \c .qglviewer.xml.

 This is the name of the XML file where saveStateToFile() saves the viewer state (camera state,
 widget geometry, display flags... see domElement()) on exit. Use restoreStateFromFile() to restore
 this state later (usually in your init() method).

 Setting this value to \c QString::null will disable the automatic state file saving that normally
 occurs on exit.

 If more than one viewer are created by the application, this function will return a numbered file
 name (as in ".qglviewer1.xml", ".qglviewer2.xml"... using QGLViewer::QGLViewerIndex()) for extra
 viewers. Each viewer will then read back its own information in restoreStateFromFile(), provided
 that the viewers are created in the same order, which is usually the case. */
QString QGLViewer::stateFileName() const
{
  QString name = stateFileName_;

  if (!name.isEmpty() && QGLViewer::QGLViewerIndex(this) > 0)
    {
      QFileInfo fi(name);
      if (fi.extension(false).isEmpty())
	name += QString::number(QGLViewer::QGLViewerIndex(this));
      else
#if QT_VERSION < 0x030000
	name = fi.dirPath() + '/' + fi.baseName() + QString::number(QGLViewer::QGLViewerIndex(this)) + "." + fi.extension();
#else
	name = fi.dirPath() + '/' + fi.baseName(true) + QString::number(QGLViewer::QGLViewerIndex(this)) + "." + fi.extension(false);
#endif
    }

  return name;
}

/*! Saves in stateFileName() an XML representation of the QGLViewer state, obtained from
 domElement().

 Use restoreStateFromFile() to restore this viewer state.

 This method is automatically called when a viewer is closed (using Escape or using the window's
 upper right \c x close button). setStateFileName() to \c QString::null to prevent this. */
void QGLViewer::saveStateToFile()
{
  QString name = stateFileName();

  if (name.isEmpty())
    return;

  QFileInfo fileInfo(name);

  if (fileInfo.isDir())
    {
      QMessageBox::warning(this, "Save to file error", "State file name is a directory ("+name+") and not a file.");
      return;
    }

  QString dirName = fileInfo.dirPath();
  if (!QFileInfo(dirName).exists())
    {
      QDir dir;
      if (!(dir.mkdir(dirName, true)))
	{
	  QMessageBox::warning(this, "Save to file error", "Unable to create directory "+dirName);
	  return;
	}
    }

  // Write the DOM tree to file
  QFile f(name);
  if (f.open(IO_WriteOnly))
    {
      QTextStream out(&f);
      QDomDocument doc("QGLVIEWER");
      doc.appendChild(domElement("QGLViewer", doc));
      doc.save(out, 2);
      f.flush();
      f.close();
    }
  else
#if QT_VERSION < 0x030200
    QMessageBox::warning(this, "Save to file error", "Unable to save to file "+name);
#else
    QMessageBox::warning(this, "Save to file error", "Unable to save to file "+name+":\n"+f.errorString());
#endif
}

/*! Restores the QGLViewer state from the stateFileName() file using initFromDOMElement().

 States are saved using saveStateToFile(), which is automatically called on viewer exit.

 Returns \c true when the restoration is successful. Possible problems are an non existing or
 unreadable stateFileName() file, an empty stateFileName() or an XML syntax error.

 A manipulatedFrame() should be defined \e before calling this method, so that its state can be
 restored. Initialization code put \e after this function will override saved values:
 \code
 void Viewer::init()
 {
   // Default initialization goes here (including the declaration of a possible manipulatedFrame).

   if (!restoreStateFromFile())
     showEntireScene(); // Previous state cannot be restored: fit camera to scene.

   // Specific initialization that overrides file savings goes here.
 }
 \endcode */
bool QGLViewer::restoreStateFromFile()
{
  QString name = stateFileName();

  if (name.isEmpty())
    return false;

  QFileInfo fileInfo(name);

  if (!fileInfo.isFile())
    // No warning since it would be displayed at first start.
    return false;

  if (!fileInfo.isReadable())
    {
      QMessageBox::warning(this, "restoreStateFromFile problem", "File "+name+" is not readable.");
      return false;
    }

  // Read the DOM tree form file
  QFile f(name);
  if (f.open(IO_ReadOnly) == true)
    {
      QDomDocument doc;
      doc.setContent(&f);
      f.close();
      QDomElement main = doc.documentElement();
      initFromDOMElement(main);
    }
  else
    {
#if QT_VERSION < 0x030200
      QMessageBox::warning(this, "Open file error", "Unable to open file "+name);
#else
      QMessageBox::warning(this, "Open file error", "Unable to open file "+name+":\n"+f.errorString());
#endif
      return false;
    }

  return true;
}

/*! Returns an XML \c QDomElement that represents the QGLViewer.

 Used by saveStateToFile(). restoreStateFromFile() uses initFromDOMElement() to restore the
 QGLViewer state from the resulting \c QDomElement.

 \p name is the name of the QDomElement tag. \p doc is the \c QDomDocument factory used to create
 QDomElement.

 The created QDomElement contains state values (axisIsDrawn(), FPSIsDisplayed(), isFullScreen()...),
 viewer geometry, as well as camera() (see qglviewer::Camera::domElement()) and manipulatedFrame()
 (if defined, see qglviewer::ManipulatedFrame::domElement()) states.

 Overload this method to add your own attributes to the state file:
 \code
 QDomElement Viewer::domElement(const QString& name, QDomDocument& document) const
 {
   QDomElement de = document.createElement("Light");
   de.setAttribute("state", (lightIsOn()?"on":"off"));
   de.appendChild(lightManipulatedFrame()->domElement("LightFrame", document));

   // Get default state domElement and append custom node
   QDomElement res = QGLViewer::domElement(name, document);
   res.appendChild(de);
   return res;
 }
 \endcode
 See initFromDOMElement() for the associated restoration code.

 \attention For the manipulatedFrame(), qglviewer::Frame::constraint() and
 qglviewer::Frame::referenceFrame() are not saved. See qglviewer::Frame::domElement(). */
QDomElement QGLViewer::domElement(const QString& name, QDomDocument& document) const
{
  QDomElement de = document.createElement(name);
  de.setAttribute("version", QGLViewerVersionString());

  QDomElement stateNode = document.createElement("State");
  // stateNode.setAttribute("mouseTracking", (hasMouseTracking()?"true":"false"));
  stateNode.appendChild(DomUtils::QColorDomElement(foregroundColor(), "foregroundColor", document));
  stateNode.appendChild(DomUtils::QColorDomElement(backgroundColor(), "backgroundColor", document));
  stateNode.setAttribute("stereo", (displaysInStereo()?"true":"false"));
  stateNode.setAttribute("cameraMode", (cameraIsInRevolveMode()?"revolve":"fly"));
  de.appendChild(stateNode);

  QDomElement displayNode = document.createElement("Display");
  displayNode.setAttribute("axisIsDrawn",       (axisIsDrawn()?"true":"false"));
  displayNode.setAttribute("gridIsDrawn",       (gridIsDrawn()?"true":"false"));
  displayNode.setAttribute("FPSIsDisplayed",    (FPSIsDisplayed()?"true":"false"));
  displayNode.setAttribute("cameraIsEdited",    (cameraIsEdited()?"true":"false"));
  displayNode.setAttribute("zBufferIsDisplayed",(zBufferIsDisplayed()?"true":"false"));
  // displayNode.setAttribute("textIsEnabled",  (textIsEnabled()?"true":"false"));
  de.appendChild(displayNode);

  QDomElement geometryNode = document.createElement("Geometry");
  geometryNode.setAttribute("fullScreen", (isFullScreen()?"true":"false"));
  if (isFullScreen())
    {
      geometryNode.setAttribute("prevPosX", QString::number(prevPos_.x()));
      geometryNode.setAttribute("prevPosY", QString::number(prevPos_.y()));
    }
  else
    {
      QWidget* tlw = topLevelWidget();
      geometryNode.setAttribute("width",  QString::number(tlw->width()));
      geometryNode.setAttribute("height", QString::number(tlw->height()));
      geometryNode.setAttribute("posX",   QString::number(tlw->pos().x()));
      geometryNode.setAttribute("posY",   QString::number(tlw->pos().y()));
    }
  de.appendChild(geometryNode);

  // Restore original Camera zClippingCoefficient before saving.
  if (cameraIsEdited())
    camera()->setZClippingCoefficient(previousCameraZClippingCoefficient_);
  de.appendChild(camera()->domElement("Camera", document));
  if (cameraIsEdited())
    camera()->setZClippingCoefficient(5.0);

  if (manipulatedFrame())
    de.appendChild(manipulatedFrame()->domElement("ManipulatedFrame", document));

  return de;
}

/*! Restores the QGLViewer state from a \c QDomElement created by domElement().

 Used by restoreStateFromFile() to restore the QGLViewer state from a file.

 Overload this method to retrieve custom attributes from the QGLViewer state file. This code
 corresponds to the one given in the domElement() documentation:
 \code
 void Viewer::initFromDOMElement(const QDomElement& element)
 {
   // Restore standard state
   QGLViewer::initFromDOMElement(element);

   QDomElement child=element.firstChild().toElement();
   while (!child.isNull())
   {
     if (child.tagName() == "Light")
     {
       if (child.hasAttribute("state"))
	 setLightOn(child.attribute("state").lower() == "on");

       // Assumes there is only one child. Otherwise you need to parse child's children recursively.
       QDomElement lf = child.firstChild().toElement();
       if (!lf.isNull() && lf.tagName() == "LightFrame")
         lightManipulatedFrame()->initFromDomElement(lf);
     }
     child = child.nextSibling().toElement();
   }
 }
 \endcode

 See also qglviewer::Camera::initFromDOMElement(), qglviewer::ManipulatedFrame::initFromDOMElement().

 \note The manipulatedFrame() \e pointer is not modified by this method. If it is defined, its state
 is however restored from the \p element values. */
void QGLViewer::initFromDOMElement(const QDomElement& element)
{
  const QString version = element.attribute("version");
  if (version != QGLViewerVersionString())
    qWarning("State file created using QGLViewer version "+version+" and current version is "+QGLViewerVersionString());

  QDomElement child=element.firstChild().toElement();
  while (!child.isNull())
    {
      if (child.tagName() == "State")
	{
	  // #CONNECTION# default values from defaultConstructor()
	  // setMouseTracking(DomUtils::boolFromDom(child, "mouseTracking", false));
	  setStereoDisplay(DomUtils::boolFromDom(child, "stereo", false));
	  if ((child.attribute("cameraMode", "revolve") == "fly") && (cameraIsInRevolveMode()))
	    toggleCameraMode();

	  QDomElement ch=child.firstChild().toElement();
	  while (!ch.isNull())
	    {
	      if (ch.tagName() == "foregroundColor")
		setForegroundColor(DomUtils::QColorFromDom(ch));
	      if (ch.tagName() == "backgroundColor")
		setBackgroundColor(DomUtils::QColorFromDom(ch));
	      ch = ch.nextSibling().toElement();
	    }
	}

      if (child.tagName() == "Display")
	{
	  // #CONNECTION# default values from defaultConstructor()
	  setAxisIsDrawn(DomUtils::boolFromDom(child, "axisIsDrawn", false));
	  setGridIsDrawn(DomUtils::boolFromDom(child, "gridIsDrawn", false));
	  setFPSIsDisplayed(DomUtils::boolFromDom(child, "FPSIsDisplayed", false));
	  setZBufferIsDisplayed(DomUtils::boolFromDom(child, "zBufferIsDisplayed", false));
	  // Possible problem here. The Camera should be restored before the Display
	  // Its zClippingCoefficient would then be initialized before editCameraPaths sets it to 5.0
	  setCameraIsEdited(DomUtils::boolFromDom(child, "cameraIsEdited", false));
	  // setTextIsEnabled(DomUtils::boolFromDom(child, "textIsEnabled", true));
	}

      if (child.tagName() == "Geometry")
	{
	  setFullScreen(DomUtils::boolFromDom(child, "fullScreen", false));

	  if (isFullScreen())
	    {
	      prevPos_.setX(DomUtils::intFromDom(child, "prevPosX", 0));
	      prevPos_.setY(DomUtils::intFromDom(child, "prevPosY", 0));
	    }
	  else
	    {
	      int width  = DomUtils::intFromDom(child, "width",  600);
	      int height = DomUtils::intFromDom(child, "height", 400);
	      topLevelWidget()->resize(width, height);

	      QPoint pos;
	      pos.setX(DomUtils::intFromDom(child, "posX", 0));
	      pos.setY(DomUtils::intFromDom(child, "posY", 0));
	      topLevelWidget()->move(pos);
	    }
	}

      if (child.tagName() == "Camera")
	{
	  connectAllCameraKFIInterpolatedSignals(false);
	  camera()->initFromDOMElement(child);
	  connectAllCameraKFIInterpolatedSignals();
	}

      if ((child.tagName() == "ManipulatedFrame") && (manipulatedFrame()))
	manipulatedFrame()->initFromDOMElement(child);

      child = child.nextSibling().toElement();
    }
}

#ifndef DOXYGEN
/*! This method is deprecated since version 1.3.9-5. Use saveStateToFile() and setStateFileName()
  instead. */
void QGLViewer::saveToFile(const QString& fileName)
{
  if (!fileName.isEmpty())
    setStateFileName(fileName);

  qWarning("saveToFile() is deprecated, use saveStateToFile() instead.");
  saveStateToFile();
}

/*! This function is deprecated since version 1.3.9-5. Use restoreStateFromFile() and
  setStateFileName() instead. */
bool QGLViewer::restoreFromFile(const QString& fileName)
{
  if (!fileName.isEmpty())
    setStateFileName(fileName);

  qWarning("restoreFromFile() is deprecated, use restoreStateFromFile() instead.");
  return restoreStateFromFile();
}
#endif

/* Makes a copy of the current buffer into a texture.

 Creates a texture (when needed) and use glCopyTexSubImage2D() to directly copy the buffer in it.

 Use \p internalFormat and \p format to define the texture format and hence which and how components
 of the buffer are copied into the texture. See the glTexImage2D() documentation for details.

 When \p format is c GL_NONE (default), its value is set to \p internalFormat, which fits most
 cases. Typical \p internalFormat (and \p format) values are \c GL_DEPTH_COMPONENT and \c GL_RGBA.
 Use \c GL_LUMINANCE as the \p internalFormat and \c GL_RED, \c GL_GREEN or \c GL_BLUE as \p format
 to capture a single color component as a luminance (grey scaled) value. Note that \c GL_STENCIL is
 not supported as a format.

 The texture has dimensions which are powers of two. It is as small as possible while always being
 larger or equal to the current size of the widget. The buffer image hence does not entirely fill
 the texture: it is stuck to the lower left corner (corresponding to the (0,0) texture coordinates).
 Use bufferTextureMaxU() and bufferTextureMaxV() to get the upper right corner maximum u and v
 texture coordinates. Use bufferTextureId() to retrieve the id of the created texture.

 Once the texture is created, you will typically use it to texture a quad with (0,0) to
 (bufferTextureMaxU(), bufferTextureMaxV()) texture coordinates. Use
 startScreenCoordinatesSystem(true) to draw a quad facing the camera, with dimensions expressed in
 pixels.

 Use glReadBuffer() to select which buffer is copied into the texture. See also glPixelTransfer(),
 glPixelZoom() and glCopyPixel for pixel color transformations during copy.

 \note The \c GL_DEPTH_COMPONENT format may not be supported by all hardware. It may sometimes be
 emulated in software, resulting in poor performances.

 \note The bufferTextureId() texture is binded at the end of this method. */
void QGLViewer::copyBufferToTexture(GLint internalFormat, GLenum format)
{
  int h = 16;
  int w = 16;
  while (w < width())
    w <<= 1;
  while (h < height())
    h <<= 1;

  bool init = false;

  if ((w != bufferTextureWidth_) || (h != bufferTextureHeight_))
    {
      bufferTextureWidth_ = w;
      bufferTextureHeight_ = h;
      bufferTextureMaxU_ = width()  / float(bufferTextureWidth_);
      bufferTextureMaxV_ = height() / float(bufferTextureHeight_);
      init = true;
    }

  if (bufferTextureId() == 0)
    {
      glGenTextures(1, &bufferTextureId_);
      glBindTexture(GL_TEXTURE_2D, bufferTextureId_);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      init = true;
    }
  else
    glBindTexture(GL_TEXTURE_2D, bufferTextureId_);

  if ((format != previousBufferTextureFormat_) ||
      (internalFormat != previousBufferTextureInternalFormat_))
    {
      previousBufferTextureFormat_ = format;
      previousBufferTextureInternalFormat_ = internalFormat;
      init = true;
    }

  if (init)
    {
      if (format == GL_NONE)
	format = internalFormat;

      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, bufferTextureWidth_, bufferTextureHeight_, 0, format, GL_UNSIGNED_BYTE, NULL);

      // glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, bufferTextureWidth_, bufferTextureHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);// rapide gris
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, bufferTextureWidth_, bufferTextureHeight_, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL); // gris
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, bufferTextureWidth_, bufferTextureHeight_, 0, GL_LUMINANCE, GL_FLOAT, NULL); //rapide gris


      // glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, bufferTextureWidth_, bufferTextureHeight_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL); //lent ok
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, bufferTextureWidth_, bufferTextureHeight_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL); // idem
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferTextureWidth_, bufferTextureHeight_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL); // couleur f
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferTextureWidth_, bufferTextureHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);  // rapide et couleur


      // glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, bufferTextureWidth_, bufferTextureHeight_, 0, GL_RED, GL_UNSIGNED_BYTE, NULL); //ok
      // glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, bufferTextureWidth_, bufferTextureHeight_, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL); // ok
    }

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width(), height());
}


/* Displays the current z-buffer in the color buffer.

 Assumes copyBufferToTexture() has previously been called with a \c GL_DEPTH_COMPONENT parameter.
 Useful for debugging purposes. */
void QGLViewer::displayZBuffer() const
{
  glMatrixMode(GL_TEXTURE);
  glLoadIdentity();

  startScreenCoordinatesSystem(true);

  // glBind is done just before in copyBufferToTexture
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  // Reset texture setup
  glDisable(GL_TEXTURE_GEN_S);
  glDisable(GL_TEXTURE_GEN_T);
  glDisable(GL_TEXTURE_GEN_R);
  glDisable(GL_TEXTURE_GEN_Q);

  glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0, 0.0);
  glVertex2i(0, 0);
  glTexCoord2f(bufferTextureMaxU(), 0.0);
  glVertex2i(width(), 0);
  glTexCoord2f(bufferTextureMaxU(), bufferTextureMaxV());
  glVertex2i(width(), height());
  glTexCoord2f(0.0, bufferTextureMaxV());
  glVertex2i(0, height());
  glEnd();
  glDisable(GL_TEXTURE_2D);

  stopScreenCoordinatesSystem();
}

/*! Returns the texture id of the texture created by copyBufferToTexture().

Use glBindTexture() to use this texture. Note that this is already done by copyBufferToTexture().

Returns \c 0 is copyBufferToTexture() was never called or if the texure was deleted using
glDeleteTextures() since then. */
GLuint QGLViewer::bufferTextureId() const
{
  if (glIsTexture(bufferTextureId_))
    return bufferTextureId_;
  else
    return 0;
}
