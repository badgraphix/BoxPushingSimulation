//
//  gl_frontEnd.h
//  GL threads
//
//  Created by Jean-Yves Hervé on 2019-12-08
//

#ifndef GL_FRONT_END_H
#define GL_FRONT_END_H


//------------------------------------------------------------------------------
//	Find out whether we are on Linux or macOS (sorry, Windows people)
//	and load the OpenGL & glut headers.
//	For the macOS, lets us choose which glut to use
//------------------------------------------------------------------------------
#if (defined(__dest_os) && (__dest_os == __mac_os )) || \
	defined(__APPLE_CPP__) || defined(__APPLE_CC__)
	//	Either use the Apple-provided---but deprecated---glut
	//	or the third-party freeglut install
	#if 1
		#include <GLUT/GLUT.h>
	#else
		#include <GL/freeglut.h>
		#include <GL/gl.h>
	#endif
#elif defined(linux)
	#include <GL/glut.h>
#else
	#error unknown OS
#endif


//-----------------------------------------------------------------------------
//	Data types
//-----------------------------------------------------------------------------

/*! One of the cardinal directions. */
using Direction = enum  {
	NORTH = 0, /*!<Moving north.*/
	WEST, /*!<Moving west.*/
	SOUTH, /*!<Moving south.*/
	EAST, /*!<Moving east.*/
	//
	NUM_TRAVEL_DIRECTIONS /*!<The number of this value specifies the number of travel directions.*/
};

/*! Keeps track of data relating to an individual robot. */
typedef struct Robot {
	int row; /*!<Row of the robot on the grid.*/
	int col; /*!<Column of the robot on the grid.*/
	bool isLive; /*!<Boolean determining if the robot is still live.*/
} Robot;
/*! Keeps track of data relating to an individual box. */
typedef struct Box {
	int row; /*!<Row of the box on the grid.*/
	int col; /*!<Column of the box on the grid.*/
} Box;
/*! Keeps track of data relating to an individual door. */
typedef struct Door {
	int row; /*!<Row of the door on the grid.*/
	int col; /*!<Column of the door on the grid.*/
} Door;

//-----------------------------------------------------------------------------
//	Function prototypes
//-----------------------------------------------------------------------------

//	I don't want to impose how you store the information about your robots,
//	boxes and doors, so the two functions below will have to be called once for
//	each pair robot/box and once for each door.

/*!
This function draws together a robot and the box it is supposed to move
Since a robot corresponds to a box, they should have the same index in their
respective array, so only one id needs to be passed.
We also pass the number of the door assigned to the robot/box pair, so that
can display them all with a matching color.
@param[in] id The shared id of the robot and box.
@param[in] robotRow The row of the robot.
@param[in] robotCol The column of the robot.
@param[in] boxRow The row of the box.
@param[in] boxCol The column of the box.
@param[in] doorNumber The number of the door associated with the robot and box.
*/
void drawRobotAndBox(int id, int robotRow, int robotCol, int boxRow, int boxCol, int doorNumber);

/*!
This function assigns a color to the door based on its number.
@param[in] doorNumber The index of the door in its associated array.
@param[in] doorRow The row of the door.
@param[in] doorCol The column of the door.
*/
void drawDoor(int doorNumber, int doorRow, int doorCol);

/*!
Draws the grid.
*/
void drawGrid(void);

/*!
Draws the state pane.
@param[in] numMessages The number of messages to display.
@param[in] message An array of the messages to display.
*/
void drawState(int numMessages, char** message);

/*!
Initializes the front-end with all of the necessary graphics.
@param[in] argc The argc from main.
@param[in] argv The argv from main.
@param[in] gridCB The displayGridPane function.
@param[in] stateCB The displayStatePane function.
*/
void initializeFrontEnd(int argc, char** argv,
						void (*gridCB)(void), void (*stateCB)(void));

#endif // GL_FRONT_END_H

