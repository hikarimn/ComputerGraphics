////////////////////////////////////////////////////////////////////////
//
//   Source code based on asst.cpp by
//   Professor Steven Gortler
//
////////////////////////////////////////////////////////////////////////
//NEW ONE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#pragma
#include "pch.h"
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#define _USE_MATH_DEFINES // for C++
#include <cmath>
#include <stdio.h>      /* printf */
#include <math.h> 

#include <GL/glew.h>
#include <GL/glut.h>

#include "cvec.h"
#include "matrix4.h"
#include "rigtform.h"
#include "geometrymaker.h"
#include "ppm.h"
#include "glsupport.h"

using namespace std;      // for string, vector, iostream, and other standard C++ stuff

// G L O B A L S ///////////////////////////////////////////////////

static const float g_frustMinFov = 60.0;  // A minimal of 60 degree field of view
static float g_frustFovY = g_frustMinFov; // FOV in y direction (updated by updateFrustFovY)

static const float g_frustNear = -0.1;    // near plane
static const float g_frustFar = -50.0;    // far plane
static const float g_groundY = -2.0;      // y coordinate of the ground
static const float g_groundSize = 10.0;   // half the ground length

static int g_windowWidth = 512;
static int g_windowHeight = 512;
static bool g_mouseClickDown = false;    // is the mouse button pressed
static bool g_mouseLClickButton, g_mouseRClickButton, g_mouseMClickButton;
static int g_mouseClickX, g_mouseClickY; // coordinates for mouse click event
static int g_activeShader = 0;
static int g_multisample = 0;


struct ShaderState {
	GlProgram program;

	// Handles to uniform variables
	GLint h_uLight, h_uLight2;
	GLint h_uProjMatrix;
	GLint h_uModelViewMatrix;
	GLint h_uNormalMatrix;
	GLint h_uColor;
	GLint h_uUseTexture;
	GLint h_uTexUnit;
	GLint h_aTexCoord;
	GLint h_uTexUnit1;
	GLint h_uSphere;

	// Handles to vertex attributes
	GLint h_aPosition;
	GLint h_aNormal;
	GLint h_aTexCoord0;
	GLint h_aBinormal;
	GLint h_aTangent;


	ShaderState(const char* vsfn, const char* fsfn) {
		readAndCompileShader(program, vsfn, fsfn);

		const GLuint h = program; // short hand

		// Retrieve handles to uniform variables
		h_uLight = safe_glGetUniformLocation(h, "uLight");
		h_uProjMatrix = safe_glGetUniformLocation(h, "uProjMatrix");
		h_uModelViewMatrix = safe_glGetUniformLocation(h, "uModelViewMatrix");
		h_uNormalMatrix = safe_glGetUniformLocation(h, "uNormalMatrix");
		h_uColor = safe_glGetUniformLocation(h, "uColor");

		// Retrieve handles to vertex attributes
		h_aPosition = safe_glGetAttribLocation(h, "aPosition");
		h_aNormal = safe_glGetAttribLocation(h, "aNormal");

		checkGlErrors();
	}

};

static const int g_numShaders = 2;
static const char * const g_shaderFiles[g_numShaders][2] = {
  {"./shaders/basic.vshader", "./shaders/diffuse.fshader"},
  {"./shaders/basic.vshader", "./shaders/specular.fshader"}
};
static vector<shared_ptr<ShaderState> > g_shaderStates; // our global shader states

// --------- Geometry

// Macro used to obtain relative offset of a field within a struct
#define FIELD_OFFSET(StructType, field) &(((StructType *)0)->field)

// A vertex with floating point position and normal
struct VertexPN {
	Cvec3f p, n;

	VertexPN() {}
	VertexPN(float x, float y, float z,
		float nx, float ny, float nz)
		: p(x, y, z), n(nx, ny, nz)
	{}

	VertexPN(const SmallVertex& v)
		: p(v.pos), n(v.normal)
	{}

	// Define copy constructor and assignment operator from GenericVertex so we can
	// use make* functions from geometrymaker.h
	VertexPN(const GenericVertex& v) {
		*this = v;
	}

