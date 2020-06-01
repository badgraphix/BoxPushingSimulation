//
//  main.c
//  Final Project CSC412
//
//  Created by Jean-Yves Hervé on 2019-12-12
//	This is public domain code.  By all means appropriate it and change is to your
//	heart's content.
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <time.h>
#include <thread>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
//
#include "gl_frontEnd.h"

using namespace std;

//==================================================================================
//	Function prototypes
//==================================================================================
void displayGridPane(void);
void displayStatePane(void);
void initializeApplication(void);
void cleanupGridAndLists(void);

//==================================================================================
//	Application-level global variables
//==================================================================================

//	Don't touch
extern int	GRID_PANE, STATE_PANE;
extern int 	GRID_PANE_WIDTH, GRID_PANE_HEIGHT;
extern int	gMainWindow, gSubwindow[2];

//	Don't rename any of these variables
//-------------------------------------
//	The state grid and its dimensions (arguments to the program)
int** grid;
int numRows = -1;	//	height of the grid
int numCols = -1;	//	width
int numBoxes = -1;	//	also the number of robots
int numDoors = -1;	//	The number of doors.

int numLiveThreads = 0;		//	the number of live robot threads

//	robot sleep time between moves (in microseconds)
const int MIN_SLEEP_TIME = 1000;
int robotSleepTime = 300000;

//	An array of C-string where you can store things you want displayed
//	in the state pane to display (for debugging purposes?)
//	Dont change the dimensions as this may break the front end
const int MAX_NUM_MESSAGES = 8;
const int MAX_LENGTH_MESSAGE = 32;
char** message;
time_t startTime;

//Mutex locks
pthread_mutex_t fileWriteMutex; //Controls access to writing the file.
pthread_mutex_t threadCountMutex; //Used when we want to change the thread count.
pthread_mutex_t pathRerouteMutex; //Used when a robot want to change routes and needs to assess the position of other objects. This only exists so two robots in deadlock with each other don't both try changing course at the same time.
pthread_mutex_t* mutexGridAvailableSquares;

//==================================================================================
//	These are the functions that tie the simulation with the rendering.
//	Some parts are "don't touch."  Other parts need your intervention
//	to make sure that access to critical section is properly synchronized
//==================================================================================

char DirectionSymbol[] = "NWSE";
string fileName = "robotSimulOut.txt";
thread threadList[256]; //Dynamically allocating caused termination without an active exception/
Robot *robots;
Box *boxes;
Door *doors;
int *doorAssign; //door id assigned to each robot-box pair

