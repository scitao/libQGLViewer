#include "multiView.h"

using namespace qglviewer;
using namespace std;

Viewer::Viewer(const Scene* const s, int type, QWidget* parent, const QGLWidget* shareWidget)
  : QGLViewer(parent, "viewer", shareWidget), scene_(s)
{
  setAxisIsDrawn();
  setGridIsDrawn();

  if (type < 3)
    {
      // Move camera according to viewer type (on X, Y or Z axis)
      camera()->setPosition(Vec((type==0)? 1.0 : 0.0, (type==1)? 1.0 : 0.0, (type==2)? 1.0 : 0.0));
      camera()->lookAt(sceneCenter());

      camera()->setType(Camera::ORTHOGRAPHIC);
      camera()->showEntireScene();

      // Forbid rotation
      WorldConstraint* constraint = new WorldConstraint();
      constraint->setRotationConstraintType(AxisPlaneConstraint::FORBIDDEN);
      camera()->frame()->setConstraint(constraint);
    }

  restoreStateFromFile();
}

void Viewer::draw()
{
  scene_->draw();
}

// Draws a spiral
void Scene::draw() const
{
  const float nbSteps = 200.0;
  glBegin(GL_QUAD_STRIP);
  for (float i=0; i<nbSteps; ++i)
    {
      float ratio = i/nbSteps;
      float angle = 21.0*ratio;
      float c = cos(angle);
      float s = sin(angle);
      float r1 = 1.0 - 0.8*ratio;
      float r2 = 0.8 - 0.8*ratio;
      float alt = ratio - 0.5;
      const float nor = .5;
      const float up = sqrt(1.0-nor*nor);
      glColor3f(1-ratio, .2 , ratio);
      glNormal3f(nor*c, up, nor*s);
      glVertex3f(r1*c, alt, r1*s);
      glVertex3f(r2*c, alt+0.05, r2*s);
    }
  glEnd();
}