	VertexPN& operator = (const GenericVertex& v) {
		p = v.pos;
		n = v.normal;
		return *this;
	}
};

struct Geometry {
	GlBufferObject vbo, ibo;
	int vboLen, iboLen;
	int strips;


	Geometry(VertexPN *vtx, unsigned short *idx, int vboLen, int iboLen, int strip_count = 0) {
		this->vboLen = vboLen;
		this->iboLen = iboLen;
		this->strips = strip_count;

		// Now create the VBO and IBO
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPN) * vboLen, vtx, GL_STATIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned short) * iboLen, idx, GL_STATIC_DRAW);
	}


	void draw(const ShaderState& curSS) {
		// Enable the attributes used by our shader
		safe_glEnableVertexAttribArray(curSS.h_aPosition);
		safe_glEnableVertexAttribArray(curSS.h_aNormal);
		
		// bind vbo
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		safe_glVertexAttribPointer(curSS.h_aPosition, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), FIELD_OFFSET(VertexPN, p));
		safe_glVertexAttribPointer(curSS.h_aNormal, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPN), FIELD_OFFSET(VertexPN, n));

		// bind ibo
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

		// draw!
		if (strips > 0)
			for (int s = 0; s < strips; s++)
				glDrawElements(GL_TRIANGLE_STRIP, iboLen / strips, GL_UNSIGNED_SHORT,
				(const GLvoid*)(s*iboLen / strips * sizeof(unsigned short)));
		else
			glDrawElements(GL_TRIANGLES, iboLen, GL_UNSIGNED_SHORT, 0);

		// Disable the attributes used by our shader
		safe_glDisableVertexAttribArray(curSS.h_aPosition);
		safe_glDisableVertexAttribArray(curSS.h_aNormal);
	}
};


// Vertex buffer and index buffer associated with the ground and wall geometry amd sphere geometry
static shared_ptr<Geometry> g_ground, g_arch,g_arch1, g_arch2;

// --------- Scene

static const Cvec3 g_light1(2.0, 3.0, 14.0), g_light2(-2, -3.0, -5.0);  // define two lights positions in world space
static Cvec3 eyeRbtInit = Cvec3(0.0,0.25,8.0);
static Matrix4 g_skyRbt = Matrix4::makeTranslation(eyeRbtInit);

static int g_active = 0;
static Matrix4 g_Arch = Matrix4::makeTranslation(Cvec3(0,3.5,0));
static Cvec3f g_archColor = Cvec3f(0.45, 0.45, 0.45);
static Matrix4 g_objectRbt[1] = { g_Arch };
static RigTForm A = RigTForm(Cvec3(0,1,0), Quat(0.0, Cvec3(0.0, 0.0, 4.0)));
static RigTForm M1 = RigTForm(Quat::makeYRotation(10));
static RigTForm M2 = RigTForm(Quat::makeYRotation(-10));

static Matrix4 eyeRbt = g_skyRbt;
static Matrix4 invEyeRbt = inv(eyeRbt);

///////////////// END OF G L O B A L S //////////////////////////////////////////////////

static void initGround() {
	// A x-z plane at y = g_groundY of dimension [-g_groundSize, g_groundSize]^2
	VertexPN vtx[4] = {
	  VertexPN(-g_groundSize, g_groundY, -g_groundSize, 0, 1, 0),
	  VertexPN(-g_groundSize, g_groundY,  g_groundSize, 0, 1, 0),
	  VertexPN(g_groundSize, g_groundY,  g_groundSize, 0, 1, 0),
	  VertexPN(g_groundSize, g_groundY, -g_groundSize, 0, 1, 0),
	};
	unsigned short idx[] = { 0, 1, 2, 0, 2, 3 };
	g_ground.reset(new Geometry(&vtx[0], &idx[0], 4, 6, false));
}

//http://mathworld.wolfram.com/Catenary.html