/*!
Displays the grid pane and all the objects on it.
*/
void displayGridPane(void)
{

	//	This is OpenGL/glut magic.  Don't touch
	glutSetWindow(gSubwindow[GRID_PANE]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glTranslatef(0, GRID_PANE_HEIGHT, 0);
	glScalef(1.f, -1.f, 1.f);
	

	for (int i=0; i<numBoxes; i++)
	{
		//	here I would test if the robot thread is still live
		//						row				column			row			column
		if (robots[i].isLive == true)
		{
			drawRobotAndBox(i, robots[i].row, robots[i].col, boxes[i].row, boxes[i].col, doorAssign[i]);
		}	
	}

	for (int i=0; i<numDoors; i++)
	{
		drawDoor(i, doors[i].row, doors[i].col);
	}

	//	This call does nothing important. It only draws lines
	//	There is nothing to synchronize here
	drawGrid();

	//	This is OpenGL/glut magic.  Don't touch
	glutSwapBuffers();
	
	glutSetWindow(gMainWindow);
}

/*!
Displays the state pane.
*/
void displayStatePane(void)
{
	//	This is OpenGL/glut magic.  Don't touch
	glutSetWindow(gSubwindow[STATE_PANE]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	//	Here I hard-code a few messages that I want to see displayed
	//	in my state pane.  The number of live robot threads will
	//	always get displayed.  No need to pass a message about it.
	time_t currentTime = time(NULL);
	double deltaT = difftime(currentTime, startTime);

	int numMessages = 3;
	sprintf(message[0], "We have %d doors", numDoors);
	sprintf(message[1], " ");
	sprintf(message[2], "Run time is %4.0f", deltaT);

	//---------------------------------------------------------
	//	This is the call that makes OpenGL render information
	//	about the state of the simulation.
	//
	//	You *must* synchronize this call.
	//
	//---------------------------------------------------------
	drawState(numMessages, message);
	
	
	//	This is OpenGL/glut magic.  Don't touch
	glutSwapBuffers();
	
	glutSetWindow(gMainWindow);
}

//------------------------------------------------------------------------
//	You shouldn't have to touch this one.  Definitely if you don't
//	add the "producer" threads, and probably not even if you do.
//------------------------------------------------------------------------
/*!
Speeds up the robots.
*/
void speedupRobots(void)
{
	//	decrease sleep time by 20%, but don't get too small
	int newSleepTime = (8 * robotSleepTime) / 10;
	
	if (newSleepTime > MIN_SLEEP_TIME)
	{
		robotSleepTime = newSleepTime;
	}
}

/*!
Slows down the robots.
*/
void slowdownRobots(void)
{
	//	increase sleep time by 20%
	robotSleepTime = (12 * robotSleepTime) / 10;
}

/*!
Writes the associated text (fileLine) to the file. Writing to the file is protected behind a mutex lock to prevent simultaneous writing between multiple threads.
@param[in] fileLine The text to write to the file.
*/
void writeToFile(string fileLine)
{
	pthread_mutex_lock(&fileWriteMutex);
	ofstream myFile;
	myFile.open (fileName, fstream::app);
	myFile << fileLine << "\n";
	myFile.close();
	pthread_mutex_unlock(&fileWriteMutex);
}

/*!
Moves the robot of the specified number in the specified direction and writes this information to the file.
@param[in] robotNo The index of the robot in its array.
@param[in] direction The direction the robot is going in.
@param[in] alternatePath True if this function was called as an attempt at an alternate path after discovering that the intended direction was blocked by another object.
*/
void moveRobot(int robotNo, Direction direction, bool alternatePath = false)
{
	int newRobotPos[2] = {robots[robotNo].row, robots[robotNo].col};
	switch (direction) {
		case NORTH:
			newRobotPos[0] = robots[robotNo].row - 1;
			break;
		case SOUTH:
			newRobotPos[0] = robots[robotNo].row + 1;
			break;
		case WEST:
			newRobotPos[1] = robots[robotNo].col - 1;
			break;
		case EAST:
			newRobotPos[1] = robots[robotNo].col + 1;
			break;
		default:
			break;
	}
	
	bool objectInWay = false;
	
	for (int i=0; i<numBoxes; i++)
	{
		if (newRobotPos[0] == boxes[i].row && newRobotPos[1] == boxes[i].col)
		{
			objectInWay = true;
			break;
		}
		if (i != robotNo && newRobotPos[0] == robots[i].row && newRobotPos[1] == robots[i].col)
		{
			objectInWay = true;
			break;
		}
	}
	if (objectInWay == true && alternatePath == false) //About to move onto an object! We don't want that. Try another direction.
	{
		pthread_mutex_lock(&pathRerouteMutex); //Only one object at a time can decide on a path reroute, otherwise more deadlock may occur!
		switch (direction) {
			case NORTH:
			case SOUTH:	
				if (robots[robotNo].col > 0) //Go west, unless you can't. Then go east instead.
				{
					moveRobot(robotNo, WEST, true);
				} else {
					moveRobot(robotNo, EAST, true);
				}
				break;
			case WEST:
			case EAST:
				if (robots[robotNo].row > 0) //Go north, unless you can't. Then go south instead.
				{
					moveRobot(robotNo, NORTH, true);
				} else {
					moveRobot(robotNo, SOUTH, true);
				}
				break;
			default:
				break;
		}
		pthread_mutex_unlock(&pathRerouteMutex);
	}
	else
	{
		//Now we try reserving the new square.
		pthread_mutex_lock(&mutexGridAvailableSquares[newRobotPos[0]*numCols + newRobotPos[1]]); //Code will hang until the square is open to move to.
		pthread_mutex_unlock(&mutexGridAvailableSquares[robots[robotNo].row*numCols + robots[robotNo].col]); //Free up the old square to be used by others.
		robots[robotNo].row = newRobotPos[0]; robots[robotNo].col = newRobotPos[1]; //Update position.

		string fileLine = "robot "; fileLine.append(to_string(robotNo)); fileLine.append(" move "); fileLine.push_back(DirectionSymbol[direction]);
		writeToFile(fileLine);
	}
}

/*!
Pushes the box of the specified number with the robot of the same number and writes this information to the file.
@param[in] robotNo The index of the robot and the box in their respective arrays.
@param[in] direction The direction the robot and box are going in.
*/
void pushBox(int robotNo, Direction direction)
{
	int newBoxPos[2] = {boxes[robotNo].row, boxes[robotNo].col};
	int newRobotPos[2] = {robots[robotNo].row, robots[robotNo].col};	
	switch (direction) {
		case NORTH:
			newRobotPos[0] = robots[robotNo].row - 1;
			newBoxPos[0] = boxes[robotNo].row - 1;			
			break;
		case SOUTH:
			newRobotPos[0] = robots[robotNo].row + 1;
			newBoxPos[0] = boxes[robotNo].row + 1;
			break;
		case WEST:
			newRobotPos[1] = robots[robotNo].col - 1;
			newBoxPos[1] = boxes[robotNo].col - 1;
			break;
		case EAST:
			newRobotPos[1] = robots[robotNo].col + 1;
			newBoxPos[1] = boxes[robotNo].col + 1;
			break;
		default:
			break;
	}

	pthread_mutex_lock(&mutexGridAvailableSquares[newBoxPos[0]*numCols + newBoxPos[1]]); //Code will hang until the square is open for the box to move to.
	pthread_mutex_unlock(&mutexGridAvailableSquares[robots[robotNo].row*numCols + robots[robotNo].col]); //Free up the old square used by the robot to be used by others.
	//Note we don't need to unlock the square the box is moving from because the robot is going to be there now.

	boxes[robotNo].row = newBoxPos[0]; boxes[robotNo].col = newBoxPos[1];	
	robots[robotNo].row = newRobotPos[0]; robots[robotNo].col = newRobotPos[1]; //Update position.
	
	string fileLine = "robot "; fileLine.append(to_string(robotNo)); fileLine.append(" push "); fileLine.push_back(DirectionSymbol[direction]);
	writeToFile(fileLine);
}



/*!
Ends the existence of the robot of the specified number and writes this information to the file.
@param[in] robotNo The index of the robot in its array.
@param[in] direction The direction the robot is going in.
*/
void endRobot(int robotNo)
{
	string fileLine = "robot "; fileLine.append(to_string(robotNo)); fileLine.append(" end ");
	writeToFile(fileLine);
	pthread_mutex_unlock(&mutexGridAvailableSquares[robots[robotNo].row*numCols + robots[robotNo].col]); //Free up the old square used by the robot to be used by others.
	pthread_mutex_unlock(&mutexGridAvailableSquares[boxes[robotNo].row*numCols + boxes[robotNo].col]); //Do the same for the associated box.
}	

/*!
Moves the robot of the specified number and pathfinds it over to the box of its number. The side of the box it targets is dependent on where the robot plans to move the box to.
Leaves the function when it's done the apth.
@param[in] robotNo The index of the robot in its array.
@param[in] robotTargetLocation The location that the pathfinding for the robot should target.
@param[in] sideOfBox The side of the box the robot should be traveling to.
*/
void moveRobotNextToBox(int robotNo, int* robotTargetLocation, Direction sideOfBox)
{
	while (true)
	{
		usleep(robotSleepTime);
		int robotBoxDifference[2] = {robotTargetLocation[0] - robots[robotNo].row, robotTargetLocation[1] - robots[robotNo].col};
		if (sideOfBox == NORTH or sideOfBox == SOUTH)
		{ //If we are traveling to the north or south of a box, prioritize moving vertically first.
			if (robotBoxDifference[0] > 0)
			{
				moveRobot(robotNo, SOUTH);	
			}
			else if (robotBoxDifference[0] < 0)
			{
				moveRobot(robotNo, NORTH);
			}
			else if (robotBoxDifference[1] > 0)
			{
				moveRobot(robotNo, EAST);
			}
			else if (robotBoxDifference[1] < 0)
			{
				moveRobot(robotNo, WEST);
			}
			else
			{
				return; //At target location.
			}
		}
		else //sideOfBox is EAST or WEST
		{ //If we are traveling to the east or west of a box, prioritize moving horizontally first.;
			if (robotBoxDifference[1] > 0)
			{
				moveRobot(robotNo, EAST);
			}
			else if (robotBoxDifference[1] < 0)
			{
				moveRobot(robotNo, WEST);
			}
			else if (robotBoxDifference[0] > 0)
			{
				moveRobot(robotNo, SOUTH);	
			}
			else if (robotBoxDifference[0] < 0)
			{
				
				moveRobot(robotNo, NORTH);
			}
			else
			{
				return; //At target location.
			}			
		}
	}
}
/*!
Marks a robot as live. This is what the program references to determine if the robot's thread is still live.
@param[in,out] robot The robot we are marking.
*/
void activateThread(Robot *robot)
{
	pthread_mutex_lock(&threadCountMutex);
	robot->isLive = true;
	numLiveThreads++;
	pthread_mutex_unlock(&threadCountMutex);
}
/*!
Marks a robot as dead. This is what the program references to determine if the robot's thread is still live.
@param[in,out] robot The robot we are marking.
*/
void killThread(Robot *robot)
{
	pthread_mutex_lock(&threadCountMutex);
	robot->isLive = false;
	numLiveThreads--;
	pthread_mutex_unlock(&threadCountMutex);
}

/*!
Plans out a path for the robot that involves going to its box and pushing the box to the door.
@param[in] robotNo The index of the robot in its array.
@param[out] robotTargetLocation The location that the pathfinding for the robot should target.
@param[out] nextBoxMovement A 2-element array representing the direction and distance of the next movement of the box.
@param[out] sideOfBox The side of the box the robot should be traveling to.
*/
void planRouteToDoor(int robotNo, int* robotTargetLocation, int* nextBoxMovement, Direction* sideOfBox)
{
	//First, find out what side of the box we want to push from.
	int targetDoorNo = doorAssign[robotNo];
	int boxDoorDifference[2] = {doors[targetDoorNo].row - boxes[robotNo].row, doors[targetDoorNo].col - boxes[robotNo].col};
	nextBoxMovement[0] = 0; nextBoxMovement[1] = 0;
	robotTargetLocation[0] = boxes[robotNo].row; robotTargetLocation[1] = boxes[robotNo].col;
	if (boxDoorDifference[0] != 0) 
	{
		nextBoxMovement[0] = boxDoorDifference[0];
		if (boxDoorDifference[0] > 0) { //Must go to above the box.
			robotTargetLocation[0] = boxes[robotNo].row - 1;
			*sideOfBox = NORTH;
		} else { //Must go to below the box.
			robotTargetLocation[0] = boxes[robotNo].row + 1;
			*sideOfBox = SOUTH;
		}
	}
	else
	{
		nextBoxMovement[1] = boxDoorDifference[1];
		if (boxDoorDifference[1] > 0) { //Must go to the left of the box.
			robotTargetLocation[1] = boxes[robotNo].col - 1;
			*sideOfBox = WEST;
		} else { //Must go to the right of the box.
			robotTargetLocation[1] = boxes[robotNo].col + 1;
			*sideOfBox = EAST;			
		}
	}
}

/*!
Has the robot push the associated box to a target location.
@param[in] robotNo The index of the robot in its array.
@param[in] nextBoxMovement A 2-element array representing the direction and distance of the next movement of the box.
*/
void pushBoxToTargetLocation(int robotNo, int* nextBoxMovement)
{
	while (true)
	{
		//Move and push the box one space
		if (nextBoxMovement[0] != 0) {
			int direction = nextBoxMovement[0]/abs(nextBoxMovement[0]);
			switch (direction) {
				case -1:
					pushBox(robotNo, NORTH);
					break;
				case 1:
					pushBox(robotNo, SOUTH);
			}
			nextBoxMovement[0] -= direction;
		} else if (nextBoxMovement[1] != 0) {	
			int direction = nextBoxMovement[1]/abs(nextBoxMovement[1]);
			switch (direction) {
				case -1:
					pushBox(robotNo, WEST);
					break;
				case 1:
					
					pushBox(robotNo, EAST);
			}
			nextBoxMovement[1] -= direction;
		}				
		if (nextBoxMovement[0] == 0 && nextBoxMovement[1] == 0) {
			break;
		}
		usleep(robotSleepTime);
	}
}
/*!
The main function for robot threads.
@param[in] robotNo The index of the robot in its array.
*/
void robotFunc(int robotNo)
{
	activateThread(&robots[robotNo]);
	int targetDoorNo = doorAssign[robotNo];
	int nextBoxMovement[2];
	int robotTargetLocation[2];
	Direction sideOfBox;
	while (not (boxes[robotNo].row == doors[targetDoorNo].row && boxes[robotNo].col == doors[targetDoorNo].col))
	{
		planRouteToDoor(robotNo, robotTargetLocation, nextBoxMovement, &sideOfBox); //First, plan a route.
		moveRobotNextToBox(robotNo, robotTargetLocation, sideOfBox); //Then, pathfind over to a certain side of the box.	
		pushBoxToTargetLocation(robotNo, nextBoxMovement); //Push the box to a specific location.
	}
	endRobot(robotNo);
	killThread(&robots[robotNo]);
}

/*!
Determines a position on the grid for the box of the specified number. The position can be anywhere on the grid besides the edges or a position already occupied by another box or a door.
Will continually recall the function until a valid position is generated.
@param[in] boxNo The index of the box in its array.
*/
void rollForBoxPosition(int boxNo) //"Roll" as in "the role of a die". If the position ends up being the same as an existing object, we reroll until we get something.
{
	
	bool objectFound = false;
	boxes[boxNo].row = rand() % (numRows-2) + 1; boxes[boxNo].col = rand() % (numCols-2) + 1; //Anywhere but the edges of the grid. (Should not be at same position as other boxes or any doors.)
	for (int k=0; k<boxNo; k++) //Is the specified box in the same position as any of the other boxes? (By design we won't need to worry about comparing it to itself.)
	{
		if (boxes[boxNo].row == boxes[k].row && boxes[boxNo].col == boxes[k].col) //Box is in same position as another box.
		{
			objectFound = true;
			break;
		}			
	}
	{
		for (int k=0; k<numDoors; k++) //Is the specified box in the same position as any doors?
		{
			if (boxes[boxNo].row == doors[k].row && boxes[boxNo].col == doors[k].col) //Box is in same position as a door.
			{
				objectFound = true;
				break;
			}
		}
	}
	if (objectFound == true)
	{
		rollForBoxPosition(boxNo); //Reroll.
	}
	else
	{ //Now that the starting position has been determined, let's lock that spot on the mutex grid to signal that this space is occupied.
		pthread_mutex_lock(&mutexGridAvailableSquares[boxes[boxNo].row*numCols + boxes[boxNo].col]);
	}
}

/*!
Determines a position on the grid for the robot of the specified number. The position can be anywhere on the grid besides a position already occupied by another robot, a box or a door.
Will continually recall the function until a valid position is generated.
@param[in] robotNo The index of the robot in its array.
*/
void rollForRobotPosition(int robotNo)
{
	bool objectFound = false;
	robots[robotNo].row = rand() % numRows; robots[robotNo].col = rand() % numCols; //Anywhere on the grid.
	for (int k=0; k<robotNo; k++) //Is the specified robot in the same position as any of the other robots?
	{
		if (robots[robotNo].row == robots[k].row && robots[robotNo].col == robots[k].col) //Robot is in same position as another robot.
		{
			objectFound = true;
			break;
		}
	}
	if (objectFound == false)
	{
		for (int k=0; k<numBoxes; k++) //Is the specified robot in the same position as any boxes?
		{
			if (robots[robotNo].row == boxes[k].row && robots[robotNo].col == boxes[k].col) //Robot is in same position as a box.
			{
				objectFound = true;
				break;
			}			
		}
	}
	if (objectFound == false) 
	{
		for (int k=0; k<numDoors; k++) //Is the specified robot in the same position as any doors?
		{
			if (robots[robotNo].row == doors[k].row && robots[robotNo].col == doors[k].col) //Robot is in same position as a door.
			{
				objectFound = true;
				break;
			}
		}
	}
	if (objectFound == true)
	{	
		rollForRobotPosition(robotNo); //Reroll.
	}
	else
	{ //Now that the starting position has been determined, let's lock that spot on the mutex grid to signal that this space is occupied.
		pthread_mutex_lock(&mutexGridAvailableSquares[robots[robotNo].row*numCols + robots[robotNo].col]);
	}
}

//------------------------------------------------------------------------
//	You shouldn't have to change anything in the main function besides
//	the initialization of numRows, numCos, numDoors, numBoxes.
//------------------------------------------------------------------------
int main(int argc, char** argv)
{

	numCols = atoi(argv[1]); //width
	numRows = atoi(argv[2]); //height
	numBoxes = atoi(argv[3]); //and robots
	numDoors = atoi(argv[4]); //can be 1, 2 or 3*/
	if (numDoors < 1 or numDoors > 3)
	{
		cout << "Doors count must be between 1 and 3." << endl;
		exit(0);
	}
	
	//Write to file
	string fileLine = "COLS: "; fileLine.append(to_string(numCols)); fileLine.append(", ROWS: "); fileLine.append(to_string(numRows)); fileLine.append(", # of BOXES: "); fileLine.append(to_string(numBoxes)); fileLine.append(", # of DOORS: "); fileLine.append(to_string(numDoors)); fileLine.append("\n");
	writeToFile(fileLine);
	
	//	Even though we extracted the relevant information from the argument
	//	list, I still need to pass argc and argv to the front-end init
	//	function because that function passes them to glutInit, the required call
	//	to the initialization of the glut library.
	initializeFrontEnd(argc, argv, displayGridPane, displayStatePane);
	

	//	Now we can do application-level initialization
	initializeApplication();
	


	//	Now we enter the main loop of the program and to a large extend
	//	"lose control" over its execution.  The callback functions that 
	//	we set up earlier will be called when the corresponding event
	//	occurs
	glutMainLoop();
	
	cleanupGridAndLists();
	//	This will probably never be executed (the exit point will be in one of the
	//	call back functions).
	return 0;
}

/*!
Free allocated resources before leaving.
*/
void cleanupGridAndLists(void)
{
	for (int i=0; i< numRows; i++)
		free(grid[i]);
	free(grid);
	for (int k=0; k<MAX_NUM_MESSAGES; k++)
		free(message[k]);
	free(message);
	
}


//==================================================================================
//
//	This is a part that you have to edit and add to.
//
//==================================================================================

/*!
Allocate the grid and generate every object on it.
*/
void initializeApplication(void)
{
	//Seed the pseudo-random generator.
	startTime = time(NULL);
	srand((unsigned int) startTime);

	//Allocate the grid.
	grid = (int**) malloc(numRows * sizeof(int*));
	for (int i=0; i<numRows; i++)
		grid[i] = (int*) malloc(numCols * sizeof(int));

	mutexGridAvailableSquares = (pthread_mutex_t*) malloc(numRows * numCols * sizeof(pthread_mutex_t));

	message = (char**) malloc(MAX_NUM_MESSAGES*sizeof(char*));
	for (int k=0; k<MAX_NUM_MESSAGES; k++)
		message[k] = (char*) malloc((MAX_LENGTH_MESSAGE+1)*sizeof(char));

	//Malloc arrays.
	//threadList = (thread*) malloc(numBoxes * sizeof(thread));
	robots = (Robot*) malloc(numBoxes * sizeof(Robot));
	boxes = (Box*) malloc(numBoxes * sizeof(Box));
	doors = (Door*) malloc(numDoors * sizeof(Door));
	doorAssign = (int*) malloc(numBoxes * sizeof(int));


	string fileLine;
	//(Pseudo) randomize the arrays.
	//Doors
	for (int i=0; i<numDoors; i++)
	{	
		doors[i].row = rand() % numRows; doors[i].col = rand() % numCols; //Anywhere on the grid.
		//Write to file.
		fileLine = "Door "; fileLine.append(to_string(i)); fileLine.append(": row "); fileLine.append(to_string(doors[i].row)); fileLine.append(", col "); fileLine.append(to_string(doors[i].col));
		writeToFile(fileLine);
	}
	fileLine = "";
	writeToFile(fileLine);

	//Boxes
	for (int i=0; i<numBoxes; i++)
	{
		
		rollForBoxPosition(i);

		//Write to file
		fileLine = "Box "; fileLine.append(to_string(i)); fileLine.append(": row "); fileLine.append(to_string(boxes[i].row)); fileLine.append(", col "); fileLine.append(to_string(boxes[i].col));
		writeToFile(fileLine);
	}

	fileLine = "";
	writeToFile(fileLine);

	//Robots
	for (int i=0; i<numBoxes; i++)
	{
		rollForRobotPosition(i);
		doorAssign[i] = rand() % numDoors; //Any door id.
		//Write to file
		fileLine = "Robot "; fileLine.append(to_string(i)); fileLine.append(": row "); fileLine.append(to_string(robots[i].row)); fileLine.append(", col "); fileLine.append(to_string(robots[i].col)); fileLine.append(", destination door: "); fileLine.append(to_string(doorAssign[i]));
		writeToFile(fileLine);

	}

	fileLine = "";
	writeToFile(fileLine);
	
	//Threads
	for (int i=0; i < numBoxes; i++)
	{
		threadList[i] = thread(robotFunc, i);
	}
	
}