Cvec3f makeArchV(float t, float a, Cvec3f point) {
	Cvec4f T = Cvec4f(1, -sinh(t / a), 0, 0).normalize();
	Cvec4f N = Cvec4f(T[1], -T[0], 0, 0).normalize();
	Cvec4f B = Cvec4f(cross(Cvec3f(N), Cvec3f(T)), 0).normalize();
	Cvec4f p = Cvec4f(t, -a*cosh(t / a), 0, 1);
	Matrix4 m4 = Matrix4(N, T, B, p);
	return Cvec3f(m4 * Cvec4f(point,1));
}


static void initArch() {
	float a1 = 0.5;
	float a2 = 0.5;
	float a3 = 0.5;
	float side = 0.5;
	int steps = 80;
	int iblen, vblen;
	getArchVbIbLen(steps, vblen, iblen);

	vector<VertexPN> vtx(vblen);
	vector<unsigned short> idx(iblen);
	Cvec3f point1 = Cvec3f(-2, 0, 0); // starting point
	Cvec3f point2 = Cvec3f(point1[0] + sqrt(side*side - side*side/4), 0, point1[2] - side/2);
	Cvec3f point3 = Cvec3f(point1[0], 0, point1[2] - side);

	makeArch(a1, a2, a3, steps, point1, point2, point3, makeArchV,  vtx.begin(), idx.begin());
	g_arch.reset(new Geometry(&vtx[0], &idx[0], vblen, iblen, 1));

	makeArch(a2, a3, a1, steps, point2, point3, point1, makeArchV,  vtx.begin(), idx.begin());
	g_arch1.reset(new Geometry(&vtx[0], &idx[0], vblen, iblen, 1));

	makeArch(a1, a3, a2, steps, point3, point1,point2, makeArchV,  vtx.begin(), idx.begin());
	g_arch2.reset(new Geometry(&vtx[0], &idx[0], vblen, iblen, 1));

	cout << "initArch" << endl;
}

// takes a projection matrix and send to the the shaders
static void sendProjectionMatrix(const ShaderState& curSS, const Matrix4& projMatrix) {
	GLfloat glmatrix[16];
	projMatrix.writeToColumnMajorMatrix(glmatrix); // send projection matrix
	safe_glUniformMatrix4fv(curSS.h_uProjMatrix, glmatrix);
}

// takes MVM and its normal matrix to the shaders
static void sendModelViewNormalMatrix(const ShaderState& curSS, const Matrix4& MVM, const Matrix4& NMVM) {
	GLfloat glmatrix[16];
	MVM.writeToColumnMajorMatrix(glmatrix); // send MVM
	safe_glUniformMatrix4fv(curSS.h_uModelViewMatrix, glmatrix);

	NMVM.writeToColumnMajorMatrix(glmatrix); // send NMVM
	safe_glUniformMatrix4fv(curSS.h_uNormalMatrix, glmatrix);
}

// update g_frustFovY from g_frustMinFov, g_windowWidth, and g_windowHeight
static void updateFrustFovY() {
	if (g_windowWidth >= g_windowHeight)
		g_frustFovY = g_frustMinFov;
	else {
		const double RAD_PER_DEG = 0.5 * CS175_PI / 180;
		g_frustFovY = atan2(sin(g_frustMinFov * RAD_PER_DEG) * g_windowHeight / g_windowWidth, cos(g_frustMinFov * RAD_PER_DEG)) / RAD_PER_DEG;
	}
}

static Matrix4 makeProjectionMatrix() {
	return Matrix4::makeProjection(
		g_frustFovY, g_windowWidth / static_cast <double> (g_windowHeight),
		g_frustNear, g_frustFar);
}

static void drawStuff() {
	// short hand for current shader state
	const ShaderState& curSS = *g_shaderStates[g_activeShader];

	// build & send proj. matrix to vshader
	const Matrix4 projmat = makeProjectionMatrix();
	sendProjectionMatrix(curSS, projmat);
	
	invEyeRbt = inv(eyeRbt);

	const Cvec3 eyeLight1 = Cvec3(invEyeRbt * Cvec4(g_light1, 1)); // g_light1 position in eye coordinates
	const Cvec3 eyeLight2 = Cvec3(invEyeRbt * Cvec4(g_light2, 1)); // g_light2 position in eye coordinates
	safe_glUniform3f(curSS.h_uLight, eyeLight1[0], eyeLight1[1], eyeLight1[2]);
	safe_glUniform3f(curSS.h_uLight2, eyeLight2[0], eyeLight2[1], eyeLight2[2]);

	safe_glUniform1i(curSS.h_uUseTexture, 0); // Turn off textures for the ground and the first cube

	// draw ground
	// ===========
	//
	const Matrix4 groundRbt = Matrix4();  // identity
	Matrix4 MVM = invEyeRbt * groundRbt;
	Matrix4 NMVM = normalMatrix(MVM);
	sendModelViewNormalMatrix(curSS, MVM, NMVM);
	safe_glUniform3f(curSS.h_uColor, 0.1, 0.95, 0.1); // set color

	g_ground->draw(curSS);

	//draw an arch
	// ==========
	//
	MVM = invEyeRbt * g_objectRbt[0];
	NMVM = normalMatrix(MVM);
	sendModelViewNormalMatrix(curSS, MVM, NMVM);
	safe_glUniform3f(curSS.h_uColor, g_archColor[0], g_archColor[1], g_archColor[2]);
	g_arch->draw(curSS);
	g_arch1->draw(curSS);
	g_arch2->draw(curSS);
}

static void display() {
	glUseProgram(g_shaderStates[g_activeShader]->program);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);                   // clear framebuffer color&depth

	if (g_multisample) {
		glEnable(GL_MULTISAMPLE_ARB);
	}
	else {
		glDisable(GL_MULTISAMPLE_ARB);
	}

	drawStuff();

	glutSwapBuffers();                                    // show the back buffer (where we rendered stuff)

	checkGlErrors();
}

static void reshape(const int w, const int h) {
	g_windowWidth = w;
	g_windowHeight = h;
	glViewport(0, 0, w, h);
	cerr << "Size of window is now " << w << "x" << h << endl;
	updateFrustFovY();
	glutPostRedisplay();
}
static void motion(const int x, const int y) {
	const double dx = x - g_mouseClickX;
	const double dy = g_windowHeight - y - 1 - g_mouseClickY;

	if (g_mouseClickDown) {
		//new part
		Matrix4 A = transFact(g_objectRbt[g_active]) * linFact(g_skyRbt);
		Matrix4 M;
		if (g_mouseLClickButton && !g_mouseRClickButton) { // left button down?
			// do m to o with respect to a
			M = Matrix4::makeXRotation(-dy) * Matrix4::makeYRotation(dx);
			g_objectRbt[g_active] = A * M*inv(A)*g_objectRbt[g_active];
		}
		else if (g_mouseRClickButton && !g_mouseLClickButton) { // right button down?
			M = Matrix4::makeTranslation(Cvec3(dx, dy, 0) * 0.01);
			g_objectRbt[g_active] = A * M*inv(A)*g_objectRbt[g_active];
		}
		else if (g_mouseMClickButton || (g_mouseLClickButton && g_mouseRClickButton)) {  // middle or (left and right) button down?
			M = Matrix4::makeTranslation(Cvec3(0, 0, -dy) * 0.01);
			g_objectRbt[g_active] = A * M*inv(A)*g_objectRbt[g_active];
		}
		glutPostRedisplay();// we always redraw if we changed the scene

	}

	g_mouseClickX = x;
	g_mouseClickY = g_windowHeight - y - 1;
}


static void mouse(const int button, const int state, const int x, const int y) {
	g_mouseClickX = x;
	g_mouseClickY = g_windowHeight - y - 1;  // conversion from GLUT window-coordinate-system to OpenGL window-coordinate-system

	g_mouseLClickButton |= (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN);
	g_mouseRClickButton |= (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN);
	g_mouseMClickButton |= (button == GLUT_MIDDLE_BUTTON && state == GLUT_DOWN);

	g_mouseLClickButton &= !(button == GLUT_LEFT_BUTTON && state == GLUT_UP);
	g_mouseRClickButton &= !(button == GLUT_RIGHT_BUTTON && state == GLUT_UP);
	g_mouseMClickButton &= !(button == GLUT_MIDDLE_BUTTON && state == GLUT_UP);

	g_mouseClickDown = g_mouseLClickButton || g_mouseRClickButton || g_mouseMClickButton;
}

static void keyboard(const unsigned char key, const int x, const int y) {
	float dis = 0.0;
	switch (key) {
	case 27:
		exit(0);                                  // ESC
	case 'h':
		cout << " ============== H E L P ==============\n\n"
			<< "h\t\thelp menu\n"
			<< "s\t\tsave screenshot\n"
			<< "f\t\tToggle flat shading on/off.\n"
			<< "o\t\tCycle object to edit\n"
			<< "1\t\tDiffuse only\n"
			<< "2\t\tDiffuse and specular\n"
			<< "drag left mouse to rotate\n" << endl;
		break;
	case '0':
		glFlush();
		writePpmScreenshot(g_windowWidth, g_windowHeight, "out.ppm");
		break;
	case 'o':
		
		if (g_active == 0) g_active = 1;
		else g_active = 0;
		
		//g_activeCube = 2;
		break;
	case 'w':
		eyeRbt[11] = eyeRbt[11] - 0.1;
		break;
	case 'a':
		eyeRbt[3] = eyeRbt[3] - 0.1;
		break;
	case 's':
		eyeRbt[11] = eyeRbt[11] + 0.1;
		break;
	case 'd':
		eyeRbt[3] = eyeRbt[3] + 0.1;
		break;
	case 'k':
		eyeRbt = rigTFormToMatrix(A) * rigTFormToMatrix(M1) * inv(rigTFormToMatrix(A)) * eyeRbt;
		break;
	case 'l':
		eyeRbt = rigTFormToMatrix(A) * rigTFormToMatrix(M2) * inv(rigTFormToMatrix(A)) * eyeRbt;
		break;
	case '1':
		g_activeShader = 0;
		break;
	case '2':
		g_activeShader = 1;
		break;
		
	}
	glutPostRedisplay();
}

static void initGlutState(int argc, char * argv[]) {
	glutInit(&argc, argv);                                  // initialize Glut based on cmd-line args
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);  //  RGBA pixel channels and double buffering
	glutInitWindowSize(g_windowWidth, g_windowHeight);      // create a window
	glutCreateWindow("Lighting");                       // title the window

	glutDisplayFunc(display);                               // display rendering callback
	glutReshapeFunc(reshape);                               // window reshape callback
	glutMotionFunc(motion);                                 // mouse movement callback
	glutMouseFunc(mouse);                                   // mouse click callback
	glutKeyboardFunc(keyboard);
}

static void initGLState() {
	glClearColor(128. / 255., 200. / 255., 255. / 255., 0.);
	glClearDepth(0.);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	//glCullFace(GL_BACK);
	//glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GREATER);
	glReadBuffer(GL_BACK);

	int samples;
	glGetIntegerv(GL_SAMPLES, &samples);
	cout << "Number of samples is " << samples << endl;
}

static void initShaders() {
	g_shaderStates.resize(g_numShaders);
	for (int i = 0; i < g_numShaders; ++i) {
		g_shaderStates[i].reset(new ShaderState(g_shaderFiles[i][0], g_shaderFiles[i][1]));
	}
}

static void initGeometry() {
	initGround();
	initArch();
}



int main(int argc, char * argv[]) {
	try {
		initGlutState(argc, argv);

		glewInit(); // load the OpenGL extensions

		if (!GLEW_VERSION_3_0)
			throw runtime_error("Error: card/driver does not support OpenGL Shading Language v1.3");

		initGLState();
		initShaders();
		initGeometry();

		glutMainLoop();
		return 0;
	}
	catch (const runtime_error& e) {
		cout << "Exception caught: " << e.what() << endl;
		return -1;
	}
}
